/**
 * prop_limit.cpp - Property-based test for Limit/LimitExcept duality
 *
 * Feature: htaccess-v2-enhancements, Property 36
 *
 * Property 36: For any HTTP method list M and request method R,
 * Limit executes children iff R ∈ M, LimitExcept executes children iff R ∉ M.
 * Limit and LimitExcept are complementary for the same method list.
 *
 * Validates: Requirements 9.2, 9.3, 9.5, 9.6
 */
#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>
#include <string>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <algorithm>

#include "mock_lsiapi.h"

extern "C" {
#include "htaccess_exec_limit.h"
#include "htaccess_directive.h"
}

static rc::Gen<std::string> genHttpMethod()
{
    return rc::gen::element<std::string>(
        "GET", "POST", "PUT", "DELETE", "PATCH", "HEAD", "OPTIONS");
}

/* Generate a non-empty subset of HTTP methods as a space-separated string */
static rc::Gen<std::pair<std::string, std::vector<std::string>>> genMethodList()
{
    return rc::gen::mapcat(
        rc::gen::inRange(1, 5),
        [](int count) {
            return rc::gen::map(
                rc::gen::container<std::vector<std::string>>(
                    (std::size_t)count, genHttpMethod()),
                [](std::vector<std::string> methods) {
                    /* Deduplicate */
                    std::sort(methods.begin(), methods.end());
                    methods.erase(std::unique(methods.begin(), methods.end()),
                                  methods.end());
                    std::string joined;
                    for (size_t i = 0; i < methods.size(); i++) {
                        if (i > 0) joined += " ";
                        joined += methods[i];
                    }
                    return std::make_pair(joined, methods);
                });
        });
}

static htaccess_directive_t *make_limit(directive_type_t type, const char *methods)
{
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = type;
    d->data.limit.methods = strdup(methods);
    d->data.limit.children = nullptr;
    return d;
}

/* Property 36: Limit/LimitExcept duality */
RC_GTEST_PROP(LimitDuality, LimitAndLimitExceptAreComplementary, ())
{
    auto [method_str, method_vec] = *genMethodList();
    auto request_method = *genHttpMethod();

    auto *limit_dir = make_limit(DIR_LIMIT, method_str.c_str());
    auto *except_dir = make_limit(DIR_LIMIT_EXCEPT, method_str.c_str());

    int limit_exec = limit_should_exec(limit_dir, request_method.c_str());
    int except_exec = limit_should_exec(except_dir, request_method.c_str());

    /* They must be complementary: exactly one should be true */
    RC_ASSERT(limit_exec != except_exec);

    /* Verify correctness: Limit exec iff method in list */
    bool in_list = std::find(method_vec.begin(), method_vec.end(),
                             request_method) != method_vec.end();
    RC_ASSERT(limit_exec == (in_list ? 1 : 0));
    RC_ASSERT(except_exec == (in_list ? 0 : 1));

    free(limit_dir->data.limit.methods);
    free(limit_dir);
    free(except_dir->data.limit.methods);
    free(except_dir);
}

/* Additional: Limit with method in list always executes */
RC_GTEST_PROP(LimitDuality, LimitExecsWhenMethodInList, ())
{
    auto [method_str, method_vec] = *genMethodList();
    /* Pick a method that IS in the list */
    auto idx = *rc::gen::inRange((size_t)0, method_vec.size());
    std::string request_method = method_vec[idx];

    auto *d = make_limit(DIR_LIMIT, method_str.c_str());
    RC_ASSERT(limit_should_exec(d, request_method.c_str()) == 1);

    free(d->data.limit.methods);
    free(d);
}
