/**
 * test_directive.cpp - Unit tests for htaccess_directive_t and related types
 *
 * Tests enum values, struct layout, and htaccess_directives_free().
 */
#include <gtest/gtest.h>
#include <cstring>
#include <cstdlib>

extern "C" {
#include "htaccess_directive.h"
}

/* ---- Helper: allocate a directive with strdup'd name/value ---- */

static htaccess_directive_t *make_directive(directive_type_t type,
                                            const char *name,
                                            const char *value,
                                            int line) {
    auto *d = static_cast<htaccess_directive_t *>(
        calloc(1, sizeof(htaccess_directive_t)));
    d->type = type;
    d->line_number = line;
    if (name)  d->name  = strdup(name);
    if (value) d->value = strdup(value);
    d->next = nullptr;
    return d;
}

/* ---- Enum value tests ---- */

TEST(DirectiveEnums, DirectiveTypeHas28Values) {
    /* The last enum value should be 27 (0-indexed) */
    EXPECT_EQ(static_cast<int>(DIR_HEADER_SET), 0);
    EXPECT_EQ(static_cast<int>(DIR_BRUTE_FORCE_THROTTLE_DURATION), 27);
}

TEST(DirectiveEnums, AclOrderValues) {
    EXPECT_EQ(static_cast<int>(ORDER_ALLOW_DENY), 0);
    EXPECT_EQ(static_cast<int>(ORDER_DENY_ALLOW), 1);
}

TEST(DirectiveEnums, BfActionValues) {
    EXPECT_EQ(static_cast<int>(BF_ACTION_BLOCK), 0);
    EXPECT_EQ(static_cast<int>(BF_ACTION_THROTTLE), 1);
}

/* ---- Struct field access tests ---- */

TEST(DirectiveStruct, BasicFieldAccess) {
    htaccess_directive_t d = {};
    d.type = DIR_HEADER_SET;
    d.line_number = 42;
    d.name = nullptr;
    d.value = nullptr;
    d.next = nullptr;

    EXPECT_EQ(d.type, DIR_HEADER_SET);
    EXPECT_EQ(d.line_number, 42);
    EXPECT_EQ(d.next, nullptr);
}

TEST(DirectiveStruct, UnionAclField) {
    htaccess_directive_t d = {};
    d.type = DIR_ORDER;
    d.data.acl.order = ORDER_DENY_ALLOW;
    EXPECT_EQ(d.data.acl.order, ORDER_DENY_ALLOW);
}

TEST(DirectiveStruct, UnionRedirectField) {
    htaccess_directive_t d = {};
    d.type = DIR_REDIRECT;
    d.data.redirect.status_code = 301;
    d.data.redirect.pattern = nullptr;
    EXPECT_EQ(d.data.redirect.status_code, 301);
}

TEST(DirectiveStruct, UnionErrorDocField) {
    htaccess_directive_t d = {};
    d.type = DIR_ERROR_DOCUMENT;
    d.data.error_doc.error_code = 404;
    EXPECT_EQ(d.data.error_doc.error_code, 404);
}

TEST(DirectiveStruct, UnionExpiresField) {
    htaccess_directive_t d = {};
    d.type = DIR_EXPIRES_BY_TYPE;
    d.data.expires.active = 1;
    d.data.expires.duration_sec = 3600;
    EXPECT_EQ(d.data.expires.active, 1);
    EXPECT_EQ(d.data.expires.duration_sec, 3600);
}

TEST(DirectiveStruct, UnionBruteForceField) {
    htaccess_directive_t d = {};
    d.type = DIR_BRUTE_FORCE_PROTECTION;
    d.data.brute_force.enabled = 1;
    d.data.brute_force.allowed_attempts = 10;
    d.data.brute_force.window_sec = 300;
    d.data.brute_force.action = BF_ACTION_THROTTLE;
    d.data.brute_force.throttle_ms = 5000;

    EXPECT_EQ(d.data.brute_force.enabled, 1);
    EXPECT_EQ(d.data.brute_force.allowed_attempts, 10);
    EXPECT_EQ(d.data.brute_force.window_sec, 300);
    EXPECT_EQ(d.data.brute_force.action, BF_ACTION_THROTTLE);
    EXPECT_EQ(d.data.brute_force.throttle_ms, 5000);
}

/* ---- Free function tests ---- */

TEST(DirectiveFree, FreeNullIsNoOp) {
    /* Should not crash */
    htaccess_directives_free(nullptr);
}

