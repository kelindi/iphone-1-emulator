/*
 * S5LBox — Wolfson WM8991 audio codec, an I2C slave at address 0x1B on i2c0.
 *
 * The register model stays deliberately bounded, exactly as the PMU's does.
 * Register 0 is the identity the driver gates on and is read-only. Register 1
 * bit 5 is the part discriminator and is unimplemented on purpose. Everything
 * else is storage: bytes the guest wrote read back, and a register nobody has
 * written returns zero while being recorded, so a boot tells the next reader
 * which registers the driver actually wanted.
 *
 * See the WM8991 block in soc.h for where every constant and both wire
 * encodings were read out of the shipped firmware.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#include "soc.h"
#include <string.h>

void s5l_wm8991_reset(s5l_wm8991_t *codec) {
    if (!codec) return;
    /* Total initialization, as on I2C: this makes reset semantics explicit and
     * is safe for a stack object holding any prior byte pattern. Every register
     * resets to zero because no reset value other than register 0's identity
     * was established from the firmware, and inventing one would be
     * indistinguishable from a measured value until the boot diverged. */
    memset(codec, 0, sizeof *codec);
}

/*
 * Bit 12 of the status register is not storage. It reads back the level
 * commanded through bit 12 of the control register, which is what lets the
 * driver's timeout-free poll at 0xc068d4ac terminate — see the WM8991 block in
 * soc.h for the loop and for why this is the narrowest reading consistent with
 * it. Every other bit of both registers is ordinary storage.
 */
static uint16_t apply_status_mirror(const s5l_wm8991_t *codec, uint16_t val) {
    val &= (uint16_t)~WM8991_GP_BIT;
    if (codec->regs[WM8991_REG_GPCTRL] & WM8991_GP_BIT) val |= WM8991_GP_BIT;
    return val;
}

uint16_t s5l_wm8991_peek(const s5l_wm8991_t *codec, uint8_t reg) {
    if (!codec || reg >= WM8991_NREG) return 0u;
    /* Register 0 answers its identity whether or not anything wrote to it.
     * This is the single hard gate in the driver's identify method: the
     * comparison at 0xc068b0b0 against the literal 0x8990 at 0xc068b124 is what
     * decides whether the codec is accepted at all. */
    if (reg == WM8991_REG_ID) return WM8991_ID_VALUE;
    if (reg == WM8991_REG_GPSTAT)
        return apply_status_mirror(codec, codec->regs[reg]);
    return codec->regs[reg];
}

static void note_unknown_read(s5l_wm8991_t *codec, uint8_t reg) {
    codec->unknown_reads++;
    for (unsigned i = 0; i < codec->unknown_reg_count; i++)
        if (codec->unknown_reg[i] == reg) return;
    if (codec->unknown_reg_count < WM8991_UNKNOWN_REGS)
        codec->unknown_reg[codec->unknown_reg_count++] = reg;
}

static uint16_t reg_read(s5l_wm8991_t *codec, uint8_t reg) {
    /* The index space is seven bits and every caller is supposed to have
     * narrowed to it already. This is the backstop, not the narrowing: keeping
     * exactly one place that masks is what makes the auto-increment's own wrap
     * observable instead of quietly redundant. */
    if (reg >= WM8991_NREG) return 0u;
    codec->reg_reads++;
    if (reg == WM8991_REG_ID) {
        codec->id_reads++;
        return WM8991_ID_VALUE;
    }
    if (!codec->written[reg]) note_unknown_read(codec, reg);
    if (reg == WM8991_REG_GPSTAT) {
        codec->status_mirror_reads++;
        return apply_status_mirror(codec, codec->regs[reg]);
    }
    return codec->regs[reg];
}

static void reg_write(s5l_wm8991_t *codec, uint8_t reg, uint16_t val) {
    if (reg >= WM8991_NREG) return;      /* the same backstop as reg_read */
    codec->reg_writes++;
    /* Register 0 is the part identity. A guest write to it must not be able to
     * change what the next identify() reads back, because a model whose ID can
     * be overwritten would pass the probe once and then fail a re-probe for
     * reasons nothing in the firmware explains. */
    if (reg == WM8991_REG_ID) return;
    if (reg == WM8991_REG_PWR1 && (val & WM8991_PWR1_PROBE))
        codec->probe_bit_writes++;
    /*
     * Register 1 bit 5 is not implemented on this part. The driver writes it,
     * reads it back and uses the result to choose between the names "WM8991"
     * and "WM1817" (getter 0xc068b044); a bit that sticks selects WM1817. The
     * shipped device tree describes both of its audio nodes as `wm8991` and
     * contains no occurrence of "1817", so on this board the bit must read back
     * clear. Discarding the write rather than storing it is what makes that
     * true for every subsequent read, including ones this model has not seen.
     */
    if (reg == WM8991_REG_PWR1) val &= (uint16_t)~WM8991_PWR1_PROBE;
    /* Bit 12 of the status register is read-only: the driver clears it on every
     * write and then waits for it to come back set, so storing what was written
     * is exactly the behaviour that makes its poll never terminate. Keeping the
     * stored bit at zero also means the snapshot invariant can assert it. */
    if (reg == WM8991_REG_GPSTAT) val &= (uint16_t)~WM8991_GP_BIT;
    codec->regs[reg] = val;
    codec->written[reg] = 1u;
}

