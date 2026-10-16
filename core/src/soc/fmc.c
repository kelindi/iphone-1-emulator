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
static bool tracing(void) {
    if (fmc_trace < 0) fmc_trace = getenv("S5LBOX_FMC_TRACE") != NULL;
    return fmc_trace != 0;
}

#define NAND_CMD_READ_ID 0x90u
#define NAND_CMD_RESET   0xffu
#define NAND_CMD_READ_GO 0x30u

void s5l_fmc_reset(s5l_fmc_t *f) {
    if (!f) return;
    memset(f, 0, sizeof *f);
    f->chip_id = FMC_DEFAULT_CHIP_ID;
    f->bank_mask = 0x0fu;
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
    if (off == FMC_FIFO) return f->fifo;
    if (off == FMC_CMD_CTRL) return f->regs[word(off)] & ~FMC_CC_RESET;
    return off < S5L_FMC_SIZE ? f->regs[word(off)] : 0u;
}

static void nand_command(s5l_fmc_t *f, uint32_t cmd) {
    f->commands++;
    f->last_cmd = cmd & 0xffu;
    /* Commands that leave the array busy report completion through the
     * selected bank's ready bit; the driver sleeps until the FMC interrupt
     * filter (c041fd4c) sees it. Transfers are instantaneous here. */
    if (f->last_cmd == NAND_CMD_RESET || f->last_cmd == NAND_CMD_READ_GO) {
        if (f->last_cmd == NAND_CMD_RESET) f->resets++;
        f->status |= (selected_banks(f) & f->bank_mask) << 4;
    }
}

/* +0x44 as the driver programs it: bit 2 is the master enable (cleared
 * around the polled READ ID data phase), bits 3+n enable bank n's ready
 * bit (reset writes (#banks mask << 3) | 4). Address-done and data-ready
 * are polled, never interrupt sources. */
bool s5l_fmc_irq(const s5l_fmc_t *f) {
    uint32_t en = f->regs[word(FMC_INT_EN)];
    if (!(en & 4u)) return false;
    return (f->status & en & ~FMC_ST_ADDR_DONE) != 0u;
}

static void start(s5l_fmc_t *f, uint32_t val) {
    if (val & FMC_CC_RESET) {
        uint32_t chip = f->chip_id, mask = f->bank_mask;
        uint64_t r = f->reads, w = f->writes, c = f->commands,
                 rs = f->resets, id = f->id_reads;
        s5l_fmc_reset(f);
        f->chip_id = chip; f->bank_mask = mask;
        f->reads = r; f->writes = w; f->commands = c;
        f->resets = rs; f->id_reads = id;
        return;
    }
    if (val & FMC_CC_ADDR) f->status |= FMC_ST_ADDR_DONE;
    if ((val & FMC_CC_READ) && f->last_cmd == NAND_CMD_READ_ID) {
        uint32_t bank = selected_banks(f);
        f->fifo = (bank & f->bank_mask) && !(bank & (bank - 1u)) ? f->chip_id
                                                                  : 0u;
        f->id_reads++;
        f->status |= FMC_ST_DATA_READY;
    } else if ((val & FMC_CC_READ) && f->last_cmd == NAND_CMD_READ_GO) {
        /* Erased media: every data and spare byte reads 0xff. */
        f->fifo = 0xffffffffu;
        f->status |= FMC_ST_DATA_READY;
    }
}

void s5l_fmc_write(s5l_fmc_t *f, uint32_t off, uint32_t val) {
    f->writes++;
    if (tracing()) fprintf(stderr, "fmc w %03x = %08x\n", off, val);
    if (off >= S5L_FMC_SIZE) return;
    if (off == FMC_STATUS) { f->status &= ~val; return; }
    if (off == FMC_FIFO) return;
    f->regs[word(off)] = val;
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
                if (want == 0xffffffffu || want != crc)
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