TEST(DirectiveFree, FreeSingleNode) {
    auto *d = make_directive(DIR_HEADER_SET, "X-Frame-Options", "DENY", 1);
    htaccess_directives_free(d);
    /* No crash = pass (memory checked by sanitizers) */
}

TEST(DirectiveFree, FreeLinkedList) {
    auto *d1 = make_directive(DIR_HEADER_SET, "H1", "V1", 1);
    auto *d2 = make_directive(DIR_HEADER_UNSET, "H2", nullptr, 2);
    auto *d3 = make_directive(DIR_PHP_VALUE, "upload_max", "64M", 3);
    d1->next = d2;
    d2->next = d3;
    htaccess_directives_free(d1);
}

TEST(DirectiveFree, FreeRedirectWithPattern) {
    auto *d = make_directive(DIR_REDIRECT_MATCH, nullptr, "/new-url", 5);
    d->data.redirect.status_code = 301;
    d->data.redirect.pattern = strdup("^/old/(.*)$");
    htaccess_directives_free(d);
}

TEST(DirectiveFree, FreeFilesMatchWithChildren) {
    /* Create a FilesMatch directive with nested children */
    auto *fm = make_directive(DIR_FILES_MATCH, nullptr, nullptr, 10);
    fm->data.files_match.pattern = strdup("\\.php$");

    auto *child1 = make_directive(DIR_HEADER_SET, "X-Content-Type-Options",
                                  "nosniff", 11);
    auto *child2 = make_directive(DIR_HEADER_SET, "X-Frame-Options",
                                  "SAMEORIGIN", 12);
    child1->next = child2;
    fm->data.files_match.children = child1;

    htaccess_directives_free(fm);
}

TEST(DirectiveFree, FreeSetEnvIfWithFields) {
    auto *d = make_directive(DIR_SETENVIF, "no_gzip", "1", 7);
    d->data.envif.attribute = strdup("User-Agent");
    d->data.envif.pattern = strdup("MSIE [1-6]");
    htaccess_directives_free(d);
}

TEST(DirectiveFree, FreeBrowserMatchWithFields) {
    auto *d = make_directive(DIR_BROWSER_MATCH, "no_gzip", "1", 8);
    d->data.envif.attribute = strdup("User-Agent");
    d->data.envif.pattern = strdup("Googlebot");
    htaccess_directives_free(d);
}

/* ---- v2 Enum value tests ---- */

TEST(DirectiveEnums, V2EnumValuesStartAt28) {
    /* P1: Panel core directives */
    EXPECT_EQ(static_cast<int>(DIR_IFMODULE), 28);
    EXPECT_EQ(static_cast<int>(DIR_OPTIONS), 29);
    EXPECT_EQ(static_cast<int>(DIR_FILES), 30);

    /* P2: Advanced directives */
    EXPECT_EQ(static_cast<int>(DIR_HEADER_ALWAYS_SET), 31);
    EXPECT_EQ(static_cast<int>(DIR_HEADER_ALWAYS_UNSET), 32);
    EXPECT_EQ(static_cast<int>(DIR_HEADER_ALWAYS_APPEND), 33);
    EXPECT_EQ(static_cast<int>(DIR_HEADER_ALWAYS_MERGE), 34);
    EXPECT_EQ(static_cast<int>(DIR_HEADER_ALWAYS_ADD), 35);
    EXPECT_EQ(static_cast<int>(DIR_EXPIRES_DEFAULT), 36);
    EXPECT_EQ(static_cast<int>(DIR_REQUIRE_ALL_GRANTED), 37);
    EXPECT_EQ(static_cast<int>(DIR_REQUIRE_ALL_DENIED), 38);
    EXPECT_EQ(static_cast<int>(DIR_REQUIRE_IP), 39);
    EXPECT_EQ(static_cast<int>(DIR_REQUIRE_NOT_IP), 40);
    EXPECT_EQ(static_cast<int>(DIR_REQUIRE_ANY_OPEN), 41);
    EXPECT_EQ(static_cast<int>(DIR_REQUIRE_ALL_OPEN), 42);
    EXPECT_EQ(static_cast<int>(DIR_LIMIT), 43);
    EXPECT_EQ(static_cast<int>(DIR_LIMIT_EXCEPT), 44);

    /* P3: Auth/Handler directives */
    EXPECT_EQ(static_cast<int>(DIR_AUTH_TYPE), 45);
    EXPECT_EQ(static_cast<int>(DIR_AUTH_NAME), 46);
    EXPECT_EQ(static_cast<int>(DIR_AUTH_USER_FILE), 47);
    EXPECT_EQ(static_cast<int>(DIR_REQUIRE_VALID_USER), 48);
    EXPECT_EQ(static_cast<int>(DIR_ADD_HANDLER), 49);
    EXPECT_EQ(static_cast<int>(DIR_SET_HANDLER), 50);
    EXPECT_EQ(static_cast<int>(DIR_ADD_TYPE), 51);
    EXPECT_EQ(static_cast<int>(DIR_DIRECTORY_INDEX), 52);

    /* P4: Low priority directives */
    EXPECT_EQ(static_cast<int>(DIR_FORCE_TYPE), 53);
    EXPECT_EQ(static_cast<int>(DIR_ADD_ENCODING), 54);
    EXPECT_EQ(static_cast<int>(DIR_ADD_CHARSET), 55);

    /* Brute force enhancements */
    EXPECT_EQ(static_cast<int>(DIR_BRUTE_FORCE_X_FORWARDED_FOR), 56);
    EXPECT_EQ(static_cast<int>(DIR_BRUTE_FORCE_WHITELIST), 57);
    EXPECT_EQ(static_cast<int>(DIR_BRUTE_FORCE_PROTECT_PATH), 58);
}

