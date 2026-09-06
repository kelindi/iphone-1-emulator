/*
 * S5LBox — S5L8900 NOR flash.
 *
 * Holds the low-level boot images. Reads are served memory-mapped; the image
 * directory is built by scanning for IMG3 containers rather than by parsing a
 * guessed-at Apple image table (see soc.h for why).
 *
 * The scanner treats NOR contents as untrusted: every candidate header is
 * bounds-checked against the flash size before it is believed, so a corrupt or
 * hostile dump produces a short directory, never an out-of-bounds read.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#include "soc.h"
#include "img3.h"
#include <stdlib.h>
#include <string.h>

/* Shared with the parser so the two layers cannot disagree: a real image
 * starts with the bytes 33 67 6d 49 ("3gmI"), read little-endian as IMG3_MAGIC. */
#define IMG3_DISK_MAGIC IMG3_MAGIC
#define IMG3_HEADER     20u

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool s5l_nor_init(s5l_nor_t *n, uint32_t size) {
    if (!n || !size) return false;
    memset(n, 0, sizeof *n);
    n->data = malloc(size);
    if (!n->data) return false;
    n->size = size;
    /* Erased flash reads as all ones. Starting from zero would make the device
     * behave like RAM-that-cannot-be-written: programming only clears bits, so
     * with every bit already 0 no write could ever have an effect. */
    memset(n->data, 0xff, size);
    return true;
}

void s5l_nor_free(s5l_nor_t *n) {
    if (!n) return;
    free(n->data);
    n->data = NULL;
    n->size = 0;
    n->image_count = 0;
}

uint32_t s5l_nor_read(const s5l_nor_t *n, uint32_t off, unsigned bytes) {
    /* The result is a uint32_t, so a wider access cannot be represented — bound
     * it here rather than trusting every present and future caller. */
    if (!n || (bytes != 1u && bytes != 2u && bytes != 4u)) return 0;
    /* 64-bit comparison so an offset near the top of the space cannot wrap. */
    if (!n->data || (uint64_t)off + bytes > (uint64_t)n->size) return 0;
    uint32_t v = 0;
    memcpy(&v, &n->data[off], bytes);
    return v;
}

void s5l_nor_program(s5l_nor_t *n, uint32_t off, const void *src, size_t len) {
    if (!n || !n->data || !src || !len || len > n->size) return;
    if ((uint64_t)off + len > (uint64_t)n->size) return;
    memcpy(&n->data[off], src, len);
}

unsigned s5l_nor_scan(s5l_nor_t *n) {
    if (!n) return 0;
    n->image_count = 0;
    if (!n->data || n->size < IMG3_HEADER) return 0;

    /* IMG3 containers are word-aligned in flash, so step by 4. */
    uint32_t off = 0;
    while ((uint64_t)off + IMG3_HEADER <= (uint64_t)n->size) {
        if (rd32(&n->data[off]) != IMG3_DISK_MAGIC) { off += 4; continue; }

        uint32_t full  = rd32(&n->data[off + 4]);
        uint32_t ident = rd32(&n->data[off + 16]);

        /* Believe the header only if the container actually fits. */
        if (full < IMG3_HEADER || (uint64_t)off + full > (uint64_t)n->size) {
            off += 4;
            continue;
        }

        n->images[n->image_count].ident  = ident;
        n->images[n->image_count].offset = off;
        n->images[n->image_count].size   = full;
        if (++n->image_count == S5L_NOR_MAX_IMAGES) break;

        /* Skip past the container, then re-align: a container whose size is not
         * a multiple of 4 would otherwise leave the scan permanently off the
         * word grid and it would never see any later image. */
        uint32_t next = (uint32_t)(((uint64_t)off + full + 3u) & ~(uint64_t)3u);
        if (next <= off) break;                  /* never move backwards */
        off = next;
    }
    return n->image_count;
}

const s5l_nor_entry_t *s5l_nor_find(const s5l_nor_t *n, uint32_t ident) {
    if (!n || !n->data) return NULL;
    unsigned count = n->image_count < S5L_NOR_MAX_IMAGES
                   ? n->image_count : S5L_NOR_MAX_IMAGES;
    for (unsigned i = 0; i < count; i++) {
        const s5l_nor_entry_t *e = &n->images[i];
        if (e->ident == ident && e->size >= 20u &&
            (uint64_t)e->offset + e->size <= n->size)
            return e;
    }
    return NULL;
}

bool s5l_nor_write(s5l_nor_t *n, uint32_t off, uint32_t val, unsigned bytes) {
    if (!n || !n->data ||
        (bytes != 1u && bytes != 2u && bytes != 4u)) return false;
    if ((uint64_t)off + bytes > (uint64_t)n->size) return false;

    /* Flash programming can only clear bits. Refuse a write that would need to
     * set one back to 1 rather than silently behaving like RAM — the same
     * discipline the NAND model uses. */
    for (unsigned i = 0; i < bytes; i++) {
        uint8_t want = (uint8_t)(val >> (8 * i));
        if ((n->data[off + i] & want) != want) return false;
    }
    for (unsigned i = 0; i < bytes; i++)
        n->data[off + i] &= (uint8_t)(val >> (8 * i));
    return true;
}

bool s5l_nor_erase_sector(s5l_nor_t *n, uint32_t off) {
    if (!n || !n->data) return false;
    uint32_t base = off & ~(S5L8900_NOR_SECTOR - 1u);
    if ((uint64_t)base + S5L8900_NOR_SECTOR > (uint64_t)n->size) return false;
    memset(&n->data[base], 0xff, S5L8900_NOR_SECTOR);
    return true;
}
