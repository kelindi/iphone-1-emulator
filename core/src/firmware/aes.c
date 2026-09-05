/*
 * S5LBox — AES (128/192/256) and CBC mode.
 *
 * A compact byte-oriented implementation: no lookup-table generation beyond the
 * S-box, and the inverse S-box is derived from the forward one at init so it
 * cannot silently disagree with it. Correctness is established by the FIPS-197
 * known-answer vectors in tests/test_aes.c.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#include "aes.h"
#include <string.h>

static const uint8_t SBOX[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

/* Derived once from SBOX so the two can never disagree. */
static uint8_t INV_SBOX[256];
static bool    g_tables_ready = false;

static void build_tables(void) {
    if (g_tables_ready) return;
    for (unsigned i = 0; i < 256; i++) INV_SBOX[SBOX[i]] = (uint8_t)i;
    g_tables_ready = true;
}

/* Multiply by x in GF(2^8) modulo the AES polynomial. */
static inline uint8_t xtime(uint8_t a) {
    return (uint8_t)((a << 1) ^ ((a & 0x80) ? 0x1b : 0x00));
}

/* General GF(2^8) multiply, used by InvMixColumns. */
static uint8_t gmul(uint8_t a, uint8_t b) {
    uint8_t r = 0;
    while (b) {
        if (b & 1) r ^= a;
        a = xtime(a);
        b >>= 1;
    }
    return r;
}

static inline uint32_t sub_word(uint32_t w) {
    return ((uint32_t)SBOX[(w >> 24) & 0xff] << 24)
         | ((uint32_t)SBOX[(w >> 16) & 0xff] << 16)
         | ((uint32_t)SBOX[(w >> 8)  & 0xff] << 8)
         | ((uint32_t)SBOX[ w        & 0xff]);
}
static inline uint32_t rot_word(uint32_t w) { return (w << 8) | (w >> 24); }

bool aes_init(aes_ctx_t *ctx, const uint8_t *key, unsigned key_bits) {
    unsigned nk;
    switch (key_bits) {
        case 128: nk = 4; ctx->rounds = 10; break;
        case 192: nk = 6; ctx->rounds = 12; break;
        case 256: nk = 8; ctx->rounds = 14; break;
        default: return false;
    }
    build_tables();

    const unsigned total = 4 * (ctx->rounds + 1);
    for (unsigned i = 0; i < nk; i++) {
        ctx->rk[i] = ((uint32_t)key[4*i] << 24) | ((uint32_t)key[4*i+1] << 16)
                   | ((uint32_t)key[4*i+2] << 8) | (uint32_t)key[4*i+3];
    }

    uint8_t rcon = 1;
    for (unsigned i = nk; i < total; i++) {
        uint32_t t = ctx->rk[i - 1];
        if (i % nk == 0) {
            t = sub_word(rot_word(t)) ^ ((uint32_t)rcon << 24);
            rcon = xtime(rcon);
        } else if (nk > 6 && i % nk == 4) {
            t = sub_word(t);
        }
        ctx->rk[i] = ctx->rk[i - nk] ^ t;
    }
    return true;
}

/* State is column-major: s[r][c] = block[c*4 + r]. */
static void add_round_key(uint8_t s[16], const uint32_t *rk) {
    for (unsigned c = 0; c < 4; c++) {
        uint32_t w = rk[c];
        s[4*c + 0] ^= (uint8_t)(w >> 24);
        s[4*c + 1] ^= (uint8_t)(w >> 16);
        s[4*c + 2] ^= (uint8_t)(w >> 8);
        s[4*c + 3] ^= (uint8_t)w;
    }
}

static void shift_rows(uint8_t s[16]) {
    uint8_t t[16];
    memcpy(t, s, 16);
    for (unsigned r = 1; r < 4; r++)
        for (unsigned c = 0; c < 4; c++)
            s[4*c + r] = t[4*((c + r) % 4) + r];
}

static void inv_shift_rows(uint8_t s[16]) {
    uint8_t t[16];
    memcpy(t, s, 16);
    for (unsigned r = 1; r < 4; r++)
        for (unsigned c = 0; c < 4; c++)
            s[4*((c + r) % 4) + r] = t[4*c + r];
}

