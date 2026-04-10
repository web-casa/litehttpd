/**
 * prop_v2_roundtrip.cpp - Property 25: v2 directive parse/print round-trip
 *
 * Validates: Requirements 2.5, 3.8, 4.6, 5.6, 6.6, 7.5, 8.9, 9.8,
 *            10.10, 11.7, 12.5, 13.4, 14.4, 15.4, 16.9
 *
 * For any valid .htaccess content containing v2 directives,
 * parse → print → parse produces an equivalent directive list.
 */
#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>
#include <string>
#include <vector>
#include <cstring>

extern "C" {
#include "htaccess_directive.h"
#include "htaccess_parser.h"
#include "htaccess_printer.h"
}

#include "gen_header.h"
#include "gen_cidr.h"
#include "gen_regex.h"
#include "gen_expires.h"
#include "gen_htaccess.h"

/* ------------------------------------------------------------------ */
/*  Directive comparison — extended for v2 types                       */
/* ------------------------------------------------------------------ */

static bool directives_equivalent(const htaccess_directive_t *a,
                                  const htaccess_directive_t *b)
{
    while (a && b) {
        if (a->type != b->type)
            return false;

        /* Compare name */
        if ((a->name == nullptr) != (b->name == nullptr))
            return false;
        if (a->name && b->name && strcmp(a->name, b->name) != 0)
            return false;

        /* Compare value */
        if ((a->value == nullptr) != (b->value == nullptr))
            return false;
        if (a->value && b->value && strcmp(a->value, b->value) != 0)
            return false;

        /* Compare env_condition (Header env=VAR) */
        if ((a->env_condition == nullptr) != (b->env_condition == nullptr))
            return false;
        if (a->env_condition && b->env_condition &&
            strcmp(a->env_condition, b->env_condition) != 0)
            return false;

        /* Type-specific fields */
        switch (a->type) {
        case DIR_ORDER:
            if (a->data.acl.order != b->data.acl.order) return false;
            break;
        case DIR_REDIRECT:
        case DIR_REDIRECT_MATCH:
            if (a->data.redirect.status_code != b->data.redirect.status_code)
                return false;
            if ((a->data.redirect.pattern == nullptr) !=
                (b->data.redirect.pattern == nullptr))
                return false;
            if (a->data.redirect.pattern && b->data.redirect.pattern &&
                strcmp(a->data.redirect.pattern, b->data.redirect.pattern) != 0)
                return false;
            break;
        case DIR_ERROR_DOCUMENT:
            if (a->data.error_doc.error_code != b->data.error_doc.error_code)
                return false;
            break;
        case DIR_FILES_MATCH:
            if ((a->data.files_match.pattern == nullptr) !=
                (b->data.files_match.pattern == nullptr))
                return false;
            if (a->data.files_match.pattern && b->data.files_match.pattern &&
                strcmp(a->data.files_match.pattern, b->data.files_match.pattern) != 0)
                return false;
            if (!directives_equivalent(a->data.files_match.children,
                                       b->data.files_match.children))
                return false;
            break;
        case DIR_EXPIRES_ACTIVE:
            if (a->data.expires.active != b->data.expires.active) return false;
            break;
        case DIR_EXPIRES_BY_TYPE:
        case DIR_EXPIRES_DEFAULT:
            if (a->data.expires.duration_sec != b->data.expires.duration_sec)
                return false;
            break;
        case DIR_BRUTE_FORCE_PROTECTION:
        case DIR_BRUTE_FORCE_X_FORWARDED_FOR:
            if (a->data.brute_force.enabled != b->data.brute_force.enabled)
                return false;
            break;
        case DIR_BRUTE_FORCE_ALLOWED_ATTEMPTS:
            if (a->data.brute_force.allowed_attempts !=
                b->data.brute_force.allowed_attempts)
                return false;
            break;
        case DIR_BRUTE_FORCE_WINDOW:
            if (a->data.brute_force.window_sec != b->data.brute_force.window_sec)
                return false;
            break;
        case DIR_BRUTE_FORCE_ACTION:
            if (a->data.brute_force.action != b->data.brute_force.action)
                return false;
            break;
        case DIR_BRUTE_FORCE_THROTTLE_DURATION:
            if (a->data.brute_force.throttle_ms != b->data.brute_force.throttle_ms)
                return false;
            break;
        /* v2 container types */
        case DIR_IFMODULE:
            if (!directives_equivalent(a->data.ifmodule.children,
                                       b->data.ifmodule.children))
                return false;
            break;
        case DIR_OPTIONS:
            if (a->data.options.indexes != b->data.options.indexes) return false;
            if (a->data.options.follow_symlinks != b->data.options.follow_symlinks)
                return false;
            if (a->data.options.multiviews != b->data.options.multiviews)
                return false;
            if (a->data.options.exec_cgi != b->data.options.exec_cgi) return false;
            break;
        case DIR_FILES:
            if (!directives_equivalent(a->data.files.children,
                                       b->data.files.children))
                return false;
            break;
        case DIR_REQUIRE_ANY_OPEN:
        case DIR_REQUIRE_ALL_OPEN:
            if (!directives_equivalent(a->data.require_container.children,
                                       b->data.require_container.children))
                return false;
            break;
        case DIR_LIMIT:
        case DIR_LIMIT_EXCEPT:
            if ((a->data.limit.methods == nullptr) !=
                (b->data.limit.methods == nullptr))
                return false;
            if (a->data.limit.methods && b->data.limit.methods &&
                strcmp(a->data.limit.methods, b->data.limit.methods) != 0)
                return false;
            if (!directives_equivalent(a->data.limit.children,
                                       b->data.limit.children))
                return false;
            break;
        case DIR_SETENVIF:
        case DIR_SETENVIF_NOCASE:
        case DIR_BROWSER_MATCH:
            if ((a->data.envif.attribute == nullptr) !=
                (b->data.envif.attribute == nullptr))
                return false;
            if (a->data.envif.attribute && b->data.envif.attribute &&
                strcmp(a->data.envif.attribute, b->data.envif.attribute) != 0)
                return false;
            if ((a->data.envif.pattern == nullptr) !=
                (b->data.envif.pattern == nullptr))
                return false;
            if (a->data.envif.pattern && b->data.envif.pattern &&
                strcmp(a->data.envif.pattern, b->data.envif.pattern) != 0)
                return false;
            break;
        case DIR_HEADER_EDIT:
        case DIR_HEADER_EDIT_STAR:
        case DIR_HEADER_ALWAYS_EDIT:
        case DIR_HEADER_ALWAYS_EDIT_STAR:
            if ((a->data.header_ext.edit_pattern == nullptr) !=
                (b->data.header_ext.edit_pattern == nullptr))
                return false;
            if (a->data.header_ext.edit_pattern &&
                b->data.header_ext.edit_pattern &&
                strcmp(a->data.header_ext.edit_pattern,
                       b->data.header_ext.edit_pattern) != 0)
                return false;
            break;
        default:
            break;
        }

        a = a->next;
        b = b->next;
    }
    return (a == nullptr && b == nullptr);
}

