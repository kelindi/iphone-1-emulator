/* /arm-io/fmc ("fmc,s5l8900x"), the S5L8900 NAND flash controller, as driven
 * by 1A543a's AppleS5L8900XFMC. Observed in the genuine driver:
 *  - _fmcResetEverything c0420208: +0x04 = 0x800 resets the controller and
 *    is polled until the bit clears; +0x00 selects banks (bit 0 and 0x800
 *    enable, chip selects at bits 1-8, broadcast 0x1fe); +0x08 = 0xff resets
 *    the NAND; after 10 ms +0x48 bits 4-11 are the ready-bank mask; 0xff0
 *    written to +0x48 acknowledges (write-one-to-clear).
 *  - _fmcReadID c0420d74 / c041fd1c / c041fcc4: +0x08 = 0x90, +0x2c = 0
 *    address bytes - 1, +0x0c = 0, +0x04 = 1 starts the address cycle and
 *    +0x48 bit 2 reports it; +0x30 = 3 asks for four bytes, +0x04 = 0x7e2
 *    starts the read, +0x48 bit 3 reports data ready, +0x80 holds the ID.
 *  - page read (c04214xx): +0x08 = 0x00, +0x2c = 4, +0x0c/+0x10 = column/
 *    row, address cycle, +0x08 = 0x30, then a gate sleep until the IRQ
 *    filter sees the bank-ready bit; +0x44 = 0x7c enables the interrupt.
 * Page data, DMA and RSECC are not modelled yet. */
#include "soc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fmc_trace = -1;
static void trace_init(void) {
    if (fmc_trace >= 0) return;
    const char *e = getenv("S5LBOX_FMC_TRACE");
    fmc_trace = !e ? 0 : strcmp(e, "cmd") ? 1 : 2;
}
static int fmc_escalated;
static bool tracing(void) {
    trace_init();
    return fmc_trace == 1 || (fmc_escalated && fmc_escalated++ < 40000);
}
static bool tracing_cmds(void) { trace_init(); return fmc_trace == 2; }

#define NAND_CMD_READ_ID 0x90u
#define NAND_CMD_RESET   0xffu
#define NAND_CMD_READ_GO 0x30u
#define NAND_CMD_PROG    0x80u
#define NAND_CMD_PROG_GO 0x10u
#define NAND_CMD_ERASE   0x60u
#define NAND_CMD_ERASE_GO 0xd0u
#define NAND_CMD_STATUS  0x70u
#define FMC_CC_WRITE     0x004u
/* READ STATUS: ready, not write-protected; bit 0 = last operation failed. */
#define NAND_STATUS_PASS 0xe0u
#define NAND_STATUS_FAIL 0xe1u
/* Busy times, shortened from the part's tR 25 us / tPROG 200 us / tBERS
 * 2 ms so whole-array scans stay quick. */
#define FMC_T_READ_US    25u
#define FMC_T_WRITE_US   50u
#define FMC_T_XFER_US    5u

void s5l_fmc_reset(s5l_fmc_t *f) {
    if (!f) return;
    nand_t *backing = f->nand;   /* the array survives controller reset */
    memset(f, 0, sizeof *f);
    f->nand = backing;
    f->chip_id = FMC_DEFAULT_CHIP_ID;
    f->bank_mask = 0x0fu;
    f->nand_status = NAND_STATUS_PASS;
    memset(f->wbuf, 0xff, sizeof f->wbuf);
    f->rpos = S5L_FMC_PAGE_BYTES;
}

bool s5l_fmc_rx_ready(const s5l_fmc_t *f) {
    return f->last_cmd == NAND_CMD_READ_GO && f->rpos < f->rend;
}

static uint32_t op_page(const s5l_fmc_t *f) {
    return f->op_bank * S5L_FMC_PAGES_PER_BANK + f->row;
}

static unsigned word(uint32_t off) { return (off >> 2) % (S5L_FMC_SIZE / 4u); }

