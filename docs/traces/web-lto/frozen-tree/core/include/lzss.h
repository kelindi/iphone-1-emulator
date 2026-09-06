/*
 * S5LBox — LZSS decompression for Apple's compressed kernelcache.
 *
 * The kernelcache in an IPSW is AES-encrypted and then LZSS-compressed behind a
 * small "complzss" header. Decrypting it is not enough; this is the last step
 * between the IPSW and an actual Mach-O kernel we can load.
 *
 * The algorithm is Okumura's classic LZSS (public domain, widely documented and
 * used far beyond Apple) — a 4096-byte ring buffer, 18-byte maximum match, with
 * a flag byte every eight items selecting literal or back-reference. Nothing
 * here is Apple-specific beyond the container header.
 *
 * The compressed data comes from a user-supplied firmware file, so the
 * decompressor is bounded on both sides and cannot be made to write past the
 * caller's buffer.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#ifndef S5LBOX_LZSS_H
#define S5LBOX_LZSS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* "complzss" header, all fields big-endian, 0x180 bytes total. */
#define LZSS_HEADER_SIZE 0x180u
#define LZSS_SIG_COMP    0x636f6d70u   /* 'comp' */
#define LZSS_SIG_LZSS    0x6c7a7373u   /* 'lzss' */

typedef struct {
    uint32_t adler32;
    uint32_t uncompressed_size;
    uint32_t compressed_size;
} lzss_header_t;

/* Parse and validate the header. Returns false if it is not a complzss blob or
 * the declared compressed size does not fit the buffer. */
bool lzss_parse_header(const uint8_t *buf, size_t len, lzss_header_t *out);

/*
 * Decompress `srclen` bytes into `dst`, writing at most `dstcap` bytes.
 * Returns the number of bytes produced, or 0 on malformed input. Never writes
 * past dstcap.
 */
size_t lzss_decompress(uint8_t *dst, size_t dstcap,
                       const uint8_t *src, size_t srclen);

/* Adler-32, used to verify a decompressed kernelcache against its header. */
uint32_t lzss_adler32(const uint8_t *data, size_t len);

#endif /* S5LBOX_LZSS_H */
