/*
 * S5LBox — S5L8900 NAND flash device.
 *
 * NAND holds the root filesystem, so it is what M4/M5 ultimately need. This
 * header models the *device*: geometry, page reads and writes, block erase,
 * and per-page spare (OOB) bytes. That part is well understood and testable.
 *
 * SCOPE, STATED HONESTLY: sitting above the raw device on a real iPhone are
 * Apple's VFL (virtual flash layer) and FTL (flash translation layer), which
 * map logical pages to physical ones, handle wear levelling, bad blocks and
 * write journalling. Those layers are a substantial reverse-engineering job in
 * their own right and cannot be faithfully implemented without validating
 * against real firmware behaviour. This file deliberately does NOT guess at
 * them. It provides the raw device the FTL will sit on, plus a trivial
 * identity mapping useful for tests and for loading a prepared image.
 *
 * Attempting a plausible-looking FTL that has never been checked against real
 * data would be worse than having none: it would pass our tests and fail
 * silently on a genuine NAND dump.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#ifndef S5LBOX_NAND_H
#define S5LBOX_NAND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Geometry of the 8 GB-class SLC NAND used on this SoC generation. Kept as
 * fields rather than constants so tests can use a small device. */
#define NAND_DEFAULT_PAGE_SIZE  2048u
#define NAND_DEFAULT_SPARE_SIZE 64u
#define NAND_DEFAULT_PAGES_PER_BLOCK 128u

typedef struct {
    uint32_t page_size;        /* data bytes per page            */
    uint32_t spare_size;       /* out-of-band bytes per page     */
    uint32_t pages_per_block;
    uint32_t block_count;

    uint8_t *data;             /* page_size  * total_pages       */
    uint8_t *spare;            /* spare_size * total_pages       */
    uint8_t *bad;              /* one byte per block: nonzero = bad */
} nand_t;

typedef enum {
    NAND_OK = 0,
    NAND_ERR_RANGE,            /* page or block out of range      */
    NAND_ERR_BAD_BLOCK,        /* block marked bad                */
    NAND_ERR_NOT_ERASED        /* NAND can only clear bits: erase first */
} nand_status_t;

bool nand_init(nand_t *n, uint32_t page_size, uint32_t spare_size,
               uint32_t pages_per_block, uint32_t block_count);
void nand_free(nand_t *n);

uint32_t nand_total_pages(const nand_t *n);

/* Read a page's data and/or spare. Either pointer may be NULL. */
nand_status_t nand_read_page(const nand_t *n, uint32_t page,
                             uint8_t *data, uint8_t *spare);

/*
 * Program a page. Real NAND can only turn 1 bits into 0, so programming over
 * un-erased data is rejected rather than silently succeeding — that mistake is
 * a classic source of "works in the emulator, corrupts on hardware" bugs.
 */
nand_status_t nand_program_page(nand_t *n, uint32_t page,
                                const uint8_t *data, const uint8_t *spare);

/* Erase a block back to all-ones. */
nand_status_t nand_erase_block(nand_t *n, uint32_t block);

/* Bad-block marking, as a factory or the FTL would. */
void nand_mark_bad(nand_t *n, uint32_t block);
bool nand_is_bad(const nand_t *n, uint32_t block);

#endif /* S5LBOX_NAND_H */