static uint32_t selected_banks(const s5l_fmc_t *f) {
    return (f->regs[word(FMC_CTRL)] >> 1) & ((1u << S5L_FMC_BANKS) - 1u);
}

static uint32_t fmc_read(s5l_fmc_t *f, uint32_t off);
uint32_t s5l_fmc_read(s5l_fmc_t *f, uint32_t off) {
    static uint32_t last_off = ~0u, last_v, repeats;
    uint32_t v = fmc_read(f, off);
    if (tracing()) {
        if (off == last_off && v == last_v) {
            if (++repeats % 100000u == 0u)
                fprintf(stderr, "fmc   (x%u so far)\n", repeats);
        }
        else {
            if (repeats) fprintf(stderr, "fmc   (x%u)\n", repeats);
            fprintf(stderr, "fmc r %03x = %08x\n", off, v);
            last_off = off; last_v = v; repeats = 0;
        }
    }
    return v;
}

static uint32_t fmc_read(s5l_fmc_t *f, uint32_t off) {
    f->reads++;
    if (off == FMC_STATUS) return f->status;
    if (off == FMC_FIFO && f->last_cmd == NAND_CMD_READ_GO) {
        if (f->rpos + 4u > f->rend) return 0xffffffffu;
        const uint8_t *b = &f->rbuf[f->rpos];
        f->rpos += 4u;
        if (f->rpos >= f->rend) f->xfer_state = 1u;
        return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 |
               (uint32_t)b[3] << 24;
    }
    if (off == FMC_FIFO) return f->fifo;
    if (off == FMC_CMD_CTRL) return f->regs[word(off)] & ~FMC_CC_RESET;
    return off < S5L_FMC_SIZE ? f->regs[word(off)] : 0u;
}

/* The array is allocated on first use: an untouched device costs nothing. */
static bool fmc_backing(s5l_fmc_t *f) {
    if (f->nand) return true;
    nand_t *n = malloc(sizeof *n);
    if (!n) return false;
    if (!nand_init(n, 2048u, 64u, 64u, 0x2000u * 4u)) { free(n); return false; }
    f->nand = n;
    return true;
}

nand_t *s5l_fmc_backing(s5l_fmc_t *f) {
    return f && fmc_backing(f) ? f->nand : NULL;
}

void s5l_fmc_free(s5l_fmc_t *f) {
    if (!f || !f->nand) return;
    nand_free(f->nand);
    free(f->nand);
    f->nand = NULL;
}

/* Address cycles as the driver loads them: +0x0c holds cycles 0-3, +0x10
 * cycle 4; +0x2c is the cycle count minus one. Page operations use two
 * column + three row cycles, erase three row cycles. */
static void latch_address(s5l_fmc_t *f) {
    uint32_t a0 = f->regs[word(FMC_ADDR)], a1 = f->regs[word(FMC_ADDR) + 1u];
    uint32_t cycles = (f->regs[word(FMC_ADDR_COUNT)] & 7u) + 1u;
    if (cycles >= 5u) {
        f->col = a0 & 0xffffu;
        f->row = (a0 >> 16) | (a1 & 0xffu) << 16;
    } else {
        f->col = 0u;
        f->row = a0 & 0xffffffu;
    }
    if (f->row >= S5L_FMC_PAGES_PER_BANK) f->row %= S5L_FMC_PAGES_PER_BANK;
    uint32_t sel = selected_banks(f) & f->bank_mask;
    f->op_bank = 0u;
    while (sel && !(sel & 1u)) { sel >>= 1; f->op_bank++; }
}

