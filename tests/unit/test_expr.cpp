/**
 * test_expr.cpp - Unit tests for AST-based expression engine and
 *                 If/ElseIf/Else parsing
 *
 * Tests:
 * - Expression parsing: ==, !=, =~, !~, <, >, <=, >=
 * - Integer comparisons: -eq, -ne, -lt, -le, -gt, -ge
 * - File tests: -f, -d, -s, -l, -e
 * - Boolean operators: &&, ||, !
 * - Parentheses and precedence
 * - IP match: -ipmatch, -R
 * - Functions: tolower, toupper
 * - Variable expansion patterns
 * - If/ElseIf/Else parsing via htaccess_parse()
 * - RewriteOptions/RewriteMap parsing
 * - Round-trip: parse -> to_string -> parse consistency
 * - Edge cases: empty, malformed, unbalanced parens, max depth
 */
#include <gtest/gtest.h>
#include <cstring>
#include <cstdlib>
#include <string>
#include <fstream>

#include "mock_lsiapi.h"

extern "C" {
#include "htaccess_parser.h"
#include "htaccess_directive.h"
#include "htaccess_expr.h"
#include "htaccess_printer.h"
}

class ExprTest : public ::testing::Test {
protected:
    void SetUp() override {
        mock_lsiapi::reset_global_state();
        session_.reset();
    }

    MockSession session_;

    htaccess_directive_t *parse(const std::string &content) {
        return htaccess_parse(content.c_str(), content.size(), "/test/.htaccess");
    }
};

/* ================================================================== */
/*  Expression parsing tests (updated for AST structure)               */
/* ================================================================== */

TEST_F(ExprTest, ParseEqualityComparison) {
    expr_node_t *expr = expr_parse("%{REQUEST_URI} == '/test'");
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->type, NODE_CMP_EQ);
    ASSERT_NE(expr->left, nullptr);
    EXPECT_EQ(expr->left->type, NODE_VAR);
    EXPECT_STREQ(expr->left->str_val, "%{REQUEST_URI}");
    ASSERT_NE(expr->right, nullptr);
    EXPECT_EQ(expr->right->type, NODE_STRING);
    EXPECT_STREQ(expr->right->str_val, "/test");
    expr_free(expr);
}

TEST_F(ExprTest, ParseInequalityComparison) {
    expr_node_t *expr = expr_parse("%{REQUEST_URI} != '/admin'");
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->type, NODE_CMP_NE);
    ASSERT_NE(expr->left, nullptr);
    EXPECT_STREQ(expr->left->str_val, "%{REQUEST_URI}");
    ASSERT_NE(expr->right, nullptr);
    EXPECT_STREQ(expr->right->str_val, "/admin");
    expr_free(expr);
}

TEST_F(ExprTest, ParseRegexMatch) {
    expr_node_t *expr = expr_parse("%{HTTP_HOST} =~ /^www\\./");
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->type, NODE_REGEX_MATCH);
    ASSERT_NE(expr->left, nullptr);
    EXPECT_STREQ(expr->left->str_val, "%{HTTP_HOST}");
    ASSERT_NE(expr->right, nullptr);
    EXPECT_STREQ(expr->right->str_val, "^www\\.");
    expr_free(expr);
}

TEST_F(ExprTest, ParseRegexNegation) {
    expr_node_t *expr = expr_parse("%{REQUEST_URI} !~ /\\.php$/");
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->type, NODE_REGEX_NOMATCH);
    ASSERT_NE(expr->left, nullptr);
    EXPECT_STREQ(expr->left->str_val, "%{REQUEST_URI}");
    ASSERT_NE(expr->right, nullptr);
    EXPECT_STREQ(expr->right->str_val, "\\.php$");
    expr_free(expr);
}

TEST_F(ExprTest, ParseFileExistsTest) {
    expr_node_t *expr = expr_parse("-f %{REQUEST_FILENAME}");
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->type, NODE_FILE_F);
    ASSERT_NE(expr->left, nullptr);
    EXPECT_EQ(expr->left->type, NODE_VAR);
    EXPECT_STREQ(expr->left->str_val, "%{REQUEST_FILENAME}");
    EXPECT_EQ(expr->right, nullptr);
    expr_free(expr);
}

TEST_F(ExprTest, ParseDirectoryTest) {
    expr_node_t *expr = expr_parse("-d /var/www/html");
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->type, NODE_FILE_D);
    ASSERT_NE(expr->left, nullptr);
    EXPECT_STREQ(expr->left->str_val, "/var/www/html");
    expr_free(expr);
}

TEST_F(ExprTest, ParseFileSizeTest) {
    expr_node_t *expr = expr_parse("-s /tmp/test.txt");
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->type, NODE_FILE_S);
    ASSERT_NE(expr->left, nullptr);
    EXPECT_STREQ(expr->left->str_val, "/tmp/test.txt");
    expr_free(expr);
}

TEST_F(ExprTest, ParseSymlinkTest) {
    expr_node_t *expr = expr_parse("-l /tmp/link");
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->type, NODE_FILE_L);
    ASSERT_NE(expr->left, nullptr);
    EXPECT_STREQ(expr->left->str_val, "/tmp/link");
    expr_free(expr);
}

TEST_F(ExprTest, ParseFileExistsE) {
    expr_node_t *expr = expr_parse("-e /tmp");
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->type, NODE_FILE_E);
    ASSERT_NE(expr->left, nullptr);
    EXPECT_STREQ(expr->left->str_val, "/tmp");
    expr_free(expr);
}

