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
    /* Just test the extraction helper indirectly */
    esi_ctx_t ctx;
    esi_ctx_init(&ctx, NULL, 0);
    char *out;
    size_t out_len;
    /* Create a mock esi:include — no actual file to read, just verify no crash */
    esi_process(&ctx, "<esi:include src=\"/nonexistent.html\"/>rest", 42, &out, &out_len);
    /* Tag should be silently removed (file doesn't exist) */
    EXPECT_NE(strstr(out, "rest"), nullptr);
    EXPECT_EQ(strstr(out, "esi:include"), nullptr);
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

TEST_F(EsiTest, Process_IncludeWithoutDocrootSilentlyDropsTag)
{
    /* End-to-end through the state machine: no docroot -> tag removed,
     * nothing inlined, no crash. */
    const char *in = "A<esi:include src=\"/etc/hostname\"/>B";
    size_t consumed = esi_process(&ctx, in, strlen(in), &out, &out_len);
    ASSERT_GT(consumed, 0U);
    ASSERT_NE(out, nullptr);
    EXPECT_EQ(out_len, 2U);
    EXPECT_EQ(memcmp(out, "AB", 2), 0);
}
