/*
 * S5LBox — NXP PCF50635 PMU/RTC at i2c0 address 0x73.
 *
 * The register model stays deliberately bounded. Bytes written by the guest
 * are persistent storage, the RTC bytes are computed from deterministic guest
 * time, and an unwritten/unknown register returns zero while being recorded.
 */
#include "soc.h"
#include <string.h>

/* Howard Hinnant's days-to-civil algorithm, shifted to a Unix epoch. Keeping
 * this in portable C avoids host timezone, locale and libc-time dependencies. */
void s5l_pcf50635_civil(uint64_t unix_seconds, int *year, int *month, int *day,
                        int *hour, int *minute, int *second, int *weekday) {
    /* The chip exposes a two-digit year and the stock driver unconditionally
     * adds 2000. Clamp the public helper too, so even a hostile direct caller
     * cannot drive the later uint64-to-int year conversion out of range. */
    if (unix_seconds < PCF50635_MIN_TIME) unix_seconds = PCF50635_MIN_TIME;
    if (unix_seconds > PCF50635_MAX_TIME) unix_seconds = PCF50635_MAX_TIME;
    uint64_t days = unix_seconds / 86400ull;
    uint32_t sod = (uint32_t)(unix_seconds % 86400ull);
    if (weekday) *weekday = (int)((days + 4ull) % 7ull);

    uint64_t z = days + 719468ull;
    uint64_t era = z / 146097ull;
    uint32_t doe = (uint32_t)(z - era * 146097ull);
    uint32_t yoe = (doe - doe / 1460u + doe / 36524u -
                    doe / 146096u) / 365u;
    uint64_t yy = (uint64_t)yoe + era * 400ull;
    uint32_t doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    uint32_t mp = (5u * doy + 2u) / 153u;
    uint32_t dd = doy - (153u * mp + 2u) / 5u + 1u;
    uint32_t mm = mp < 10u ? mp + 3u : mp - 9u;
    if (mm <= 2u) yy++;

    if (year)   *year = (int)yy;
    if (month)  *month = (int)mm;
    if (day)    *day = (int)dd;
    if (hour)   *hour = (int)(sod / 3600u);
    if (minute) *minute = (int)((sod / 60u) % 60u);
    if (second) *second = (int)(sod % 60u);
}

static uint8_t bcd(int val) {
    if (val < 0) val = 0;
    return (uint8_t)(((val / 10) % 10) * 16 + (val % 10));
}

void s5l_pcf50635_reset(s5l_pcf50635_t *pmu, uint32_t tick_hz) {
    if (!pmu) return;
    memset(pmu, 0, sizeof *pmu);
    pmu->tick_hz = tick_hz;
    pmu->seconds = PCF50635_DEFAULT_TIME;
}

void s5l_pcf50635_set_time(s5l_pcf50635_t *pmu,
                           uint64_t unix_seconds) {
    if (!pmu) return;
    if (unix_seconds < PCF50635_MIN_TIME) unix_seconds = PCF50635_MIN_TIME;
    if (unix_seconds > PCF50635_MAX_TIME) unix_seconds = PCF50635_MAX_TIME;
    pmu->seconds = unix_seconds;
    pmu->tick_accum = 0;
}

void s5l_pcf50635_tick(s5l_pcf50635_t *pmu, uint32_t ticks) {
    if (!pmu || !pmu->tick_hz) return;
    /* The previous implementation used a 32-bit `+= ticks`; UINT32_MAX plus
     * an existing remainder wrapped and moved the RTC backwards relative to
     * guest time. The 64-bit lumped form is exact and bounded. */
    if (pmu->seconds < PCF50635_MIN_TIME) pmu->seconds = PCF50635_MIN_TIME;
    uint64_t total = (pmu->tick_accum % pmu->tick_hz) + (uint64_t)ticks;
    uint64_t advance = total / pmu->tick_hz;
    if (pmu->seconds >= PCF50635_MAX_TIME ||
        advance > PCF50635_MAX_TIME - pmu->seconds) {
        pmu->seconds = PCF50635_MAX_TIME;
        pmu->tick_accum = 0;
    } else {
        pmu->seconds += advance;
        pmu->tick_accum = total % pmu->tick_hz;
    }
}

static bool is_rtc(uint8_t reg) {
    return reg >= PCF50635_RTCSC && reg <= PCF50635_RTCYR;
}

static bool is_event_latch(uint8_t reg) {
    return reg >= PCF50635_INT1 && reg <= PCF50635_INT5;
}

static bool is_event_mask(uint8_t reg) {
    return reg >= PCF50635_INT1MASK && reg <= PCF50635_INT5MASK;
}

static uint8_t rtc_byte(const s5l_pcf50635_t *pmu, uint8_t reg) {
    int y, mo, d, h, mi, s, wd;
    s5l_pcf50635_civil(pmu->seconds, &y, &mo, &d, &h, &mi, &s, &wd);
    switch (reg) {
        case 0x59: return bcd(s);
        case 0x5a: return bcd(mi);
        case 0x5b: return bcd(h);
        case 0x5c: return (uint8_t)wd; /* the stock driver leaves this binary */
        case 0x5d: return bcd(d);
        case 0x5e: return bcd(mo);
        case 0x5f: return bcd(y - 2000);
        default:   return 0;
    }
}

