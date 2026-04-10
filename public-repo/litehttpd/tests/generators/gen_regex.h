/**
 * gen_regex.h - Simple regex pattern generator for RapidCheck
 *
 * Generates simple random regex patterns suitable for FilesMatch,
 * RedirectMatch, and SetEnvIf directives. Patterns are kept simple
 * to avoid invalid regex compilation.
 *
 * Validates: Requirements 13.1
 */
#ifndef GEN_REGEX_H
#define GEN_REGEX_H

#include <rapidcheck.h>
#include <string>
#include <vector>

namespace gen {

/**
 * Character set safe for use inside regex character classes.
 */
static const std::string kRegexLiteralChars =
    "abcdefghijklmnopqrstuvwxyz0123456789";

/**
 * Generate a simple literal regex fragment (1-6 alphanumeric chars).
 */
inline rc::Gen<std::string> regexLiteral()
{
    return rc::gen::mapcat(
        rc::gen::inRange(1, 7),
        [](int len) {
            return rc::gen::map(
                rc::gen::container<std::vector<char>>(
                    (std::size_t)len,
                    rc::gen::elementOf(kRegexLiteralChars)),
                [](const std::vector<char> &v) {
                    return std::string(v.begin(), v.end());
                });
        });
}

/**
 * Generate a simple regex pattern.
 *
 * Produces patterns like:
 *   - "abc"           (literal)
 *   - "^abc"          (anchored start)
 *   - "abc$"          (anchored end)
 *   - "^abc$"         (fully anchored)
 *   - ".*\\.php"      (extension match)
 *   - "[a-z]+"        (character class)
 *   - "(abc)"         (capture group)
 */
inline rc::Gen<std::string> simpleRegex()
{
    return rc::gen::oneOf(
        /* Literal string */
        regexLiteral(),

        /* Anchored start: ^literal */
        rc::gen::map(regexLiteral(),
            [](const std::string &s) { return "^" + s; }),

        /* Anchored end: literal$ */
        rc::gen::map(regexLiteral(),
            [](const std::string &s) { return s + "$"; }),

        /* Fully anchored: ^literal$ */
        rc::gen::map(regexLiteral(),
            [](const std::string &s) { return "^" + s + "$"; }),

        /* Extension match: .*\.ext */
        rc::gen::map(regexLiteral(),
            [](const std::string &s) {
                return std::string(".*\\.") + s;
            }),

        /* Character class: [a-z]+ */
        rc::gen::just(std::string("[a-z]+")),

        /* Capture group: (literal) */
        rc::gen::map(regexLiteral(),
            [](const std::string &s) { return "(" + s + ")"; }),

        /* Dot-star prefix: .*literal */
        rc::gen::map(regexLiteral(),
            [](const std::string &s) { return ".*" + s; })
    );
}

/**
 * Generate a file-matching regex pattern (for FilesMatch).
 * Produces patterns like: .*\.php$, .*\.html$, ^index\.
 */
inline rc::Gen<std::string> fileMatchRegex()
{
    auto extensions = std::vector<std::string>{
        "php", "html", "css", "js", "png", "jpg", "gif", "txt"};
    return rc::gen::oneOf(
        /* .*\.ext$ */
        rc::gen::map(rc::gen::elementOf(extensions),
            [](const std::string &ext) {
                return ".*\\." + ext + "$";
            }),
        /* ^filename */
        rc::gen::map(regexLiteral(),
            [](const std::string &s) { return "^" + s; }),
        /* Simple literal */
        regexLiteral()
    );
}

} /* namespace gen */

#endif /* GEN_REGEX_H */
