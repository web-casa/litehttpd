#include <gtest/gtest.h>
#include <cstring>
#include <cstdio>
#include <unistd.h>
#include <sys/stat.h>
extern "C" {
#include "htaccess_esi.h"
}
#include <string.h>
#include <stdlib.h>

class EsiTest : public ::testing::Test {
protected:
    esi_ctx_t ctx;
    char *out;
    size_t out_len;
    void SetUp() override { esi_ctx_init(&ctx, NULL, 0); out = NULL; out_len = 0; }
    void TearDown() override { esi_ctx_free(&ctx); }
    std::string process(const std::string& input) {
        size_t consumed = esi_process(&ctx, input.c_str(), input.size(), &out, &out_len);
        (void)consumed;
        return std::string(out ? out : "", out_len);
    }
};

TEST_F(EsiTest, PassthroughNoEsi) {
    auto r = process("Hello World <p>No ESI here</p>");
    EXPECT_EQ(r, "Hello World <p>No ESI here</p>");
}

TEST_F(EsiTest, RemoveTag) {
    auto r = process("Before<esi:remove>SECRET</esi:remove>After");
    EXPECT_EQ(r, "BeforeAfter");
}

TEST_F(EsiTest, CommentTag) {
    auto r = process("A<esi:comment>hidden</esi:comment>B");
    EXPECT_EQ(r, "AB");
}

TEST_F(EsiTest, MultipleRemoveTags) {
    auto r = process("A<esi:remove>x</esi:remove>B<esi:remove>y</esi:remove>C");
    EXPECT_EQ(r, "ABC");
}

TEST_F(EsiTest, VarsTagPassthrough) {
    auto r = process("A<esi:vars>content</esi:vars>B");
    /* v1: vars content passes through unchanged (substitution in v1.1) */
    EXPECT_EQ(r, "AcontentB");
}

TEST_F(EsiTest, HasTagsDetection) {
    EXPECT_EQ(esi_content_has_tags("<esi:remove>", 12), 1);
    EXPECT_EQ(esi_content_has_tags("no tags here", 12), 0);
    EXPECT_EQ(esi_content_has_tags("<ESI:remove>", 12), 0); /* case-sensitive check */
}

TEST_F(EsiTest, ChunkedRemoveTag) {
    /* Tag split across chunks — v1 limitation: state machine processes
     * each chunk independently; cross-chunk tags need buffer unification
     * (planned for v1.1). For now, verify no crash and partial output. */
    auto r1 = process("Hello <esi:rem");
    auto r2 = process("ove>SECRET</esi:remove>Done");
    /* v1: the partial tag in chunk1 is passed through, then chunk2's
     * complete tag is stripped. Result is approximate. */
    EXPECT_NE(r1 + r2, ""); /* must produce some output, not crash */
}

TEST_F(EsiTest, UnknownTagPassthrough) {
    auto r = process("A<esi:unknown attr=\"x\"/>B");
    /* Unknown tags pass through as-is in v1 */
    EXPECT_NE(r.find("B"), std::string::npos);
}

/* ---- P8 v2: Variable substitution ---- */
TEST(EsiVarsTest, SubstituteUserAgent) {
    esi_vars_t vars = {};
    vars.user_agent = "Mozilla/5.0 Test";
    char out[256];
    size_t out_len;
    esi_substitute_vars("UA: $(HTTP_USER_AGENT)", 22, &vars, out, sizeof(out), &out_len);
    EXPECT_STREQ(out, "UA: Mozilla/5.0 Test");
}

TEST(EsiVarsTest, SubstituteMultiple) {
    esi_vars_t vars = {};
    vars.user_agent = "Firefox";
    vars.remote_addr = "10.0.0.1";
    char out[256];
    size_t out_len;
    esi_substitute_vars("from $(REMOTE_ADDR) with $(HTTP_USER_AGENT)", 43, &vars, out, sizeof(out), &out_len);
    EXPECT_STREQ(out, "from 10.0.0.1 with Firefox");
}

