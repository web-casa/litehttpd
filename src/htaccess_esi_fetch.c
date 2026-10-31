/**
 * htaccess_esi_fetch.c — loopback sub-request fetcher for dynamic ESI.
 *
 * A dynamic <esi:include> (src without a matching static file) is resolved
 * by an ordinary HTTP GET against the local OLS listener — the fragment
 * then runs the full pipeline (.htaccess, PHP/LSAPI, modsec) as its own
 * request. The fetch is blocking with a hard deadline and is only called
 * from the EOF join (never mid-stream), so the worker never depends on
 * filter re-invocation semantics.
 *
 * Single-worker caveat (documented): with exactly one OLS worker the
 * joiner is the same process that must serve the loopback request; the
 * fragment then times out and the tag is removed fail-open. Set Children
 * >= 2 for dynamic ESI.
 */
#include "htaccess_esi.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>

esi_dyn_fetch_fn_t esi_dyn_fetch_fn = NULL; /* test seam; real impl below */

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

/* "host:port" from LITEHTTPD_ESI_LOOPBACK (default 127.0.0.1:80) */
static int loopback_addr(struct sockaddr_in *addr)
{
    const char *env = getenv("LITEHTTPD_ESI_LOOPBACK");
    char host[128] = "127.0.0.1";
    int port = 80;
    if (env && env[0]) {
        char buf[128];
        snprintf(buf, sizeof(buf), "%s", env);
        char *colon = strrchr(buf, ':');
        if (colon && colon > buf) {
            *colon = '\0';
            int p = atoi(colon + 1);
            if (p > 0 && p < 65536)
                port = p;
            snprintf(host, sizeof(host), "%s", buf);
        }
    }
    memset(addr, 0, sizeof(*addr));
    addr->sin_family = AF_INET;
    addr->sin_port = htons((uint16_t)port);
    return inet_pton(AF_INET, host, &addr->sin_addr) == 1 ? 0 : -1;
}

static int wait_fd(int fd, short events, uint64_t deadline_ms)
{
    uint64_t now = now_ms();
    if (now >= deadline_ms)
        return 0;
    struct pollfd pfd = {.fd = fd, .events = events, .revents = 0};
    int rc = poll(&pfd, 1, (int)(deadline_ms - now));
    return rc > 0 && (pfd.revents & events);
}

/* One attempt: connect, send, read — all bounded by the absolute deadline. */
static char *esi_fetch_once(const char *uri, const char *host_hdr,
                            const char *cookie_hdr, uint64_t deadline);

/* Self-landing race: with N OLS workers the loopback connection can land
 * on the very worker blocked in the EOF join (it cannot accept until we
 * return) — that attempt always times out. Retry on a FRESH connection:
 * each attempt lands on an independently chosen worker, so 3 short
 * attempts drive the failure probability to ~1/N^3 while the total stays
 * inside the caller's deadline. */
static char *esi_dyn_fetch_real(const char *uri, const char *host_hdr,
                                const char *cookie_hdr, int deadline_ms)
{
    if (!uri || uri[0] != '/')
        return NULL;
    if (deadline_ms <= 0)
        deadline_ms = 1;

    for (int attempt = 0; attempt < 3; attempt++) {
        int slice = deadline_ms / 3;
        if (slice < 150)
            slice = 150;
        int left = deadline_ms - attempt * (deadline_ms / 3 > 0 ? deadline_ms / 3 : 1);
        if (slice > left)
            slice = left;
        if (slice <= 0)
            break;
        char *body = esi_fetch_once(uri, host_hdr, cookie_hdr,
                                    now_ms() + (uint64_t)slice);
        if (body)
            return body;
    }
    return NULL;
}

static char *esi_fetch_once(const char *uri, const char *host_hdr,
                            const char *cookie_hdr, uint64_t deadline)
{
    if (!uri || uri[0] != '/')
        return NULL;

    struct sockaddr_in addr;
    if (loopback_addr(&addr) != 0)
        return NULL;

    int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (fd < 0)
        return NULL;

    int rc = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    if (rc < 0 && errno != EINPROGRESS) {
        close(fd);
        return NULL;
    }
    if (rc < 0 && !wait_fd(fd, POLLOUT, deadline)) {
        close(fd);
        return NULL;
    }

    /* Request: identity encoding (we must splice raw text), loop marker,
     * parent cookies so session fragments work, connection close. */
    char req[4096];
    int n = snprintf(req, sizeof(req),
                     "GET %s HTTP/1.1\r\n"
                     "Host: %s\r\n"
                     "Accept-Encoding: identity\r\n"
                     "X-LiteHttpd-Esi-Subrequest: 1\r\n"
                     "Connection: close\r\n"
                     "%s%s%s"
                     "\r\n",
                     uri,
                     (host_hdr && host_hdr[0]) ? host_hdr : "localhost",
                     (cookie_hdr && cookie_hdr[0]) ? "Cookie: " : "",
                     (cookie_hdr && cookie_hdr[0]) ? cookie_hdr : "",
                     (cookie_hdr && cookie_hdr[0]) ? "\r\n" : "");
    if (n <= 0 || n >= (int)sizeof(req)) {
        close(fd);
        return NULL;
    }

    size_t sent = 0;
    while (sent < (size_t)n) {
        if (!wait_fd(fd, POLLOUT, deadline)) {
            close(fd);
            return NULL;
        }
        ssize_t w = send(fd, req + sent, (size_t)n - sent, MSG_NOSIGNAL);
        if (w <= 0) {
            close(fd);
            return NULL;
        }
        sent += (size_t)w;
    }

    /* Read the whole response (bounded) */
    size_t cap = ESI_DYN_BODY_MAX + 8192; /* body + headers slack */
    char *resp = malloc(cap + 1);
    if (!resp) {
        close(fd);
        return NULL;
    }
    size_t len = 0;
    for (;;) {
        if (len >= cap)
            break;
        if (!wait_fd(fd, POLLIN, deadline))
            break; /* timeout or close */
        ssize_t r = recv(fd, resp + len, cap - len, 0);
        if (r <= 0)
            break;
        len += (size_t)r;
    }
    close(fd);
    resp[len] = '\0';

    /* Status line must be 200 */
    if (len < 12 || memcmp(resp, "HTTP/1.", 7) != 0)
        goto fail;
    int status = atoi(resp + 9);
    if (status != 200)
        goto fail;

    /* Body starts after the header terminator */
    char *body = strstr(resp, "\r\n\r\n");
    if (!body)
        goto fail;
    body += 4;
    size_t body_len = len - (size_t)(body - resp);
    if (body_len > ESI_DYN_BODY_MAX)
        body_len = ESI_DYN_BODY_MAX;

    /* Trim a possible chunked encoding (identity was requested, but be
     * defensive): reject transfer-encoding responses outright. */
    if (strncasecmp(resp, "HTTP/1.", 7) == 0) {
        const char *hdr_end = body - 4;
        for (const char *h = resp; h && h < hdr_end; ) {
            const char *eol = strstr(h, "\r\n");
            if (!eol || eol > hdr_end)
                break;
            if (strncasecmp(h, "Transfer-Encoding:", 18) == 0)
                goto fail;
            h = eol + 2;
        }
    }

    memmove(resp, body, body_len);
    resp[body_len] = '\0';
    return resp;

fail:
    free(resp);
    return NULL;
}

/* Install the real fetcher at load time unless a test already set the seam. */
__attribute__((constructor)) static void esi_fetch_init(void)
{
    if (!esi_dyn_fetch_fn)
        esi_dyn_fetch_fn = esi_dyn_fetch_real;
}
