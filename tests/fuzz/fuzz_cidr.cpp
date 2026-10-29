/**
 * fuzz_cidr.cpp — Fuzzing harness for cidr_parse() / cidr_match()
 *
 * Targets: memory safety and correctness of CIDR notation parsing
 * ("A.B.C.D/N", plain IP, "all") and IP-in-range matching.
 */
extern "C" {
#include "htaccess_cidr.h"
}

#include <cstdint>
#include <cstdlib>
#include <cstring>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size > 64)
        return 0; /* CIDR strings are short by nature */
    char *buf = (char *)malloc(size + 1);
    if (!buf)
        return 0;
    memcpy(buf, data, size);
    buf[size] = '\0';

    cidr_v4_t c;
    if (cidr_parse(buf, &c) == 0) {
        /* Valid parse: exercise matching with a few derived IPs.
         * Should never crash regardless of input. */
        uint32_t probe = c.network;
        (void)cidr_match(&c, probe);
        (void)cidr_match(&c, probe ^ 0xffffffff);
        (void)cidr_match(&c, 0);
        (void)cidr_match(&c, 0xffffffff);
        /* Invariant: network must be masked */
        if ((c.network & c.mask) != c.network)
            __builtin_trap();
    }
    free(buf);
    return 0;
}