TEST_F(ExprTest, ParseNullReturnsNull) {
    EXPECT_EQ(expr_parse(nullptr), nullptr);
    EXPECT_EQ(expr_parse(""), nullptr);
}

TEST_F(ExprTest, ParseQuotedExpression) {
    expr_node_t *expr = expr_parse("\"%{REQUEST_URI} == '/test'\"");
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->type, NODE_CMP_EQ);
    expr_free(expr);
}

/* ================================================================== */
/*  Boolean operators: &&, ||, !                                       */
/* ================================================================== */

TEST_F(ExprTest, ParseAnd) {
    expr_node_t *expr = expr_parse("%{REQUEST_URI} == '/a' && %{HTTPS} == 'on'");
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->type, NODE_AND);
    ASSERT_NE(expr->left, nullptr);
    EXPECT_EQ(expr->left->type, NODE_CMP_EQ);
    ASSERT_NE(expr->right, nullptr);
    EXPECT_EQ(expr->right->type, NODE_CMP_EQ);
    expr_free(expr);
}

TEST_F(ExprTest, ParseOr) {
    expr_node_t *expr = expr_parse("%{REQUEST_URI} == '/a' || %{REQUEST_URI} == '/b'");
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->type, NODE_OR);
    ASSERT_NE(expr->left, nullptr);
    EXPECT_EQ(expr->left->type, NODE_CMP_EQ);
    ASSERT_NE(expr->right, nullptr);
    EXPECT_EQ(expr->right->type, NODE_CMP_EQ);
    expr_free(expr);
}

TEST_F(ExprTest, ParseNot) {
    expr_node_t *expr = expr_parse("!-f %{REQUEST_FILENAME}");
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->type, NODE_NOT);
    ASSERT_NE(expr->left, nullptr);
    EXPECT_EQ(expr->left->type, NODE_FILE_F);
    expr_free(expr);
}

TEST_F(ExprTest, ParseNotNested) {
    expr_node_t *expr = expr_parse("!(%{REQUEST_URI} == '/admin')");
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->type, NODE_NOT);
    ASSERT_NE(expr->left, nullptr);
    EXPECT_EQ(expr->left->type, NODE_CMP_EQ);
    expr_free(expr);
}

TEST_F(ExprTest, ParseAndOrPrecedence) {
    /* a && b || c should parse as (a && b) || c */
    expr_node_t *expr = expr_parse(
        "%{REQUEST_URI} == '/a' && %{HTTPS} == 'on' || %{REQUEST_METHOD} == 'POST'"
    );
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->type, NODE_OR);
    ASSERT_NE(expr->left, nullptr);
    EXPECT_EQ(expr->left->type, NODE_AND);
    ASSERT_NE(expr->right, nullptr);
    EXPECT_EQ(expr->right->type, NODE_CMP_EQ);
    expr_free(expr);
}

TEST_F(ExprTest, ParseParensOverridePrecedence) {
    /* (a || b) && c */
    expr_node_t *expr = expr_parse(
        "(%{REQUEST_URI} == '/a' || %{REQUEST_URI} == '/b') && %{HTTPS} == 'on'"
    );
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->type, NODE_AND);
    ASSERT_NE(expr->left, nullptr);
    EXPECT_EQ(expr->left->type, NODE_OR);
    ASSERT_NE(expr->right, nullptr);
    EXPECT_EQ(expr->right->type, NODE_CMP_EQ);
    expr_free(expr);
}

/* ================================================================== */
/*  Integer comparisons                                                */
/* ================================================================== */

TEST_F(ExprTest, ParseIntEq) {
    expr_node_t *expr = expr_parse("%{SERVER_PORT} -eq 443");
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->type, NODE_INT_EQ);
    ASSERT_NE(expr->left, nullptr);
    EXPECT_STREQ(expr->left->str_val, "%{SERVER_PORT}");
    ASSERT_NE(expr->right, nullptr);
    EXPECT_STREQ(expr->right->str_val, "443");
    expr_free(expr);
}

TEST_F(ExprTest, ParseIntLt) {
    expr_node_t *expr = expr_parse("%{SERVER_PORT} -lt 1024");
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->type, NODE_INT_LT);
    expr_free(expr);
}

TEST_F(ExprTest, ParseIntGe) {
    expr_node_t *expr = expr_parse("%{SERVER_PORT} -ge 443");
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->type, NODE_INT_GE);
    expr_free(expr);
}

/* ================================================================== */
/*  String comparisons: <, >, <=, >=                                   */
/* ================================================================== */

TEST_F(ExprTest, ParseCmpLt) {
    expr_node_t *expr = expr_parse("%{REQUEST_URI} < '/z'");
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->type, NODE_CMP_LT);
    expr_free(expr);
}

TEST_F(ExprTest, ParseCmpGe) {
    expr_node_t *expr = expr_parse("%{REQUEST_URI} >= '/a'");
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->type, NODE_CMP_GE);
    expr_free(expr);
}

/* ================================================================== */
/*  IP match                                                           */
/* ================================================================== */