/* ------------------------------------------------------------------ */
/*  Property tests                                                     */
/* ------------------------------------------------------------------ */

/**
 * Property 25a: v2 simple directive round-trip.
 * Uses anyTaggedDirectiveLine() which covers all v1+v2 non-container types.
 */
RC_GTEST_PROP(V2RoundTrip, SimpleDirectivesRoundTrip, ())
{
    auto tc = *gen::taggedHtaccessContentV2(8);
    const std::string &content = tc.first;

    htaccess_directive_t *parsed1 = htaccess_parse(
        content.c_str(), content.size(), "v2_rt_test");
    RC_ASSERT(parsed1 != nullptr);

    char *printed = htaccess_print(parsed1);
    RC_ASSERT(printed != nullptr);

    htaccess_directive_t *parsed2 = htaccess_parse(
        printed, strlen(printed), "v2_rt_reparse");
    RC_ASSERT(parsed2 != nullptr);

    RC_ASSERT(directives_equivalent(parsed1, parsed2));

    free(printed);
    htaccess_directives_free(parsed1);
    htaccess_directives_free(parsed2);
}

/**
 * Property 25b: IfModule container round-trip.
 */
RC_GTEST_PROP(V2RoundTrip, IfModuleContainerRoundTrip, ())
{
    auto innerLine = *gen::taggedDirectiveLine();
    std::string content = "<IfModule mod_headers.c>\n" +
                          innerLine.first + "\n</IfModule>\n";

    htaccess_directive_t *parsed1 = htaccess_parse(
        content.c_str(), content.size(), "ifmod_rt");
    RC_ASSERT(parsed1 != nullptr);
    RC_ASSERT(parsed1->type == DIR_IFMODULE);

    char *printed = htaccess_print(parsed1);
    RC_ASSERT(printed != nullptr);

    htaccess_directive_t *parsed2 = htaccess_parse(
        printed, strlen(printed), "ifmod_rt2");
    RC_ASSERT(parsed2 != nullptr);
    RC_ASSERT(directives_equivalent(parsed1, parsed2));

    free(printed);
    htaccess_directives_free(parsed1);
    htaccess_directives_free(parsed2);
}

