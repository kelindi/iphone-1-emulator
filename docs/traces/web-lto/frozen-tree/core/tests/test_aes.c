/*
 * S5LBox — AES tests.
 *
 * Validated against the FIPS-197 Appendix C known-answer vectors. These are
 * published constants: if our implementation reproduces them exactly for all
 * three key sizes, in both directions, it is correct.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#include "aes.h"
#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, ...) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("  FAIL %s:%d: ", __func__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

static void hexdump(const uint8_t *p, size_t n, char *out) {
    static const char *h = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) { out[2*i] = h[p[i] >> 4]; out[2*i+1] = h[p[i] & 15]; }
    out[2*n] = '\0';
}

/* FIPS-197 C.1/C.2/C.3 all use this plaintext. */
static const uint8_t PT[16] = {
    0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,
    0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff
};

static void run_kat(const char *name, const uint8_t *key, unsigned bits,
                    const uint8_t expect[16]) {
    aes_ctx_t ctx;
    CHECK(aes_init(&ctx, key, bits), "%s: init failed", name);

    uint8_t ct[16], back[16];
    aes_encrypt_block(&ctx, PT, ct);
    char got[33], want[33];
    hexdump(ct, 16, got); hexdump(expect, 16, want);
    CHECK(memcmp(ct, expect, 16) == 0, "%s encrypt: got %s expect %s", name, got, want);

    aes_decrypt_block(&ctx, ct, back);
    CHECK(memcmp(back, PT, 16) == 0, "%s decrypt did not recover the plaintext", name);
}

static void test_fips197_aes128(void) {
    static const uint8_t key[16] = {
        0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
        0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f
    };
    static const uint8_t ct[16] = {
        0x69,0xc4,0xe0,0xd8,0x6a,0x7b,0x04,0x30,
        0xd8,0xcd,0xb7,0x80,0x70,0xb4,0xc5,0x5a
    };
    run_kat("AES-128", key, 128, ct);
}

static void test_fips197_aes192(void) {
    static const uint8_t key[24] = {
        0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
        0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
        0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17
    };
    static const uint8_t ct[16] = {
        0xdd,0xa9,0x7c,0xa4,0x86,0x4c,0xdf,0xe0,
        0x6e,0xaf,0x70,0xa0,0xec,0x0d,0x71,0x91
    };
    run_kat("AES-192", key, 192, ct);
}

static void test_fips197_aes256(void) {
    static const uint8_t key[32] = {
        0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
        0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
        0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,
        0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f
    };
    static const uint8_t ct[16] = {
        0x8e,0xa2,0xb7,0xca,0x51,0x67,0x45,0xbf,
        0xea,0xfc,0x49,0x90,0x4b,0x49,0x60,0x89
    };
    run_kat("AES-256", key, 256, ct);
}

static void test_bad_key_size_rejected(void) {
    aes_ctx_t ctx;
    uint8_t key[32] = {0};
    CHECK(!aes_init(&ctx, key, 64),  "64-bit key accepted");
    CHECK(!aes_init(&ctx, key, 512), "512-bit key accepted");
}

static void test_cbc_roundtrip(void) {
    /* CBC over several blocks must round-trip, and chaining must actually
     * happen: two identical plaintext blocks must encrypt differently. */
    aes_ctx_t ctx;
    uint8_t key[16], iv[16];
    for (unsigned i = 0; i < 16; i++) { key[i] = (uint8_t)i; iv[i] = (uint8_t)(0xf0 + i); }
    aes_init(&ctx, key, 128);

    uint8_t pt[48], ct[48], back[48];
    memset(pt, 0xAB, 32);                      /* two identical blocks */
    memset(pt + 32, 0x5C, 16);

    CHECK(aes_cbc_encrypt(&ctx, iv, pt, ct, sizeof pt), "cbc encrypt failed");
    CHECK(memcmp(ct, ct + 16, 16) != 0,
          "identical plaintext blocks produced identical ciphertext (chaining broken)");

    CHECK(aes_cbc_decrypt(&ctx, iv, ct, back, sizeof ct), "cbc decrypt failed");
    CHECK(memcmp(back, pt, sizeof pt) == 0, "cbc round-trip mismatch");
}

static void test_cbc_in_place(void) {
    /* Decrypting in place (in == out) must work — we do this on firmware. */
    aes_ctx_t ctx;
    uint8_t key[16] = {0}, iv[16] = {0};
    aes_init(&ctx, key, 128);

    uint8_t pt[32], buf[32];
    for (unsigned i = 0; i < 32; i++) pt[i] = (uint8_t)(i * 7);
    aes_cbc_encrypt(&ctx, iv, pt, buf, sizeof pt);
    CHECK(aes_cbc_decrypt(&ctx, iv, buf, buf, sizeof buf), "in-place decrypt failed");
    CHECK(memcmp(buf, pt, sizeof pt) == 0, "in-place decrypt mismatch");
}

static void test_cbc_rejects_partial_block(void) {
    aes_ctx_t ctx;
    uint8_t key[16] = {0}, iv[16] = {0}, buf[17] = {0};
    aes_init(&ctx, key, 128);
    CHECK(!aes_cbc_decrypt(&ctx, iv, buf, buf, 17), "non-multiple-of-16 accepted");
}

int main(void) {
    printf("S5LBox AES tests (FIPS-197 known-answer vectors)\n");
    test_fips197_aes128();
    test_fips197_aes192();
    test_fips197_aes256();
    test_bad_key_size_rejected();
    test_cbc_roundtrip();
    test_cbc_in_place();
    test_cbc_rejects_partial_block();
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