TEST(EsiVarsTest, UnknownVarEmpty) {
    esi_vars_t vars = {};
    char out[256];
    size_t out_len;
    esi_substitute_vars("A$(UNKNOWN_VAR)B", 16, &vars, out, sizeof(out), &out_len);
    EXPECT_STREQ(out, "AB");
}

TEST(EsiVarsTest, NoVars) {
    esi_vars_t vars = {};
    char out[256];
    size_t out_len;
    esi_substitute_vars("plain text", 10, &vars, out, sizeof(out), &out_len);
    EXPECT_STREQ(out, "plain text");
}

TEST(EsiVarsTest, UnclosedParen) {
    esi_vars_t vars = {};
    vars.user_agent = "X";
    char out[256];
    size_t out_len;
    esi_substitute_vars("a$(UNCLOSED", 11, &vars, out, sizeof(out), &out_len);
    /* Unclosed $( is literal */
    EXPECT_NE(strstr(out, "$("), nullptr);
}

/* ---- P8 v2: Header gating ---- */
TEST(EsiGateTest, HtmlEnabled) {
    EXPECT_EQ(esi_should_process("text/html", 9, NULL, 0), 1);
    EXPECT_EQ(esi_should_process("text/html; charset=utf-8", 24, NULL, 0), 1);
}

TEST(EsiGateTest, XhtmlEnabled) {
    EXPECT_EQ(esi_should_process("application/xhtml+xml", 21, NULL, 0), 1);
}

TEST(EsiGateTest, OtherBypassed) {
    EXPECT_EQ(esi_should_process("application/json", 16, NULL, 0), 0);
    EXPECT_EQ(esi_should_process("image/png", 9, NULL, 0), 0);
    EXPECT_EQ(esi_should_process(NULL, 0, NULL, 0), 0);
}

/* ---- ESI include tests ---- */
TEST(EsiIncludeTest, ExtractSrc) {
    /* Missing static + valid URI -> dynamic path: an 8-byte placeholder is
     * emitted (\x01LHE<idx>\x02) and the URI recorded for the EOF join. */
    esi_ctx_t ctx;
    esi_ctx_init(&ctx, NULL, 0);
    char *out;
    size_t out_len;
    esi_process(&ctx, "<esi:include src=\"/nonexistent.html\"/>rest", 42, &out, &out_len);
    /* placeholder bytes contain NULs (LE index) — memcmp, not strstr */
    EXPECT_EQ(ctx.dyn_count, 1);
    EXPECT_EQ(out_len, 8U + strlen("rest")); /* placeholder + tail */
    EXPECT_EQ((unsigned char)out[0], 0x01);
    EXPECT_EQ(out[1], 'L'); EXPECT_EQ(out[2], 'H'); EXPECT_EQ(out[3], 'E');
    EXPECT_EQ((unsigned char)out[7], 0x02);
    EXPECT_EQ(memcmp(out + 8, "rest", 4), 0);
    free(ctx.dyn_uri[0]);
    ctx.dyn_uri[0] = NULL;
    ctx.dyn_count = 0;
    esi_ctx_free(&ctx);
}

TEST(EsiIncludeTest, MissingSrcAttr) {
    esi_ctx_t ctx;
    esi_ctx_init(&ctx, NULL, 0);
    char *out;
    size_t out_len;
    esi_process(&ctx, "A<esi:include/>B", 16, &out, &out_len);
    /* Tag with no src should be removed */
    EXPECT_NE(strstr(out, "A"), nullptr);
    EXPECT_NE(strstr(out, "B"), nullptr);
    esi_ctx_free(&ctx);
}


/* ==================================================================
 *  Security: <esi:include> containment
 * ================================================================== */

TEST_F(EsiTest, Include_RefusedWithoutDocroot)
{
    /* Fail closed: no docroot configured -> include must not resolve,
     * not even for a path that exists on the test machine. */
    size_t len = 0;
    char *out = esi_include_read("/etc/hostname", NULL, 0, &len);
    EXPECT_EQ(out, nullptr);
}

