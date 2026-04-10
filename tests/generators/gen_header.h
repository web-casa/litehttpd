/**
 * gen_header.h - HTTP Header name and value generator for RapidCheck
 *
 * Generates random HTTP header names (X-Something style) and safe
 * header values without whitespace or quotes that could break parsing.
 *
 * Validates: Requirements 2.6
 */
#ifndef GEN_HEADER_H
#define GEN_HEADER_H

#include <rapidcheck.h>
#include <string>
#include <vector>

namespace gen {

static const std::string kAlphaChars =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
static const std::string kAlnumChars =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
static const std::string kHeaderValueChars =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_./:;=";

/**
 * Generate a simple alphanumeric identifier (1-12 chars, starts with letter).
 */
inline rc::Gen<std::string> alphaIdent()
{
    return rc::gen::mapcat(
        rc::gen::inRange(1, 12),
        [](int len) {
            return rc::gen::map(
                rc::gen::pair(
                    rc::gen::elementOf(kAlphaChars),
                    rc::gen::container<std::vector<char>>(
                        (std::size_t)(len - 1),
                        rc::gen::elementOf(kAlnumChars))),
                [](const std::pair<char, std::vector<char>> &p) {
                    return std::string(1, p.first) +
                           std::string(p.second.begin(), p.second.end());
                });
        });
}

/**
 * Generate a valid HTTP header name.
 * Produces names like X-Custom, Content-Type, Cache-Control, etc.
 */
inline rc::Gen<std::string> headerName()
{
    auto prefixes = std::vector<std::string>{
        "X", "Content", "Cache", "Accept", "Access", "Strict"};
    return rc::gen::map(
        rc::gen::pair(
            rc::gen::elementOf(prefixes),
            alphaIdent()),
        [](const std::pair<std::string, std::string> &p) {
            return p.first + "-" + p.second;
        });
}

/**
 * Generate a safe header value (1-30 chars, no whitespace or quotes).
 */
inline rc::Gen<std::string> headerValue()
{
    return rc::gen::mapcat(
        rc::gen::inRange(1, 31),
        [](int len) {
            return rc::gen::map(
                rc::gen::container<std::vector<char>>(
                    (std::size_t)len,
                    rc::gen::elementOf(kHeaderValueChars)),
                [](const std::vector<char> &v) {
                    return std::string(v.begin(), v.end());
                });
        });
}

/**
 * Generate a simple value string (1-20 chars, alphanumeric + -_.).
 * Suitable for directive values that must not contain whitespace.
 */
inline rc::Gen<std::string> simpleValue()
{
    static const std::string kSimpleValueChars =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.";
    return rc::gen::mapcat(
        rc::gen::inRange(1, 21),
        [](int len) {
            static const std::string chars =
                "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.";
            return rc::gen::map(
                rc::gen::container<std::vector<char>>(
                    (std::size_t)len,
                    rc::gen::elementOf(chars)),
                [](const std::vector<char> &v) {
                    return std::string(v.begin(), v.end());
                });
        });
}

} /* namespace gen */

#endif /* GEN_HEADER_H */
