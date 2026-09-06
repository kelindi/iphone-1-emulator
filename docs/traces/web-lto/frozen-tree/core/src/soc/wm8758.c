/* M68 WM8758 control endpoint. Observed in genuine 1A543a:
 * AppleWM8758Audio vtable c045d344, write c0459a8c..c0459b4c;
 * packed byte0=(register<<1)|value[8], byte1=value[7:0]. The driver's
 * RMW helper c0459b58 uses its own shadow; no codec read transaction was
 * observed. Unsupported reads/forms are refused, not given invented data.
 * This is register/control bringup only: no PCM or analog behavior. */
#include "soc.h"
#include <string.h>

/* Driver's reset-shadow table at c045d8cc. ffff denotes a reserved entry.
 * Unwritten storage remains zero for legacy v39 M68 saves, whose codec
 * storage was unused. Diagnostic peek derives these reset values lazily. */
static const uint16_t reset_values[WM8758_NREG] = {
    0,      0,      0,    0,    0x50,   0,      0x140,  0,      0,      0,     0,      0xff, 0xff,
    0,      0x100,  0xff, 0xff, 0xffff, 0x12c,  0x2c,   0x2c,   0x2c,   0x2c,  0xffff, 0x32, 0,
    0xffff, 0,      0,    0,    0,      0xffff, 0x38,   0xb,    0x32,   0,     8,      0xc,  0x93,
    0xe9,   0xffff, 0,    0,    0,      3,      0x10,   0x10,   0x100,  0x100, 2,      1,    1,
    0x39,   0x39,   0x39, 0x39, 1,      1,      0xffff, 0xffff, 0xffff, 0};

bool s5l_wm8758_register_valid(unsigned reg) {
    return reg < WM8758_NREG && reset_values[reg] != 0xffffu;
}

uint16_t s5l_wm8758_peek(const s5l_wm8758_t *codec, unsigned reg) {
    if (!codec || !s5l_wm8758_register_valid(reg))
        return 0u;
    return codec->written[reg] ? codec->regs[reg] : reset_values[reg];
}

void s5l_wm8758_reset(s5l_wm8758_t *codec) {
    if (codec)
        memset(codec, 0, sizeof *codec);
}

static bool start(void *ctx, bool read) {
    s5l_wm8758_t *codec = ctx;
    if (!codec)
        return false;
    codec->reading = read;
    codec->wlen = 0u;
    if (read)
        codec->unknown_reads++;
    return !read;
}

static bool write_byte(void *ctx, uint8_t byte) {
    s5l_wm8758_t *codec = ctx;
    if (!codec || codec->reading)
        return false;
    if (codec->wlen >= 2u) {
        /* Three is an overflow marker that still fits the existing v39
         * transaction storage/invariant. Never partially commit a long write. */
        codec->wlen = 3u;
        return false;
    }
    codec->wbuf[codec->wlen++] = byte;
    return true;
}

static void stop(void *ctx) {
    s5l_wm8758_t *codec = ctx;
    if (!codec || codec->reading)
        return;
    if (!codec->wlen)
        return;
    if (codec->wlen != 2u) {
        codec->refused_writes++;
    } else {
        unsigned reg = codec->wbuf[0] >> 1;
        uint16_t value = (uint16_t)(((codec->wbuf[0] & 1u) << 8) | codec->wbuf[1]);
        if (!s5l_wm8758_register_valid(reg) || (reg == 0u && value != 0u)) {
            codec->refused_writes++;
        } else {
            codec->ptr = (uint8_t)reg;
            codec->packed_writes++;
            codec->reg_writes++;
            if (!reg) {
                /* The observed software reset is register0=0. Clear the
                 * register state without erasing host diagnostic counters. */
                memset(codec->regs, 0, sizeof codec->regs);
                memset(codec->written, 0, sizeof codec->written);
            } else {
                codec->regs[reg] = value;
                codec->written[reg] = 1u;
            }
        }
    }
    codec->wlen = 0u;
}

void s5l_wm8758_bind(s5l_wm8758_t *codec, s5l_i2c_slave_t *slave) {
    if (!slave)
        return;
    memset(slave, 0, sizeof *slave);
    slave->addr = WM8758_I2C_ADDR;
    slave->ctx = codec;
    slave->start = start;
    slave->write = write_byte;
    slave->stop = stop;
}
