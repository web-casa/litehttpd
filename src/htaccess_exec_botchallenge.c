/**
 * htaccess_exec_botchallenge.c — self-hosted JS PoW bot challenge.
 */
#include "htaccess_exec_botchallenge.h"
#include "htaccess_botcrypto.h"
#include "htaccess_regex.h"
#include "htaccess_cidr.h"
#include "htaccess_expr.h"

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

/* ── effective client IP (trusted-proxy aware) ───────────────────── */

/* compact IP sanity check (v4 dotted-quad or v6-ish with digits/:/. only) */
static int bc_looks_like_ip(const char *s)
{
    if (!s || !*s)
        return 0;
    int digits = 0, dots = 0, colons = 0;
    for (const char *p = s; *p; p++) {
        if (*p == '.') dots++;
        else if (*p == ':') colons++;
        else if (*p >= '0' && *p <= '9') digits++;
        else return 0;
    }
    return digits > 0 && (dots == 3 || colons >= 2);
}

/* rightmost XFF token — the address the trusted proxy APPENDED; the
 * leftmost entries are client-supplied and spoofable (same rule as the
 * brute-force engine). Returns a strdup'd validated IP or NULL. */
static char *bc_xff_rightmost(const char *xff)
{
    if (!xff || !*xff)
        return NULL;
    const char *end = xff + strlen(xff);
    while (end > xff && (end[-1] == ' ' || end[-1] == '\t'))
        end--;
    const char *start = end;
    while (start > xff && start[-1] != ',')
        start--;
    while (start < end && (*start == ' ' || *start == '\t'))
        start++;
    if (start >= end || (size_t)(end - start) > 45)
        return NULL;
    char *ip = strndup(start, (size_t)(end - start));
    if (ip && !bc_looks_like_ip(ip)) {
        free(ip);
        return NULL;
    }
    return ip;
}

/* space/comma separated IPv4 CIDR list membership */
static int bc_ip_in_cidrs(const char *ip, const char *list)
{
    if (!ip || !list)
        return 0;
    cidr_v4_t ip_c, c;
    if (cidr_parse(ip, &ip_c) != 0)
        return 0;
    char *copy = strdup(list);
    if (!copy)
        return 0;
    int hit = 0;
    char *sp = NULL;
    for (char *tok = strtok_r(copy, " ,\t", &sp); tok;
         tok = strtok_r(NULL, " ,\t", &sp)) {
        if (cidr_parse(tok, &c) == 0 && cidr_match(&c, ip_c.network)) {
            hit = 1;
            break;
        }
    }
    free(copy);
    return hit;
}

/**
 * Pick the IP the credential binds to. When the DIRECT peer is a
 * configured trusted proxy, bind to the rightmost X-Forwarded-For token
 * (validated as an IP); otherwise ignore XFF entirely and use the peer —
 * a spoofable XFF must never rotate or share identity buckets.
 * Returns 0 and fills out; never fails (falls back to peer/unknown).
 */
int bc_pick_effective_ip(const char *peer_ip, const char *xff,
                         const char *trusted_cidrs, char *out,
                         size_t out_sz)
{
    const char *chosen = peer_ip ? peer_ip : "unknown";
    if (trusted_cidrs && trusted_cidrs[0] && peer_ip && peer_ip[0] &&
        bc_ip_in_cidrs(peer_ip, trusted_cidrs) && xff && xff[0]) {
        char *xff_ip = bc_xff_rightmost(xff);
        if (xff_ip) {
            snprintf(out, out_sz, "%s", xff_ip);
            free(xff_ip);
            return 0;
        }
    }
    snprintf(out, out_sz, "%s", chosen);
    return 0;
}

/* ── challenge page ──────────────────────────────────────────────── */

/* Escape a string for embedding inside a single-quoted JS string in
 * inline HTML: kills quote/backslash breakouts and </script> sequences.
 * Hex-escapes every character that is not [A-Za-z0-9/_?.=&%-] — URI-
 * safe characters pass through, everything else becomes \xHH. */
int bc_js_escape(const char *in, char *out, size_t out_sz)
{
    if (!in || !out || out_sz == 0)
        return -1;
    static const char hex[] = "0123456789ABCDEF";
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p; p++) {
        unsigned char c = *p;
        int safe = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                   (c >= '0' && c <= '9') ||
                   c == '/' || c == '_' || c == '?' || c == '.' ||
                   c == '=' || c == '&' || c == '%' || c == '-' || c == '~';
        if (safe) {
            if (o + 1 >= out_sz) return -1;
            out[o++] = (char)c;
        } else {
            if (o + 4 >= out_sz) return -1;
            out[o++] = '\\';
            out[o++] = 'x';
            out[o++] = hex[c >> 4];
            out[o++] = hex[c & 0xF];
        }
    }
    out[o] = '\0';
    return (int)o;
}

