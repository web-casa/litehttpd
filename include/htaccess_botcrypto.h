/**
 * htaccess_botcrypto.h — self-contained crypto for the Bot Challenge.
 *
 * - SHA-256 + HMAC-SHA256: standard public-domain implementations,
 *   no OpenSSL dependency (module stays portable across OLS builds).
 * - FNV-1a 32-bit: the proof-of-work hash shared with the challenge JS.
 *   Deliberately cheap: a browser solves it in a loop in milliseconds at
 *   the default difficulty, the server verifies in one pass.
 */
#ifndef HTACCESS_BOTCRYPTO_H
#define HTACCESS_BOTCRYPTO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t  data[64];
    size_t   datalen;
} bc_sha256_ctx;

void bc_sha256_init(bc_sha256_ctx *ctx);
void bc_sha256_update(bc_sha256_ctx *ctx, const uint8_t *data, size_t len);
void bc_sha256_final(bc_sha256_ctx *ctx, uint8_t out[32]);
void bc_sha256(const uint8_t *data, size_t len, uint8_t out[32]);

/* HMAC-SHA256(key, keylen, msg, msglen, out[32]) */
void bc_hmac_sha256(const uint8_t *key, size_t keylen,
                    const uint8_t *msg, size_t msglen, uint8_t out[32]);

/* FNV-1a 32-bit over a NUL-terminated string */
uint32_t bc_fnv1a(const char *s);

/* True when the PoW (salt:nonce) meets `difficulty` leading zero bits. */
int bc_pow_ok(const char *salt, uint32_t nonce, int difficulty);

/* Constant-time byte comparison for MAC checks. */
int bc_cteq(const uint8_t *a, const uint8_t *b, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* HTACCESS_BOTCRYPTO_H */
