/*
 * S5LBox — S5L8900 NAND flash device.
 *
 * Models the raw device faithfully, including the property that programming can
 * only clear bits. See nand.h for why the Apple VFL/FTL layers are deliberately
 * not guessed at here.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#include "nand.h"
#include <stdlib.h>
#include <string.h>

uint32_t nand_total_pages(const nand_t *n) {
    if (!n) return 0;
    return n->pages_per_block * n->block_count;
}

bool nand_init(nand_t *n, uint32_t page_size, uint32_t spare_size,
               uint32_t pages_per_block, uint32_t block_count) {
    if (!n) return false;
    memset(n, 0, sizeof *n);
    if (!page_size || !pages_per_block || !block_count) return false;

    /* Reject geometries whose byte counts would overflow 32-bit arithmetic. */
    uint64_t pages = (uint64_t)pages_per_block * block_count;
    uint64_t dbytes = pages * page_size;
    uint64_t sbytes = pages * spare_size;
    if (pages > 0xffffffffu || dbytes > (uint64_t)SIZE_MAX ||
        sbytes > (uint64_t)SIZE_MAX ||
        (uint64_t)page_size + spare_size > 0xffffffffu) return false;

    n->page_size = page_size;
    n->spare_size = spare_size;
    n->pages_per_block = pages_per_block;
    n->block_count = block_count;

    n->blocks = calloc(block_count, sizeof *n->blocks);
    n->bad    = calloc(block_count, 1);
    if (!n->blocks || !n->bad) {
        nand_free(n);
        return false;
    }
    return true;
}

static void free_block(nand_t *n, uint32_t block) {
    uint8_t **pages = n->blocks[block];
    if (!pages) return;
    for (uint32_t p = 0; p < n->pages_per_block; p++) {
        if (pages[p]) { free(pages[p]); n->live_pages--; }
    }
    free(pages);
    n->blocks[block] = NULL;
}

void nand_free(nand_t *n) {
    if (!n) return;
    if (n->blocks)
        for (uint32_t b = 0; b < n->block_count; b++) free_block(n, b);
    free(n->blocks); n->blocks = NULL;
    free(n->bad);    n->bad = NULL;
    free(n->changed); n->changed = NULL;
    n->block_count = 0;
    n->live_pages = 0;
}

static void note_change(nand_t *n, uint32_t page) {
    if (n->changed) n->changed[page >> 3] |= (uint8_t)(1u << (page & 7u));
}

bool nand_track_changes(nand_t *n, uint64_t base_hash) {
    if (!n || !n->blocks) return false;
    free(n->changed);
    n->changed = calloc(((size_t)nand_total_pages(n) + 7u) / 8u, 1);
    n->base_hash = base_hash;
    return n->changed != NULL;
}

bool nand_page_changed(const nand_t *n, uint32_t page) {
    return n && n->changed && page < nand_total_pages(n) &&
           (n->changed[page >> 3] >> (page & 7u)) & 1u;
}

static const uint8_t *page_ptr(const nand_t *n, uint32_t page) {
    uint8_t **pages = n->blocks[page / n->pages_per_block];
    return pages ? pages[page % n->pages_per_block] : NULL;
}

/* Returns the page's buffer, materialising it (all ones) if needed. */
static uint8_t *page_mut(nand_t *n, uint32_t page) {
    uint32_t block = page / n->pages_per_block, p = page % n->pages_per_block;
    if (!n->blocks[block]) {
        n->blocks[block] = calloc(n->pages_per_block, sizeof(uint8_t *));
        if (!n->blocks[block]) return NULL;
    }
    uint8_t *buf = n->blocks[block][p];
    if (!buf) {
        size_t len = (size_t)n->page_size + n->spare_size;
        buf = malloc(len);
        if (!buf) return NULL;
        memset(buf, 0xff, len);
        n->blocks[block][p] = buf;
        n->live_pages++;
    }
    return buf;
}