int bc_build_challenge_page(char *buf, size_t buf_size, const char *uri,
                            const char *salt, int difficulty)
{
    if (!buf || buf_size == 0 || !uri || !salt)
        return -1;
    char euri[8192]; /* worst case: uribuf cap 2047 * \xHH(4) + NUL */
    if (bc_js_escape(uri, euri, sizeof(euri)) < 0)
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
        salt, difficulty, euri);
    return (n > 0 && (size_t)n < buf_size) ? n : -1;
}

/* Substitute %{SALT} %{DIFF} %{URI} in an operator-supplied template.
 * URI is JS-escaped; SALT/DIFF are module-generated (trusted). Any
 * rendering problem returns -1 so the caller falls back to the built-in
 * page — a broken template must never break the gate. */
int bc_render_template(char *buf, size_t buf_size, const char *tpl,
                       const char *uri, const char *salt, int difficulty)
{
    if (!buf || buf_size == 0 || !tpl || !uri || !salt)
        return -1;
    char euri[8192]; /* uribuf cap 2047 * \xHH(4) + NUL */
    if (bc_js_escape(uri, euri, sizeof(euri)) < 0)
        return -1;
    char diffs[16];
    snprintf(diffs, sizeof(diffs), "%d", difficulty);
    size_t o = 0;
    for (const char *p = tpl; *p; p++) {
        if (p[0] == '%' && p[1] == '{') {
            const char *end = strchr(p + 2, '}');
            if (end) {
                size_t len = (size_t)(end - (p + 2));
                const char *rep = NULL; size_t rlen = 0;
                if (len == 4 && strncmp(p + 2, "SALT", 4) == 0) {
                    rep = salt; rlen = strlen(salt);
                } else if (len == 4 && strncmp(p + 2, "DIFF", 4) == 0) {
                    rep = diffs; rlen = strlen(diffs);
                } else if (len == 3 && strncmp(p + 2, "URI", 3) == 0) {
                    rep = euri; rlen = strlen(euri);
                }
                if (rep) {
                    if (o + rlen >= buf_size) return -1;
                    memcpy(buf + o, rep, rlen);
                    o += rlen;
                    p = end;
                    continue;
                }
            }
        }
        if (o + 1 >= buf_size) return -1;
        buf[o++] = *p;
    }
    buf[o] = '\0';
    return (int)o;
}

/**
 * Load an operator template by path, confined to the .htaccess tree:
 * an absolute path must resolve inside base_dir; a relative path is
 * resolved against base_dir first. O_NOFOLLOW + realpath close the
 * symlink/escape paths (same confinement family as AuthUserFile).
 * Returns a malloc'd NUL-terminated body (<= 16 KiB) or NULL.
 */
