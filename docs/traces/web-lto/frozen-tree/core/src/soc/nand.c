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
        sbytes > (uint64_t)SIZE_MAX) return false;

    n->page_size = page_size;
    n->spare_size = spare_size;
    n->pages_per_block = pages_per_block;
    n->block_count = block_count;

    n->data  = malloc((size_t)dbytes);
    n->spare = spare_size ? malloc((size_t)sbytes) : NULL;
    n->bad   = calloc(block_count, 1);
    if (!n->data || (spare_size && !n->spare) || !n->bad) {
        nand_free(n);
        return false;
    }

    /* Erased NAND reads as all ones. */
    memset(n->data, 0xff, (size_t)dbytes);
    if (n->spare) memset(n->spare, 0xff, (size_t)sbytes);
    return true;
}

void nand_free(nand_t *n) {
    if (!n) return;
    free(n->data);  n->data = NULL;
    free(n->spare); n->spare = NULL;
    free(n->bad);   n->bad = NULL;
    n->block_count = 0;
}

nand_status_t nand_read_page(const nand_t *n, uint32_t page,
                             uint8_t *data, uint8_t *spare) {
    if (!n || !n->data || page >= nand_total_pages(n)) return NAND_ERR_RANGE;
    if (data)
        memcpy(data, &n->data[(size_t)page * n->page_size], n->page_size);
    if (spare && n->spare)
        memcpy(spare, &n->spare[(size_t)page * n->spare_size], n->spare_size);
    return NAND_OK;
}

nand_status_t nand_program_page(nand_t *n, uint32_t page,
                                const uint8_t *data, const uint8_t *spare) {
    if (!n || !n->data || !n->bad || page >= nand_total_pages(n))
        return NAND_ERR_RANGE;
    uint32_t block = page / n->pages_per_block;
    if (n->bad[block]) return NAND_ERR_BAD_BLOCK;

    uint8_t *d = &n->data[(size_t)page * n->page_size];

    /* NAND programming can only turn 1 bits into 0. Writing a bit that is
     * already 0 back to 1 is physically impossible, so reject it rather than
     * pretending it worked — that is exactly the class of bug that "works in
     * the emulator and corrupts on hardware". */
    if (data) {
        for (uint32_t i = 0; i < n->page_size; i++)
            if ((d[i] & data[i]) != data[i]) return NAND_ERR_NOT_ERASED;
    }
    if (spare && n->spare) {
        uint8_t *s = &n->spare[(size_t)page * n->spare_size];
        for (uint32_t i = 0; i < n->spare_size; i++)
            if ((s[i] & spare[i]) != spare[i]) return NAND_ERR_NOT_ERASED;
    }

    if (data) for (uint32_t i = 0; i < n->page_size; i++) d[i] &= data[i];
    if (spare && n->spare) {
        uint8_t *s = &n->spare[(size_t)page * n->spare_size];
        for (uint32_t i = 0; i < n->spare_size; i++) s[i] &= spare[i];
    }
    return NAND_OK;
}

nand_status_t nand_erase_block(nand_t *n, uint32_t block) {
    if (!n || !n->data || !n->bad || block >= n->block_count)
        return NAND_ERR_RANGE;
    if (n->bad[block]) return NAND_ERR_BAD_BLOCK;

    size_t first = (size_t)block * n->pages_per_block;
    memset(&n->data[first * n->page_size], 0xff,
           (size_t)n->pages_per_block * n->page_size);
    if (n->spare)
        memset(&n->spare[first * n->spare_size], 0xff,
               (size_t)n->pages_per_block * n->spare_size);
    return NAND_OK;
}

void nand_mark_bad(nand_t *n, uint32_t block) {
    if (n && n->bad && block < n->block_count) n->bad[block] = 1;
}

bool nand_is_bad(const nand_t *n, uint32_t block) {
    return n && n->bad && block < n->block_count && n->bad[block] != 0;
}