static void nand_command(s5l_fmc_t *f, uint32_t cmd) {
    f->commands++;
    f->last_cmd = cmd & 0xffu;
    f->rpos = S5L_FMC_PAGE_BYTES;   /* a new command drops any unread page */
    if (fmc_trace == 2 && f->last_cmd == 0x60u && !fmc_escalated)
        fmc_escalated = 1;
    /* Commands that leave the array busy report completion through the
     * selected bank's ready bit; the driver sleeps until the FMC interrupt
     * filter (c041fd4c) sees it. Transfers are instantaneous here. */
    if (f->last_cmd == NAND_CMD_PROG_GO || f->last_cmd == NAND_CMD_ERASE_GO) {
        nand_status_t st = NAND_ERR_NOMEM;
        if (fmc_backing(f)) {
            if (f->last_cmd == NAND_CMD_PROG_GO) {
                st = nand_program_page(f->nand, op_page(f), f->wbuf,
                                       f->wbuf + f->nand->page_size);
                f->programs++;
            } else {
                st = nand_erase_block(f->nand,
                                      op_page(f) / f->nand->pages_per_block);
                f->erases++;
            }
        }
        if (st != NAND_OK) f->prog_fails++;
        f->nand_status = st == NAND_OK ? NAND_STATUS_PASS : NAND_STATUS_FAIL;
        f->wpos = 0u;
        memset(f->wbuf, 0xff, sizeof f->wbuf);
    }
    if (f->last_cmd == NAND_CMD_RESET || f->last_cmd == NAND_CMD_READ_GO ||
        f->last_cmd == NAND_CMD_ERASE_GO || f->last_cmd == NAND_CMD_PROG_GO) {
        if (f->last_cmd == NAND_CMD_RESET) f->resets++;
        uint32_t sel = selected_banks(f) & f->bank_mask;
        if (f->last_cmd == NAND_CMD_READ_GO)
            for (uint32_t b = 0; b < S5L_FMC_BANKS; b++)
                if (sel & (1u << b)) f->bank_row[b] = f->row;
        if (f->last_cmd == NAND_CMD_RESET) {
            f->status |= sel << 4;
        } else {
            f->ready_pending |= sel << 4;
            f->ready_us = f->last_cmd == NAND_CMD_READ_GO ? FMC_T_READ_US
                                                          : FMC_T_WRITE_US;
            f->ready_armed = 1u;
        }
    }
}

/* +0x44 as the driver programs it: bit 2 is the master enable (cleared
 * around the polled READ ID data phase), bits 3+n enable bank n's ready
 * bit (reset writes (#banks mask << 3) | 4). Address-done and data-ready
 * are polled, never interrupt sources. */
static uint32_t us_ticks(uint64_t tb_hz, uint32_t us) {
    uint64_t t = tb_hz * us / 1000000u;
    return (uint32_t)(t ? t : 1u);
}

void s5l_fmc_tick(s5l_fmc_t *f, uint32_t tb, uint64_t tb_hz) {
    /* The DMA drains the FIFO in one go, often from inside the filter that
     * is about to ack the previous transfer's bit 3; the bus time of a real
     * 2112-byte transfer keeps the new bit 3 behind that ack. */
    if (f->xfer_state == 1u) {
        f->xfer_tb = tb + us_ticks(tb_hz, FMC_T_XFER_US);
        f->xfer_state = 2u;
    } else if (f->xfer_state == 2u && (int32_t)(tb - f->xfer_tb) >= 0) {
        f->status |= FMC_ST_DATA_READY;
        f->xfer_state = 0u;
    }
    if (!f->ready_pending) return;
    if (f->ready_armed) {
        f->ready_tb = tb + us_ticks(tb_hz, f->ready_us);
        f->ready_armed = 0u;
        return;
    }
    if ((int32_t)(tb - f->ready_tb) < 0) return;
    f->status |= f->ready_pending;
    f->ready_pending = 0u;
}

bool s5l_fmc_irq(const s5l_fmc_t *f) {
    uint32_t en = f->regs[word(FMC_INT_EN)];
    if (!(en & 4u)) return false;
    uint32_t banks = (f->status >> 1) & en & 0x7f8u;   /* status 4+n <- en 3+n */
    return (banks | (f->status & en & FMC_ST_DATA_READY)) != 0u;
}

