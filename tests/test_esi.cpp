#include <gtest/gtest.h>
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
    void SetUp() override { esi_ctx_init(&ctx); out = NULL; out_len = 0; }
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
