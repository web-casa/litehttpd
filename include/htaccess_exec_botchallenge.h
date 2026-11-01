/**
 * htaccess_exec_botchallenge.h — self-hosted JS proof-of-work bot challenge.
 *
 * Directives (per-directory, default off):
 *   BotChallenge On|Off          — gate the challenge for this tree
 *   BotChallengeTTL seconds     — signed-credential lifetime (default 3600)
 *   BotChallengeDifficulty bits — PoW leading-zero bits (default 16, cap 24)
 *   BotChallengeExempt regex    — UA allowlist (e.g. verified crawler UAs)
 *
 * Flow (stateless, no storage):
 *   1. Request arrives with no valid `lhttpd_bot` cookie (and UA not
 *      exempt): serve an inline HTML+JS challenge — HTTP 200 with
 *      no-cache, containing salt + difficulty. The JS loops FNV-1a
 *      (same 32-bit hash as the C verifier) over "salt:nonce" until the
 *      top `difficulty` bits are zero, then navigates to
 *      `<uri>?__lhc=<nonce>` .
 *   2. That request reaches the gate: the PoW is verified in one pass;
 *      on success an HMAC-SHA256 credential cookie is issued
 *      (`exp|hexmac` over secret + client /24 + expiry) and the request
 *      is internally rewritten to the original URI (query param removed).
 *   3. Subsequent requests verify cookie expiry + MAC in constant time.
 *      Wrong/absent cookie → challenge again. Everything fails open to
 *      the challenge (never to a hang).
 *
 * Secret: read from /var/run/litehttpd/botchallenge.secret (created with
 * 0600 on first use; shared by every worker so cookies verify across
 * workers). Override path with LITEHTTPD_BOT_SECRET.
 */
#ifndef HTACCESS_EXEC_BOTCHALLENGE_H
#define HTACCESS_EXEC_BOTCHALLENGE_H

#include "htaccess_directive.h"
#include "ls.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BC_COOKIE_NAME   "lhttpd_bot"
#define BC_QUERY_KEY     "__lhc"
#define BC_DEFAULT_TTL   3600
#define BC_DEFAULT_DIFF  16
#define BC_SECRET_PATH   "/var/run/litehttpd/botchallenge.secret"

/* Result codes from the gate. */
#define BC_PASS        0  /* valid cookie / verified PoW: serve normally */
#define BC_CHALLENGE   1  /* no/invalid cookie: challenge page served */
#define BC_OFF         2  /* directive off for this tree */
#define BC_REDIRECT    3  /* PoW accepted: cookie minted, 302 to the clean
                             URL already queued — caller ends processing */

/**
 * Evaluate bot challenge directives for this request. On BC_CHALLENGE the
 * challenge response has already been written to the session (status,
 * headers, body) and the caller should end the response. The PoW-answer
 * branch verifies the nonce, sets the credential cookie, and strips the
 * query param from the URI before returning BC_PASS.
 */
int exec_bot_challenge(lsi_session_t *session,
                       const htaccess_directive_t *directives,
                       const char *base_dir);

/* Exposed for tests: build the challenge HTML into buf. */
int bc_build_challenge_page(char *buf, size_t buf_size, const char *uri,
                            const char *salt, int difficulty);

/* Exposed for tests: JS-string escape and trusted-proxy IP selection. */
int bc_js_escape(const char *in, char *out, size_t out_sz);
int bc_pick_effective_ip(const char *peer_ip, const char *xff,
                         const char *trusted_cidrs, char *out,
                         size_t out_sz);

/* Exposed for tests: render an operator template (%{SALT} %{DIFF}
 * %{URI}); returns -1 on any problem so callers fall back to built-in. */
int bc_render_template(char *buf, size_t buf_size, const char *tpl,
                       const char *uri, const char *salt, int difficulty);

/* Exposed for tests: verify + mint the credential cookie. Returns 0 and
 * fills cookie_out ("exp|hexmac") on success. */
int bc_mint_cookie(const char *client_ip, long ttl, char *cookie_out,
                   size_t out_size);
int bc_check_cookie(const char *client_ip, const char *cookie_value);

/* Shared secret loader (file-backed, read once per process). */
const uint8_t *bc_secret(size_t *len);

/* Non-zero once the secret could be neither loaded nor created: cookie
 * credentials fall back to an empty-key HMAC (still functional, but
 * forgeable). Callers should warn once and fix directory permissions. */
int bc_secret_degraded(void);

#ifdef __cplusplus
}
#endif

#endif /* HTACCESS_EXEC_BOTCHALLENGE_H */