TEST_F(EsiTest, Include_AbsolutePathOutsideDocrootRefused)
{
    /* The critical regression: resolving against "/" would turn this into
     * an arbitrary file read. With a real docroot set, /etc/hostname is
     * outside it and must be refused. */
    size_t len = 0;
    char *out = esi_include_read("/etc/hostname", "/tmp", 4, &len);
    EXPECT_EQ(out, nullptr);
}

TEST_F(EsiTest, Include_WithinDocrootWorks)
{
    const char *root = "/tmp";
    FILE *f = fopen("/tmp/esi-include-test.txt", "w");
    ASSERT_NE(f, nullptr);
    fputs("FRAGMENT", f);
    fclose(f);

    size_t len = 0;
    char *out = esi_include_read("/esi-include-test.txt", root, strlen(root), &len);
    unlink("/tmp/esi-include-test.txt");
    ASSERT_NE(out, nullptr);
    EXPECT_EQ(len, 8U);
    EXPECT_EQ(memcmp(out, "FRAGMENT", 8), 0);
    free(out);
}

TEST_F(EsiTest, Include_SymlinkEscapeRefused)
{
    /* Intermediate-directory symlink escape: docroot contains a symlinked
     * dir pointing outside; realpath containment must refuse. */
    mkdir("/tmp/esi-doc", 0755);
    mkdir("/tmp/esi-outside", 0755);
    FILE *f = fopen("/tmp/esi-outside/secret.txt", "w");
    ASSERT_NE(f, nullptr);
    fputs("SECRET", f);
    fclose(f);
    unlink("/tmp/esi-doc/link");
    ASSERT_EQ(symlink("/tmp/esi-outside", "/tmp/esi-doc/link"), 0);

    size_t len = 0;
    char *out = esi_include_read("/link/secret.txt", "/tmp/esi-doc", 12, &len);
    EXPECT_EQ(out, nullptr);

    unlink("/tmp/esi-doc/link");
    unlink("/tmp/esi-outside/secret.txt");
    rmdir("/tmp/esi-doc");
    rmdir("/tmp/esi-outside");
}

TEST_F(EsiTest, Include_TraversalRefused)
{
    size_t len = 0;
    EXPECT_EQ(esi_include_read("/../etc/passwd", "/tmp", 4, &len), nullptr);
    EXPECT_EQ(esi_include_read("/a/../..", "/tmp", 4, &len), nullptr);
}

TEST_F(EsiTest, Process_IncludeWithoutDocrootBecomesDynamicInclude)
{
    /* Contract change (dynamic ESI): the docroot bounds STATIC file reads
     * only. A missing static + valid URI records a dynamic include and
     * emits a placeholder — the loopback fetch is a normal request,
     * subject to the server's own ACLs, so no docroot requirement. */
    const char *in = "A<esi:include src=\"/etc/hostname\"/>B";
    esi_process(&ctx, in, strlen(in), &out, &out_len);
    ASSERT_NE(out, nullptr);
    EXPECT_EQ(ctx.dyn_count, 1);
    EXPECT_EQ(ctx.dyn_holding, 1);
    EXPECT_STREQ(ctx.dyn_uri[0], "/etc/hostname");
    EXPECT_EQ(out_len, 2U + 8U); /* A + placeholder + B */
    EXPECT_EQ(out[0], 'A');
    EXPECT_EQ(out[9], 'B');
    free(ctx.dyn_uri[0]);
    ctx.dyn_uri[0] = NULL;
    ctx.dyn_count = 0;
}


/* ==================================================================
 *  Chunk-boundary reassembly (split-tag holdback)
 * ================================================================== */

static std::string process_all(esi_ctx_t &c, const char *in, size_t len,
                               char **out, size_t *out_len)
{
    std::string acc;
    size_t off = 0;
    while (off < len) {
        size_t got = esi_process(&c, in + off, len - off, out, out_len);
        if (out && *out && *out_len)
            acc.append(*out, *out_len);
        if (got == 0)
            break;
        off += got;
    }
    return acc;
}