TEST_F(ExprTest, ParseIpmatch) {
    expr_node_t *expr = expr_parse("-ipmatch '192.168.0.0/16'");
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->type, NODE_IPMATCH);
    ASSERT_NE(expr->left, nullptr);
    EXPECT_STREQ(expr->left->str_val, "192.168.0.0/16");
    EXPECT_EQ(expr->right, nullptr);  /* Unary form */
    expr_free(expr);
}

TEST_F(ExprTest, ParseIpmatchBinary) {
    expr_node_t *expr = expr_parse("%{REMOTE_ADDR} -ipmatch '10.0.0.0/8'");
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->type, NODE_IPMATCH);
    ASSERT_NE(expr->left, nullptr);
    EXPECT_STREQ(expr->left->str_val, "%{REMOTE_ADDR}");
    ASSERT_NE(expr->right, nullptr);
    EXPECT_STREQ(expr->right->str_val, "10.0.0.0/8");
    expr_free(expr);
}

/* ================================================================== */
/*  Function calls                                                     */
/* ================================================================== */

TEST_F(ExprTest, ParseTolower) {
    expr_node_t *expr = expr_parse("tolower(%{HTTP_HOST}) == 'example.com'");
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->type, NODE_CMP_EQ);
    ASSERT_NE(expr->left, nullptr);
    EXPECT_EQ(expr->left->type, NODE_FUNC_TOLOWER);
    ASSERT_NE(expr->left->left, nullptr);
    EXPECT_EQ(expr->left->left->type, NODE_VAR);
    EXPECT_STREQ(expr->left->left->str_val, "%{HTTP_HOST}");
    ASSERT_NE(expr->right, nullptr);
    EXPECT_STREQ(expr->right->str_val, "example.com");
    expr_free(expr);
}

TEST_F(ExprTest, ParseToupper) {
    expr_node_t *expr = expr_parse("toupper(%{REQUEST_METHOD}) == 'GET'");
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->type, NODE_CMP_EQ);
    ASSERT_NE(expr->left, nullptr);
    EXPECT_EQ(expr->left->type, NODE_FUNC_TOUPPER);
    expr_free(expr);
}

/* ================================================================== */
/*  Complex real-world patterns                                        */
/* ================================================================== */

TEST_F(ExprTest, ParseWordPressPattern) {
    /* !-f %{REQUEST_FILENAME} && !-d %{REQUEST_FILENAME} */
    expr_node_t *expr = expr_parse(
        "!-f %{REQUEST_FILENAME} && !-d %{REQUEST_FILENAME}"
    );
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->type, NODE_AND);

    /* Left: !-f ... */
    ASSERT_NE(expr->left, nullptr);
    EXPECT_EQ(expr->left->type, NODE_NOT);
    ASSERT_NE(expr->left->left, nullptr);
    EXPECT_EQ(expr->left->left->type, NODE_FILE_F);

    /* Right: !-d ... */
    ASSERT_NE(expr->right, nullptr);
    EXPECT_EQ(expr->right->type, NODE_NOT);
    ASSERT_NE(expr->right->left, nullptr);
    EXPECT_EQ(expr->right->left->type, NODE_FILE_D);

    expr_free(expr);
}

TEST_F(ExprTest, ParseHttpsOrProxy) {
    expr_node_t *expr = expr_parse(
        "%{HTTPS} == 'on' || %{HTTP:X-Forwarded-Proto} == 'https'"
    );
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->type, NODE_OR);
    ASSERT_NE(expr->left, nullptr);
    EXPECT_EQ(expr->left->type, NODE_CMP_EQ);
    ASSERT_NE(expr->right, nullptr);
    EXPECT_EQ(expr->right->type, NODE_CMP_EQ);
    /* Check right side has HTTP: variable */
    ASSERT_NE(expr->right->left, nullptr);
    EXPECT_STREQ(expr->right->left->str_val, "%{HTTP:X-Forwarded-Proto}");
    expr_free(expr);
}

TEST_F(ExprTest, ParseDeeplyNested) {
    /* ((a && b) || (c && d)) && !e */
    expr_node_t *expr = expr_parse(
        "((%{REQUEST_URI} == '/a' && %{HTTPS} == 'on') || "
        "(%{REQUEST_METHOD} == 'POST' && %{REQUEST_URI} == '/b')) && "
        "!-f %{REQUEST_FILENAME}"
    );
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->type, NODE_AND);
    ASSERT_NE(expr->left, nullptr);
    EXPECT_EQ(expr->left->type, NODE_OR);
    ASSERT_NE(expr->right, nullptr);
    EXPECT_EQ(expr->right->type, NODE_NOT);
    expr_free(expr);
}

/* ================================================================== */
/*  Evaluation tests                                                   */
/* ================================================================== */

TEST_F(ExprTest, EvalEqualityTrue) {
    session_.set_request_uri("/test");
    expr_node_t *expr = expr_parse("%{REQUEST_URI} == '/test'");
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr_eval(session_.handle(), expr), 1);
    expr_free(expr);
}

TEST_F(ExprTest, EvalEqualityFalse) {
    session_.set_request_uri("/other");
    expr_node_t *expr = expr_parse("%{REQUEST_URI} == '/test'");
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr_eval(session_.handle(), expr), 0);
    expr_free(expr);
}

TEST_F(ExprTest, EvalInequalityTrue) {
    session_.set_request_uri("/page");
    expr_node_t *expr = expr_parse("%{REQUEST_URI} != '/admin'");
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr_eval(session_.handle(), expr), 1);
    expr_free(expr);
}