static void start(s5l_fmc_t *f, uint32_t val) {
    if (val & FMC_CC_RESET) {
        uint32_t chip = f->chip_id, mask = f->bank_mask;
        uint8_t wbuf[S5L_FMC_PAGE_BYTES];
        uint32_t wpos = f->wpos;
        memcpy(wbuf, f->wbuf, sizeof wbuf);
        uint64_t r = f->reads, w = f->writes, c = f->commands,
                 rs = f->resets, id = f->id_reads;
        s5l_fmc_reset(f);
        f->chip_id = chip; f->bank_mask = mask;
        f->reads = r; f->writes = w; f->commands = c;
        f->resets = rs; f->id_reads = id;
        /* DMA may fill the write FIFO before the command sequence resets
         * the controller state machine. */
        memcpy(f->wbuf, wbuf, sizeof wbuf); f->wpos = wpos;
        return;
    }
    if (val & FMC_CC_ADDR) { latch_address(f); f->status |= FMC_ST_ADDR_DONE; }
    if ((val & FMC_CC_READ) && f->last_cmd == NAND_CMD_READ_ID) {
        uint32_t bank = selected_banks(f);
        f->fifo = (bank & f->bank_mask) && !(bank & (bank - 1u)) ? f->chip_id
                                                                  : 0u;
        f->id_reads++;
        f->status |= FMC_ST_DATA_READY;
    } else if ((val & FMC_CC_READ) && f->last_cmd == 0x70u) {
        /* READ STATUS: ready, not write-protected, pass. */
        f->fifo = f->nand_status * 0x01010101u;
        f->status |= FMC_ST_DATA_READY;
    } else if ((val & FMC_CC_READ) && f->last_cmd == NAND_CMD_READ_GO) {
        uint32_t sel = selected_banks(f) & f->bank_mask, bank = 0u;
        if (sel) { while (!(sel & 1u)) { sel >>= 1; bank++; } }
        else bank = f->op_bank;
        uint32_t page = bank * S5L_FMC_PAGES_PER_BANK + f->bank_row[bank];
        if (!f->nand || nand_read_page(f->nand, page, f->rbuf,
                                       f->rbuf + f->nand->page_size) != NAND_OK)
            memset(f->rbuf, 0xff, sizeof f->rbuf);
        f->rpos = f->col < S5L_FMC_PAGE_BYTES ? f->col & ~3u : 0u;
        /* Transfer-done (status bit 3) is raised once DATA_COUNT bytes have
         * left the FIFO, not when the transfer starts: the read pipeline in
         * the FMC interrupt filter acks the previous transfer's bit 3 right
         * after starting the next one. */
        uint32_t len = f->regs[word(FMC_DATA_COUNT)] + 1u;
        if (!f->regs[word(FMC_DATA_COUNT)] || len > S5L_FMC_PAGE_BYTES - f->rpos)
            len = S5L_FMC_PAGE_BYTES - f->rpos;
        f->rend = f->rpos + ((len + 3u) & ~3u);
        if (f->rend > S5L_FMC_PAGE_BYTES) f->rend = S5L_FMC_PAGE_BYTES;
    } else if ((val & FMC_CC_WRITE) && f->last_cmd == NAND_CMD_PROG) {
        f->status |= FMC_ST_DATA_READY;
    }
}

void s5l_fmc_write(s5l_fmc_t *f, uint32_t off, uint32_t val) {
    f->writes++;
    if (tracing()) fprintf(stderr, "fmc w %03x = %08x\n", off, val);
    if (off >= S5L_FMC_SIZE) return;
    if (off == FMC_STATUS) { f->status &= ~val; return; }
    if (off == FMC_FIFO) {
        if (f->wpos + 4u <= S5L_FMC_PAGE_BYTES) {
            uint8_t *b = &f->wbuf[f->wpos];
            b[0] = (uint8_t)val; b[1] = (uint8_t)(val >> 8);
            b[2] = (uint8_t)(val >> 16); b[3] = (uint8_t)(val >> 24);
            f->wpos += 4u;
        }
        return;
    }
    f->regs[word(off)] = val;
    if (off == FMC_NAND_CMD && tracing_cmds())
        fprintf(stderr, "fmc cmd %02x ctrl %08x addr %08x %08x\n", val,
                f->regs[word(FMC_CTRL)], f->regs[word(FMC_ADDR)],
                f->regs[word(FMC_ADDR) + 1u]);
    if (off == FMC_NAND_CMD) nand_command(f, val);
    else if (off == FMC_CMD_CTRL) start(f, val);
}