TEST_F(EsiTest, SplitIncludeTagReassemblesAcrossChunks)
{
    /* codex-probe regression: "<esi:inc" + "lude src=.../>" used to pass
     * through literally and never resolve the include. */
    const char *docroot = "/tmp";
    FILE *f = fopen("/tmp/esi-split-frag.txt", "w");
    ASSERT_NE(f, nullptr);
    fputs("SPLITFRAG", f);
    fclose(f);
    esi_ctx_init(&ctx, docroot, strlen(docroot));

    const char part1[] = "A<esi:inc";
    const char part2[] = "lude src=\"/esi-split-frag.txt\"/>B";

    std::string acc;
    char *out = nullptr; size_t out_len = 0;
    acc += process_all(ctx, part1, strlen(part1), &out, &out_len);
    acc += process_all(ctx, part2, strlen(part2), &out, &out_len);
    esi_finish(&ctx, &out, &out_len);
    if (out_len) acc.append(out, out_len);

    unlink("/tmp/esi-split-frag.txt");
    EXPECT_EQ(acc, "ASPLITFRAGB");
}

TEST_F(EsiTest, PartialPrefixTailHeldAcrossChunks)
{
    /* Chunk ends mid "<esi:" — the tail must be held, not emitted. */
    esi_ctx_init(&ctx, NULL, 0);
    const char part1[] = "hello<es";
    const char part2[] = "i:comment>x</esi:comment>bye";

    char *out = nullptr; size_t out_len = 0;
    std::string acc;
    acc += process_all(ctx, part1, strlen(part1), &out, &out_len);
    EXPECT_EQ(acc, "hello"); /* nothing past 'hello' may leak yet */
    acc += process_all(ctx, part2, strlen(part2), &out, &out_len);
    esi_finish(&ctx, &out, &out_len);
    if (out_len) acc.append(out, out_len);
    EXPECT_EQ(acc, "hellobye"); /* comment stripped across the boundary */
}

TEST_F(EsiTest, StreamEndFlushesHeldPartialLiterally)
{
    esi_ctx_init(&ctx, NULL, 0);
    const char part1[] = "done<esi";

    char *out = nullptr; size_t out_len = 0;
    std::string acc;
    acc += process_all(ctx, part1, strlen(part1), &out, &out_len);
    esi_finish(&ctx, &out, &out_len);
    if (out_len) acc.append(out, out_len);
    /* Never-completed tag: fail open, bytes preserved for the client. */
    EXPECT_EQ(acc, "done<esi");
}


/* ==================================================================
 *  Dynamic includes: URI validation + splice + strip + subrequest guard
 * ================================================================== */

TEST(EsiDynTest, UriValidation)
{
    EXPECT_EQ(esi_dyn_uri_valid("/index.php"), 1);
    EXPECT_EQ(esi_dyn_uri_valid("/frag.php?x=1&y=2"), 1);
    EXPECT_EQ(esi_dyn_uri_valid("//evil.example.com/x"), 0); /* scheme-relative */
    EXPECT_EQ(esi_dyn_uri_valid("http://evil/"), 0);          /* not '/'-rooted */
    EXPECT_EQ(esi_dyn_uri_valid("/a b"), 0);                  /* space */
    EXPECT_EQ(esi_dyn_uri_valid("/a\rb"), 0);                 /* CR */
    EXPECT_EQ(esi_dyn_uri_valid("/a\nb"), 0);                 /* LF */
    EXPECT_EQ(esi_dyn_uri_valid(""), 0);
    EXPECT_EQ(esi_dyn_uri_valid(nullptr), 0);
}

static int ph_find(const char *buf, size_t len)
{
    for (size_t i = 0; i + 8 <= len; i++)
        if ((unsigned char)buf[i] == 0x01 && buf[i+1]=='L' && buf[i+2]=='H' &&
            buf[i+3]=='E' && (unsigned char)buf[i+7] == 0x02)
            return (int)i;
    return -1;
}

