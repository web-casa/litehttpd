/**
 * test_esi_stream_hook.cpp — unit tests for the ESI body-filter hooks.
 *
 * These pin the stream-filter contract learned from OLS's modgzip:
 *   - a filter must FORWARD bytes via stream_write (returning len1 alone
 *     drops the body — the exact regression fixed in 2c9654c)
 *   - EOF arrives as flag_in & LSI_CBFI_EOF in the RECV hook
 *   - backpressure (write accepted < offered) parks the remainder and
 *     reports LSI_CBFO_BUFFERED + zero consumption
 *
 * The hooks are exercised through the ht_stream_write test seam; the
 * gate is primed via mod_htaccess_test_prime with real directive lists.
 */
#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "mock_lsiapi.h"

extern "C" {
#include "htaccess_directive.h"
#include "htaccess_stream.h"

/* mod_htaccess.c internals exposed for tests */
int on_recv_resp_body_esi_hook(lsi_param_t *param);
int on_rcvd_resp_body_esi_hook(lsi_param_t *param);
void mod_htaccess_test_prime(lsi_session_t *session, htaccess_directive_t *dirs);
}

namespace {

struct RecordedWrite {
    std::string data; /* includes "" for the NULL end-of-stream marker */
    bool is_eof_marker;
};

std::vector<RecordedWrite> g_writes;
int g_accept_limit = -1; /* <0: accept all; else accept at most this many bytes */

static int record_write(lsi_param_t *, const char *buf, int len)
{
    if (buf == NULL && len == 0) {
        g_writes.push_back({"", true});
        return 0;
    }
    if (g_accept_limit >= 0 && len > g_accept_limit)
        len = g_accept_limit;
    g_writes.push_back({std::string(buf, (size_t)len), false});
    return len;
}

static std::string all_data()
{
    std::string s;
    for (auto &w : g_writes)
        if (!w.is_eof_marker)
            s += w.data;
    return s;
}

htaccess_directive_t *make_esi_enable(const char *v)
{
    auto *d = static_cast<htaccess_directive_t *>(calloc(1, sizeof(htaccess_directive_t)));
    d->type = DIR_ESI_ENABLE;
    d->value = strdup(v);
    d->line_number = 1;
    return d;
}

void free_dirs(htaccess_directive_t *h)
{
    while (h) {
        htaccess_directive_t *n = h->next;
        free(h->name);
        free(h->value);
        free(h);
        h = n;
    }
}

class EsiStreamHookTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        mock_lsiapi::reset_global_state();
        session_.reset();
        g_writes.clear();
        g_accept_limit = -1;
        ht_stream_write_test_fn = record_write;
        /* the gate needs a docroot for includes (fail-closed without one) */
        session_.set_doc_root("/tmp");
    }
    void TearDown() override
    {
        ht_stream_write_test_fn = NULL;
        /* release any context the hooks created */
        lsi_param_t fin{};
        fin.session = session_.handle();
        on_rcvd_resp_body_esi_hook(&fin);
    }

    lsi_param_t make_param(const char *data, int len, int flag_in = 0)
    {
        lsi_param_t p{};
        p.session = session_.handle();
        p.ptr1 = data;
        p.len1 = len;
        p.flag_in = flag_in;
        int *fout = &flag_out_;
        p.flag_out = fout;
        flag_out_ = 0;
        return p;
    }

    MockSession session_;
    int flag_out_ = 0;
};

} // namespace

/* ------------------------------------------------------------------ */
/*  The critical regression: bypass must FORWARD, not just consume.    */
/* ------------------------------------------------------------------ */

TEST_F(EsiStreamHookTest, BypassForwardsChunkDownstream)
{
    /* No EsiEnable directive: the gate must fail, and the chunk must
     * still be forwarded byte-for-byte. Returning len1 without a write
     * drops the body (modgzip contract). */
    mod_htaccess_test_prime(session_.handle(), make_esi_enable("off"));

    const char body[] = "plain content, no esi";
    auto p = make_param(body, (int)strlen(body));
    int rc = on_recv_resp_body_esi_hook(&p);

    EXPECT_EQ(rc, (int)strlen(body));
    ASSERT_GE(g_writes.size(), 1u);
    EXPECT_EQ(all_data(), std::string(body));
}

