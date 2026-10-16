/* M68 rear camera: Micron MT9D112 ("AppleMicron2020") SoC sensor on i2c1,
 * 7-bit address 0x3C. Observed in genuine 1A543a: register accessors
 * c03b9564 (2-byte big-endian address, 2-byte big-endian read) and c03b95fc
 * (address + 16-bit value); the probe at c03bd7c8 reads the model ID at
 * 0x3000; init c03bd570 polls 0x3204 bits 0/1 after writing 0x3202 = 9 and
 * 0xC; the MCU variables are reached through 0x338C (address) / 0x3390
 * (data). Only the control plane lives here. Pixel data reaches the SoC
 * through /arm-io/camin, not through this bus. */
#include "soc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int cam_trace = -1;
static bool tracing(void) {
    if (cam_trace < 0) cam_trace = getenv("S5LBOX_CAMSENSOR_TRACE") != NULL;
    return cam_trace != 0;
}

#define REG_MODEL_ID   0x3000u
#define REG_STANDBY    0x3202u
#define REG_STATUS     0x3204u
#define REG_MCU_ADDR   0x338cu
#define REG_MCU_DATA   0x3390u
/* Sequencer driver (id 1) variables, 8-bit logical access cleared of bit 15. */
#define VAR_SEQ_CMD    0x2103u
#define VAR_SEQ_STATE  0x2104u

void s5l_mt9d112_reset(s5l_mt9d112_t *s) {
    if (s) memset(s, 0, sizeof *s);
}

static uint16_t *slot(uint16_t *keys, uint16_t *vals, uint32_t *count,
                      unsigned cap, uint16_t key, bool create) {
    for (uint32_t i = 0; i < *count; i++)
        if (keys[i] == key) return &vals[i];
    if (!create || *count >= cap) return NULL;
    keys[*count] = key;
    vals[*count] = 0u;
    return &vals[(*count)++];
}

static uint16_t var_key(uint16_t addr) { return (uint16_t)(addr & 0x7fffu); }

static uint16_t var_get(const s5l_mt9d112_t *s, uint16_t addr) {
    uint16_t key = var_key(addr);
    for (uint32_t i = 0; i < s->var_count; i++)
        if (s->var_addr[i] == key) return s->var_val[i];
    return 0u;
}

static void var_set(s5l_mt9d112_t *s, uint16_t addr, uint16_t value) {
    uint16_t key = var_key(addr);
    if (key == VAR_SEQ_CMD) {
        /* The sequencer acts on a command immediately and acknowledges it by
         * clearing the command byte: 1 = preview (state 3), 2 = capture
         * (state 7); 5/6 are refresh requests that leave the state alone. */
        uint16_t *st = slot(s->var_addr, s->var_val, &s->var_count,
                            MT9D112_VARS, VAR_SEQ_STATE, true);
        if (st && value == 1u) *st = 3u;
        else if (st && value == 2u) *st = 7u;
        s->seq_commands++;
        value = 0u;
    }
    uint16_t *v = slot(s->var_addr, s->var_val, &s->var_count,
                       MT9D112_VARS, key, true);
    if (v) *v = value;
    else s->refused++;
}

static uint16_t reg_get(s5l_mt9d112_t *s, uint16_t reg) {
    switch (reg) {
    case REG_MODEL_ID: return MT9D112_MODEL_ID;
    case REG_STATUS: {
        /* Standby handshake: bit0 follows the standby request, bit1 the
         * stop-MCU request in REG_STANDBY. */
        uint16_t *ctl = slot(s->reg_addr, s->reg_val, &s->reg_count,
                             MT9D112_REGS, REG_STANDBY, false);
        uint16_t c = ctl ? *ctl : 0u;
        return (uint16_t)(((c & 1u) ? 1u : 0u) | ((c & 4u) ? 2u : 0u));
    }
    case REG_MCU_DATA: return var_get(s, s->mcu_addr);
    case REG_MCU_ADDR: return s->mcu_addr;
    default: {
        uint16_t *v = slot(s->reg_addr, s->reg_val, &s->reg_count,
                           MT9D112_REGS, reg, false);
        if (!v) s->unknown_reads++;
        return v ? *v : 0u;
    }
    }
}

static void reg_set(s5l_mt9d112_t *s, uint16_t reg, uint16_t value) {
    s->reg_writes++;
    if (tracing()) fprintf(stderr, "cam i2c w %04x = %04x (mcu %04x)\n", reg, value, s->mcu_addr);
    if (reg == REG_MCU_ADDR) { s->mcu_addr = value; return; }
    if (reg == REG_MCU_DATA) { var_set(s, s->mcu_addr, value); return; }
    uint16_t *v = slot(s->reg_addr, s->reg_val, &s->reg_count,
                       MT9D112_REGS, reg, true);
    if (v) *v = value;
    else s->refused++;
}

static bool start(void *ctx, bool read) {
    s5l_mt9d112_t *s = ctx;
    if (!s) return false;
    if (read && s->wlen == 2u) {
        s->ptr = (uint16_t)(s->wbuf[0] << 8 | s->wbuf[1]);
        s->rpos = 0u;
    } else if (read) {
        s->rpos = 0u;
    }
    s->reading = read;
    s->wlen = 0u;
    return true;
}

static bool write_byte(void *ctx, uint8_t byte) {
    s5l_mt9d112_t *s = ctx;
    if (!s || s->reading) return false;
    if (s->wlen >= sizeof s->wbuf) {
        s->refused++;
        return false;
    }
    s->wbuf[s->wlen++] = byte;
    return true;
}

static uint8_t read_byte(void *ctx) {
    s5l_mt9d112_t *s = ctx;
    if (!s) return 0xffu;
    uint16_t v = reg_get(s, s->ptr);
    uint8_t out = s->rpos ? (uint8_t)v : (uint8_t)(v >> 8);
    if (s->rpos && tracing())
        fprintf(stderr, "cam i2c r %04x = %04x (mcu %04x)\n", s->ptr, v, s->mcu_addr);
    if (s->rpos) {
        s->reg_reads++;
        if (s->ptr != REG_MCU_DATA) s->ptr = (uint16_t)(s->ptr + 2u);
    }
    s->rpos ^= 1u;
    return out;
}

static void stop(void *ctx) {
    s5l_mt9d112_t *s = ctx;
    if (!s) return;
    if (!s->reading) {
        if (s->wlen == 2u) {
            s->ptr = (uint16_t)(s->wbuf[0] << 8 | s->wbuf[1]);
            s->rpos = 0u;
            /* Keep the address for a following read transaction. */
            return;
        }
        if (s->wlen == 4u)
            reg_set(s, (uint16_t)(s->wbuf[0] << 8 | s->wbuf[1]),
                    (uint16_t)(s->wbuf[2] << 8 | s->wbuf[3]));
        else if (s->wlen)
            s->refused++;
    }
    s->wlen = 0u;
    s->reading = false;
}

void s5l_mt9d112_bind(s5l_mt9d112_t *s, s5l_i2c_slave_t *slave) {
    if (!slave) return;
    memset(slave, 0, sizeof *slave);
    slave->addr = MT9D112_I2C_ADDR;
    slave->ctx = s;
    slave->start = start;
    slave->write = write_byte;
    slave->read = read_byte;
    slave->stop = stop;
}

uint16_t s5l_mt9d112_var(const s5l_mt9d112_t *s, uint16_t addr) {
    return s ? var_get(s, addr) : 0u;
}