TEST_F(ExprTest, EvalRegexMatch) {
    session_.set_request_uri("/test.php");
    expr_node_t *expr = expr_parse("%{REQUEST_URI} =~ /\\.php$/");
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr_eval(session_.handle(), expr), 1);
    expr_free(expr);
}

TEST_F(ExprTest, EvalRegexNoMatch) {
    session_.set_request_uri("/test.html");
    expr_node_t *expr = expr_parse("%{REQUEST_URI} =~ /\\.php$/");
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr_eval(session_.handle(), expr), 0);
    expr_free(expr);
}

TEST_F(ExprTest, EvalRegexNegMatch) {
    session_.set_request_uri("/test.html");
    expr_node_t *expr = expr_parse("%{REQUEST_URI} !~ /\\.php$/");
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr_eval(session_.handle(), expr), 1);
    expr_free(expr);
}

TEST_F(ExprTest, EvalFileExistsOnRealFile) {
    const char *path = "/tmp/ols_expr_test_file";
    std::ofstream f(path);
    f << "test";
    f.close();

    expr_node_t *expr = expr_parse("-f /tmp/ols_expr_test_file");
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr_eval(session_.handle(), expr), 1);
    expr_free(expr);
    std::remove(path);
}

TEST_F(ExprTest, EvalFileExistsOnMissing) {
    expr_node_t *expr = expr_parse("-f /tmp/nonexistent_ols_test_file_xyz");
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr_eval(session_.handle(), expr), 0);
    expr_free(expr);
}

TEST_F(ExprTest, EvalDirExists) {
    expr_node_t *expr = expr_parse("-d /tmp");
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr_eval(session_.handle(), expr), 1);
    expr_free(expr);
}

TEST_F(ExprTest, EvalNullExprReturnsZero) {
    EXPECT_EQ(expr_eval(session_.handle(), nullptr), 0);
}

/* --- Boolean evaluation --- */

TEST_F(ExprTest, EvalAndTrueTrue) {
    session_.set_request_uri("/test");
    session_.add_env_var("HTTPS", "on");
    expr_node_t *expr = expr_parse(
        "%{REQUEST_URI} == '/test' && %{HTTPS} == 'on'"
    );
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr_eval(session_.handle(), expr), 1);
    expr_free(expr);
}

TEST_F(ExprTest, EvalAndTrueFalse) {
    session_.set_request_uri("/test");
    session_.add_env_var("HTTPS", "off");
    expr_node_t *expr = expr_parse(
        "%{REQUEST_URI} == '/test' && %{HTTPS} == 'on'"
    );
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr_eval(session_.handle(), expr), 0);
    expr_free(expr);
}

TEST_F(ExprTest, EvalOrTrueFalse) {
    session_.set_request_uri("/test");
    session_.add_env_var("HTTPS", "off");
    expr_node_t *expr = expr_parse(
        "%{REQUEST_URI} == '/test' || %{HTTPS} == 'on'"
    );
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr_eval(session_.handle(), expr), 1);
    expr_free(expr);
}

TEST_F(ExprTest, EvalOrFalseFalse) {
    session_.set_request_uri("/other");
    session_.add_env_var("HTTPS", "off");
    expr_node_t *expr = expr_parse(
        "%{REQUEST_URI} == '/test' || %{HTTPS} == 'on'"
    );
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr_eval(session_.handle(), expr), 0);
    expr_free(expr);
}

TEST_F(ExprTest, EvalNotTrue) {
    /* -d /tmp is true, so !-d /tmp should be false */
    expr_node_t *expr = expr_parse("!-d /tmp");
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr_eval(session_.handle(), expr), 0);
    expr_free(expr);
}

TEST_F(ExprTest, EvalNotFalse) {
    /* -f /nonexistent is false, so !-f is true */
    expr_node_t *expr = expr_parse("!-f /tmp/nonexistent_ols_test_xyz");
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr_eval(session_.handle(), expr), 1);
    expr_free(expr);
}

TEST_F(ExprTest, EvalShortCircuitAnd) {
    /* If left side of && is false, right should not matter */
    session_.set_request_uri("/other");
    expr_node_t *expr = expr_parse(
        "%{REQUEST_URI} == '/test' && %{NONEXISTENT_VAR} == 'x'"
    );
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr_eval(session_.handle(), expr), 0);
    expr_free(expr);
}

TEST_F(ExprTest, EvalShortCircuitOr) {
    /* If left side of || is true, right should not matter */
    session_.set_request_uri("/test");
    expr_node_t *expr = expr_parse(
        "%{REQUEST_URI} == '/test' || %{NONEXISTENT_VAR} == 'x'"
    );
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr_eval(session_.handle(), expr), 1);
    expr_free(expr);
}

TEST_F(ExprTest, EvalParens) {
    /* (false || true) && true = true */
    session_.set_request_uri("/test");
    session_.add_env_var("HTTPS", "on");
    expr_node_t *expr = expr_parse(
        "(%{REQUEST_URI} == '/other' || %{REQUEST_URI} == '/test') && %{HTTPS} == 'on'"
    );
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr_eval(session_.handle(), expr), 1);
    expr_free(expr);
}

/* --- Integer evaluation --- */

TEST_F(ExprTest, EvalIntEq) {
    /* SERVER_PORT returns "80" by default */
    expr_node_t *expr = expr_parse("%{SERVER_PORT} -eq 80");
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr_eval(session_.handle(), expr), 1);
    expr_free(expr);
}

