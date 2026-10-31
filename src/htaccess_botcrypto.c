/**
 * htaccess_botcrypto.c — SHA-256 / HMAC / FNV-1a PoW primitives.
 *
 * SHA-256 implementation follows FIPS 180-4 (public-domain style, adapted
 * from the widely known Brad Conte reference); HMAC follows RFC 2104.
 */
#include "htaccess_botcrypto.h"

#include <string.h>
#include <stdio.h>

/* ── SHA-256 ─────────────────────────────────────────────────────── */

#define BC_ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b,
    0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
    0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7,
    0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152,
    0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
    0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819,
    0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
    0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
    0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

void bc_sha256_init(bc_sha256_ctx *ctx)
{
    ctx->datalen = 0;
    ctx->bitlen = 0;
    ctx->state[0] = 0x6a09e667; ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372; ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f; ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab; ctx->state[7] = 0x5be0cd19;
}

static void sha256_transform(bc_sha256_ctx *ctx, const uint8_t data[64])
{
    uint32_t m[64], a, b, c, d, e, f, g, h, t1, t2;
    int i, j;

    for (i = 0, j = 0; i < 16; ++i, j += 4)
        m[i] = ((uint32_t)data[j] << 24) | ((uint32_t)data[j + 1] << 16) |
               ((uint32_t)data[j + 2] << 8) | (uint32_t)data[j + 3];
    for (; i < 64; ++i)
        m[i] = (BC_ROTR(m[i - 15], 7) ^ BC_ROTR(m[i - 15], 18) ^
                (m[i - 15] >> 3)) +
               m[i - 7] +
               (BC_ROTR(m[i - 2], 17) ^ BC_ROTR(m[i - 2], 19) ^
                (m[i - 2] >> 10)) +
               m[i - 16];

    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2];
    d = ctx->state[3]; e = ctx->state[4]; f = ctx->state[5];
    g = ctx->state[6]; h = ctx->state[7];

    for (i = 0; i < 64; ++i) {
        t1 = h + (BC_ROTR(e, 6) ^ BC_ROTR(e, 11) ^ BC_ROTR(e, 25)) +
             ((e & f) ^ (~e & g)) + K[i] + m[i];
        t2 = (BC_ROTR(a, 2) ^ BC_ROTR(a, 13) ^ BC_ROTR(a, 22)) +
             ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c;
    ctx->state[3] += d; ctx->state[4] += e; ctx->state[5] += f;
    ctx->state[6] += g; ctx->state[7] += h;
}

void bc_sha256_update(bc_sha256_ctx *ctx, const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        ctx->data[ctx->datalen] = data[i];
        ctx->datalen++;
        if (ctx->datalen == 64) {
            sha256_transform(ctx, ctx->data);
            ctx->bitlen += 512;
            ctx->datalen = 0;
        }
    }
}

void bc_sha256_final(bc_sha256_ctx *ctx, uint8_t out[32])
{
    size_t i = ctx->datalen;

    if (ctx->datalen < 56) {
        ctx->data[i++] = 0x80;
        while (i < 56)
            ctx->data[i++] = 0x00;
    } else {
        ctx->data[i++] = 0x80;
        while (i < 64)
            ctx->data[i++] = 0x00;
        sha256_transform(ctx, ctx->data);
        memset(ctx->data, 0, 56);
    }

    ctx->bitlen += (uint64_t)ctx->datalen * 8;
    ctx->data[63] = (uint8_t)(ctx->bitlen);
    ctx->data[62] = (uint8_t)(ctx->bitlen >> 8);
    ctx->data[61] = (uint8_t)(ctx->bitlen >> 16);
    ctx->data[60] = (uint8_t)(ctx->bitlen >> 24);
    ctx->data[59] = (uint8_t)(ctx->bitlen >> 32);
    ctx->data[58] = (uint8_t)(ctx->bitlen >> 40);
    ctx->data[57] = (uint8_t)(ctx->bitlen >> 48);
    ctx->data[56] = (uint8_t)(ctx->bitlen >> 56);
    sha256_transform(ctx, ctx->data);

    for (int k = 0; k < 8; ++k) {
        out[k * 4 + 0] = (uint8_t)((ctx->state[k] >> 24) & 0xff);
        out[k * 4 + 1] = (uint8_t)((ctx->state[k] >> 16) & 0xff);
        out[k * 4 + 2] = (uint8_t)((ctx->state[k] >> 8) & 0xff);
        out[k * 4 + 3] = (uint8_t)(ctx->state[k]) & 0xff;
    }
}

void bc_sha256(const uint8_t *data, size_t len, uint8_t out[32])
{
    bc_sha256_ctx ctx;
    bc_sha256_init(&ctx);
    bc_sha256_update(&ctx, data, len);
    bc_sha256_final(&ctx, out);
}

/* ── HMAC-SHA256 (RFC 2104) ─────────────────────────────────────── */

void bc_hmac_sha256(const uint8_t *key, size_t keylen,
                    const uint8_t *msg, size_t msglen, uint8_t out[32])
{
    uint8_t k[64], ipad[64], opad[64], inner[32];
    bc_sha256_ctx ctx;

    memset(k, 0, sizeof(k));
    if (keylen > 64)
        bc_sha256(key, keylen, k); /* long keys are hashed first */
    else
        memcpy(k, key, keylen);

    for (int i = 0; i < 64; i++) {
        ipad[i] = k[i] ^ 0x36;
        opad[i] = k[i] ^ 0x5c;
    }

    bc_sha256_init(&ctx);
    bc_sha256_update(&ctx, ipad, 64);
    bc_sha256_update(&ctx, msg, msglen);
    bc_sha256_final(&ctx, inner);

    bc_sha256_init(&ctx);
    bc_sha256_update(&ctx, opad, 64);
    bc_sha256_update(&ctx, inner, 32);
    bc_sha256_final(&ctx, out);
}

/* ── FNV-1a 32-bit PoW ──────────────────────────────────────────── */

uint32_t bc_fnv1a(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; s++) {
        h ^= (uint8_t)*s;
        h *= 16777619u;
    }
    return h;
}

int bc_pow_ok(const char *salt, uint32_t nonce, int difficulty)
{
    if (difficulty <= 0)
        return 1;
    if (difficulty > 24)
        difficulty = 24; /* cap: keep JS solve time bounded */

    char buf[256];
    int n = snprintf(buf, sizeof(buf), "%s:%u", salt ? salt : "", nonce);
    if (n <= 0 || n >= (int)sizeof(buf))
        return 0;

    uint32_t h = bc_fnv1a(buf);

    /* leading zero bits of h (big-endian bit order) */
    int zeros = 0;
    for (int bit = 31; bit >= 0 && zeros < difficulty; bit--) {
        if ((h >> bit) & 1u)
            break;
        zeros++;
    }
    return zeros >= difficulty;
}

int bc_cteq(const uint8_t *a, const uint8_t *b, size_t len)
{
    uint8_t d = 0;
    for (size_t i = 0; i < len; i++)
        d |= a[i] ^ b[i];
    return d == 0;
}
