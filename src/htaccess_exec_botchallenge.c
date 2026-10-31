/**
 * htaccess_exec_botchallenge.c — self-hosted JS PoW bot challenge.
 */
#include "htaccess_exec_botchallenge.h"
#include "htaccess_botcrypto.h"
#include "htaccess_regex.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

/* ── shared secret ───────────────────────────────────────────────── */

static uint8_t g_secret[32];
static int g_secret_loaded = 0;
static int g_secret_degraded = 0;

/* Best-effort mkdir of the secret's parent directory, so root-run workers
 * can create the file even when the dir is missing. Worker-user installs
 * need the dir pre-provisioned by packaging (tmpfiles.d / install script). */
static void bc_ensure_dir(const char *path)
{
    char dir[256];
    if (snprintf(dir, sizeof(dir), "%s", path) >= (int)sizeof(dir))
        return;
    char *slash = strrchr(dir, '/');
    if (!slash || slash == dir)
        return;
    *slash = '\0';
    mkdir(dir, 0755);
}

static int bc_load_secret(void)
{
    if (g_secret_loaded)
        return 0;

    const char *path = getenv("LITEHTTPD_BOT_SECRET");
    if (!path || !path[0])
        path = BC_SECRET_PATH;

    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        ssize_t r = read(fd, g_secret, sizeof(g_secret));
        close(fd);
        if (r == (ssize_t)sizeof(g_secret)) {
            g_secret_loaded = 1;
            return 0;
        }
    }

    bc_ensure_dir(path);

    /* Create it: first worker wins, others read it back. */
    fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd >= 0) {
        uint8_t rnd[32];
        int got = 0;
        int ur = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
        if (ur >= 0) {
            got = (read(ur, rnd, sizeof(rnd)) == (ssize_t)sizeof(rnd));
            close(ur);
        }
        if (!got) {
            /* fallback: time + pid mixing */
            unsigned seed = (unsigned)time(NULL) ^ (unsigned)getpid();
            for (int i = 0; i < 32; i++) {
                seed = seed * 1103515245u + 12345u;
                rnd[i] = (uint8_t)(seed >> 16);
            }
        }
        if (write(fd, rnd, sizeof(rnd)) == (ssize_t)sizeof(rnd)) {
            close(fd);
            memcpy(g_secret, rnd, sizeof(rnd));
            g_secret_loaded = 1;
            return 0;
        }
        close(fd);
    }

    /* Lost the race or no perms: read it back. */
    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        ssize_t r = read(fd, g_secret, sizeof(g_secret));
        close(fd);
        if (r == (ssize_t)sizeof(g_secret)) {
            g_secret_loaded = 1;
            return 0;
        }
    }
    /* Neither loaded nor created: cookie mint/verify falls back to an
     * empty-key HMAC. That keeps the gate serving (no 500s, no loops)
     * but the credential is forgeable by anyone who knows the scheme —
     * surface it so operators fix the directory permissions. */
    g_secret_degraded = 1;
    return -1;
}

int bc_secret_degraded(void)
{
    return g_secret_degraded;
}

const uint8_t *bc_secret(size_t *len)
{
    if (bc_load_secret() != 0) {
        if (len)
            *len = 0;
        return NULL;
    }
    if (len)
        *len = sizeof(g_secret);
    return g_secret;
}

/* ── client-IP classing (/24 for v4, /64 for v6 — coarse by design) ── */

static void ip_class(const char *ip, char *out, size_t out_size)
{
    if (!ip) {
        snprintf(out, out_size, "unknown");
        return;
    }
    /* IPv4: keep first 3 octets */
    const char *colon = strchr(ip, ':');
    if (!colon) {
        int a, b, c;
        if (sscanf(ip, "%d.%d.%d", &a, &b, &c) == 3)
            snprintf(out, out_size, "%d.%d.%d", a, b, c);
        else
            snprintf(out, out_size, "%s", ip);
        return;
    }
    /* IPv6: keep the first 4 groups */
    int groups = 0;
    size_t n = 0;
    for (const char *p = ip; *p && n + 1 < out_size && groups < 4; p++) {
        if (*p == ':')
            groups++;
        out[n++] = *p;
        if (groups == 4)
            break;
    }
    out[n] = '\0';
}