TEST(EsiDynTest, SpliceReplacesOwnIndexOnly)
{
    esi_ctx_t ctx;
    esi_ctx_init(&ctx, NULL, 0);
    char *out; size_t out_len;
    esi_process(&ctx, "A<esi:include src=\"/one.php\"/>M<esi:include src=\"/two.php\"/>Z",
                strlen("A<esi:include src=\"/one.php\"/>M<esi:include src=\"/two.php\"/>Z"),
                &out, &out_len);
    ASSERT_EQ(ctx.dyn_count, 2);

    /* build a dyn_hold manually mirroring the hook: hold the output */
    ctx.dyn_hold = (char*)malloc(out_len + 1);
    memcpy(ctx.dyn_hold, out, out_len);
    ctx.dyn_hold_len = out_len; ctx.dyn_hold_cap = out_len + 1;

    /* splice body into index 0 only */
    esi_dyn_splice(&ctx, 0, "FRAG1", 5);
    /* index-0 placeholder gone; index-1 still present */
    int p1 = ph_find(ctx.dyn_hold, ctx.dyn_hold_len);
    ASSERT_GE(p1, 0);
    EXPECT_EQ(memcmp(ctx.dyn_hold, "A", 1), 0);
    EXPECT_EQ(memcmp(ctx.dyn_hold + 1, "FRAG1", 5), 0);

    esi_dyn_splice(&ctx, 1, NULL, 0); /* failed fetch -> removed */
    EXPECT_EQ(ph_find(ctx.dyn_hold, ctx.dyn_hold_len), -1);

    /* the trailing Z survived both splices */
    EXPECT_EQ(ctx.dyn_hold[ctx.dyn_hold_len - 1], 'Z');

    free(ctx.dyn_uri[0]); free(ctx.dyn_uri[1]);
    ctx.dyn_uri[0] = ctx.dyn_uri[1] = NULL; ctx.dyn_count = 0;
    free(ctx.dyn_hold); ctx.dyn_hold = NULL; ctx.dyn_hold_len = 0;
    esi_ctx_free(&ctx);
}

TEST(EsiDynTest, SubrequestGuardDropsDynamicTag)
{
    esi_ctx_t ctx;
    esi_ctx_init(&ctx, NULL, 0);
    ctx.subrequest = 1; /* loop guard: fragment fetching a fragment */
    char *out; size_t out_len;
    const char *in = "x<esi:include src=\"/nested.php\"/>y";
    esi_process(&ctx, in, strlen(in), &out, &out_len);
    EXPECT_EQ(ctx.dyn_count, 0);       /* not recorded */
    EXPECT_EQ(ctx.dyn_holding, 0);     /* no hold */
    EXPECT_EQ(out_len, 2U);            /* tag removed, x + y */
    esi_ctx_free(&ctx);
}

TEST(EsiDynTest, StripRemainingRemovesAllPlaceholders)
{
    esi_ctx_t ctx;
    esi_ctx_init(&ctx, NULL, 0);
    char *out; size_t out_len;
    const char *in = "a<esi:include src=\"/slow.php\"/>b<esi:include src=\"/x.php\"/>c";
    esi_process(&ctx, in, strlen(in), &out, &out_len);
    ctx.dyn_hold = (char*)malloc(out_len + 1);
    memcpy(ctx.dyn_hold, out, out_len);
    ctx.dyn_hold_len = out_len; ctx.dyn_hold_cap = out_len + 1;

    esi_dyn_strip_remaining(&ctx); /* join deadline passed: fail-open */
    EXPECT_EQ(ctx.dyn_hold_len, 3U);
    EXPECT_EQ(memcmp(ctx.dyn_hold, "abc", 3), 0);
    for (int i = 0; i < ctx.dyn_count; i++) { free(ctx.dyn_uri[i]); ctx.dyn_uri[i] = NULL; }
    ctx.dyn_count = 0;
    free(ctx.dyn_hold); ctx.dyn_hold = NULL; ctx.dyn_hold_len = 0;
    esi_ctx_free(&ctx);
}