static char *bc_load_template(const char *page_val, const char *base_dir)
{
    if (!page_val || !page_val[0] || !base_dir || !base_dir[0])
        return NULL;
    char full[4096];
    if (page_val[0] == '/')
        snprintf(full, sizeof(full), "%s", page_val);
    else
        snprintf(full, sizeof(full), "%s/%s", base_dir, page_val);
    char resolved[4096], base_r[4096];
    if (!realpath(full, resolved) || !realpath(base_dir, base_r))
        return NULL;
    size_t bl = strlen(base_r);
    if (strncmp(resolved, base_r, bl) != 0 ||
        (resolved[bl] != '/' && resolved[bl] != '\0'))
        return NULL;
    int fd = open(resolved, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return NULL;
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size <= 0 ||
        st.st_size > 16 * 1024) {
        close(fd);
        return NULL;
    }
    char *body = malloc((size_t)st.st_size + 1);
    if (!body) {
        close(fd);
        return NULL;
    }
    ssize_t r = read(fd, body, (size_t)st.st_size);
    close(fd);
    if (r != st.st_size) {
        free(body);
        return NULL;
    }
    body[r] = '\0';
    return body;
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
                /* a valid nonce never exceeds 2^31; bail on anything
                 * bigger before the signed multiply overflows (UB) */
                if (v > 999999999L)
                    return -1;
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
                       const htaccess_directive_t *directives,
                       const char *base_dir)
{
    if (!session || !directives)
        return BC_OFF;

    int enabled = 0;
    long ttl = BC_DEFAULT_TTL;
    int difficulty = BC_DEFAULT_DIFF;
    const char *exempt_re = NULL;
    const char *trusted_proxies = NULL;
    const char *page_path = NULL;
    const char *cond_expr = NULL;

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
        case DIR_BOT_CHALLENGE_TRUSTED_PROXY:
            trusted_proxies = d->value;
            break;
        case DIR_BOT_CHALLENGE_PAGE:
            page_path = d->value;
            break;
        case DIR_BOT_CHALLENGE_IF:
            cond_expr = d->value;
            break;
        default:
            break;
        }
    }
    if (!enabled)
        return BC_OFF;
    /* Optional ap_expr trigger, e.g. GeoIP-scoped gating:
     *   BotChallengeIf "%{ENV:GEOIP_COUNTRY_CODE} !~ /^(US|GB)$/"
     * Unparsable expression: fail open (gate off) and say so — a broken
     * config must never lock humans out. */
    if (cond_expr && cond_expr[0]) {
        expr_node_t *e = expr_parse(cond_expr);
        if (!e) {
            lsi_log(session, LSI_LOG_WARN,
                    "mod_htaccess: BotChallengeIf expression unparsable "
                    "(gate disabled): %s",
                    cond_expr);
            return BC_OFF;
        }
        int hit = expr_eval(session, e);
        expr_free(e);
        if (!hit)
            return BC_OFF; /* condition not met: no gate this request */
    }
    if (ttl < 60)
        ttl = 60;
    if (difficulty < 8)
        difficulty = 8;

    int ip_len = 0;
    const char *peer = lsi_session_get_client_ip(session, &ip_len);
    char peerbuf[64];
    if (peer && ip_len > 0) {
        if (ip_len > 63)
            ip_len = 63;
        memcpy(peerbuf, peer, ip_len);
        peerbuf[ip_len] = '\0';
        peer = peerbuf;
    } else {
        peer = NULL;
    }
    /* Identity the credential binds to: rightmost XFF token when the
     * direct peer is a configured trusted proxy, else the peer itself
     * (a spoofable XFF must never rotate or merge identity buckets). */
    char ipbuf[64];
    const char *xff = NULL;
    {
        int xff_len = 0;
        const char *h = lsi_session_get_req_header_by_name(
            session, "X-Forwarded-For", 15, &xff_len);
        if (h && xff_len > 0 && xff_len < 512) {
            static __thread char xffbuf[512];
            memcpy(xffbuf, h, (size_t)xff_len);
            xffbuf[xff_len] = '\0';
            xff = xffbuf;
        }
    }
    bc_pick_effective_ip(peer, xff, trusted_proxies, ipbuf, sizeof(ipbuf));
    const char *ip = ipbuf;

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
            /* Answer accepted. Instead of serving the content on the
             * ?__lhc= URL (which would leave the nonce in the browser's
             * address bar, pollute cache keys, and let a CDN cache a
             * credential-free 200 as a PoW-free copy), redirect 302 to
             * the CLEAN URL: the browser re-requests with the fresh
             * cookie and lands on a cacheable, canonical address. */
            int uri_len = 0, qs_len = 0;
            const char *uri = lsi_session_get_uri(session, &uri_len);
            const char *qs = lsi_session_get_query_string(session, &qs_len);
            if (uri && uri_len > 0) {
                char newq[2048];
                int qn = 0;
                int i = 0;
                int had_answer = 0;
                while (i < qs_len) {
                    int seg_end = i;
                    while (seg_end < qs_len && qs[seg_end] != '&')
                        seg_end++;
                    int seg_len = seg_end - i;
                    int is_ans =
                        (seg_len > 6 &&
                         memcmp(qs + i, BC_QUERY_KEY "=",
                                strlen(BC_QUERY_KEY "=")) == 0);
                    if (is_ans) {
                        had_answer = 1;
                    } else {
                        if (qn > 0 && qn < (int)sizeof(newq))
                            newq[qn++] = '&';
                        if (qn + seg_len < (int)sizeof(newq)) {
                            memcpy(newq + qn, qs + i, (size_t)seg_len);
                            qn += seg_len;
                        }
                    }
                    i = seg_end + 1;
                }
                if (had_answer) {
                    lsi_session_redirect_uri_qs(
                        session, uri, uri_len, newq, qn,
                        qn == 0 /* nothing left → drop the query */);
                    return BC_REDIRECT; /* 302; engine ends the response */
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
    char page[20 * 1024];
    int page_len = -1;
    char *tpl = bc_load_template(page_path, base_dir);
    if (tpl) {
        page_len = bc_render_template(page, sizeof(page), tpl, uribuf,
                                      salt, difficulty);
        free(tpl);
    }
    if (page_len < 0) {
        /* no/invalid template: built-in page (16 KiB template + headroom
         * must fit; overflow only for absurd templates → fail open) */
        if (bc_build_challenge_page(page, (int)sizeof(page), uribuf,
                                     salt, difficulty) < 0)
            return BC_OFF;
    }

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