/* ---- v2 Container type free tests ---- */

TEST(DirectiveFree, FreeIfModuleWithChildren) {
    auto *d = make_directive(DIR_IFMODULE, "mod_rewrite.c", nullptr, 1);
    d->data.ifmodule.negated = 0;

    auto *child1 = make_directive(DIR_HEADER_SET, "X-Powered-By", "OLS", 2);
    auto *child2 = make_directive(DIR_PHP_VALUE, "memory_limit", "256M", 3);
    child1->next = child2;
    d->data.ifmodule.children = child1;

    htaccess_directives_free(d);
}

TEST(DirectiveFree, FreeFilesWithChildren) {
    auto *d = make_directive(DIR_FILES, "wp-config.php", nullptr, 10);

    auto *child = make_directive(DIR_REQUIRE_ALL_DENIED, nullptr, nullptr, 11);
    d->data.files.children = child;

    htaccess_directives_free(d);
}

TEST(DirectiveFree, FreeRequireAnyWithChildren) {
    auto *d = make_directive(DIR_REQUIRE_ANY_OPEN, nullptr, nullptr, 20);

    auto *child1 = make_directive(DIR_REQUIRE_ALL_GRANTED, nullptr, nullptr, 21);
    auto *child2 = make_directive(DIR_REQUIRE_IP, nullptr, "192.168.1.0/24", 22);
    child1->next = child2;
    d->data.require_container.children = child1;

    htaccess_directives_free(d);
}

TEST(DirectiveFree, FreeRequireAllWithChildren) {
    auto *d = make_directive(DIR_REQUIRE_ALL_OPEN, nullptr, nullptr, 30);

    auto *child1 = make_directive(DIR_REQUIRE_IP, nullptr, "10.0.0.0/8", 31);
    auto *child2 = make_directive(DIR_REQUIRE_VALID_USER, nullptr, nullptr, 32);
    child1->next = child2;
    d->data.require_container.children = child1;

    htaccess_directives_free(d);
}

TEST(DirectiveFree, FreeLimitWithChildren) {
    auto *d = make_directive(DIR_LIMIT, nullptr, nullptr, 40);
    d->data.limit.methods = strdup("GET POST");

    auto *child = make_directive(DIR_REQUIRE_ALL_DENIED, nullptr, nullptr, 41);
    d->data.limit.children = child;

    htaccess_directives_free(d);
}

TEST(DirectiveFree, FreeLimitExceptWithChildren) {
    auto *d = make_directive(DIR_LIMIT_EXCEPT, nullptr, nullptr, 50);
    d->data.limit.methods = strdup("GET HEAD");

    auto *child1 = make_directive(DIR_REQUIRE_ALL_DENIED, nullptr, nullptr, 51);
    auto *child2 = make_directive(DIR_REQUIRE_IP, nullptr, "172.16.0.0/12", 52);
    child1->next = child2;
    d->data.limit.children = child1;

    htaccess_directives_free(d);
}
