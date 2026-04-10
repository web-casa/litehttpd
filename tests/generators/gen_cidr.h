/**
 * gen_cidr.h - CIDR range and IP address generator for RapidCheck
 *
 * Generates random CIDR ranges (cidr_v4_t) and IPv4 addresses (uint32_t)
 * for testing access control and CIDR matching logic.
 *
 * Validates: Requirements 6.3
 */
#ifndef GEN_CIDR_H
#define GEN_CIDR_H

#include <rapidcheck.h>
#include <string>
#include <cstdint>

extern "C" {
#include "htaccess_cidr.h"
}

namespace gen {

/**
 * Generate a random IPv4 address as uint32_t in host byte order.
 */
inline rc::Gen<uint32_t> ipv4Address()
{
    return rc::gen::arbitrary<uint32_t>();
}

/**
 * Generate a random CIDR prefix length (0-32).
 */
inline rc::Gen<int> cidrPrefix()
{
    return rc::gen::inRange(0, 33);
}

/**
 * Generate a random cidr_v4_t structure.
 * Network is properly masked: network = ip & mask.
 */
inline rc::Gen<cidr_v4_t> cidrRange()
{
    return rc::gen::map(
        rc::gen::pair(ipv4Address(), cidrPrefix()),
        [](const std::pair<uint32_t, int> &p) {
            cidr_v4_t cidr;
            uint32_t mask = (p.second == 0) ? 0 :
                            (~(uint32_t)0) << (32 - p.second);
            cidr.mask = mask;
            cidr.network = p.first & mask;
            return cidr;
        });
}

/**
 * Generate a CIDR string like "192.168.1.0/24".
 */
inline rc::Gen<std::string> cidrString()
{
    return rc::gen::map(
        rc::gen::pair(
            rc::gen::tuple(
                rc::gen::inRange(0, 256),
                rc::gen::inRange(0, 256),
                rc::gen::inRange(0, 256),
                rc::gen::inRange(0, 256)),
            rc::gen::inRange(8, 33)),
        [](const std::pair<std::tuple<int,int,int,int>, int> &p) {
            auto &t = p.first;
            return std::to_string(std::get<0>(t)) + "." +
                   std::to_string(std::get<1>(t)) + "." +
                   std::to_string(std::get<2>(t)) + "." +
                   std::to_string(std::get<3>(t)) + "/" +
                   std::to_string(p.second);
        });
}

/**
 * Generate a CIDR string or "all" keyword.
 */
inline rc::Gen<std::string> cidrOrAll()
{
    return rc::gen::oneOf(
        rc::gen::just(std::string("all")),
        cidrString()
    );
}

/**
 * Generate an IPv4 address string like "10.0.1.55".
 */
inline rc::Gen<std::string> ipv4String()
{
    return rc::gen::map(
        rc::gen::tuple(
            rc::gen::inRange(0, 256),
            rc::gen::inRange(0, 256),
            rc::gen::inRange(0, 256),
            rc::gen::inRange(0, 256)),
        [](const std::tuple<int,int,int,int> &t) {
            return std::to_string(std::get<0>(t)) + "." +
                   std::to_string(std::get<1>(t)) + "." +
                   std::to_string(std::get<2>(t)) + "." +
                   std::to_string(std::get<3>(t));
        });
}

/**
 * Generate an IP address that is guaranteed to be within a given CIDR range.
 */
inline rc::Gen<uint32_t> ipInCidr(const cidr_v4_t &cidr)
{
    if (cidr.mask == 0) {
        /* "all" — any IP matches */
        return ipv4Address();
    }
    uint32_t host_bits = ~cidr.mask;
    return rc::gen::map(
        rc::gen::arbitrary<uint32_t>(),
        [=](uint32_t rand_val) -> uint32_t {
            return cidr.network | (rand_val & host_bits);
        });
}

/**
 * Generate an IP address that is guaranteed to be outside a given CIDR range.
 * Precondition: mask != 0 (not "all").
 */
inline rc::Gen<uint32_t> ipOutsideCidr(const cidr_v4_t &cidr)
{
    return rc::gen::suchThat(
        ipv4Address(),
        [=](uint32_t ip) {
            return (ip & cidr.mask) != cidr.network;
        });
}

} /* namespace gen */

#endif /* GEN_CIDR_H */