TEST_F(ExprTest, EvalIntNe) {
    expr_node_t *expr = expr_parse("%{SERVER_PORT} -ne 443");
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr_eval(session_.handle(), expr), 1);
    expr_free(expr);
}

TEST_F(ExprTest, EvalIntLt) {
    expr_node_t *expr = expr_parse("%{SERVER_PORT} -lt 1024");
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr_eval(session_.handle(), expr), 1);
    expr_free(expr);
}

TEST_F(ExprTest, EvalIntGt) {
    expr_node_t *expr = expr_parse("%{SERVER_PORT} -gt 1024");
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr_eval(session_.handle(), expr), 0);
    expr_free(expr);
}

TEST_F(ExprTest, EvalIntLe) {
    expr_node_t *expr = expr_parse("%{SERVER_PORT} -le 80");
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr_eval(session_.handle(), expr), 1);
    expr_free(expr);
}

TEST_F(ExprTest, EvalIntGe) {
    expr_node_t *expr = expr_parse("%{SERVER_PORT} -ge 80");
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr_eval(session_.handle(), expr), 1);
    expr_free(expr);
}

/* --- IP match evaluation --- */

TEST_F(ExprTest, EvalIpmatchV4True) {
    session_.set_client_ip("192.168.1.100");
    expr_node_t *expr = expr_parse("-ipmatch '192.168.0.0/16'");
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr_eval(session_.handle(), expr), 1);
    expr_free(expr);
}

TEST_F(ExprTest, EvalIpmatchV4False) {
    session_.set_client_ip("10.0.0.1");
    expr_node_t *expr = expr_parse("-ipmatch '192.168.0.0/16'");
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr_eval(session_.handle(), expr), 0);
    expr_free(expr);
}

TEST_F(ExprTest, EvalIpmatchBinary) {
    session_.set_client_ip("10.5.3.1");
    expr_node_t *expr = expr_parse("%{REMOTE_ADDR} -ipmatch '10.0.0.0/8'");
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr_eval(session_.handle(), expr), 1);
    expr_free(expr);
}

/* --- Function evaluation --- */

TEST_F(ExprTest, EvalTolower) {
    session_.add_request_header("Host", "EXAMPLE.COM");
    expr_node_t *expr = expr_parse("tolower(%{HTTP_HOST}) == 'example.com'");
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr_eval(session_.handle(), expr), 1);
    expr_free(expr);
}

TEST_F(ExprTest, EvalToupper) {
    session_.set_method("get");
    expr_node_t *expr = expr_parse("toupper(%{REQUEST_METHOD}) == 'GET'");
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr_eval(session_.handle(), expr), 1);
    expr_free(expr);
}

/* --- File existence test: -e --- */

TEST_F(ExprTest, EvalFileExistsE) {
    expr_node_t *expr = expr_parse("-e /tmp");
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr_eval(session_.handle(), expr), 1);
    expr_free(expr);

    expr = expr_parse("-e /tmp/nonexistent_ols_test_xyz");
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr_eval(session_.handle(), expr), 0);
    expr_free(expr);
}

/* --- String comparison operators --- */

TEST_F(ExprTest, EvalCmpLt) {
    session_.set_request_uri("/abc");
    expr_node_t *expr = expr_parse("%{REQUEST_URI} < '/xyz'");
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr_eval(session_.handle(), expr), 1);
    expr_free(expr);
}

TEST_F(ExprTest, EvalCmpGt) {
    session_.set_request_uri("/xyz");
    expr_node_t *expr = expr_parse("%{REQUEST_URI} > '/abc'");
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr_eval(session_.handle(), expr), 1);
    expr_free(expr);
}

/* ================================================================== */
/*  Variable expansion tests                                           */
/* ================================================================== */

TEST_F(ExprTest, ExpandRequestUri) {
    session_.set_request_uri("/hello");
    const char *val = expr_expand_var(session_.handle(), "%{REQUEST_URI}");
    EXPECT_STREQ(val, "/hello");
}

TEST_F(ExprTest, ExpandNonVariable) {
    const char *val = expr_expand_var(session_.handle(), "plain_string");
    EXPECT_STREQ(val, "plain_string");
}

TEST_F(ExprTest, ExpandNullSession) {
    const char *val = expr_expand_var(nullptr, "%{REQUEST_URI}");
    EXPECT_STREQ(val, "");
}

TEST_F(ExprTest, ExpandEmptyBraces) {
    const char *val = expr_expand_var(session_.handle(), "%{}");
    EXPECT_STREQ(val, "");
}

/* ================================================================== */
/*  Edge cases                                                         */
/* ================================================================== */

TEST_F(ExprTest, ParseEmpty) {
    EXPECT_EQ(expr_parse(""), nullptr);
    EXPECT_EQ(expr_parse("   "), nullptr);
    EXPECT_EQ(expr_parse("\"\""), nullptr);
}

TEST_F(ExprTest, ParseMalformed) {
    /* Incomplete operator */
    expr_node_t *expr = expr_parse("== 'value'");
    /* May or may not parse -- just ensure no crash */
    expr_free(expr);

    expr = expr_parse("%{VAR} ==");
    expr_free(expr);
}

TEST_F(ExprTest, ParseUnbalancedParens) {
    EXPECT_EQ(expr_parse("(%{VAR} == 'x'"), nullptr);
    EXPECT_EQ(expr_parse("%{VAR} == 'x')"), nullptr);
}