/**
 * Property 25c: Files container round-trip.
 */
RC_GTEST_PROP(V2RoundTrip, FilesContainerRoundTrip, ())
{
    auto innerLine = *gen::taggedDirectiveLine();
    auto filename = *rc::gen::element<std::string>(
        "index.html", ".htaccess", "wp-config.php");
    std::string content = "<Files " + filename + ">\n" +
                          innerLine.first + "\n</Files>\n";

    htaccess_directive_t *parsed1 = htaccess_parse(
        content.c_str(), content.size(), "files_rt");
    RC_ASSERT(parsed1 != nullptr);
    RC_ASSERT(parsed1->type == DIR_FILES);

    char *printed = htaccess_print(parsed1);
    RC_ASSERT(printed != nullptr);

    htaccess_directive_t *parsed2 = htaccess_parse(
        printed, strlen(printed), "files_rt2");
    RC_ASSERT(parsed2 != nullptr);
    RC_ASSERT(directives_equivalent(parsed1, parsed2));

    free(printed);
    htaccess_directives_free(parsed1);
    htaccess_directives_free(parsed2);
}

/**
 * Property 25d: Limit container round-trip.
 */
RC_GTEST_PROP(V2RoundTrip, LimitContainerRoundTrip, ())
{
    auto methods = *rc::gen::element<std::string>(
        "GET", "POST", "GET POST", "PUT DELETE");
    std::string content = "<Limit " + methods + ">\nRequire all granted\n</Limit>\n";

    htaccess_directive_t *parsed1 = htaccess_parse(
        content.c_str(), content.size(), "limit_rt");
    RC_ASSERT(parsed1 != nullptr);
    RC_ASSERT(parsed1->type == DIR_LIMIT);

    char *printed = htaccess_print(parsed1);
    RC_ASSERT(printed != nullptr);

    htaccess_directive_t *parsed2 = htaccess_parse(
        printed, strlen(printed), "limit_rt2");
    RC_ASSERT(parsed2 != nullptr);
    RC_ASSERT(directives_equivalent(parsed1, parsed2));

    free(printed);
    htaccess_directives_free(parsed1);
    htaccess_directives_free(parsed2);
}

/**
 * Property 25e: LimitExcept container round-trip.
 */
RC_GTEST_PROP(V2RoundTrip, LimitExceptContainerRoundTrip, ())
{
    auto methods = *rc::gen::element<std::string>(
        "GET", "POST", "GET POST");
    std::string content = "<LimitExcept " + methods +
                          ">\nRequire all denied\n</LimitExcept>\n";

    htaccess_directive_t *parsed1 = htaccess_parse(
        content.c_str(), content.size(), "limex_rt");
    RC_ASSERT(parsed1 != nullptr);
    RC_ASSERT(parsed1->type == DIR_LIMIT_EXCEPT);

    char *printed = htaccess_print(parsed1);
    RC_ASSERT(printed != nullptr);

    htaccess_directive_t *parsed2 = htaccess_parse(
        printed, strlen(printed), "limex_rt2");
    RC_ASSERT(parsed2 != nullptr);
    RC_ASSERT(directives_equivalent(parsed1, parsed2));

    free(printed);
    htaccess_directives_free(parsed1);
    htaccess_directives_free(parsed2);
}

/**
 * Property 25f: RequireAny container round-trip.
 */
RC_GTEST_PROP(V2RoundTrip, RequireAnyContainerRoundTrip, ())
{
    auto cidr = *gen::cidrString();
    std::string content = "<RequireAny>\nRequire ip " + cidr +
                          "\nRequire all granted\n</RequireAny>\n";

    htaccess_directive_t *parsed1 = htaccess_parse(
        content.c_str(), content.size(), "reqany_rt");
    RC_ASSERT(parsed1 != nullptr);
    RC_ASSERT(parsed1->type == DIR_REQUIRE_ANY_OPEN);

    char *printed = htaccess_print(parsed1);
    RC_ASSERT(printed != nullptr);

    htaccess_directive_t *parsed2 = htaccess_parse(
        printed, strlen(printed), "reqany_rt2");
    RC_ASSERT(parsed2 != nullptr);
    RC_ASSERT(directives_equivalent(parsed1, parsed2));

    free(printed);
    htaccess_directives_free(parsed1);
    htaccess_directives_free(parsed2);
}
