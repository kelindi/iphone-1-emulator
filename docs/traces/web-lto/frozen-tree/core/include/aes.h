/*
 * S5LBox — AES (128/192/256) with CBC mode.
 *
 * Apple encrypts the DATA payload of 3.x-era IMG3 images with AES-CBC, so
 * decryption sits between us and running real firmware. The keys and IVs for
 * 3.x devices are published by the reverse-engineering community; the user
 * supplies them (we ship none).
 *
 * This is a compact, self-contained implementation so the core keeps its
 * zero-dependency property — no OpenSSL, nothing to cross-compile for iOS.
 * It is validated against the FIPS-197 known-answer vectors.
 *
 * NOTE: this implementation is written for correctness and portability, not to
 * resist side-channel attacks. It decrypts publicly-known firmware keys on the
 * user's own device; it is not suitable for protecting secrets.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#ifndef S5LBOX_AES_H
#define S5LBOX_AES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AES_BLOCK_SIZE 16u
#define AES_MAX_ROUNDS 14u

typedef struct {
    uint32_t rk[4 * (AES_MAX_ROUNDS + 1)];  /* expanded key schedule */
    unsigned rounds;
} aes_ctx_t;

/* key_bits must be 128, 192 or 256. Returns false otherwise. */
bool aes_init(aes_ctx_t *ctx, const uint8_t *key, unsigned key_bits);

/* Single-block operations (ECB primitives). */
void aes_encrypt_block(const aes_ctx_t *ctx, const uint8_t in[16], uint8_t out[16]);
void aes_decrypt_block(const aes_ctx_t *ctx, const uint8_t in[16], uint8_t out[16]);

/*
 * CBC decryption in place or out of place. `len` must be a multiple of 16;
 * returns false otherwise. `iv` is not modified. in and out may be equal.
 */
bool aes_cbc_decrypt(const aes_ctx_t *ctx, const uint8_t iv[16],
                     const uint8_t *in, uint8_t *out, size_t len);

/* CBC encryption, used by the tests to verify round-trips. */
bool aes_cbc_encrypt(const aes_ctx_t *ctx, const uint8_t iv[16],
                     const uint8_t *in, uint8_t *out, size_t len);

#endif /* S5LBOX_AES_H */
