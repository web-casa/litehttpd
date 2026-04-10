/**
 * gen_htaccess.h - .htaccess content string generator for RapidCheck
 *
 * Generates random valid .htaccess file content strings that can be
 * parsed by htaccess_parse(). Each generated string contains 1-N
 * directive lines covering all supported directive types.
 *
 * Validates: Requirements 2.6
 */
#ifndef GEN_HTACCESS_H
#define GEN_HTACCESS_H

#include <rapidcheck.h>
#include <string>
#include <vector>
#include <utility>

#include "gen_header.h"
#include "gen_cidr.h"
#include "gen_regex.h"
#include "gen_expires.h"
#include "gen_options.h"
#include "gen_http_method.h"
#include "gen_mime.h"
#include "gen_extension.h"

extern "C" {
#include "htaccess_directive.h"
}

namespace gen {

/**
 * Tagged directive line: the text line and its expected directive_type_t.
 */
using TaggedLine = std::pair<std::string, directive_type_t>;

/**
 * Generate a single random .htaccess directive line paired with its
 * expected directive type. Covers all parseable directive types.
 */
inline rc::Gen<TaggedLine> taggedDirectiveLine()
{
    return rc::gen::oneOf(
        /* Header set <name> <value> */
        rc::gen::map(
            rc::gen::pair(headerName(), simpleValue()),
            [](const std::pair<std::string, std::string> &p) -> TaggedLine {
                return {"Header set " + p.first + " " + p.second,
                        DIR_HEADER_SET};
            }),
        /* Header unset <name> */
        rc::gen::map(headerName(),
            [](const std::string &n) -> TaggedLine {
                return {"Header unset " + n, DIR_HEADER_UNSET};
            }),
        /* Header append <name> <value> */
        rc::gen::map(
            rc::gen::pair(headerName(), simpleValue()),
            [](const std::pair<std::string, std::string> &p) -> TaggedLine {
                return {"Header append " + p.first + " " + p.second,
                        DIR_HEADER_APPEND};
            }),
        /* Header merge <name> <value> */
        rc::gen::map(
            rc::gen::pair(headerName(), simpleValue()),
            [](const std::pair<std::string, std::string> &p) -> TaggedLine {
                return {"Header merge " + p.first + " " + p.second,
                        DIR_HEADER_MERGE};
            }),
        /* Header add <name> <value> */
        rc::gen::map(
            rc::gen::pair(headerName(), simpleValue()),
            [](const std::pair<std::string, std::string> &p) -> TaggedLine {
                return {"Header add " + p.first + " " + p.second,
                        DIR_HEADER_ADD};
            }),
        /* RequestHeader set <name> <value> */
        rc::gen::map(
            rc::gen::pair(headerName(), simpleValue()),
            [](const std::pair<std::string, std::string> &p) -> TaggedLine {
                return {"RequestHeader set " + p.first + " " + p.second,
                        DIR_REQUEST_HEADER_SET};
            }),
        /* RequestHeader unset <name> */
        rc::gen::map(headerName(),
            [](const std::string &n) -> TaggedLine {
                return {"RequestHeader unset " + n,
                        DIR_REQUEST_HEADER_UNSET};
            }),
        /* php_value <name> <value> */
        rc::gen::map(
            rc::gen::pair(alphaIdent(), simpleValue()),
            [](const std::pair<std::string, std::string> &p) -> TaggedLine {
                return {"php_value " + p.first + " " + p.second,
                        DIR_PHP_VALUE};
            }),
        /* php_flag <name> on|off */
        rc::gen::map(
            rc::gen::pair(alphaIdent(), rc::gen::arbitrary<bool>()),
            [](const std::pair<std::string, bool> &p) -> TaggedLine {
                return {"php_flag " + p.first + " " +
                        (p.second ? "on" : "off"),
                        DIR_PHP_FLAG};
            }),
        /* php_admin_value <name> <value> */
        rc::gen::map(
            rc::gen::pair(alphaIdent(), simpleValue()),
            [](const std::pair<std::string, std::string> &p) -> TaggedLine {
                return {"php_admin_value " + p.first + " " + p.second,
                        DIR_PHP_ADMIN_VALUE};
            }),
        /* php_admin_flag <name> on|off */
        rc::gen::map(
            rc::gen::pair(alphaIdent(), rc::gen::arbitrary<bool>()),
            [](const std::pair<std::string, bool> &p) -> TaggedLine {
                return {"php_admin_flag " + p.first + " " +
                        (p.second ? "on" : "off"),
                        DIR_PHP_ADMIN_FLAG};
            }),
        /* Order Allow,Deny / Deny,Allow */
        rc::gen::map(
            rc::gen::arbitrary<bool>(),
            [](bool ad) -> TaggedLine {
                return {ad ? "Order Allow,Deny" : "Order Deny,Allow",
                        DIR_ORDER};
            }),
        /* Allow from <cidr|all> */
        rc::gen::map(cidrOrAll(),
            [](const std::string &v) -> TaggedLine {
                return {"Allow from " + v, DIR_ALLOW_FROM};
            }),
        /* Deny from <cidr|all> */
        rc::gen::map(cidrOrAll(),
            [](const std::string &v) -> TaggedLine {
                return {"Deny from " + v, DIR_DENY_FROM};
            }),
        /* SetEnv <name> <value> */
        rc::gen::map(
            rc::gen::pair(alphaIdent(), simpleValue()),
            [](const std::pair<std::string, std::string> &p) -> TaggedLine {
                return {"SetEnv " + p.first + " " + p.second, DIR_SETENV};
            }),
        /* ExpiresActive On/Off */
        rc::gen::map(
            rc::gen::arbitrary<bool>(),
            [](bool on) -> TaggedLine {
                return {on ? "ExpiresActive On" : "ExpiresActive Off",
                        DIR_EXPIRES_ACTIVE};
            }),
        /* BruteForceProtection On/Off */
        rc::gen::map(
            rc::gen::arbitrary<bool>(),
            [](bool on) -> TaggedLine {
                return {on ? "BruteForceProtection On"
                           : "BruteForceProtection Off",
                        DIR_BRUTE_FORCE_PROTECTION};
            }),
        /* BruteForceAllowedAttempts <N> */
        rc::gen::map(
            rc::gen::inRange(1, 100),
            [](int n) -> TaggedLine {
                return {"BruteForceAllowedAttempts " + std::to_string(n),
                        DIR_BRUTE_FORCE_ALLOWED_ATTEMPTS};
            }),
        /* BruteForceWindow <N> */
        rc::gen::map(
            rc::gen::inRange(1, 3600),
            [](int n) -> TaggedLine {
                return {"BruteForceWindow " + std::to_string(n),
                        DIR_BRUTE_FORCE_WINDOW};
            }),
        /* BruteForceAction block|throttle */
        rc::gen::map(
            rc::gen::arbitrary<bool>(),
            [](bool block) -> TaggedLine {
                return {block ? "BruteForceAction block"
                              : "BruteForceAction throttle",
                        DIR_BRUTE_FORCE_ACTION};
            })
    );
}

/**
 * Generate v2 directive lines — first batch.
 */
inline rc::Gen<TaggedLine> taggedDirectiveLineV2a()
{
    return rc::gen::oneOf(
        /* Options +/-flags */
        rc::gen::map(options_line(),
            [](const std::string &s) -> TaggedLine {
                return {s, DIR_OPTIONS};
            }),
        /* Header always set <name> <value> */
        rc::gen::map(
            rc::gen::pair(headerName(), simpleValue()),
            [](const std::pair<std::string, std::string> &p) -> TaggedLine {
                return {"Header always set " + p.first + " " + p.second,
                        DIR_HEADER_ALWAYS_SET};
            }),
        /* Header always unset <name> */
        rc::gen::map(headerName(),
            [](const std::string &n) -> TaggedLine {
                return {"Header always unset " + n, DIR_HEADER_ALWAYS_UNSET};
            }),
        /* Header always append <name> <value> */
        rc::gen::map(
            rc::gen::pair(headerName(), simpleValue()),
            [](const std::pair<std::string, std::string> &p) -> TaggedLine {
                return {"Header always append " + p.first + " " + p.second,
                        DIR_HEADER_ALWAYS_APPEND};
            }),
        /* ExpiresDefault */
        rc::gen::map(expiresDuration(),
            [](const ExpiresResult &p) -> TaggedLine {
                return {"ExpiresDefault \"" + p.first + "\"",
                        DIR_EXPIRES_DEFAULT};
            }),
        /* Require all granted */
        rc::gen::just(TaggedLine{"Require all granted", DIR_REQUIRE_ALL_GRANTED}),
        /* Require all denied */
        rc::gen::just(TaggedLine{"Require all denied", DIR_REQUIRE_ALL_DENIED}),
        /* Require ip <cidr> */
        rc::gen::map(cidrString(),
            [](const std::string &v) -> TaggedLine {
                return {"Require ip " + v, DIR_REQUIRE_IP};
            }),
        /* Require not ip <cidr> */
        rc::gen::map(cidrString(),
            [](const std::string &v) -> TaggedLine {
                return {"Require not ip " + v, DIR_REQUIRE_NOT_IP};
            }),
        /* AuthType Basic */
        rc::gen::just(TaggedLine{"AuthType Basic", DIR_AUTH_TYPE}),
        /* AuthName <realm> */
        rc::gen::map(simpleValue(),
            [](const std::string &v) -> TaggedLine {
                return {"AuthName \"" + v + "\"", DIR_AUTH_NAME};
            }),
        /* AuthUserFile <path> */
        rc::gen::map(simpleValue(),
            [](const std::string &v) -> TaggedLine {
                return {"AuthUserFile /etc/htpasswd/" + v, DIR_AUTH_USER_FILE};
            }),
        /* Require valid-user */
        rc::gen::just(TaggedLine{"Require valid-user", DIR_REQUIRE_VALID_USER})
    );
}

/**
 * Generate v2 directive lines — second batch.
 */
inline rc::Gen<TaggedLine> taggedDirectiveLineV2b()
{
    return rc::gen::oneOf(
        /* AddHandler <handler> <ext> */
        rc::gen::map(
            rc::gen::pair(simpleValue(), file_extension()),
            [](const std::pair<std::string, std::string> &p) -> TaggedLine {
                return {"AddHandler " + p.first + " " + p.second,
                        DIR_ADD_HANDLER};
            }),
        /* SetHandler <handler> */
        rc::gen::map(simpleValue(),
            [](const std::string &v) -> TaggedLine {
                return {"SetHandler " + v, DIR_SET_HANDLER};
            }),
        /* AddType <mime> <ext> */
        rc::gen::map(
            rc::gen::pair(mime_type(), file_extension()),
            [](const std::pair<std::string, std::string> &p) -> TaggedLine {
                return {"AddType " + p.first + " " + p.second, DIR_ADD_TYPE};
            }),
        /* DirectoryIndex <files> */
        rc::gen::map(
            rc::gen::element<std::string>("index.html", "index.php",
                "index.html index.php", "default.html"),
            [](const std::string &v) -> TaggedLine {
                return {"DirectoryIndex " + v, DIR_DIRECTORY_INDEX};
            }),
        /* ForceType <mime> */
        rc::gen::map(mime_type(),
            [](const std::string &v) -> TaggedLine {
                return {"ForceType " + v, DIR_FORCE_TYPE};
            }),
        /* AddEncoding <enc> <ext> */
        rc::gen::map(
            rc::gen::pair(
                rc::gen::element<std::string>("gzip", "deflate", "br"),
                file_extension()),
            [](const std::pair<std::string, std::string> &p) -> TaggedLine {
                return {"AddEncoding " + p.first + " " + p.second,
                        DIR_ADD_ENCODING};
            }),
        /* AddCharset <charset> <ext> */
        rc::gen::map(
            rc::gen::pair(
                rc::gen::element<std::string>("UTF-8", "ISO-8859-1"),
                file_extension()),
            [](const std::pair<std::string, std::string> &p) -> TaggedLine {
                return {"AddCharset " + p.first + " " + p.second,
                        DIR_ADD_CHARSET};
            }),
        /* BruteForceXForwardedFor On/Off */
        rc::gen::map(
            rc::gen::arbitrary<bool>(),
            [](bool on) -> TaggedLine {
                return {on ? "BruteForceXForwardedFor On"
                           : "BruteForceXForwardedFor Off",
                        DIR_BRUTE_FORCE_X_FORWARDED_FOR};
            }),
        /* BruteForceWhitelist <cidr> */
        rc::gen::map(cidrString(),
            [](const std::string &v) -> TaggedLine {
                return {"BruteForceWhitelist " + v,
                        DIR_BRUTE_FORCE_WHITELIST};
            }),
        /* BruteForceProtectPath <path> */
        rc::gen::map(
            rc::gen::element<std::string>("/wp-login.php", "/admin",
                "/login", "/xmlrpc.php"),
            [](const std::string &v) -> TaggedLine {
                return {"BruteForceProtectPath " + v,
                        DIR_BRUTE_FORCE_PROTECT_PATH};
            })
    );
}

/**
 * Generate any tagged directive line (v1 + v2).
 */
inline rc::Gen<TaggedLine> anyTaggedDirectiveLine()
{
    return rc::gen::oneOf(
        taggedDirectiveLine(),
        taggedDirectiveLineV2a(),
        taggedDirectiveLineV2b()
    );
}

/**
 * Generate a random .htaccess file content string with 1-maxLines
 * directive lines (v1 only). Returns the content string.
 */
inline rc::Gen<std::string> htaccessContent(int maxLines = 10)
{
    return rc::gen::mapcat(
        rc::gen::inRange(1, maxLines + 1),
        [](int count) {
            return rc::gen::map(
                rc::gen::container<std::vector<TaggedLine>>(
                    (std::size_t)count,
                    taggedDirectiveLine()),
                [](const std::vector<TaggedLine> &lines) {
                    std::string content;
                    for (const auto &tl : lines)
                        content += tl.first + "\n";
                    return content;
                });
        });
}

/**
 * Generate a random .htaccess file content string with v1+v2 directives.
 */
inline rc::Gen<std::string> htaccessContentV2(int maxLines = 10)
{
    return rc::gen::mapcat(
        rc::gen::inRange(1, maxLines + 1),
        [](int count) {
            return rc::gen::map(
                rc::gen::container<std::vector<TaggedLine>>(
                    (std::size_t)count,
                    anyTaggedDirectiveLine()),
                [](const std::vector<TaggedLine> &lines) {
                    std::string content;
                    for (const auto &tl : lines)
                        content += tl.first + "\n";
                    return content;
                });
        });
}

/**
 * Generate tagged .htaccess content: the content string plus a vector
 * of expected directive types in order.
 */
using TaggedContent = std::pair<std::string, std::vector<directive_type_t>>;

inline rc::Gen<TaggedContent> taggedHtaccessContent(int maxLines = 10)
{
    return rc::gen::mapcat(
        rc::gen::inRange(1, maxLines + 1),
        [](int count) {
            return rc::gen::map(
                rc::gen::container<std::vector<TaggedLine>>(
                    (std::size_t)count,
                    taggedDirectiveLine()),
                [](const std::vector<TaggedLine> &lines) -> TaggedContent {
                    std::string content;
                    std::vector<directive_type_t> types;
                    for (const auto &tl : lines) {
                        content += tl.first + "\n";
                        types.push_back(tl.second);
                    }
                    return {content, types};
                });
        });
}

/**
 * Generate tagged .htaccess content with v1+v2 directives.
 */
inline rc::Gen<TaggedContent> taggedHtaccessContentV2(int maxLines = 10)
{
    return rc::gen::mapcat(
        rc::gen::inRange(1, maxLines + 1),
        [](int count) {
            return rc::gen::map(
                rc::gen::container<std::vector<TaggedLine>>(
                    (std::size_t)count,
                    anyTaggedDirectiveLine()),
                [](const std::vector<TaggedLine> &lines) -> TaggedContent {
                    std::string content;
                    std::vector<directive_type_t> types;
                    for (const auto &tl : lines) {
                        content += tl.first + "\n";
                        types.push_back(tl.second);
                    }
                    return {content, types};
                });
        });
}

} /* namespace gen */

#endif /* GEN_HTACCESS_H */