TEST_F(EsiStreamHookTest, NoDirectivesAtAllStillForwards)
{
    /* Gate never evaluated a list (NULL directives): forward unchanged. */
    lsi_param_t p{};
    p.session = session_.handle();
    p.ptr1 = "x";
    p.len1 = 1;
    int rc = on_recv_resp_body_esi_hook(&p);
    EXPECT_EQ(rc, 1);
    EXPECT_EQ(all_data(), "x");
}

/* ------------------------------------------------------------------ */
/*  Active filter: comment stripped, EOF finishes the stream.          */
/* ------------------------------------------------------------------ */

TEST_F(EsiStreamHookTest, ActiveStripsCommentAndEndsStreamAtEof)
{
    mod_htaccess_test_prime(session_.handle(), make_esi_enable("on"));
    lsi_session_set_resp_content_type(session_.handle(), "text/html", 9);

    const char part1[] = "A<esi:comment>hidden</esi:comment>B";
    auto p1 = make_param(part1, (int)strlen(part1));
    int rc1 = on_recv_resp_body_esi_hook(&p1);
    EXPECT_EQ(rc1, (int)strlen(part1));

    const char part2[] = "tail";
    auto p2 = make_param(part2, (int)strlen(part2), LSI_CBFI_EOF);
    int rc2 = on_recv_resp_body_esi_hook(&p2);
    EXPECT_EQ(rc2, (int)strlen(part2));

    EXPECT_EQ(all_data(), "ABtail");
    /* end-of-stream marker was sent exactly once */
    int markers = 0;
    for (auto &w : g_writes)
        if (w.is_eof_marker)
            markers++;
    EXPECT_EQ(markers, 1);
}

/* ------------------------------------------------------------------ */
/*  Backpressure: partial accept parks remainder and signals BUFFERED. */
/* ------------------------------------------------------------------ */

TEST_F(EsiStreamHookTest, BackpressureParksRemainderAndRequestsReinvocation)
{
    mod_htaccess_test_prime(session_.handle(), make_esi_enable("on"));
    lsi_session_set_resp_content_type(session_.handle(), "text/html", 9);

    const char body[] = "hello world body";
    auto p1 = make_param(body, (int)strlen(body));
    g_accept_limit = 5; /* downstream accepts only 5 bytes per write */
    int rc1 = on_recv_resp_body_esi_hook(&p1);
    /* Input IS consumed (transformed once); only its output is parked.
     * Returning 0 here would make OLS re-deliver the chunk and double
     * the output. */
    EXPECT_EQ(rc1, (int)strlen(body));
    EXPECT_NE(p1.flag_out && (*p1.flag_out & LSI_CBFO_BUFFERED), 0);
    ASSERT_EQ(g_writes.size(), 1u);
    EXPECT_EQ(g_writes[0].data, "hello");

    /* OLS delivers the NEXT chunk (previous one was consumed): flush-first
     * must deliver the parked remainder before the new output. */
    g_accept_limit = -1; /* pressure released */
    const char next[] = "!";
    auto p2 = make_param(next, 1);
    int rc2 = on_recv_resp_body_esi_hook(&p2);
    EXPECT_EQ(rc2, 1);
    EXPECT_EQ(all_data(), "hello world body!");
}

/* ------------------------------------------------------------------ */
/*  Abort safety: RCVD hook releases the context without writing twice. */
/* ------------------------------------------------------------------ */

TEST_F(EsiStreamHookTest, AbortedResponseFlushesOnceViaSafetyNet)
{
    mod_htaccess_test_prime(session_.handle(), make_esi_enable("on"));
    lsi_session_set_resp_content_type(session_.handle(), "text/html", 9);

    const char body[] = "kept<esi:inc"; /* partial tag held at abort */
    auto p1 = make_param(body, (int)strlen(body));
    on_recv_resp_body_esi_hook(&p1);
    EXPECT_EQ(all_data(), "kept"); /* partial tag must not leak yet */

    /* No EOF chunk; RCVD fires as the safety net. */
    lsi_param_t fin{};
    fin.session = session_.handle();
    on_rcvd_resp_body_esi_hook(&fin);

    /* Fail-open flush: the never-completed tag is preserved literally. */
    EXPECT_EQ(all_data(), "kept<esi:inc");

    /* A second RCVD must not re-emit anything (context released). */
    size_t before = g_writes.size();
    on_rcvd_resp_body_esi_hook(&fin);
    EXPECT_EQ(g_writes.size(), before);
}