static void mix_columns(uint8_t s[16]) {
    for (unsigned c = 0; c < 4; c++) {
        uint8_t *p = &s[4*c];
        uint8_t a0 = p[0], a1 = p[1], a2 = p[2], a3 = p[3];
        uint8_t x = (uint8_t)(a0 ^ a1 ^ a2 ^ a3);
        p[0] ^= (uint8_t)(x ^ xtime((uint8_t)(a0 ^ a1)));
        p[1] ^= (uint8_t)(x ^ xtime((uint8_t)(a1 ^ a2)));
        p[2] ^= (uint8_t)(x ^ xtime((uint8_t)(a2 ^ a3)));
        p[3] ^= (uint8_t)(x ^ xtime((uint8_t)(a3 ^ a0)));
    }
}

static void inv_mix_columns(uint8_t s[16]) {
    for (unsigned c = 0; c < 4; c++) {
        uint8_t *p = &s[4*c];
        uint8_t a0 = p[0], a1 = p[1], a2 = p[2], a3 = p[3];
        p[0] = (uint8_t)(gmul(a0,14) ^ gmul(a1,11) ^ gmul(a2,13) ^ gmul(a3, 9));
        p[1] = (uint8_t)(gmul(a0, 9) ^ gmul(a1,14) ^ gmul(a2,11) ^ gmul(a3,13));
        p[2] = (uint8_t)(gmul(a0,13) ^ gmul(a1, 9) ^ gmul(a2,14) ^ gmul(a3,11));
        p[3] = (uint8_t)(gmul(a0,11) ^ gmul(a1,13) ^ gmul(a2, 9) ^ gmul(a3,14));
    }
}

void aes_encrypt_block(const aes_ctx_t *ctx, const uint8_t in[16], uint8_t out[16]) {
    uint8_t s[16];
    memcpy(s, in, 16);
    add_round_key(s, &ctx->rk[0]);
    for (unsigned r = 1; r < ctx->rounds; r++) {
        for (unsigned i = 0; i < 16; i++) s[i] = SBOX[s[i]];
        shift_rows(s);
        mix_columns(s);
        add_round_key(s, &ctx->rk[4*r]);
    }
    for (unsigned i = 0; i < 16; i++) s[i] = SBOX[s[i]];
    shift_rows(s);
    add_round_key(s, &ctx->rk[4*ctx->rounds]);
    memcpy(out, s, 16);
}

void aes_decrypt_block(const aes_ctx_t *ctx, const uint8_t in[16], uint8_t out[16]) {
    uint8_t s[16];
    memcpy(s, in, 16);
    add_round_key(s, &ctx->rk[4*ctx->rounds]);
    for (unsigned r = ctx->rounds - 1; r > 0; r--) {
        inv_shift_rows(s);
        for (unsigned i = 0; i < 16; i++) s[i] = INV_SBOX[s[i]];
        add_round_key(s, &ctx->rk[4*r]);
        inv_mix_columns(s);
    }
    inv_shift_rows(s);
    for (unsigned i = 0; i < 16; i++) s[i] = INV_SBOX[s[i]];
    add_round_key(s, &ctx->rk[0]);
    memcpy(out, s, 16);
}

bool aes_cbc_decrypt(const aes_ctx_t *ctx, const uint8_t iv[16],
                     const uint8_t *in, uint8_t *out, size_t len) {
    if (len % AES_BLOCK_SIZE) return false;
    uint8_t prev[16], cipher[16], plain[16];
    memcpy(prev, iv, 16);
    for (size_t off = 0; off < len; off += AES_BLOCK_SIZE) {
        memcpy(cipher, in + off, 16);           /* copy first: in may alias out */
        aes_decrypt_block(ctx, cipher, plain);
        for (unsigned i = 0; i < 16; i++) plain[i] ^= prev[i];
        memcpy(out + off, plain, 16);
        memcpy(prev, cipher, 16);
    }
    return true;
}

bool aes_cbc_encrypt(const aes_ctx_t *ctx, const uint8_t iv[16],
                     const uint8_t *in, uint8_t *out, size_t len) {
    if (len % AES_BLOCK_SIZE) return false;
    uint8_t prev[16], block[16];
    memcpy(prev, iv, 16);
    for (size_t off = 0; off < len; off += AES_BLOCK_SIZE) {
        for (unsigned i = 0; i < 16; i++) block[i] = (uint8_t)(in[off + i] ^ prev[i]);
        aes_encrypt_block(ctx, block, prev);
        memcpy(out + off, prev, 16);
    }
    return true;
}