TEST_F(ExprTest, ParseMaxDepth) {
    /* Build an expression with 33 levels of nesting -- should fail */
    std::string deep = "";
    for (int i = 0; i < 33; i++) deep += "(";
    deep += "'a' == 'a'";
    for (int i = 0; i < 33; i++) deep += ")";
    EXPECT_EQ(expr_parse(deep.c_str()), nullptr);
}

/* ================================================================== */
/*  Deep copy (expr_clone)                                             */
/* ================================================================== */

TEST_F(ExprTest, CloneSimple) {
    expr_node_t *orig = expr_parse("%{REQUEST_URI} == '/test'");
    ASSERT_NE(orig, nullptr);

    expr_node_t *clone = expr_clone(orig);
    ASSERT_NE(clone, nullptr);
    EXPECT_EQ(clone->type, orig->type);
    ASSERT_NE(clone->left, nullptr);
    EXPECT_STREQ(clone->left->str_val, orig->left->str_val);
    /* Different pointer */
    EXPECT_NE(clone, orig);
    EXPECT_NE(clone->left, orig->left);

    expr_free(orig);
    expr_free(clone);
}

TEST_F(ExprTest, CloneComplex) {
    expr_node_t *orig = expr_parse(
        "!-f %{REQUEST_FILENAME} && !-d %{REQUEST_FILENAME}"
    );
    ASSERT_NE(orig, nullptr);

    expr_node_t *clone = expr_clone(orig);
    ASSERT_NE(clone, nullptr);
    EXPECT_EQ(clone->type, NODE_AND);

    /* Evaluate both -- should produce same result */
    EXPECT_EQ(
        expr_eval(session_.handle(), orig),
        expr_eval(session_.handle(), clone)
    );

    expr_free(orig);
    expr_free(clone);
}

TEST_F(ExprTest, CloneNull) {
    EXPECT_EQ(expr_clone(nullptr), nullptr);
}

/* ================================================================== */
/*  expr_to_string + round-trip                                        */
/* ================================================================== */

TEST_F(ExprTest, ToStringSimple) {
    expr_node_t *expr = expr_parse("%{REQUEST_URI} == '/test'");
    ASSERT_NE(expr, nullptr);
    char *str = expr_to_string(expr);
    ASSERT_NE(str, nullptr);
    EXPECT_NE(strstr(str, "%{REQUEST_URI}"), nullptr);
    EXPECT_NE(strstr(str, "=="), nullptr);
    EXPECT_NE(strstr(str, "/test"), nullptr);
    free(str);
    expr_free(expr);
}

TEST_F(ExprTest, ToStringFileTest) {
    expr_node_t *expr = expr_parse("-f %{REQUEST_FILENAME}");
    ASSERT_NE(expr, nullptr);
    char *str = expr_to_string(expr);
    ASSERT_NE(str, nullptr);
    EXPECT_NE(strstr(str, "-f"), nullptr);
    EXPECT_NE(strstr(str, "%{REQUEST_FILENAME}"), nullptr);
    free(str);
    expr_free(expr);
}

TEST_F(ExprTest, ToStringBooleanOps) {
    expr_node_t *expr = expr_parse(
        "!-f %{REQUEST_FILENAME} && !-d %{REQUEST_FILENAME}"
    );
    ASSERT_NE(expr, nullptr);
    char *str = expr_to_string(expr);
    ASSERT_NE(str, nullptr);
    EXPECT_NE(strstr(str, "&&"), nullptr);
    free(str);
    expr_free(expr);
}

TEST_F(ExprTest, RoundtripComplex) {
    const char *original = "!-f %{REQUEST_FILENAME} && !-d %{REQUEST_FILENAME}";
    expr_node_t *expr1 = expr_parse(original);
    ASSERT_NE(expr1, nullptr);

    char *str1 = expr_to_string(expr1);
    ASSERT_NE(str1, nullptr);

    /* Parse the printed output */
    expr_node_t *expr2 = expr_parse(str1);
    ASSERT_NE(expr2, nullptr);

    /* Both should evaluate the same */
    EXPECT_EQ(
        expr_eval(session_.handle(), expr1),
        expr_eval(session_.handle(), expr2)
    );

    /* Print again -- should be consistent */
    char *str2 = expr_to_string(expr2);
    ASSERT_NE(str2, nullptr);
    EXPECT_STREQ(str1, str2);

    free(str1);
    free(str2);
    expr_free(expr1);
    expr_free(expr2);
}

TEST_F(ExprTest, RoundtripRegex) {
    expr_node_t *expr1 = expr_parse("%{REQUEST_URI} =~ /\\.php$/");
    ASSERT_NE(expr1, nullptr);

    char *str = expr_to_string(expr1);
    ASSERT_NE(str, nullptr);

    expr_node_t *expr2 = expr_parse(str);
    ASSERT_NE(expr2, nullptr);
    EXPECT_EQ(expr2->type, NODE_REGEX_MATCH);

    free(str);
    expr_free(expr1);
    expr_free(expr2);
}

