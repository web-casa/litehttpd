/**
 * prop_cidr.cpp - Property-based test for CIDR matching correctness
 *
 * Feature: ols-htaccess-module, Property 13: CIDR 匹配正确性
 *
 * Validates: Requirements 6.3, 6.4
 *
 * Property: For any valid CIDR range and IP address, cidr_match returns 1
 * if and only if (ip & mask) == (network & mask).
 */
#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>
#include <cstdint>

extern "C" {
#include "htaccess_cidr.h"
}

/**
 * Build a subnet mask from a prefix length (0-32).
 * Mirrors the implementation logic for independent verification.
 */
static uint32_t test_prefix_to_mask(int prefix)
{
    if (prefix == 0)
        return 0;
    return ~(uint32_t)0 << (32 - prefix);
}

/**
 * Property 13: CIDR 匹配正确性
 *
 * For any random prefix length [0..32], random network address, and random
 * IP address, cidr_match must return 1 iff (ip & mask) == (network & mask).
 */
RC_GTEST_PROP(CidrProperty, MatchCorrectnessMatchesManualComputation, ())
{
    // Generate random prefix length in [0, 32]
    auto prefix = *rc::gen::inRange(0, 33);

    // Generate random network and IP addresses
    auto raw_network = *rc::gen::arbitrary<uint32_t>();
    auto ip = *rc::gen::arbitrary<uint32_t>();

    // Compute mask and canonical network (apply mask to raw network)
    uint32_t mask = test_prefix_to_mask(prefix);
    uint32_t network = raw_network & mask;

    // Build the cidr_v4_t directly (avoids string round-trip issues)
    cidr_v4_t cidr;
    cidr.network = network;
    cidr.mask = mask;

    // Expected result: manual bitwise check
    int expected = ((ip & mask) == (network & mask)) ? 1 : 0;

    // Actual result from cidr_match
    int actual = cidr_match(&cidr, ip);

    RC_ASSERT(actual == expected);
}
