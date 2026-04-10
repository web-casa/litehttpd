/**
 * gen_directive.h - Directive generator for RapidCheck
 *
 * Generates random htaccess_directive_t* covering all 80 directive types.
 * Each generated directive is heap-allocated and must be freed by the caller
 * via htaccess_directives_free().
 *
 * Validates: Requirements 2.6, 13.1
 */
#ifndef GEN_DIRECTIVE_H
#define GEN_DIRECTIVE_H

#include <rapidcheck.h>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>

extern "C" {
#include "htaccess_directive.h"
}

#include "gen_header.h"
#include "gen_cidr.h"
#include "gen_regex.h"
#include "gen_expires.h"
#include "gen_options.h"
#include "gen_http_method.h"
#include "gen_mime.h"
#include "gen_extension.h"
#include "gen_require.h"

namespace gen {

/**
 * Helper: allocate a zeroed directive with given type and line number.
 */
inline htaccess_directive_t *allocDir(directive_type_t type, int line = 1)
{
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    if (d) {
        d->type = type;
        d->line_number = line;
    }
    return d;
}

/**
 * Generate a random directive_type_t covering all 28 enum values.
 */
inline rc::Gen<directive_type_t> directiveType()
{
    return rc::gen::element(
        DIR_HEADER_SET,
        DIR_HEADER_UNSET,
        DIR_HEADER_APPEND,
        DIR_HEADER_MERGE,
        DIR_HEADER_ADD,
        DIR_REQUEST_HEADER_SET,
        DIR_REQUEST_HEADER_UNSET,
        DIR_PHP_VALUE,
        DIR_PHP_FLAG,
        DIR_PHP_ADMIN_VALUE,
        DIR_PHP_ADMIN_FLAG,
        DIR_ORDER,
        DIR_ALLOW_FROM,
        DIR_DENY_FROM,
        DIR_REDIRECT,
        DIR_REDIRECT_MATCH,
        DIR_ERROR_DOCUMENT,
        DIR_FILES_MATCH,
        DIR_EXPIRES_ACTIVE,
        DIR_EXPIRES_BY_TYPE
    );
}

/**
 * Generate remaining v1 directive types (split due to rc::gen::element arity).
 */
inline rc::Gen<directive_type_t> directiveTypeExtra()
{
    return rc::gen::element(
        DIR_SETENV,
        DIR_SETENVIF,
        DIR_BROWSER_MATCH,
        DIR_BRUTE_FORCE_PROTECTION,
        DIR_BRUTE_FORCE_ALLOWED_ATTEMPTS,
        DIR_BRUTE_FORCE_WINDOW,
        DIR_BRUTE_FORCE_ACTION,
        DIR_BRUTE_FORCE_THROTTLE_DURATION
    );
}

/**
 * Generate v2 directive types — first batch.
 */
inline rc::Gen<directive_type_t> directiveTypeV2a()
{
    return rc::gen::element(
        DIR_IFMODULE,
        DIR_OPTIONS,
        DIR_FILES,
        DIR_HEADER_ALWAYS_SET,
        DIR_HEADER_ALWAYS_UNSET,
        DIR_HEADER_ALWAYS_APPEND,
        DIR_HEADER_ALWAYS_MERGE,
        DIR_HEADER_ALWAYS_ADD,
        DIR_EXPIRES_DEFAULT,
        DIR_REQUIRE_ALL_GRANTED,
        DIR_REQUIRE_ALL_DENIED,
        DIR_REQUIRE_IP,
        DIR_REQUIRE_NOT_IP,
        DIR_REQUIRE_ANY_OPEN,
        DIR_REQUIRE_ALL_OPEN,
        DIR_LIMIT,
        DIR_LIMIT_EXCEPT
    );
}

/**
 * Generate v2 directive types — second batch.
 */
inline rc::Gen<directive_type_t> directiveTypeV2b()
{
    return rc::gen::element(
        DIR_AUTH_TYPE,
        DIR_AUTH_NAME,
        DIR_AUTH_USER_FILE,
        DIR_REQUIRE_VALID_USER,
        DIR_ADD_HANDLER,
        DIR_SET_HANDLER,
        DIR_ADD_TYPE,
        DIR_DIRECTORY_INDEX,
        DIR_FORCE_TYPE,
        DIR_ADD_ENCODING,
        DIR_ADD_CHARSET,
        DIR_BRUTE_FORCE_X_FORWARDED_FOR,
        DIR_BRUTE_FORCE_WHITELIST,
        DIR_BRUTE_FORCE_PROTECT_PATH,
        DIR_SETENVIF_NOCASE
    );
}

/**
 * Generate v3 directive types — Header edit variants.
 */
inline rc::Gen<directive_type_t> directiveTypeV3()
{
    return rc::gen::element(
        DIR_HEADER_EDIT,
        DIR_HEADER_EDIT_STAR,
        DIR_HEADER_ALWAYS_EDIT,
        DIR_HEADER_ALWAYS_EDIT_STAR
    );
}

/**
 * Generate v4 directive types — Rewrite + Phase 1 additions.
 */
inline rc::Gen<directive_type_t> directiveTypeV4()
{
    return rc::gen::element(
        DIR_REWRITE_ENGINE,
        DIR_REWRITE_BASE,
        DIR_REWRITE_COND,
        DIR_REWRITE_RULE,
        DIR_ADD_DEFAULT_CHARSET,
        DIR_DEFAULT_TYPE,
        DIR_SATISFY
    );
}

/**
 * Generate v5 directive types — Phase 7b-9 additions.
 */
inline rc::Gen<directive_type_t> directiveTypeV5()
{
    return rc::gen::element(
        DIR_REWRITE_OPTIONS,
        DIR_REWRITE_MAP,
        DIR_IF,
        DIR_ELSEIF,
        DIR_ELSE,
        DIR_REQUIRE_ENV,
        DIR_REMOVE_TYPE,
        DIR_REMOVE_HANDLER,
        DIR_ACTION
    );
}

/**
 * Generate any of the 80 directive types (v1 + v2 + v3 + v4 + v5).
 * NOTE: not all types have dedicated generators — unsupported types
 * fall back to DIR_HEADER_SET. See directiveOfType() for details.
 */
inline rc::Gen<directive_type_t> anyDirectiveType()
{
    return rc::gen::oneOf(directiveType(), directiveTypeExtra(),
                          directiveTypeV2a(), directiveTypeV2b(),
                          directiveTypeV3(), directiveTypeV4(),
                          directiveTypeV5());
}

/**
 * Generate a random htaccess_directive_t* for a specific type.
 * The returned pointer is heap-allocated; caller must free.
 * Does NOT generate FilesMatch children (use directiveWithChildren for that).
 */
inline rc::Gen<htaccess_directive_t *> directiveOfType(directive_type_t type)
{
    switch (type) {

    case DIR_HEADER_SET:
    case DIR_HEADER_APPEND:
    case DIR_HEADER_MERGE:
    case DIR_HEADER_ADD:
        return rc::gen::map(
            rc::gen::pair(headerName(), headerValue()),
            [type](const std::pair<std::string, std::string> &p) {
                auto *d = allocDir(type);
                d->name = strdup(p.first.c_str());
                d->value = strdup(p.second.c_str());
                return d;
            });

    case DIR_HEADER_UNSET:
        return rc::gen::map(headerName(),
            [](const std::string &n) {
                auto *d = allocDir(DIR_HEADER_UNSET);
                d->name = strdup(n.c_str());
                return d;
            });

    case DIR_REQUEST_HEADER_SET:
        return rc::gen::map(
            rc::gen::pair(headerName(), headerValue()),
            [](const std::pair<std::string, std::string> &p) {
                auto *d = allocDir(DIR_REQUEST_HEADER_SET);
                d->name = strdup(p.first.c_str());
                d->value = strdup(p.second.c_str());
                return d;
            });

    case DIR_REQUEST_HEADER_UNSET:
        return rc::gen::map(headerName(),
            [](const std::string &n) {
                auto *d = allocDir(DIR_REQUEST_HEADER_UNSET);
                d->name = strdup(n.c_str());
                return d;
            });

    case DIR_PHP_VALUE:
    case DIR_PHP_ADMIN_VALUE:
        return rc::gen::map(
            rc::gen::pair(alphaIdent(), simpleValue()),
            [type](const std::pair<std::string, std::string> &p) {
                auto *d = allocDir(type);
                d->name = strdup(p.first.c_str());
                d->value = strdup(p.second.c_str());
                return d;
            });

    case DIR_PHP_FLAG:
    case DIR_PHP_ADMIN_FLAG:
        return rc::gen::map(
            rc::gen::pair(alphaIdent(), rc::gen::arbitrary<bool>()),
            [type](const std::pair<std::string, bool> &p) {
                auto *d = allocDir(type);
                d->name = strdup(p.first.c_str());
                d->value = strdup(p.second ? "on" : "off");
                return d;
            });

    case DIR_ORDER:
        return rc::gen::map(
            rc::gen::arbitrary<bool>(),
            [](bool allowDeny) {
                auto *d = allocDir(DIR_ORDER);
                d->data.acl.order = allowDeny ? ORDER_ALLOW_DENY
                                              : ORDER_DENY_ALLOW;
                return d;
            });

    case DIR_ALLOW_FROM:
    case DIR_DENY_FROM:
        return rc::gen::map(cidrOrAll(),
            [type](const std::string &v) {
                auto *d = allocDir(type);
                d->value = strdup(v.c_str());
                return d;
            });

    case DIR_REDIRECT:
        return rc::gen::map(
            rc::gen::tuple(
                rc::gen::element(301, 302, 303, 307, 308),
                simpleValue(),
                simpleValue()),
            [](const std::tuple<int, std::string, std::string> &t) {
                auto *d = allocDir(DIR_REDIRECT);
                d->data.redirect.status_code = std::get<0>(t);
                d->data.redirect.pattern = nullptr;
                d->name = strdup(("/" + std::get<1>(t)).c_str());
                d->value = strdup(("https://example.com/" + std::get<2>(t)).c_str());
                return d;
            });

    case DIR_REDIRECT_MATCH:
        return rc::gen::map(
            rc::gen::tuple(
                rc::gen::element(301, 302, 303, 307, 308),
                simpleRegex(),
                simpleValue()),
            [](const std::tuple<int, std::string, std::string> &t) {
                auto *d = allocDir(DIR_REDIRECT_MATCH);
                d->data.redirect.status_code = std::get<0>(t);
                d->data.redirect.pattern = strdup(std::get<1>(t).c_str());
                d->value = strdup(("https://example.com/" + std::get<2>(t)).c_str());
                return d;
            });

    case DIR_ERROR_DOCUMENT:
        return rc::gen::map(
            rc::gen::pair(
                rc::gen::element(400, 401, 403, 404, 500, 502, 503),
                simpleValue()),
            [](const std::pair<int, std::string> &p) {
                auto *d = allocDir(DIR_ERROR_DOCUMENT);
                d->data.error_doc.error_code = p.first;
                d->value = strdup(("/errors/" + p.second + ".html").c_str());
                return d;
            });

    case DIR_FILES_MATCH:
        return rc::gen::map(fileMatchRegex(),
            [](const std::string &pattern) {
                auto *d = allocDir(DIR_FILES_MATCH);
                d->data.files_match.pattern = strdup(pattern.c_str());
                d->data.files_match.children = nullptr;
                return d;
            });

    case DIR_EXPIRES_ACTIVE:
        return rc::gen::map(
            rc::gen::arbitrary<bool>(),
            [](bool on) {
                auto *d = allocDir(DIR_EXPIRES_ACTIVE);
                d->data.expires.active = on ? 1 : 0;
                return d;
            });

    case DIR_EXPIRES_BY_TYPE: {
        auto mimeTypes = std::vector<std::string>{
            "text/html", "text/css", "image/png", "image/jpeg",
            "application/javascript", "application/json"};
        return rc::gen::map(
            rc::gen::pair(
                rc::gen::elementOf(mimeTypes),
                expiresDuration()),
            [](const std::pair<std::string, ExpiresResult> &p) {
                auto *d = allocDir(DIR_EXPIRES_BY_TYPE);
                d->name = strdup(p.first.c_str());
                d->value = strdup(p.second.first.c_str());
                d->data.expires.duration_sec = p.second.second;
                return d;
            });
    }

    case DIR_SETENV:
        return rc::gen::map(
            rc::gen::pair(alphaIdent(), simpleValue()),
            [](const std::pair<std::string, std::string> &p) {
                auto *d = allocDir(DIR_SETENV);
                d->name = strdup(p.first.c_str());
                d->value = strdup(p.second.c_str());
                return d;
            });

    case DIR_SETENVIF: {
        auto attrs = std::vector<std::string>{
            "Remote_Addr", "Request_URI", "User-Agent"};
        return rc::gen::map(
            rc::gen::tuple(
                rc::gen::elementOf(attrs),
                simpleRegex(),
                alphaIdent(),
                simpleValue()),
            [](const std::tuple<std::string, std::string,
                                std::string, std::string> &t) {
                auto *d = allocDir(DIR_SETENVIF);
                d->data.envif.attribute = strdup(std::get<0>(t).c_str());
                d->data.envif.pattern = strdup(std::get<1>(t).c_str());
                d->name = strdup(std::get<2>(t).c_str());
                d->value = strdup(std::get<3>(t).c_str());
                return d;
            });
    }

    case DIR_BROWSER_MATCH:
        return rc::gen::map(
            rc::gen::tuple(simpleRegex(), alphaIdent(), simpleValue()),
            [](const std::tuple<std::string, std::string, std::string> &t) {
                auto *d = allocDir(DIR_BROWSER_MATCH);
                d->data.envif.attribute = strdup("User-Agent");
                d->data.envif.pattern = strdup(std::get<0>(t).c_str());
                d->name = strdup(std::get<1>(t).c_str());
                d->value = strdup(std::get<2>(t).c_str());
                return d;
            });

    case DIR_BRUTE_FORCE_PROTECTION:
        return rc::gen::map(
            rc::gen::arbitrary<bool>(),
            [](bool on) {
                auto *d = allocDir(DIR_BRUTE_FORCE_PROTECTION);
                d->data.brute_force.enabled = on ? 1 : 0;
                return d;
            });

    case DIR_BRUTE_FORCE_ALLOWED_ATTEMPTS:
        return rc::gen::map(
            rc::gen::inRange(1, 100),
            [](int n) {
                auto *d = allocDir(DIR_BRUTE_FORCE_ALLOWED_ATTEMPTS);
                d->data.brute_force.allowed_attempts = n;
                return d;
            });

    case DIR_BRUTE_FORCE_WINDOW:
        return rc::gen::map(
            rc::gen::inRange(1, 3600),
            [](int n) {
                auto *d = allocDir(DIR_BRUTE_FORCE_WINDOW);
                d->data.brute_force.window_sec = n;
                return d;
            });

    case DIR_BRUTE_FORCE_ACTION:
        return rc::gen::map(
            rc::gen::inRange(0, 3),
            [](int choice) {
                auto *d = allocDir(DIR_BRUTE_FORCE_ACTION);
                bf_action_t actions[] = {BF_ACTION_BLOCK, BF_ACTION_THROTTLE,
                                         BF_ACTION_LOG};
                d->data.brute_force.action = actions[choice];
                return d;
            });

    case DIR_BRUTE_FORCE_THROTTLE_DURATION:
        return rc::gen::map(
            rc::gen::inRange(100, 10000),
            [](int ms) {
                auto *d = allocDir(DIR_BRUTE_FORCE_THROTTLE_DURATION);
                d->data.brute_force.throttle_ms = ms;
                return d;
            });

    /* === v2 directive types === */

    case DIR_IFMODULE:
        return rc::gen::map(
            rc::gen::pair(
                rc::gen::element<std::string>("mod_rewrite.c", "mod_headers.c",
                    "mod_expires.c", "mod_deflate.c"),
                rc::gen::arbitrary<bool>()),
            [](const std::pair<std::string, bool> &p) {
                auto *d = allocDir(DIR_IFMODULE);
                d->name = strdup(p.first.c_str());
                d->data.ifmodule.negated = p.second ? 1 : 0;
                d->data.ifmodule.children = nullptr;
                return d;
            });

    case DIR_OPTIONS:
        return rc::gen::map(
            rc::gen::tuple(
                rc::gen::element(-1, 0, 1),
                rc::gen::element(-1, 0, 1),
                rc::gen::element(-1, 0, 1),
                rc::gen::element(-1, 0, 1)),
            [](const std::tuple<int, int, int, int> &t) {
                auto *d = allocDir(DIR_OPTIONS);
                d->data.options.indexes = std::get<0>(t);
                d->data.options.follow_symlinks = std::get<1>(t);
                d->data.options.multiviews = std::get<2>(t);
                d->data.options.exec_cgi = std::get<3>(t);
                return d;
            });

    case DIR_FILES:
        return rc::gen::map(
            rc::gen::element<std::string>("index.html", ".htaccess",
                "wp-config.php", "robots.txt"),
            [](const std::string &name) {
                auto *d = allocDir(DIR_FILES);
                d->name = strdup(name.c_str());
                d->data.files.children = nullptr;
                return d;
            });

    case DIR_HEADER_ALWAYS_SET:
    case DIR_HEADER_ALWAYS_APPEND:
    case DIR_HEADER_ALWAYS_MERGE:
    case DIR_HEADER_ALWAYS_ADD:
        return rc::gen::map(
            rc::gen::pair(headerName(), headerValue()),
            [type](const std::pair<std::string, std::string> &p) {
                auto *d = allocDir(type);
                d->name = strdup(p.first.c_str());
                d->value = strdup(p.second.c_str());
                return d;
            });

    case DIR_HEADER_ALWAYS_UNSET:
        return rc::gen::map(headerName(),
            [](const std::string &n) {
                auto *d = allocDir(DIR_HEADER_ALWAYS_UNSET);
                d->name = strdup(n.c_str());
                return d;
            });

    case DIR_EXPIRES_DEFAULT: {
        return rc::gen::map(expiresDuration(),
            [](const ExpiresResult &p) {
                auto *d = allocDir(DIR_EXPIRES_DEFAULT);
                d->value = strdup(p.first.c_str());
                d->data.expires.duration_sec = p.second;
                return d;
            });
    }

    case DIR_REQUIRE_ALL_GRANTED:
        return rc::gen::just(allocDir(DIR_REQUIRE_ALL_GRANTED));

    case DIR_REQUIRE_ALL_DENIED:
        return rc::gen::just(allocDir(DIR_REQUIRE_ALL_DENIED));

    case DIR_REQUIRE_IP:
        return rc::gen::map(cidrString(),
            [](const std::string &v) {
                auto *d = allocDir(DIR_REQUIRE_IP);
                d->value = strdup(v.c_str());
                return d;
            });

    case DIR_REQUIRE_NOT_IP:
        return rc::gen::map(cidrString(),
            [](const std::string &v) {
                auto *d = allocDir(DIR_REQUIRE_NOT_IP);
                d->value = strdup(v.c_str());
                return d;
            });

    case DIR_REQUIRE_ANY_OPEN:
        return rc::gen::just([]() {
            auto *d = allocDir(DIR_REQUIRE_ANY_OPEN);
            d->data.require_container.children = nullptr;
            return d;
        }());

    case DIR_REQUIRE_ALL_OPEN:
        return rc::gen::just([]() {
            auto *d = allocDir(DIR_REQUIRE_ALL_OPEN);
            d->data.require_container.children = nullptr;
            return d;
        }());

    case DIR_LIMIT:
    case DIR_LIMIT_EXCEPT:
        return rc::gen::map(
            rc::gen::element<std::string>("GET", "POST", "GET POST",
                "PUT DELETE", "GET POST PUT"),
            [type](const std::string &methods) {
                auto *d = allocDir(type);
                d->data.limit.methods = strdup(methods.c_str());
                d->data.limit.children = nullptr;
                return d;
            });

    case DIR_AUTH_TYPE:
        return rc::gen::just([]() {
            auto *d = allocDir(DIR_AUTH_TYPE);
            d->value = strdup("Basic");
            return d;
        }());

    case DIR_AUTH_NAME:
        return rc::gen::map(simpleValue(),
            [](const std::string &v) {
                auto *d = allocDir(DIR_AUTH_NAME);
                d->value = strdup(v.c_str());
                return d;
            });

    case DIR_AUTH_USER_FILE:
        return rc::gen::map(simpleValue(),
            [](const std::string &v) {
                auto *d = allocDir(DIR_AUTH_USER_FILE);
                d->value = strdup(("/etc/htpasswd/" + v).c_str());
                return d;
            });

    case DIR_REQUIRE_VALID_USER:
        return rc::gen::just(allocDir(DIR_REQUIRE_VALID_USER));

    case DIR_ADD_HANDLER:
        return rc::gen::map(
            rc::gen::pair(simpleValue(), simpleValue()),
            [](const std::pair<std::string, std::string> &p) {
                auto *d = allocDir(DIR_ADD_HANDLER);
                d->name = strdup(p.first.c_str());
                d->value = strdup(("." + p.second).c_str());
                return d;
            });

    case DIR_SET_HANDLER:
        return rc::gen::map(simpleValue(),
            [](const std::string &v) {
                auto *d = allocDir(DIR_SET_HANDLER);
                d->value = strdup(v.c_str());
                return d;
            });

    case DIR_ADD_TYPE:
        return rc::gen::map(
            rc::gen::pair(
                rc::gen::element<std::string>("text/html", "application/json",
                    "image/png", "text/css"),
                rc::gen::element<std::string>(".html", ".json", ".png", ".css")),
            [](const std::pair<std::string, std::string> &p) {
                auto *d = allocDir(DIR_ADD_TYPE);
                d->name = strdup(p.first.c_str());
                d->value = strdup(p.second.c_str());
                return d;
            });

    case DIR_DIRECTORY_INDEX:
        return rc::gen::map(
            rc::gen::element<std::string>("index.html", "index.php",
                "index.html index.php", "default.html"),
            [](const std::string &v) {
                auto *d = allocDir(DIR_DIRECTORY_INDEX);
                d->value = strdup(v.c_str());
                return d;
            });

    case DIR_FORCE_TYPE:
        return rc::gen::map(
            rc::gen::element<std::string>("text/html", "application/json",
                "text/plain", "application/octet-stream"),
            [](const std::string &v) {
                auto *d = allocDir(DIR_FORCE_TYPE);
                d->value = strdup(v.c_str());
                return d;
            });

    case DIR_ADD_ENCODING:
        return rc::gen::map(
            rc::gen::pair(
                rc::gen::element<std::string>("gzip", "deflate", "br"),
                rc::gen::element<std::string>(".gz", ".Z", ".br")),
            [](const std::pair<std::string, std::string> &p) {
                auto *d = allocDir(DIR_ADD_ENCODING);
                d->name = strdup(p.first.c_str());
                d->value = strdup(p.second.c_str());
                return d;
            });

    case DIR_ADD_CHARSET:
        return rc::gen::map(
            rc::gen::pair(
                rc::gen::element<std::string>("UTF-8", "ISO-8859-1", "Windows-1252"),
                rc::gen::element<std::string>(".html", ".txt", ".css")),
            [](const std::pair<std::string, std::string> &p) {
                auto *d = allocDir(DIR_ADD_CHARSET);
                d->name = strdup(p.first.c_str());
                d->value = strdup(p.second.c_str());
                return d;
            });

    case DIR_BRUTE_FORCE_X_FORWARDED_FOR:
        return rc::gen::map(
            rc::gen::arbitrary<bool>(),
            [](bool on) {
                auto *d = allocDir(DIR_BRUTE_FORCE_X_FORWARDED_FOR);
                d->data.brute_force.enabled = on ? 1 : 0;
                return d;
            });

    case DIR_BRUTE_FORCE_WHITELIST:
        return rc::gen::map(cidrString(),
            [](const std::string &v) {
                auto *d = allocDir(DIR_BRUTE_FORCE_WHITELIST);
                d->value = strdup(v.c_str());
                return d;
            });

    case DIR_BRUTE_FORCE_PROTECT_PATH:
        return rc::gen::map(
            rc::gen::element<std::string>("/wp-login.php", "/admin",
                "/login", "/xmlrpc.php"),
            [](const std::string &v) {
                auto *d = allocDir(DIR_BRUTE_FORCE_PROTECT_PATH);
                d->value = strdup(v.c_str());
                return d;
            });

    case DIR_SETENVIF_NOCASE: {
        auto attrs = std::vector<std::string>{
            "Remote_Addr", "Request_URI", "User-Agent"};
        return rc::gen::map(
            rc::gen::tuple(
                rc::gen::elementOf(attrs),
                simpleRegex(),
                alphaIdent(),
                simpleValue()),
            [](const std::tuple<std::string, std::string,
                                std::string, std::string> &t) {
                auto *d = allocDir(DIR_SETENVIF_NOCASE);
                d->data.envif.attribute = strdup(std::get<0>(t).c_str());
                d->data.envif.pattern = strdup(std::get<1>(t).c_str());
                d->name = strdup(std::get<2>(t).c_str());
                d->value = strdup(std::get<3>(t).c_str());
                return d;
            });
    }

    case DIR_HEADER_EDIT:
    case DIR_HEADER_EDIT_STAR:
    case DIR_HEADER_ALWAYS_EDIT:
    case DIR_HEADER_ALWAYS_EDIT_STAR:
        return rc::gen::map(
            rc::gen::tuple(headerName(), rc::gen::element(
                std::string("s-maxage=\\d+"),
                std::string("^(.*)$"),
                std::string("no-cache")),
                headerValue()),
            [type](const std::tuple<std::string, std::string, std::string> &t) {
                auto *d = allocDir(type);
                d->name = strdup(std::get<0>(t).c_str());
                d->data.header_ext.edit_pattern = strdup(std::get<1>(t).c_str());
                d->value = strdup(std::get<2>(t).c_str());
                return d;
            });

    /* --- Rewrite directives --- */

    case DIR_REWRITE_ENGINE:
        return rc::gen::map(
            rc::gen::element(std::string("On"), std::string("Off")),
            [](const std::string &v) {
                auto *d = allocDir(DIR_REWRITE_ENGINE);
                d->name = strdup(v.c_str());
                return d;
            });

    case DIR_REWRITE_BASE:
        return rc::gen::map(
            rc::gen::element(std::string("/"), std::string("/blog/"),
                             std::string("/app/public/")),
            [](const std::string &v) {
                auto *d = allocDir(DIR_REWRITE_BASE);
                d->value = strdup(v.c_str());
                return d;
            });

    case DIR_REWRITE_COND:
        return rc::gen::map(
            rc::gen::tuple(
                rc::gen::element(
                    std::string("%{REQUEST_FILENAME}"),
                    std::string("%{HTTP_HOST}"),
                    std::string("%{HTTPS}"),
                    std::string("%{REQUEST_URI}")),
                rc::gen::element(
                    std::string("!-f"), std::string("!-d"),
                    std::string("^www\\."), std::string("off")),
                rc::gen::element(0, 1),  /* nocase */
                rc::gen::element(0, 1)   /* or_next */
            ),
            [](const std::tuple<std::string, std::string, int, int> &t) {
                auto *d = allocDir(DIR_REWRITE_COND);
                d->name = strdup(std::get<0>(t).c_str());
                d->data.rewrite_cond.cond_pattern = strdup(std::get<1>(t).c_str());
                d->data.rewrite_cond.nocase = std::get<2>(t);
                d->data.rewrite_cond.or_next = std::get<3>(t);
                return d;
            });

    case DIR_REWRITE_RULE:
        return rc::gen::map(
            rc::gen::tuple(
                rc::gen::element(
                    std::string("^(.*)$"), std::string("."),
                    std::string("^old$"), std::string(".*")),
                rc::gen::element(
                    std::string("/index.php"), std::string("/new"),
                    std::string("-"), std::string("https://example.com/$1")),
                rc::gen::element(
                    std::string("[L]"), std::string("[R=301,L]"),
                    std::string("[L,QSA,NC]"), std::string("[F]"),
                    std::string(""))  /* no flags */
            ),
            [](const std::tuple<std::string, std::string, std::string> &t) {
                auto *d = allocDir(DIR_REWRITE_RULE);
                d->data.rewrite_rule.pattern = strdup(std::get<0>(t).c_str());
                d->value = strdup(std::get<1>(t).c_str());
                d->data.rewrite_rule.flags_raw =
                    std::get<2>(t).empty() ? NULL : strdup(std::get<2>(t).c_str());
                d->data.rewrite_rule.conditions = NULL;
                return d;
            });

    /* --- Phase 1 directives --- */

    case DIR_ADD_DEFAULT_CHARSET:
        return rc::gen::map(
            rc::gen::element(std::string("UTF-8"), std::string("ISO-8859-1"),
                             std::string("Off")),
            [](const std::string &v) {
                auto *d = allocDir(DIR_ADD_DEFAULT_CHARSET);
                d->value = strdup(v.c_str());
                return d;
            });

    case DIR_DEFAULT_TYPE:
        return rc::gen::map(
            gen::mime_type(),
            [](const std::string &v) {
                auto *d = allocDir(DIR_DEFAULT_TYPE);
                d->value = strdup(v.c_str());
                return d;
            });

    case DIR_SATISFY:
        return rc::gen::map(
            rc::gen::element(std::string("Any"), std::string("All")),
            [](const std::string &v) {
                auto *d = allocDir(DIR_SATISFY);
                d->value = strdup(v.c_str());
                return d;
            });

    /* --- Phase 7b-9 directives --- */

    case DIR_REWRITE_OPTIONS:
        return rc::gen::map(
            rc::gen::element(std::string("inherit"), std::string("IgnoreInherit")),
            [](const std::string &v) {
                auto *d = allocDir(DIR_REWRITE_OPTIONS);
                d->value = strdup(v.c_str());
                return d;
            });

    case DIR_REWRITE_MAP:
        return rc::gen::map(
            rc::gen::tuple(
                rc::gen::element(std::string("mymap"), std::string("lookupmap")),
                rc::gen::element(std::string("txt"), std::string("rnd"),
                                 std::string("int")),
                rc::gen::element(std::string("/etc/maps/map.txt"),
                                 std::string("/var/data/lookup.rnd"),
                                 std::string("toupper"))),
            [](const std::tuple<std::string, std::string, std::string> &t) {
                auto *d = allocDir(DIR_REWRITE_MAP);
                d->data.rewrite_map.map_name = strdup(std::get<0>(t).c_str());
                d->data.rewrite_map.map_type = strdup(std::get<1>(t).c_str());
                d->data.rewrite_map.map_source = strdup(std::get<2>(t).c_str());
                return d;
            });

    case DIR_IF:
    case DIR_ELSEIF:
        return rc::gen::just([type]() {
            auto *d = allocDir(type);
            d->data.if_block.condition = nullptr;
            d->data.if_block.children = nullptr;
            return d;
        }());

    case DIR_ELSE:
        return rc::gen::just([]() {
            auto *d = allocDir(DIR_ELSE);
            d->data.if_block.condition = nullptr;
            d->data.if_block.children = nullptr;
            return d;
        }());

    case DIR_REQUIRE_ENV:
        return rc::gen::map(
            rc::gen::element(std::string("VARNAME"), std::string("REDIRECT_STATUS"),
                             std::string("AUTH_TYPE")),
            [](const std::string &v) {
                auto *d = allocDir(DIR_REQUIRE_ENV);
                d->name = strdup(v.c_str());
                return d;
            });

    case DIR_REMOVE_TYPE:
        return rc::gen::map(
            rc::gen::element(std::string(".txt"), std::string(".html"),
                             std::string(".css")),
            [](const std::string &v) {
                auto *d = allocDir(DIR_REMOVE_TYPE);
                d->value = strdup(v.c_str());
                return d;
            });

    case DIR_REMOVE_HANDLER:
        return rc::gen::map(
            rc::gen::element(std::string(".cgi"), std::string(".php"),
                             std::string(".pl")),
            [](const std::string &v) {
                auto *d = allocDir(DIR_REMOVE_HANDLER);
                d->value = strdup(v.c_str());
                return d;
            });

    case DIR_ACTION:
        return rc::gen::map(
            rc::gen::pair(
                rc::gen::element(std::string("text/html"),
                                 std::string("application/x-httpd-cgi")),
                rc::gen::element(std::string("/handler.cgi"),
                                 std::string("/cgi-bin/action.cgi"))),
            [](const std::pair<std::string, std::string> &p) {
                auto *d = allocDir(DIR_ACTION);
                d->name = strdup(p.first.c_str());
                d->value = strdup(p.second.c_str());
                return d;
            });

    default:
        /* Fallback: generate a simple Header set directive */
        return rc::gen::map(
            rc::gen::pair(headerName(), headerValue()),
            [](const std::pair<std::string, std::string> &p) {
                auto *d = allocDir(DIR_HEADER_SET);
                d->name = strdup(p.first.c_str());
                d->value = strdup(p.second.c_str());
                return d;
            });
    }
}

/**
 * Generate a random directive of any non-container type.
 */
inline rc::Gen<htaccess_directive_t *> simpleDirective()
{
    return rc::gen::mapcat(
        anyDirectiveType(),
        [](directive_type_t type) {
            /* Skip container types to avoid recursion in simple contexts */
            if (type == DIR_FILES_MATCH || type == DIR_IFMODULE ||
                type == DIR_FILES || type == DIR_REQUIRE_ANY_OPEN ||
                type == DIR_REQUIRE_ALL_OPEN || type == DIR_LIMIT ||
                type == DIR_LIMIT_EXCEPT || type == DIR_IF ||
                type == DIR_ELSEIF || type == DIR_ELSE)
                return directiveOfType(DIR_HEADER_SET);
            return directiveOfType(type);
        });
}

/**
 * Generate a linked list of 1-N random directives.
 * Caller must free with htaccess_directives_free().
 */
inline rc::Gen<htaccess_directive_t *> directiveList(int maxCount = 8)
{
    return rc::gen::mapcat(
        rc::gen::inRange(1, maxCount + 1),
        [](int count) {
            return rc::gen::map(
                rc::gen::container<std::vector<htaccess_directive_t *>>(
                    (std::size_t)count,
                    simpleDirective()),
                [](const std::vector<htaccess_directive_t *> &dirs) {
                    /* Link into a list */
                    for (std::size_t i = 0; i + 1 < dirs.size(); i++)
                        dirs[i]->next = dirs[i + 1];
                    if (!dirs.empty())
                        dirs.back()->next = nullptr;
                    return dirs.empty() ? nullptr : dirs.front();
                });
        });
}

} /* namespace gen */

#endif /* GEN_DIRECTIVE_H */
