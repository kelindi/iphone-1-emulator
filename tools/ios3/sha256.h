/*
 * S5LBox -- small, allocation-free SHA-256 implementation.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#ifndef S5LBOX_SHA256_H
#define S5LBOX_SHA256_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IOS3_SHA256_BLOCK_SIZE 64u
#define IOS3_SHA256_DIGEST_SIZE 32u
#define IOS3_SHA256_MAX_INPUT_BYTES (UINT64_MAX / UINT64_C(8))

typedef struct ios3_sha256_context {
    uint32_t state[8];
    uint64_t total_bytes;
    uint8_t block[IOS3_SHA256_BLOCK_SIZE];
    size_t block_used;
    bool finalized;
} ios3_sha256_context_t;

/* Initialize a caller-owned context. */
bool ios3_sha256_init(ios3_sha256_context_t *context);

/*
 * Add bytes to an initialized context.  A NULL data pointer is accepted only
 * for a zero-length update.  The cumulative message is limited to
 * floor(UINT64_MAX / 8) bytes so finalization's bit count cannot overflow.
 * A rejected update leaves the complete context unchanged.
 */
bool ios3_sha256_update(ios3_sha256_context_t *context,
                        const void *data, size_t length);

/* Finalize exactly once and write IOS3_SHA256_DIGEST_SIZE bytes. */
bool ios3_sha256_final(ios3_sha256_context_t *context,
                       uint8_t digest[IOS3_SHA256_DIGEST_SIZE]);

/* Hash one bounded byte string without allocation. */
bool ios3_sha256(const void *data, size_t length,
                 uint8_t digest[IOS3_SHA256_DIGEST_SIZE]);

#ifdef __cplusplus
}
#endif

#endif /* S5LBOX_SHA256_H */
