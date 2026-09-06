/*
 * S5LBox — LZSS decompression (Okumura's algorithm) and Adler-32.
 *
 * Written defensively: the input is a user-supplied firmware payload, so both
 * the source and destination are bounded and a truncated or hostile stream
 * stops cleanly rather than running off either buffer.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#include "lzss.h"
#include <string.h>

#define LZSS_N         4096u   /* ring buffer size          */
#define LZSS_F           18u   /* longest match             */
#define LZSS_THRESHOLD    2u   /* shortest encoded match    */

static uint32_t rd32be(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

bool lzss_parse_header(const uint8_t *buf, size_t len, lzss_header_t *out) {
    if (!buf || len < LZSS_HEADER_SIZE) return false;
    if (rd32be(buf) != LZSS_SIG_COMP) return false;
    if (rd32be(buf + 4) != LZSS_SIG_LZSS) return false;

    uint32_t adler = rd32be(buf + 8);
    uint32_t unc   = rd32be(buf + 12);
    uint32_t cmp   = rd32be(buf + 16);

    /* The declared compressed size must fit what we were actually given. */
    if ((uint64_t)LZSS_HEADER_SIZE + cmp > (uint64_t)len) return false;

    if (out) {
        out->adler32 = adler;
        out->uncompressed_size = unc;
        out->compressed_size = cmp;
    }
    return true;
}

size_t lzss_decompress(uint8_t *dst, size_t dstcap,
                       const uint8_t *src, size_t srclen) {
    if (!dst || !src) return 0;

    /* The ring buffer starts filled with spaces, as the algorithm specifies;
     * early back-references legitimately read from it before it is written. */
    uint8_t ring[LZSS_N + LZSS_F - 1];
    memset(ring, ' ', LZSS_N - LZSS_F);

    const uint8_t *send = src + srclen;
    size_t out = 0;
    unsigned r = LZSS_N - LZSS_F;
    unsigned flags = 0;

    for (;;) {
        /* One flag byte governs the next eight items. */
        if (((flags >>= 1) & 0x100u) == 0) {
            if (src >= send) break;
            flags = (unsigned)(*src++) | 0xff00u;
        }

        if (flags & 1u) {                       /* literal byte */
            if (src >= send) break;
            uint8_t c = *src++;
            if (out >= dstcap) break;           /* never overrun the caller */
            dst[out++] = c;
            ring[r++] = c;
            r &= (LZSS_N - 1u);
        } else {                                /* back-reference */
            if (src + 1 >= send) break;
            unsigned i = *src++;
            unsigned j = *src++;
            i |= (j & 0xf0u) << 4;              /* 12-bit ring offset */
            j = (j & 0x0fu) + LZSS_THRESHOLD;   /* match length - 1   */
            for (unsigned k = 0; k <= j; k++) {
                uint8_t c = ring[(i + k) & (LZSS_N - 1u)];
                if (out >= dstcap) return out;
                dst[out++] = c;
                ring[r++] = c;
                r &= (LZSS_N - 1u);
            }
        }
    }
    return out;
}

uint32_t lzss_adler32(const uint8_t *data, size_t len) {
    /* Apple's kernelcache header carries a plain Adler-32 of the plaintext. */
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < len; i++) {
        a = (a + data[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}
