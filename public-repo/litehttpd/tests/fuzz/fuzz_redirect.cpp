/**
 * fuzz_redirect.cpp — Fuzzing harness for exec_redirect()
 *
 * Build: same as fuzz_parser.cpp but add htaccess_exec_redirect.c
 */

extern "C" {
#include "htaccess_directive.h"
#include "htaccess_exec_redirect.h"
}

#include "../mock_lsiapi.h"
#include <cstring>
#include <cstdlib>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 4) return 0;

    /* Split input: first half = URI, second half = target */
    size_t split = size / 2;
    char *uri = (char *)malloc(split + 2);
    char *target = (char *)malloc(size - split + 1);
    if (!uri || !target) { free(uri); free(target); return 0; }

    uri[0] = '/';
    memcpy(uri + 1, data, split);
    uri[split + 1] = '\0';

    memcpy(target, data + split, size - split);
    target[size - split] = '\0';

    MockSession session;
    session.set_request_uri(uri);

    htaccess_directive_t *dir = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    if (dir) {
        dir->type = DIR_REDIRECT;
        dir->name = strdup(uri);
        dir->value = strdup(target);
        dir->data.redirect.status_code = 301;
        exec_redirect(session.handle(), dir);
        htaccess_directives_free(dir);
    }

    free(uri);
    free(target);
    return 0;
}