TEST_F(ExprTest, RoundtripOr) {
    expr_node_t *expr1 = expr_parse(
        "%{HTTPS} == 'on' || %{HTTP:X-Forwarded-Proto} == 'https'"
    );
    ASSERT_NE(expr1, nullptr);

    char *str1 = expr_to_string(expr1);
    ASSERT_NE(str1, nullptr);

    expr_node_t *expr2 = expr_parse(str1);
    ASSERT_NE(expr2, nullptr);
    EXPECT_EQ(expr2->type, NODE_OR);

    char *str2 = expr_to_string(expr2);
    ASSERT_NE(str2, nullptr);
    EXPECT_STREQ(str1, str2);

    free(str1);
    free(str2);
    expr_free(expr1);
    expr_free(expr2);
}

/* ================================================================== */
/*  If/ElseIf/Else parsing tests                                       */
/* ================================================================== */

TEST_F(ExprTest, ParseIfBlock) {
    auto *d = parse(
        "<If \"%{REQUEST_URI} == '/test'\">\n"
        "Header set X-Test \"yes\"\n"
        "</If>\n"
    );
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_IF);
    EXPECT_NE(d->data.if_block.condition, nullptr);
    EXPECT_NE(d->data.if_block.children, nullptr);
    EXPECT_EQ(d->data.if_block.children->type, DIR_HEADER_SET);
    htaccess_directives_free(d);
}

TEST_F(ExprTest, ParseIfElseChain) {
    auto *d = parse(
        "<If \"%{REQUEST_URI} == '/a'\">\n"
        "Header set X-Branch \"A\"\n"
        "</If>\n"
        "<ElseIf \"%{REQUEST_URI} == '/b'\">\n"
        "Header set X-Branch \"B\"\n"
        "</ElseIf>\n"
        "<Else>\n"
        "Header set X-Branch \"C\"\n"
        "</Else>\n"
    );
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_IF);

    int count = 0;
    for (htaccess_directive_t *n = d; n; n = n->next) {
        count++;
    }

    htaccess_directive_t *d2 = d->next;
    ASSERT_NE(d2, nullptr) << "Expected ElseIf as second node, got null. Total nodes: " << count;
    EXPECT_EQ(d2->type, DIR_ELSEIF) << "Second node type is " << d2->type << " (expected DIR_ELSEIF=74)";

    htaccess_directive_t *d3 = d2->next;
    ASSERT_NE(d3, nullptr) << "Expected Else as third node, got null. Total nodes: " << count;
    EXPECT_EQ(d3->type, DIR_ELSE) << "Third node type is " << d3->type << " (expected DIR_ELSE=75)";

    if (d3->type == DIR_ELSE) {
        EXPECT_EQ(d3->data.if_block.condition, nullptr);
        EXPECT_NE(d3->data.if_block.children, nullptr);
    }

    htaccess_directives_free(d);
}

TEST_F(ExprTest, ParseIfWithFileTest) {
    auto *d = parse(
        "<If \"-f %{REQUEST_FILENAME}\">\n"
        "Header set X-Exists \"yes\"\n"
        "</If>\n"
    );
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_IF);
    EXPECT_NE(d->data.if_block.condition, nullptr);

    expr_node_t *expr = (expr_node_t *)d->data.if_block.condition;
    EXPECT_EQ(expr->type, NODE_FILE_F);

    htaccess_directives_free(d);
}

TEST_F(ExprTest, ParseIfWithMultipleChildren) {
    auto *d = parse(
        "<If \"%{HTTPS} == 'on'\">\n"
        "Header set X-Secure \"yes\"\n"
        "Header set Strict-Transport-Security \"max-age=31536000\"\n"
        "</If>\n"
    );
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_IF);
    EXPECT_NE(d->data.if_block.children, nullptr);
    EXPECT_NE(d->data.if_block.children->next, nullptr);
    htaccess_directives_free(d);
}

/* --- If with boolean operators --- */

TEST_F(ExprTest, ParseIfWithAnd) {
    auto *d = parse(
        "<If \"!-f %{REQUEST_FILENAME} && !-d %{REQUEST_FILENAME}\">\n"
        "Header set X-Rewrite \"yes\"\n"
        "</If>\n"
    );
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_IF);
    EXPECT_NE(d->data.if_block.condition, nullptr);

    expr_node_t *expr = (expr_node_t *)d->data.if_block.condition;
    EXPECT_EQ(expr->type, NODE_AND);

    htaccess_directives_free(d);
}

TEST_F(ExprTest, ParseIfWithOr) {
    auto *d = parse(
        "<If \"%{HTTPS} == 'on' || %{HTTP:X-Forwarded-Proto} == 'https'\">\n"
        "Header set X-Secure \"yes\"\n"
        "</If>\n"
    );
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_IF);
    EXPECT_NE(d->data.if_block.condition, nullptr);

    expr_node_t *expr = (expr_node_t *)d->data.if_block.condition;
    EXPECT_EQ(expr->type, NODE_OR);

    htaccess_directives_free(d);
}

/* ================================================================== */
/*  RewriteOptions parsing tests                                       */
/* ================================================================== */

