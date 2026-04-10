/**
 * fuzz_parser.cpp — Fuzzing harness for htaccess_parse()
 *
 * Build with libFuzzer:
 *   clang++ -g -fsanitize=fuzzer,address -I include \
 *     tests/fuzz/fuzz_parser.cpp src/htaccess_parser.c \
 *     src/htaccess_directive.c src/htaccess_expires.c \
 *     -o fuzz_parser
 *
 * Or with AFL:
 *   afl-clang++ -g -I include tests/fuzz/fuzz_parser.cpp \
 *     src/htaccess_parser.c src/htaccess_directive.c \
 *     src/htaccess_expires.c -o fuzz_parser
 *
 * Run: ./fuzz_parser corpus/
 *
 * Targets: memory safety (buffer overflow, use-after-free, double-free),
 *          crashes, hangs, and assertion failures in the parser.
 */

extern "C" {
#include "htaccess_parser.h"
#include "htaccess_directive.h"
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    /* Null-terminate the input */
    char *buf = (char *)malloc(size + 1);
    if (!buf)
        return 0;
    memcpy(buf, data, size);
    buf[size] = '\0';

    /* Parse and free — should never crash */
    htaccess_directive_t *dirs = htaccess_parse(buf, size, "<fuzz>");
    if (dirs)
        htaccess_directives_free(dirs);

    free(buf);
    return 0;
}

/* Standalone mode for AFL or manual testing */
#ifndef __AFL_FUZZ_TESTCASE_LEN
#ifndef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
#include <stdio.h>
int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <input_file>\n", argv[0]);
        return 1;
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror("fopen"); return 1; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = (uint8_t *)malloc(len);
    fread(buf, 1, len, f);
    fclose(f);
    LLVMFuzzerTestOneInput(buf, len);
    free(buf);
    printf("OK: %s (%ld bytes)\n", argv[1], len);
    return 0;
}
#endif
#endif