/* ── credential cookie ────────────────────────────────────────────── */

static void mac_for(const char *ip_class_str, long exp, uint8_t out[32])
{
    size_t slen = 0;
    const uint8_t *sec = bc_secret(&slen);
    char msg[160];
    int n = snprintf(msg, sizeof(msg), "lhc1|%s|%ld", ip_class_str, exp);
    if (n <= 0)
        n = 0;
    if (!sec)
        sec = (const uint8_t *)""; /* fail closed: HMAC of empty key */
    bc_hmac_sha256(sec, slen, (const uint8_t *)msg, (size_t)n, out);
}

int bc_mint_cookie(const char *client_ip, long ttl, char *cookie_out,
                   size_t out_size)
{
    long exp = (long)time(NULL) + ttl;
    char cls[64];
    ip_class(client_ip, cls, sizeof(cls));
    uint8_t mac[32];
    mac_for(cls, exp, mac);
    char hex[65];
    for (int i = 0; i < 32; i++)
        sprintf(hex + i * 2, "%02x", mac[i]);
    return snprintf(cookie_out, out_size, "%ld|%s", exp, hex);
}

int bc_check_cookie(const char *client_ip, const char *cookie_value)
{
    if (!cookie_value)
        return -1;
    const char *bar = strchr(cookie_value, '|');
    if (!bar)
        return -1;
    long exp = 0;
    if (sscanf(cookie_value, "%ld", &exp) != 1)
        return -1;
    if (exp < (long)time(NULL))
        return -1; /* expired */

    char cls[64];
    ip_class(client_ip, cls, sizeof(cls));
    uint8_t mac[32], want[32];
    mac_for(cls, exp, mac);
    /* parse hex */
    const char *hex = bar + 1;
    if (strlen(hex) < 64)
        return -1;
    for (int i = 0; i < 32; i++) {
        unsigned v;
        if (sscanf(hex + i * 2, "%2x", &v) != 1)
            return -1;
        want[i] = (uint8_t)v;
    }
    /* constant-time compare over the FULL 32 bytes */
    if (!bc_cteq(mac, want, 32))
        return -1;
    return 0;
}

/* ── challenge page ──────────────────────────────────────────────── */

int bc_build_challenge_page(char *buf, size_t buf_size, const char *uri,
                            const char *salt, int difficulty)
{
    if (!buf || buf_size == 0 || !uri || !salt)
        return -1;
    int n = snprintf(
        buf, buf_size,
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<title>Verifying…</title></head><body>"
        "<noscript>JavaScript is required.</noscript>"
        "<script>\n"
        "function fnv(s){var h=2166136261;for(var i=0;i<s.length;i++)"
        "{h^=s.charCodeAt(i);h=(h*16777619)>>>0;}return h;}\n"
        "var SALT='%s',DIFF=%d,URI='%s';\n"
        "for(var n=0;;n++){var h=fnv(SALT+':'+n);var z=0;"
        "for(var b=31;b>=0;b--){if((h>>>b)&1)break;z++;}"
        "if(z>=DIFF){location.replace(URI+(URI.indexOf('?')<0?'?':'&')"
        "+'__lhc='+n);break;}}\n"
        "</script></body></html>",
        salt, difficulty, uri);
    return (n > 0 && (size_t)n < buf_size) ? n : -1;
}

/* ── executor ─────────────────────────────────────────────────────── */

/* find __lhc=<nonce> in the query string; return -1 when absent */
static long find_pow_answer(const char *qs, int qs_len)
{
    if (!qs || qs_len <= 0)
        return -1;
    const char *needle = BC_QUERY_KEY "=";
    size_t nl = strlen(needle);
    for (int i = 0; i + (int)nl <= qs_len; i++) {
        if ((i == 0 || qs[i - 1] == '&') &&
            memcmp(qs + i, needle, nl) == 0) {
            long v = 0;
            int used = 0;
            const char *p = qs + i + (int)nl;
            const char *end = qs + qs_len;
            for (; p < end && *p >= '0' && *p <= '9'; p++) {
                v = v * 10 + (*p - '0');
                used = 1;
            }
            return used ? v : -1;
        }
    }
    return -1;
}