TEST_F(ExprTest, ParseRewriteOptions) {
    auto *d = parse("RewriteOptions Inherit\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_REWRITE_OPTIONS);
    EXPECT_NE(d->value, nullptr);
    EXPECT_STREQ(d->value, "Inherit");
    htaccess_directives_free(d);
}

TEST_F(ExprTest, ParseRewriteOptionsIgnoreInherit) {
    auto *d = parse("RewriteOptions IgnoreInherit\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_REWRITE_OPTIONS);
    EXPECT_STREQ(d->value, "IgnoreInherit");
    htaccess_directives_free(d);
}

/* ================================================================== */
/*  RewriteMap parsing tests                                           */
/* ================================================================== */

TEST_F(ExprTest, ParseRewriteMap) {
    auto *d = parse("RewriteMap lowercase int:tolower\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_REWRITE_MAP);
    EXPECT_STREQ(d->data.rewrite_map.map_name, "lowercase");
    EXPECT_STREQ(d->data.rewrite_map.map_type, "int");
    EXPECT_STREQ(d->data.rewrite_map.map_source, "tolower");
    htaccess_directives_free(d);
}

TEST_F(ExprTest, ParseRewriteMapTxt) {
    auto *d = parse("RewriteMap mymap txt:/etc/apache2/map.txt\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_REWRITE_MAP);
    EXPECT_STREQ(d->data.rewrite_map.map_name, "mymap");
    EXPECT_STREQ(d->data.rewrite_map.map_type, "txt");
    EXPECT_STREQ(d->data.rewrite_map.map_source, "/etc/apache2/map.txt");
    htaccess_directives_free(d);
}

/* ================================================================== */
/*  Printer round-trip tests                                           */
/* ================================================================== */

TEST_F(ExprTest, PrintRewriteOptions) {
    auto *d = parse("RewriteOptions Inherit\n");
    ASSERT_NE(d, nullptr);
    char *out = htaccess_print(d);
    ASSERT_NE(out, nullptr);
    EXPECT_NE(strstr(out, "RewriteOptions Inherit"), nullptr);
    free(out);
    htaccess_directives_free(d);
}

TEST_F(ExprTest, PrintRewriteMap) {
    auto *d = parse("RewriteMap lowercase int:tolower\n");
    ASSERT_NE(d, nullptr);
    char *out = htaccess_print(d);
    ASSERT_NE(out, nullptr);
    EXPECT_NE(strstr(out, "RewriteMap lowercase int:tolower"), nullptr);
    free(out);
    htaccess_directives_free(d);
}

TEST_F(ExprTest, PrintIfBlock) {
    auto *d = parse(
        "<If \"%{REQUEST_URI} == '/test'\">\n"
        "Header set X-Test \"yes\"\n"
        "</If>\n"
    );
    ASSERT_NE(d, nullptr);
    char *out = htaccess_print(d);
    ASSERT_NE(out, nullptr);
    EXPECT_NE(strstr(out, "<If"), nullptr);
    EXPECT_NE(strstr(out, "</If>"), nullptr);
    EXPECT_NE(strstr(out, "Header set"), nullptr);
    free(out);
    htaccess_directives_free(d);
}

TEST_F(ExprTest, PrintElseBlock) {
    auto *d = parse(
        "<If \"%{REQUEST_URI} == '/a'\">\n"
        "Header set X-Test \"A\"\n"
        "</If>\n"
        "<Else>\n"
        "Header set X-Test \"B\"\n"
        "</Else>\n"
    );
    ASSERT_NE(d, nullptr);
    char *out = htaccess_print(d);
    ASSERT_NE(out, nullptr);
    EXPECT_NE(strstr(out, "<Else>"), nullptr);
    EXPECT_NE(strstr(out, "</Else>"), nullptr);
    free(out);
    htaccess_directives_free(d);
}

/* If block nested inside <Files> container */
TEST_F(ExprTest, IfInsideFiles) {
    auto *d = parse(
        "<Files \"wp-login.php\">\n"
        "<If \"%{REQUEST_METHOD} == 'POST'\">\n"
        "Header set X-Login \"yes\"\n"
        "</If>\n"
        "</Files>\n"
    );
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_FILES);
    EXPECT_STREQ(d->name, "wp-login.php");

    auto *child = d->data.files.children;
    ASSERT_NE(child, nullptr);
    EXPECT_EQ(child->type, DIR_IF);

    auto *grandchild = child->data.if_block.children;
    ASSERT_NE(grandchild, nullptr);
    EXPECT_EQ(grandchild->type, DIR_HEADER_SET);

    htaccess_directives_free(d);
}

/* ================================================================== */
/*  Rebuild rewrite text tests                                         */
/* ================================================================== */

extern "C" {
#include "htaccess_exec_rewrite.h"
}

TEST_F(ExprTest, RebuildIncludesRewriteOptions) {
    auto *d = parse(
        "RewriteEngine On\n"
        "RewriteOptions Inherit\n"
        "RewriteRule ^test$ /new [L]\n"
    );
    ASSERT_NE(d, nullptr);
    int len = 0;
    char *text = rebuild_rewrite_text(d, &len);
    ASSERT_NE(text, nullptr);
    EXPECT_NE(strstr(text, "RewriteOptions Inherit"), nullptr);
    free(text);
    htaccess_directives_free(d);
}

TEST_F(ExprTest, RebuildIncludesRewriteMap) {
    auto *d = parse(
        "RewriteEngine On\n"
        "RewriteMap lowercase int:tolower\n"
        "RewriteRule ^test$ /new [L]\n"
    );
    ASSERT_NE(d, nullptr);
    int len = 0;
    char *text = rebuild_rewrite_text(d, &len);
    ASSERT_NE(text, nullptr);
    EXPECT_NE(strstr(text, "RewriteMap lowercase int:tolower"), nullptr);
    free(text);
    htaccess_directives_free(d);
}