/*
 * RSECC. The genuine engine is a Reed-Solomon coder; nothing here needs the
 * real code because every page this machine stores was written through this
 * model. A CRC-32 per 512-byte sector, four bytes per sector at the ECC
 * pointer, is self-consistent and detects what matters: an erased page (ECC
 * bytes 0xff) fails the check, which AppleS5L8900XFMC::_fmcCompleteRead
 * reclassifies as blank once it sees an all-0xff spare.
 */
void s5l_rsecc_reset(s5l_rsecc_t *e) {
    if (e) memset(e, 0, sizeof *e);
}

uint32_t s5l_rsecc_read(s5l_rsecc_t *e, uint32_t off) {
    if (off == RSECC_STATUS) return e->status;
    return off < S5L_RSECC_SIZE ? e->regs[(off >> 2) % (S5L_RSECC_SIZE / 4u)]
                                : 0u;
}

void s5l_rsecc_write(s5l_rsecc_t *e, uint32_t off, uint32_t val) {
    if (tracing()) fprintf(stderr, "rsecc w %03x = %08x\n", off, val);
    if (off >= S5L_RSECC_SIZE) return;
    if (off == RSECC_CLEARINT) { e->irq = 0u; return; }
    e->regs[off >> 2] = val;
    if (off == RSECC_START &&
        (val == RSECC_START_CHECK || val == RSECC_START_GEN))
        e->pending = val;
}

/* sector_crc() of a sector of 0xff bytes. */
#define RSECC_ERASED_CRC 0xbd7bc39fu

static uint32_t sector_crc(const arm_bus_t *bus, uint32_t pa) {
    uint32_t crc = ~0u;
    for (uint32_t i = 0; i < RSECC_SECTOR; i += 4u) {
        uint32_t w = bus->read32(bus->ctx, pa + i);
        for (unsigned k = 0; k < 4u; k++) {
            crc ^= (w >> (8u * k)) & 0xffu;
            for (unsigned b = 0; b < 8u; b++)
                crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

bool s5l_rsecc_run(s5l_rsecc_t *e, const arm_bus_t *bus) {
    if (e->pending) {
        uint32_t data = e->regs[RSECC_DATA >> 2], ecc = e->regs[RSECC_ECC >> 2];
        uint32_t n = (e->regs[RSECC_SETUP >> 2] & 3u) + 1u;
        e->status = 0u;
        for (uint32_t i = 0; i < n; i++) {
            uint32_t crc = sector_crc(bus, data + i * RSECC_SECTOR);
            if (e->pending == RSECC_START_GEN) {
                bus->write32(bus->ctx, ecc + 4u * i, crc);
            } else {
                uint32_t want = bus->read32(bus->ctx, ecc + 4u * i);
                /* Erased media is a valid codeword: an all-0xff sector
                 * with all-0xff parity reads back clean. Calling it
                 * uncorrectable made VFL_ReadScatteredPagesInVb mark every
                 * unwritten page it touched for refresh. */
                bool erased = want == 0xffffffffu && crc == RSECC_ERASED_CRC;
                if (!erased && want != crc)
                    e->status |= RSECC_ST_UNCORRECTABLE;
            }
        }
        if (e->pending == RSECC_START_GEN) e->gens++;
        else { e->checks++; if (e->status) e->failures++; }
        if (tracing())
            fprintf(stderr, "rsecc %s n=%u data=%08x ecc=%08x st=%u\n",
                    e->pending == RSECC_START_GEN ? "gen" : "check", n, data,
                    ecc, e->status);
        e->pending = 0u;
        e->irq = 1u;
    }
    return e->irq != 0u;
}