/* extract cookie value from a Cookie header (name=value; ...) */
static int find_cookie(const char *hdr, int hdr_len, const char *name,
                       char *out, size_t out_size)
{
    if (!hdr || hdr_len <= 0)
        return -1;
    size_t nl = strlen(name);
    for (int i = 0; i + (int)nl < hdr_len; i++) {
        if ((i == 0 || hdr[i - 1] == ';' || hdr[i - 1] == ' ') &&
            hdr[i + (int)nl] == '=' &&
            strncasecmp(hdr + i, name, nl) == 0) {
            const char *v = hdr + i + (int)nl + 1;
            const char *end = v;
            while (end < hdr + hdr_len && *end != ';' && *end != ' ')
                end++;
            size_t n = (size_t)(end - v);
            if (n >= out_size)
                n = out_size - 1;
            memcpy(out, v, n);
            out[n] = '\0';
            return 0;
        }
    }
    return -1;
}

int exec_bot_challenge(lsi_session_t *session,
                       const htaccess_directive_t *directives)
{
    if (!session || !directives)
        return BC_OFF;

    int enabled = 0;
    long ttl = BC_DEFAULT_TTL;
    int difficulty = BC_DEFAULT_DIFF;
    const char *exempt_re = NULL;

    for (const htaccess_directive_t *d = directives; d; d = d->next) {
        switch (d->type) {
        case DIR_BOT_CHALLENGE:
            enabled = d->value && strcasecmp(d->value, "on") == 0;
            break;
        case DIR_BOT_CHALLENGE_TTL:
            if (d->value)
                ttl = atol(d->value);
            break;
        case DIR_BOT_CHALLENGE_DIFFICULTY:
            if (d->value)
                difficulty = atoi(d->value);
            break;
        case DIR_BOT_CHALLENGE_EXEMPT:
            exempt_re = d->value;
            break;
        default:
            break;
        }
    }
    if (!enabled)
        return BC_OFF;
    if (ttl < 60)
        ttl = 60;
    if (difficulty < 8)
        difficulty = 8;

    int ip_len = 0;
    const char *ip = lsi_session_get_client_ip(session, &ip_len);
    char ipbuf[64];
    if (ip && ip_len > 0) {
        if (ip_len > 63)
            ip_len = 63;
        memcpy(ipbuf, ip, ip_len);
        ipbuf[ip_len] = '\0';
        ip = ipbuf;
    } else {
        ip = NULL;
    }

    /* 1. Valid cookie → pass. */
    int ck_len = 0;
    const char *ck = lsi_session_get_req_header_by_name(
        session, "Cookie", 6, &ck_len);
    char cookie[160];
    if (ck && find_cookie(ck, ck_len, BC_COOKIE_NAME, cookie,
                           sizeof(cookie)) == 0 &&
        bc_check_cookie(ip, cookie) == 0)
        return BC_PASS;

    /* 2. PoW answer in the query → verify, mint cookie, clean URI. */
    int qs_len = 0;
    const char *qs = lsi_session_get_query_string(session, &qs_len);
    long nonce = find_pow_answer(qs, qs_len);
    if (nonce >= 0) {
        /* daily salt: deterministic across workers, rotates daily */
        char salt[32];
        snprintf(salt, sizeof(salt), "s%ld", (long)time(NULL) / 86400);
        if (bc_pow_ok(salt, (uint32_t)nonce, difficulty) == 1) {
            char cookie_val[128];
            bc_mint_cookie(ip, ttl, cookie_val, sizeof(cookie_val));
            char setc[256];
            snprintf(setc, sizeof(setc),
                     "%s=%s; Path=/; Max-Age=%ld; HttpOnly; SameSite=Lax",
                     BC_COOKIE_NAME, cookie_val, ttl);
            lsi_session_add_resp_header(session, "Set-Cookie", 10,
                                        setc, (int)strlen(setc));
            /* Strip the __lhc param from the URI before serving. */
            int uri_len = 0;
            const char *uri = lsi_session_get_uri(session, &uri_len);
            if (uri && uri_len > 0) {
                char clean[4096];
                int n = 0;
                const char *qm = memchr(uri, '?', (size_t)uri_len);
                size_t base = qm ? (size_t)(qm - uri) : (size_t)uri_len;
                if (base < sizeof(clean)) {
                    memcpy(clean, uri, base);
                    n = (int)base;
                    /* rebuild the query without __lhc */
                    const char *q = qm ? qm + 1 : NULL;
                    int qlen = q ? uri_len - (int)base - 1 : 0;
                    char newq[2048];
                    int qn = 0;
                    int i = 0;
                    while (i < qlen) {
                        int seg_end = i;
                        while (seg_end < qlen && q[seg_end] != '&')
                            seg_end++;
                        int seg_len = seg_end - i;
                        int is_ans =
                            (seg_len > 6 &&
                             memcmp(q + i, BC_QUERY_KEY "=",
                                    strlen(BC_QUERY_KEY "=")) == 0);
                        if (!is_ans) {
                            if (qn > 0 && qn < (int)sizeof(newq))
                                newq[qn++] = '&';
                            if (qn + seg_len < (int)sizeof(newq)) {
                                memcpy(newq + qn, q + i, (size_t)seg_len);
                                qn += seg_len;
                            }
                        }
                        i = seg_end + 1;
                    }
                    if (qn > 0 && n + 1 + qn < (int)sizeof(clean)) {
                        clean[n++] = '?';
                        memcpy(clean + n, newq, (size_t)qn);
                        n += qn;
                    }
                    clean[n] = '\0';
                    lsi_session_set_uri_internal(session, clean, n);
                }
            }
            return BC_PASS;
        }
        /* wrong answer → fall through to a fresh challenge */
    }

    /* 3. UA allowlist. */
    if (exempt_re && exempt_re[0]) {
        int ua_len = 0;
        const char *ua = lsi_session_get_req_header_by_name(
            session, "User-Agent", 10, &ua_len);
        if (ua && ua_len > 0) {
            char err[128];
            htre_regex_t *re = htre_get_cached(
                5, exempt_re, HTRE_ICASE, err, sizeof(err));
            if (re) {
                char uabuf[512];
                if (ua_len > 511)
                    ua_len = 511;
                memcpy(uabuf, ua, ua_len);
                uabuf[ua_len] = '\0';
                htre_match_t m[1];
                if (htre_exec(re, uabuf, 1, m) == HTRE_MATCH)
                    return BC_PASS; /* exempt UA: serve */
            }
        }
    }

    /* 4. Serve the challenge. */
    int uri_len = 0;
    const char *uri = lsi_session_get_uri(session, &uri_len);
    char uribuf[2048];
    if (!uri || uri_len <= 0) {
        snprintf(uribuf, sizeof(uribuf), "/");
    } else {
        size_t n = (size_t)uri_len;
        if (n >= sizeof(uribuf))
            n = sizeof(uribuf) - 1;
        memcpy(uribuf, uri, n);
        uribuf[n] = '\0';
    }

    char salt[32];
    snprintf(salt, sizeof(salt), "s%ld", (long)time(NULL) / 86400);
    char page[3072];
    if (bc_build_challenge_page(page, sizeof(page), uribuf, salt,
                                 difficulty) < 0)
        return BC_OFF; /* build failure: fail open to normal serving */

    lsi_session_set_status(session, 200);
    lsi_session_add_resp_header(session, "Cache-Control", 13,
                                "no-store", 8);
    const char *ct = "text/html; charset=utf-8";
    lsi_session_add_resp_header(session, "Content-Type", 12, ct,
                                (int)strlen(ct));
    lsi_session_set_resp_body(session, page, (int)strlen(page));
    lsi_session_end_resp(session);
    return BC_CHALLENGE;
}