nand_status_t nand_read_page(const nand_t *n, uint32_t page,
                             uint8_t *data, uint8_t *spare) {
    if (!n || !n->blocks || page >= nand_total_pages(n)) return NAND_ERR_RANGE;
    const uint8_t *buf = page_ptr(n, page);
    if (data) {
        if (buf) memcpy(data, buf, n->page_size);
        else     memset(data, 0xff, n->page_size);
    }
    if (spare && n->spare_size) {
        if (buf) memcpy(spare, buf + n->page_size, n->spare_size);
        else     memset(spare, 0xff, n->spare_size);
    }
    return NAND_OK;
}

static bool all_ones(const uint8_t *p, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) if (p[i] != 0xffu) return false;
    return true;
}

nand_status_t nand_program_page(nand_t *n, uint32_t page,
                                const uint8_t *data, const uint8_t *spare) {
    if (!n || !n->blocks || !n->bad || page >= nand_total_pages(n))
        return NAND_ERR_RANGE;
    uint32_t block = page / n->pages_per_block;
    if (n->bad[block]) return NAND_ERR_BAD_BLOCK;

    const uint8_t *cur = page_ptr(n, page);

    /* NAND programming can only turn 1 bits into 0. Writing a bit that is
     * already 0 back to 1 is physically impossible, so reject it rather than
     * pretending it worked — that is exactly the class of bug that "works in
     * the emulator and corrupts on hardware". */
    if (cur && data) {
        for (uint32_t i = 0; i < n->page_size; i++)
            if ((cur[i] & data[i]) != data[i]) return NAND_ERR_NOT_ERASED;
    }
    if (cur && spare && n->spare_size) {
        const uint8_t *s = cur + n->page_size;
        for (uint32_t i = 0; i < n->spare_size; i++)
            if ((s[i] & spare[i]) != spare[i]) return NAND_ERR_NOT_ERASED;
    }
    /* Programming all ones into an erased page changes nothing. */
    if (!cur && (!data || all_ones(data, n->page_size)) &&
        (!spare || !n->spare_size || all_ones(spare, n->spare_size)))
        return NAND_OK;

    uint8_t *d = page_mut(n, page);
    if (!d) return NAND_ERR_NOMEM;
    if (data) for (uint32_t i = 0; i < n->page_size; i++) d[i] &= data[i];
    if (spare && n->spare_size) {
        uint8_t *s = d + n->page_size;
        for (uint32_t i = 0; i < n->spare_size; i++) s[i] &= spare[i];
    }
    note_change(n, page);
    return NAND_OK;
}

nand_status_t nand_store_page(nand_t *n, uint32_t page,
                              const uint8_t *data, const uint8_t *spare) {
    if (!n || !n->blocks || page >= nand_total_pages(n) || !data ||
        (n->spare_size && !spare)) return NAND_ERR_RANGE;
    uint32_t block = page / n->pages_per_block, p = page % n->pages_per_block;
    if (all_ones(data, n->page_size) &&
        (!n->spare_size || all_ones(spare, n->spare_size))) {
        if (n->blocks[block] && n->blocks[block][p]) {
            free(n->blocks[block][p]);
            n->blocks[block][p] = NULL;
            n->live_pages--;
        }
        note_change(n, page);
        return NAND_OK;
    }
    uint8_t *d = page_mut(n, page);
    if (!d) return NAND_ERR_NOMEM;
    memcpy(d, data, n->page_size);
    if (n->spare_size) memcpy(d + n->page_size, spare, n->spare_size);
    note_change(n, page);
    return NAND_OK;
}

nand_status_t nand_erase_block(nand_t *n, uint32_t block) {
    if (!n || !n->blocks || !n->bad || block >= n->block_count)
        return NAND_ERR_RANGE;
    if (n->bad[block]) return NAND_ERR_BAD_BLOCK;
    if (n->blocks[block])
        for (uint32_t p = 0; p < n->pages_per_block; p++)
            note_change(n, block * n->pages_per_block + p);
    free_block(n, block);
    return NAND_OK;
}

void nand_mark_bad(nand_t *n, uint32_t block) {
    if (n && n->bad && block < n->block_count) n->bad[block] = 1;
}

bool nand_is_bad(const nand_t *n, uint32_t block) {
    return n && n->bad && block < n->block_count && n->bad[block] != 0;
}