static void note_unknown_read(s5l_pcf50635_t *pmu, uint8_t reg) {
    pmu->unknown_reads++;
    for (unsigned i = 0; i < pmu->unknown_reg_count; i++)
        if (pmu->unknown_reg[i] == reg) return;
    if (pmu->unknown_reg_count < PCF50635_UNKNOWN_REGS)
        pmu->unknown_reg[pmu->unknown_reg_count++] = reg;
}

static uint8_t reg_read(s5l_pcf50635_t *pmu, uint8_t reg) {
    pmu->reg_reads++;
    if (is_rtc(reg)) return rtc_byte(pmu, reg);
    /* INT1..INT5 are hardware event latches, not persistent configuration.
     * The PCF50633-family interface clears them as the host reads them. The
     * iPhone1,2 device tree's `STAT, 0x100` wake function selects INT2 bit 0,
     * so retaining that byte forever would report every later resume as a
     * Power wake too. They are known registers even when all five are zero. */
    if (is_event_latch(reg)) {
        uint8_t val = pmu->regs[reg];
        pmu->regs[reg] = 0u;
        return val;
    }
    /* Masks and the shutdown command are known even at their reset value. */
    if (is_event_mask(reg) || reg == PCF50635_OOCSHDWN)
        return pmu->regs[reg];
    if (pmu->written[reg]) return pmu->regs[reg];
    note_unknown_read(pmu, reg);
    return 0;
}

static void reg_write(s5l_pcf50635_t *pmu, uint8_t reg, uint8_t val) {
    pmu->reg_writes++;
    /* This kernel's setCurrentDateTime path is a panic stub, so writes to its
     * computed RTC are unsupported and visible rather than silently accepted. */
    if (is_rtc(reg)) {
        pmu->unknown_writes++;
        return;
    }
    pmu->regs[reg] = val;
    pmu->written[reg] = 1u;
}

static bool pmu_start(void *ctx, bool read) {
    s5l_pcf50635_t *pmu = ctx;
    if (!pmu) return false;
    pmu->reading = read;
    if (!read) pmu->have_ptr = false;
    return true;
}

static bool pmu_write(void *ctx, uint8_t byte) {
    s5l_pcf50635_t *pmu = ctx;
    if (!pmu) return false;
    if (!pmu->have_ptr) {
        pmu->ptr = byte;
        pmu->have_ptr = true;
        return true;
    }
    reg_write(pmu, pmu->ptr, byte);
    pmu->ptr = (uint8_t)(pmu->ptr + 1u);
    return true;
}

static uint8_t pmu_read(void *ctx) {
    s5l_pcf50635_t *pmu = ctx;
    if (!pmu) return 0;
    uint8_t val = reg_read(pmu, pmu->ptr);
    pmu->ptr = (uint8_t)(pmu->ptr + 1u);
    return val;
}

static void pmu_stop(void *ctx) {
    (void)ctx; /* the register pointer intentionally survives STOP */
}

void s5l_pcf50635_bind(s5l_pcf50635_t *pmu, s5l_i2c_slave_t *slave) {
    if (!slave) return;
    memset(slave, 0, sizeof *slave);
    slave->addr = PCF50635_I2C_ADDR;
    slave->ctx = pmu;
    slave->start = pmu_start;
    slave->write = pmu_write;
    slave->read = pmu_read;
    slave->stop = pmu_stop;
}

bool s5l_pcf50635_irq(const s5l_pcf50635_t *pmu) {
    if (!pmu) return false;
    for (unsigned i = 0; i < 5u; i++) {
        uint8_t events = pmu->regs[PCF50635_INT1 + i];
        uint8_t mask = pmu->regs[PCF50635_INT1MASK + i];
        if ((events & (uint8_t)~mask) != 0u) return true;
    }
    return false;
}

bool s5l_pcf50635_in_standby(const s5l_pcf50635_t *pmu) {
    return pmu && pmu->written[PCF50635_OOCSHDWN] &&
           (pmu->regs[PCF50635_OOCSHDWN] &
            PCF50635_OOCSHDWN_GO_STANDBY) != 0u;
}

void s5l_pcf50635_wake_onkey(s5l_pcf50635_t *pmu) {
    if (!pmu) return;

    /* Hardware, not an I2C master, raises this event; do not count a guest
     * register write. AppleM68Buttons later obtains bit 8 from its PMU `STAT`
     * platform function, which is INT2 bit 0 in the five-byte event bank. */
    pmu->regs[PCF50635_INT2] |= PCF50635_INT2_ONKEYR;

    /* OOCSHDWN is a command. Once ONKEY has powered the AP back up, keeping
     * GO_STANDBY asserted would make every later Power press look like another
     * hardware wake and reset the running CPU. Preserve unrelated bits. */
    pmu->regs[PCF50635_OOCSHDWN] &=
        (uint8_t)~PCF50635_OOCSHDWN_GO_STANDBY;
}