/*
 * A write is committed at the end of the transaction rather than as its bytes
 * arrive, because the byte COUNT is what distinguishes the two shipped
 * encodings from each other and from a pointer-only write:
 *
 *   1 byte   the stock controller's read setup — set the index and STOP. It is
 *            not a register store and must not be committed as one.
 *   2 bytes  the packed Wolfson form: (reg << 1) | value[8], then value[7:0].
 *   3 bytes  the wide form: index, then the 16-bit value MSB-first.
 *
 * Anything else is refused and counted rather than interpreted. The transaction
 * ends at STOP or at a repeated START, and the I2C model calls this slave's
 * stop() on both paths, so there is exactly one commit point.
 */
static void commit_write(s5l_wm8991_t *codec) {
    switch (codec->wlen) {
        case 0u:
            break;                       /* address phase only; nothing to do */
        case 1u:
            /* Pointer-only: the read setup. Narrowed to the seven-bit index
             * space HERE rather than at the point of use, because `ptr` is
             * snapshotted and its invariant has to hold for every value the
             * guest can produce — an index byte of 0xff is a legal thing for
             * the guest to send, and storing it raw would make the machine
             * refuse to checkpoint. */
            codec->ptr = (uint8_t)(codec->wbuf[0] & 0x7fu);
            break;
        case 2u: {
            uint8_t  reg = (uint8_t)((codec->wbuf[0] >> 1) & 0x7fu);
            uint16_t val = (uint16_t)(((uint16_t)(codec->wbuf[0] & 1u) << 8) |
                                      codec->wbuf[1]);
            codec->packed_writes++;
            codec->ptr = reg;
            reg_write(codec, reg, val);
            break;
        }
        case 3u: {
            uint8_t  reg = (uint8_t)(codec->wbuf[0] & 0x7fu);
            uint16_t val = (uint16_t)(((uint16_t)codec->wbuf[1] << 8) |
                                      codec->wbuf[2]);
            codec->wide_writes++;
            codec->ptr = reg;
            reg_write(codec, reg, val);
            break;
        }
        default:
            codec->refused_writes++;
            break;
    }
    codec->wlen = 0u;
}

static bool codec_start(void *ctx, bool read) {
    s5l_wm8991_t *codec = ctx;
    if (!codec) return false;
    codec->reading = read;
    if (read) {
        /* A read always begins at the most significant byte. The driver
         * assembles the value as `orr r0, r0, r3, lsl #8` over the first and
         * second bytes off the wire (0xc0540038), so starting anywhere else
         * would byte-swap every register it reads. */
        codec->second_byte = false;
    } else {
        codec->wlen = 0u;
    }
    return true;
}

static bool codec_write(void *ctx, uint8_t byte) {
    s5l_wm8991_t *codec = ctx;
    if (!codec) return false;
    if (codec->wlen >= WM8991_MAX_WRITE) {
        /* Longer than any shipped encoding. Refuse the byte rather than
         * silently dropping it: a NAK is something the driver can report, and
         * an over-long transfer is a sign this model has met a form it was
         * never shown. Count it once, at commit, via the refusal path. */
        codec->wlen = WM8991_MAX_WRITE + 1u;
        return false;
    }
    codec->wbuf[codec->wlen++] = byte;
    return true;
}

static uint8_t codec_read(void *ctx) {
    s5l_wm8991_t *codec = ctx;
    if (!codec) return 0u;
    if (!codec->second_byte) {
        /* Sample the register ONCE, at the first byte, and hold it. A real part
         * latches the value it is about to shift out; sampling twice would also
         * make every counter here read double, and would let a register whose
         * value is derived rather than stored — 0x12's mirrored bit — change
         * between the two halves of a single read. */
        codec->latch = reg_read(codec, codec->ptr);
        codec->second_byte = true;
        return (uint8_t)(codec->latch >> 8);
    }
    codec->second_byte = false;
    /* Auto-increment past the register just delivered, wrapping inside the
     * seven-bit index space the packed encoding can address. The driver reads
     * one register per transaction, so this only matters for a longer read;
     * wrapping keeps it inside the array either way. */
    codec->ptr = (uint8_t)((codec->ptr + 1u) & 0x7fu);
    return (uint8_t)(codec->latch & 0xffu);
}

static void codec_stop(void *ctx) {
    s5l_wm8991_t *codec = ctx;
    if (!codec) return;
    if (!codec->reading) commit_write(codec);
    /* The register pointer intentionally survives STOP, as the PMU's does: the
     * stock controller sets it in one transaction and reads in the next. */
}

void s5l_wm8991_bind(s5l_wm8991_t *codec, s5l_i2c_slave_t *slave) {
    if (!slave) return;
    memset(slave, 0, sizeof *slave);
    slave->addr = WM8991_I2C_ADDR;
    slave->ctx = codec;
    slave->start = codec_start;
    slave->write = codec_write;
    slave->read = codec_read;
    slave->stop = codec_stop;
}
