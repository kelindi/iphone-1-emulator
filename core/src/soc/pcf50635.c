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
    for (unsigned i = 0; i < 7; i++) pmu->written[PCF50635_RTCSC + i] = 0;
}

/* Isolated bounded date-write model: accepts the original driver's complete,
 * valid seven-register update. Partial/invalid updates are explicitly counted
 * as unsupported at the next pulse; no partial-calendar behavior is claimed.
 * The existing regs/written arrays hold pending bytes/mask and already travel
 * in snapshots. Reads continue to expose active seconds until that pulse. */
static unsigned pending_rtc(const s5l_pcf50635_t *p) {
    unsigned mask = 0;
    for (unsigned i = 0; i < 7; i++)
        if (p->written[PCF50635_RTCSC + i]) mask |= 1u << i;
    return mask;
}
static int unbcd(uint8_t b, unsigned max) {
    unsigned n = (b >> 4) * 10u + (b & 15u);
    return (b & 15u) <= 9u && (b >> 4) <= 9u && n <= max ? (int)n : -1;
}
static bool pending_date(const s5l_pcf50635_t *p, uint64_t *seconds) {
    const uint8_t *r = p->regs + PCF50635_RTCSC;
    int sec = unbcd(r[0], 59), min = unbcd(r[1], 59);
    int hour = unbcd(r[2], 23), day = unbcd(r[4], 31);
    int month = unbcd(r[5], 12), year = unbcd(r[6], 99);
    if (sec < 0 || min < 0 || hour < 0 || day < 1 ||
        month < 1 || year < 0 || r[3] > 6) return false;
    static const unsigned lengths[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    unsigned limit = lengths[month - 1] + (month == 2 && year % 4 == 0);
    if ((unsigned)day > limit) return false;
    uint64_t days = 0;
    for (int y = 0; y < year; y++) days += 365u + (y % 4 == 0);
    for (int m = 1; m < month; m++) days += lengths[m - 1] + (m == 2 && year % 4 == 0);
    days += day - 1;
    uint64_t result = PCF50635_MIN_TIME + days * 86400u + hour * 3600u + min * 60u + sec;
    /* The original driver always supplies a calendar-consistent weekday.
     * Independent weekday programming is outside this bounded operation. */
    if ((result / 86400u + 4u) % 7u != r[3]) return false;
    *seconds = result;
    return true;
}

void s5l_pcf50635_tick(s5l_pcf50635_t *pmu, uint32_t ticks) {
    if (!pmu || !pmu->tick_hz) return;
    /* The previous implementation used a 32-bit `+= ticks`; UINT32_MAX plus
     * an existing remainder wrapped and moved the RTC backwards relative to
     * guest time. The 64-bit lumped form is exact and bounded. */
    if (pmu->seconds < PCF50635_MIN_TIME) pmu->seconds = PCF50635_MIN_TIME;
    uint64_t total = (pmu->tick_accum % pmu->tick_hz) + (uint64_t)ticks;
    uint64_t advance = total / pmu->tick_hz;
    bool second_edge = advance != 0;
    if (second_edge) {
        unsigned mask = pending_rtc(pmu);
        if (mask) {
            uint64_t updated;
            if (mask == 0x7fu && pending_date(pmu, &updated)) {
                pmu->seconds = updated;
                advance--; /* New bytes become active at this first pulse. */
            } else {
                for (unsigned i = 0; i < 7; i++)
                    if (mask & (1u << i)) pmu->unknown_writes++;
            }
            for (unsigned i = 0; i < 7; i++)
                pmu->written[PCF50635_RTCSC + i] = 0;
        }
    }
    if (pmu->seconds >= PCF50635_MAX_TIME ||
        advance > PCF50635_MAX_TIME - pmu->seconds) {
        pmu->seconds = PCF50635_MAX_TIME;
        pmu->tick_accum = 0;
    } else {
        pmu->seconds += advance;
        pmu->tick_accum = total % pmu->tick_hz;
    }
    /* Each elapsed oscillator edge latches one coalesced SECOND event, even
     * when a newly applied date equals the previous calendar second. */
    if (second_edge) pmu->regs[PCF50635_INT1] |= 0x80u;
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
    /* RTC bytes are staged in the existing snapshot-backed register bank.
     * The next second pulse validates and applies the bounded full update.
     * Reads remain the active clock; unsupported partial/invalid updates are
     * counted at that pulse and cleared rather than silently normalized. */
    if (is_rtc(reg)) {
        pmu->regs[reg] = val;
        pmu->written[reg] = 1u;
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
