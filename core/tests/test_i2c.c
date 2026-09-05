/*
 * S5LBox — S5L8900 I2C and PCF50635 focused tests.
 */
#include "soc.h"
#include "snapshot.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass, g_fail;
#define CHECK(cond, ...) do { \
    if (cond) g_pass++; \
    else { \
        g_fail++; \
        printf("  FAIL %s:%d: ", __func__, __LINE__); \
        printf(__VA_ARGS__); \
        printf("\n"); \
    } \
} while (0)

static bool dummy_start(void *ctx, bool read) {
    (void)ctx; (void)read;
    return true;
}

static void ack_event(s5l_i2c_t *bus, uint32_t event) {
    uint32_t pending = s5l_i2c_read(bus, I2C_INT);
    CHECK((pending & event) != 0u, "pending=%08x missing event %08x",
          pending, event);
    s5l_i2c_write(bus, I2C_INT, event);
}

static void setup_pmu(s5l_i2c_t *bus, s5l_pcf50635_t *pmu) {
    s5l_i2c_reset(bus);
    s5l_pcf50635_reset(pmu, S5L8900_TB_HZ);
    s5l_i2c_slave_t slave;
    s5l_pcf50635_bind(pmu, &slave);
    CHECK(s5l_i2c_attach(bus, &slave), "could not attach PCF50635");
    s5l_i2c_write(bus, I2C_ENABLE, 1u);
    s5l_i2c_write(bus, I2C_INT, I2C_INT_ALL);
}

/* These are the stock Apple driver sequences: 32-bit register writes, address
 * in DS, START in STAT, and one byte moved per CON.RESUME command. */
static void drive_write(s5l_i2c_t *bus, uint8_t addr, uint8_t reg,
                        const uint8_t *data, unsigned count) {
    s5l_i2c_write(bus, I2C_STAT, I2C_STAT_MODE_MTX | I2C_STAT_ENABLE);
    s5l_i2c_write(bus, I2C_DS, (uint32_t)addr << 1);
    s5l_i2c_write(bus, I2C_STAT, I2C_STAT_MODE_MTX |
                                      I2C_STAT_ENABLE | I2C_STAT_START);
    ack_event(bus, I2C_INT_BYTE);

    s5l_i2c_write(bus, I2C_DS, reg);
    s5l_i2c_write(bus, I2C_CON, I2C_CON_RESUME);
    ack_event(bus, I2C_INT_BYTE);

    for (unsigned i = 0; i < count; i++) {
        s5l_i2c_write(bus, I2C_DS, data[i]);
        s5l_i2c_write(bus, I2C_CON, I2C_CON_ACKEN | I2C_CON_RESUME);
        ack_event(bus, I2C_INT_BYTE);
    }
    s5l_i2c_write(bus, I2C_STAT, I2C_STAT_MODE_MRX | I2C_STAT_ENABLE);
    ack_event(bus, I2C_INT_STOP);
}

static void drive_pointer(s5l_i2c_t *bus, uint8_t addr, uint8_t reg) {
    s5l_i2c_write(bus, I2C_STAT, I2C_STAT_MODE_MTX | I2C_STAT_ENABLE);
    s5l_i2c_write(bus, I2C_DS, (uint32_t)addr << 1);
    s5l_i2c_write(bus, I2C_STAT, I2C_STAT_MODE_MTX |
                                      I2C_STAT_ENABLE | I2C_STAT_START);
    ack_event(bus, I2C_INT_BYTE);
    s5l_i2c_write(bus, I2C_DS, reg);
    s5l_i2c_write(bus, I2C_CON, I2C_CON_RESUME);
    ack_event(bus, I2C_INT_BYTE);
    s5l_i2c_write(bus, I2C_STAT, I2C_STAT_MODE_MRX | I2C_STAT_ENABLE);
    ack_event(bus, I2C_INT_STOP);
}

static void drive_read(s5l_i2c_t *bus, uint8_t addr, uint8_t reg,
                       uint8_t *out, unsigned count) {
    drive_pointer(bus, addr, reg);
    s5l_i2c_write(bus, I2C_CON, I2C_CON_ACKEN);
    s5l_i2c_write(bus, I2C_DS, (uint32_t)addr << 1);
    s5l_i2c_write(bus, I2C_STAT, I2C_STAT_MODE_MRX |
                                      I2C_STAT_ENABLE | I2C_STAT_START);
    ack_event(bus, I2C_INT_BYTE);
    for (unsigned i = 0; i < count; i++) {
        uint32_t con = I2C_CON_RESUME;
        if (i + 1u < count) con |= I2C_CON_ACKEN;
        s5l_i2c_write(bus, I2C_CON, con);
        ack_event(bus, I2C_INT_BYTE);
        out[i] = (uint8_t)s5l_i2c_read(bus, I2C_DS);
    }
    s5l_i2c_write(bus, I2C_STAT, I2C_STAT_MODE_MRX | I2C_STAT_ENABLE);
    ack_event(bus, I2C_INT_STOP);
}

static void test_reset_and_attachment_are_bounded(void) {
    s5l_i2c_t bus, expected;
    memset(&bus, 0xa5, sizeof bus);
    memset(&expected, 0, sizeof expected);
    expected.sel = -1;
    s5l_i2c_reset(&bus);
    CHECK(memcmp(&bus, &expected, sizeof bus) == 0,
          "reset did not totally initialize a poisoned object");

    s5l_i2c_slave_t slave;
    memset(&slave, 0, sizeof slave);
    slave.start = dummy_start;
    CHECK(!s5l_i2c_attach(NULL, &slave), "NULL bus accepted");
    CHECK(!s5l_i2c_attach(&bus, NULL), "NULL slave accepted");
    slave.addr = 0x80u;
    CHECK(!s5l_i2c_attach(&bus, &slave), "non-seven-bit address accepted");
    slave.addr = 1u; slave.start = NULL;
    CHECK(!s5l_i2c_attach(&bus, &slave), "slave without START callback accepted");

    slave.start = dummy_start;
    CHECK(s5l_i2c_attach(&bus, &slave), "first slave refused");
    CHECK(!s5l_i2c_attach(&bus, &slave), "duplicate address shadowed");
    for (uint8_t addr = 2; addr <= S5L_I2C_SLAVES; addr++) {
        slave.addr = addr;
        CHECK(s5l_i2c_attach(&bus, &slave), "slot for address %u refused", addr);
    }
    slave.addr = 9u;
    CHECK(!s5l_i2c_attach(&bus, &slave), "full slave table overflowed");

    s5l_i2c_reset(&bus);
    CHECK(bus.slave_count == 0u && bus.sel == -1 && bus.slaves[0].start == NULL,
          "reset preserved host callback wiring");
}

static void test_controller_register_edges(void) {
    s5l_i2c_t bus;
    s5l_i2c_reset(&bus);
    CHECK(s5l_i2c_read(&bus, I2C_BUSY) == 0u, "BUSY nonzero at reset");
    s5l_i2c_write(&bus, I2C_BUSY, UINT32_MAX);
    CHECK(s5l_i2c_read(&bus, I2C_BUSY) == 0u, "BUSY accepted a guest write");

    s5l_i2c_write(&bus, I2C_ENABLE, 1u);
    s5l_i2c_write(&bus, I2C_INT, I2C_INT_ALL);
    CHECK(s5l_i2c_read(&bus, I2C_ENABLE) == 1u, "ENABLE did not read back");
    CHECK(!s5l_i2c_irq(&bus), "clear-all left the level asserted");

    s5l_i2c_write(&bus, I2C_DS, 0x1234u);
    CHECK(s5l_i2c_read(&bus, I2C_DS) == 0x34u, "DS was not byte-bounded");

    (void)s5l_i2c_read(&bus, 0x18u);
    (void)s5l_i2c_read(&bus, 0x18u);
    s5l_i2c_write(&bus, 0x24u, 1u);
    CHECK(bus.unknown_reads == 2u && bus.unknown_writes == 1u,
          "unknown access counters r=%llu w=%llu",
          (unsigned long long)bus.unknown_reads,
          (unsigned long long)bus.unknown_writes);
    CHECK(bus.unknown_off_count == 2u,
          "distinct unknown offsets=%u expect 2", bus.unknown_off_count);

    /* A present slave with no receive callback must NAK the data phase and
     * account for that NAK rather than silently returning a fabricated byte. */
    s5l_i2c_reset(&bus);
    s5l_i2c_slave_t write_only;
    memset(&write_only, 0, sizeof write_only);
    write_only.addr = 0x20u;
    write_only.start = dummy_start;
    CHECK(s5l_i2c_attach(&bus, &write_only), "write-only slave attach failed");
    s5l_i2c_write(&bus, I2C_STAT, 0x90u);
    s5l_i2c_write(&bus, I2C_DS, write_only.addr << 1);
    s5l_i2c_write(&bus, I2C_STAT, 0xb0u);
    s5l_i2c_write(&bus, I2C_INT, I2C_INT_BYTE);
    s5l_i2c_write(&bus, I2C_CON, I2C_CON_RESUME);
    CHECK(bus.nak && bus.naks == 1u,
          "missing RX callback did not produce one accounted NAK");
}

static void test_unknown_slave_naks_and_w1c_is_selective(void) {
    s5l_i2c_t bus;
    s5l_i2c_reset(&bus);
    s5l_i2c_write(&bus, I2C_ENABLE, 1u);
    s5l_i2c_write(&bus, I2C_STAT, 0xd0u);
    /* `/arm-io/i2c0/tethered` is the exact unknown device SpringBoard asks
     * through SBTetherController: seven-bit 0x29, hence DS=0x52. */
    s5l_i2c_write(&bus, I2C_DS, 0x29u << 1);
    s5l_i2c_write(&bus, I2C_STAT, 0xf0u);
    CHECK((s5l_i2c_read(&bus, I2C_STAT) & I2C_STAT_NAK) != 0u,
          "unanswered address did not set STAT.NAK");
    CHECK(s5l_i2c_read(&bus, I2C_INT) == I2C_INT_BYTE,
          "NAK must still complete the address phase");
    CHECK(s5l_i2c_irq(&bus), "pending byte event did not assert level");

    /* Exact stock error order: IRQ filter W1Cs BYTE, state 2 reads NAK, state
     * 8 clears START, then a second IRQ W1Cs STOP. NAK must therefore survive
     * the BYTE acknowledgement; clearing the event is not a new bus phase. */
    s5l_i2c_write(&bus, I2C_INT, I2C_INT_BYTE);
    CHECK((s5l_i2c_read(&bus, I2C_STAT) & I2C_STAT_NAK) != 0u,
          "BYTE W1C cleared STAT.NAK before the stock state machine read it");
    CHECK(bus.starts == 1u && bus.naks == 1u &&
          bus.bytes_tx == 0u && bus.bytes_rx == 0u,
          "0x29 address NAK incorrectly entered a data phase");
    s5l_i2c_write(&bus, I2C_STAT, 0x90u);
    CHECK(s5l_i2c_read(&bus, I2C_INT) == I2C_INT_STOP,
          "stock NAK path did not produce the second STOP event");
    s5l_i2c_write(&bus, I2C_INT, I2C_INT_STOP);
    CHECK(!s5l_i2c_irq(&bus), "stock NAK/STOP path left the level high");

    /* Separately leave BYTE pending and complete STOP: selective W1C must
     * preserve the other bit even though stock normally drains them in order. */
    s5l_i2c_write(&bus, I2C_STAT, 0xd0u);
    s5l_i2c_write(&bus, I2C_DS, 0x29u << 1);
    s5l_i2c_write(&bus, I2C_STAT, 0xf0u);
    s5l_i2c_write(&bus, I2C_STAT, 0x90u);
    CHECK(s5l_i2c_read(&bus, I2C_INT) ==
          (I2C_INT_BYTE | I2C_INT_STOP), "events did not accumulate");
    s5l_i2c_write(&bus, I2C_INT, I2C_INT_BYTE);
    CHECK(s5l_i2c_read(&bus, I2C_INT) == I2C_INT_STOP &&
          s5l_i2c_irq(&bus), "selective W1C cleared too much");
    s5l_i2c_write(&bus, I2C_INT, I2C_INT_STOP);
    CHECK(!s5l_i2c_irq(&bus), "final W1C did not deassert the level");
}

static void test_pmu_multibyte_pointer_and_wrap(void) {
    s5l_i2c_t bus;
    s5l_pcf50635_t pmu;
    setup_pmu(&bus, &pmu);

    const uint8_t masks[5] = { 0xff, 0xff, 0xbf, 0xff, 0xff };
    uint8_t got[5] = {0};
    drive_write(&bus, PCF50635_I2C_ADDR, 0x07u, masks, 5u);
    drive_read(&bus, PCF50635_I2C_ADDR, 0x07u, got, 5u);
    CHECK(memcmp(got, masks, sizeof masks) == 0,
          "stock five-byte PMU mask transaction did not round-trip");
    for (unsigned i = 0; i < 5; i++)
        CHECK(pmu.written[0x07u + i] == 1u,
              "register %02x was not marked written", 0x07u + i);

    const uint8_t wrapping[2] = { 0x11, 0x22 };
    drive_write(&bus, PCF50635_I2C_ADDR, 0xffu, wrapping, 2u);
    CHECK(pmu.regs[0xff] == 0x11u && pmu.regs[0x00] == 0x22u,
          "auto-increment did not wrap safely inside 256 registers");
    CHECK(bus.bytes_tx == 10u,
          "consecutive RESUME commands moved %llu bytes, expect 10",
          (unsigned long long)bus.bytes_tx);

    uint64_t starts = bus.starts;
    s5l_i2c_write(&bus, I2C_STAT, 0xd0u);
    s5l_i2c_write(&bus, I2C_DS, PCF50635_I2C_ADDR << 1);
    s5l_i2c_write(&bus, I2C_STAT, 0xf0u);
    s5l_i2c_write(&bus, I2C_STAT, 0xf0u);
    CHECK(bus.starts == starts + 1u,
          "writing START twice manufactured a second transaction");
    s5l_i2c_write(&bus, I2C_INT, I2C_INT_ALL);
    s5l_i2c_write(&bus, I2C_STAT, 0x90u);
    s5l_i2c_write(&bus, I2C_INT, I2C_INT_ALL);
}

static unsigned from_bcd(uint8_t val) {
    return (unsigned)(val >> 4) * 10u + (val & 0x0fu);
}

static void test_pmu_rtc_and_tick_overflow(void) {
    s5l_i2c_t bus;
    s5l_pcf50635_t pmu;
    setup_pmu(&bus, &pmu);
    uint8_t rtc[7];

    s5l_pcf50635_set_time(&pmu, 1262304000ull);
    drive_read(&bus, PCF50635_I2C_ADDR, PCF50635_RTCSC, rtc, 7u);
    CHECK(from_bcd(rtc[0]) == 0u && from_bcd(rtc[1]) == 0u &&
          from_bcd(rtc[2]) == 0u, "2010 epoch time decoded incorrectly");
    CHECK(rtc[3] == 5u && from_bcd(rtc[4]) == 1u &&
          from_bcd(rtc[5]) == 1u && from_bcd(rtc[6]) == 10u,
          "RTC BCD/binary-weekday layout is wrong");

    s5l_pcf50635_set_time(&pmu, 1456704000ull); /* 2016-02-29, Monday */
    drive_read(&bus, PCF50635_I2C_ADDR, PCF50635_RTCSC, rtc, 7u);
    CHECK(rtc[3] == 1u && from_bcd(rtc[4]) == 29u &&
          from_bcd(rtc[5]) == 2u && from_bcd(rtc[6]) == 16u,
          "leap-day RTC decode failed");

    s5l_pcf50635_t lump, split;
    s5l_pcf50635_reset(&lump, S5L8900_TB_HZ);
    s5l_pcf50635_reset(&split, S5L8900_TB_HZ);
    lump.tick_accum = split.tick_accum = S5L8900_TB_HZ - 1u;
    uint64_t total = (uint64_t)(S5L8900_TB_HZ - 1u) + UINT32_MAX;
    uint64_t expect_seconds = PCF50635_DEFAULT_TIME +
                              total / S5L8900_TB_HZ;
    uint64_t expect_remainder = total % S5L8900_TB_HZ;
    s5l_pcf50635_tick(&lump, UINT32_MAX);
    s5l_pcf50635_tick(&split, UINT32_MAX / 2u);
    s5l_pcf50635_tick(&split, UINT32_MAX - UINT32_MAX / 2u);
    CHECK(lump.seconds == expect_seconds &&
          lump.tick_accum == expect_remainder,
          "UINT32_MAX tick overflowed: sec=%llu rem=%llu",
          (unsigned long long)lump.seconds,
          (unsigned long long)lump.tick_accum);
    CHECK(split.seconds == lump.seconds &&
          split.tick_accum == lump.tick_accum,
          "chunked and lumped RTC ticks diverged");

    s5l_pcf50635_t stopped;
    s5l_pcf50635_reset(&stopped, 0u);
    s5l_pcf50635_tick(&stopped, UINT32_MAX);
    CHECK(stopped.seconds == PCF50635_DEFAULT_TIME &&
          stopped.tick_accum == 0u, "zero-rate RTC divided or advanced");

    s5l_pcf50635_set_time(&stopped, 0u);
    CHECK(stopped.seconds == PCF50635_MIN_TIME,
          "pre-2000 time was not clamped to the chip's era");
    s5l_pcf50635_set_time(&stopped, UINT64_MAX);
    CHECK(stopped.seconds == PCF50635_MAX_TIME,
          "unrepresentable future time was not clamped");
    int year = 0;
    s5l_pcf50635_civil(UINT64_MAX, &year, NULL, NULL, NULL, NULL, NULL, NULL);
    CHECK(year == 2099, "public civil conversion overflowed year=%d", year);
    s5l_pcf50635_tick(&stopped, UINT32_MAX);
    CHECK(stopped.seconds == PCF50635_MAX_TIME,
          "RTC advanced past its representable century");
}

static void test_pmu_standby_wake_event_is_one_shot(void) {
    s5l_i2c_t bus;
    s5l_pcf50635_t pmu;
    setup_pmu(&bus, &pmu);

    CHECK(!s5l_pcf50635_in_standby(&pmu),
          "reset PMU claimed the application processor was in standby");
    CHECK(!s5l_pcf50635_irq(&pmu) && !s5l_pcf50635_irq(NULL),
          "reset or NULL PMU asserted INT_N");

    uint64_t unknown = pmu.unknown_reads;
    uint8_t masks[5] = {0xffu, 0xffu, 0xffu, 0xffu, 0xffu};
    drive_read(&bus, PCF50635_I2C_ADDR, PCF50635_INT1MASK,
               masks, sizeof masks);
    CHECK(masks[0] == 0u && masks[1] == 0u && masks[2] == 0u &&
          masks[3] == 0u && masks[4] == 0u,
          "reset mask bank was %02x %02x %02x %02x %02x",
          masks[0], masks[1], masks[2], masks[3], masks[4]);
    CHECK(pmu.unknown_reads == unknown,
          "known interrupt-mask registers were counted as unknown");

    /* Bit 1 is reserved on the period PCF50633-family register map. The old
     * model called it GOHIB, which made a real iPhone OS 3 shutdown (0x01)
     * invisible and treated the reserved bit as power state instead. Keep an
     * explicit negative control so that one-bit error cannot return. */
    uint8_t command = 0x02u;
    drive_write(&bus, PCF50635_I2C_ADDR, PCF50635_OOCSHDWN,
                &command, 1u);
    CHECK(!s5l_pcf50635_in_standby(&pmu),
          "reserved OOCSHDWN bit 1 was treated as standby");

    command = (uint8_t)(0x80u | PCF50635_OOCSHDWN_GO_STANDBY);
    drive_write(&bus, PCF50635_I2C_ADDR, PCF50635_OOCSHDWN,
                &command, 1u);
    CHECK(s5l_pcf50635_in_standby(&pmu),
          "OOCSHDWN.GO_STANDBY did not enter standby");

    uint8_t mask = PCF50635_INT2_ONKEYR;
    drive_write(&bus, PCF50635_I2C_ADDR, PCF50635_INT2MASK, &mask, 1u);
    uint64_t guest_writes = pmu.reg_writes;
    s5l_pcf50635_wake_onkey(&pmu);
    CHECK(!s5l_pcf50635_in_standby(&pmu),
          "ONKEY wake left GO_STANDBY asserted");
    CHECK(pmu.regs[PCF50635_OOCSHDWN] == 0x80u,
          "ONKEY wake discarded unrelated OOCSHDWN bits: %02x",
          pmu.regs[PCF50635_OOCSHDWN]);
    CHECK(pmu.reg_writes == guest_writes,
          "hardware ONKEY event was counted as a guest I2C write");
    CHECK(!s5l_pcf50635_irq(&pmu),
          "masked ONKEY event asserted the PMU interrupt");

    mask = 0u;
    drive_write(&bus, PCF50635_I2C_ADDR, PCF50635_INT2MASK, &mask, 1u);
    CHECK(s5l_pcf50635_irq(&pmu),
          "unmasking a latched ONKEY event did not assert the PMU interrupt");

    unknown = pmu.unknown_reads;
    uint8_t events[5] = {0};
    drive_read(&bus, PCF50635_I2C_ADDR, PCF50635_INT1,
               events, sizeof events);
    CHECK(events[0] == 0u && events[1] == PCF50635_INT2_ONKEYR &&
          events[2] == 0u && events[3] == 0u && events[4] == 0u,
          "wake bank was %02x %02x %02x %02x %02x",
          events[0], events[1], events[2], events[3], events[4]);
    CHECK(pmu.unknown_reads == unknown,
          "known event registers were counted as unknown");
    CHECK(!s5l_pcf50635_irq(&pmu),
          "reading the event bank did not deassert the PMU interrupt");

    memset(events, 0xff, sizeof events);
    drive_read(&bus, PCF50635_I2C_ADDR, PCF50635_INT1,
               events, sizeof events);
    CHECK(events[0] == 0u && events[1] == 0u && events[2] == 0u &&
          events[3] == 0u && events[4] == 0u,
          "PMU wake event did not clear on read");

    CHECK(!s5l_pcf50635_in_standby(NULL), "NULL PMU entered standby");
    s5l_pcf50635_wake_onkey(NULL);
}

static void test_unknown_pmu_registers_are_visible_and_bounded(void) {
    s5l_i2c_t bus;
    s5l_pcf50635_t pmu;
    setup_pmu(&bus, &pmu);
    uint8_t val;
    for (unsigned i = 0; i < 20u; i++)
        drive_read(&bus, PCF50635_I2C_ADDR, (uint8_t)(0x80u + i), &val, 1u);
    CHECK(pmu.unknown_reads == 20u,
          "unknown PMU reads=%llu expect 20",
          (unsigned long long)pmu.unknown_reads);
    CHECK(pmu.unknown_reg_count == PCF50635_UNKNOWN_REGS,
          "bounded unknown-register set grew to %u", pmu.unknown_reg_count);

    uint8_t stored = 0x5au;
    drive_write(&bus, PCF50635_I2C_ADDR, 0x80u, &stored, 1u);
    uint64_t before = pmu.unknown_reads;
    drive_read(&bus, PCF50635_I2C_ADDR, 0x80u, &val, 1u);
    CHECK(val == stored && pmu.unknown_reads == before,
          "written PMU storage was still treated as unknown");

    uint8_t rejected = 0x99u;
    drive_write(&bus, PCF50635_I2C_ADDR, PCF50635_RTCSC, &rejected, 1u);
    CHECK(pmu.unknown_writes == 1u,
          "unsupported RTC write was not made visible");
}

static void test_machine_routes_widths_windows_and_irqs(void) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, 0u, 1u << 20), "machine init failed");

    s5l_window_t windows[S5L_WINDOW_MAX];
    unsigned count = s5l8900_windows(&m, windows, S5L_WINDOW_MAX);
    unsigned seen0 = 0, seen1 = 0;
    for (unsigned i = 0; i < count && i < S5L_WINDOW_MAX; i++) {
        if (windows[i].base == S5L8900_I2C0_BASE &&
            windows[i].size == S5L8900_DEV_SIZE) seen0++;
        if (windows[i].base == S5L8900_I2C1_BASE &&
            windows[i].size == S5L8900_DEV_SIZE) seen1++;
    }
    CHECK(seen0 == 1u && seen1 == 1u,
          "fixed window map has i2c0=%u i2c1=%u", seen0, seen1);
    const s5l_window_t *conflict =
        s5l8900_ram_conflict(S5L8900_I2C0_BASE, 4u);
    CHECK(conflict && strcmp(conflict->name, "i2c0") == 0,
          "RAM conflict did not identify i2c0");
    CHECK(!s5l8900_add_stub(&m, S5L8900_I2C0_BASE,
                            S5L8900_DEV_SIZE, "shadow"),
          "stub was allowed to shadow i2c0");
    CHECK(!s5l8900_add_stub(&m, S5L8900_I2C1_BASE,
                            S5L8900_DEV_SIZE, "shadow"),
          "stub was allowed to shadow i2c1");

    uint64_t ur = m.unmapped_reads, uw = m.unmapped_writes;
    m.bus.write32(m.bus.ctx, S5L8900_I2C0_BASE + I2C_ENABLE, 1u);
    CHECK(m.bus.read32(m.bus.ctx, S5L8900_I2C0_BASE + I2C_BUSY) == 0u,
          "aligned 32-bit controller read failed");
    CHECK(m.unmapped_reads == ur && m.unmapped_writes == uw,
          "valid i2c0 MMIO was classified unmapped");

    s5l_i2c_t before = m.i2c[0];
    m.bus.write8(m.bus.ctx, S5L8900_I2C0_BASE + I2C_DS, 0xaau);
    m.bus.write16(m.bus.ctx, S5L8900_I2C0_BASE + I2C_DS, 0xbbccu);
    m.bus.write32(m.bus.ctx, S5L8900_I2C0_BASE + 1u, 0xdeadbeefu);
    (void)m.bus.read16(m.bus.ctx, S5L8900_I2C0_BASE + I2C_DS);
    (void)m.bus.read32(m.bus.ctx, S5L8900_I2C0_BASE +
                                  S5L8900_DEV_SIZE - 2u);
    CHECK(memcmp(&before, &m.i2c[0], sizeof before) == 0,
          "invalid-width/unaligned access mutated controller state");
    CHECK(m.unmapped_writes == uw + 3u && m.unmapped_reads == ur + 2u,
          "malformed MMIO counts r=%llu w=%llu",
          (unsigned long long)(m.unmapped_reads - ur),
          (unsigned long long)(m.unmapped_writes - uw));

    /* Generate an i2c0 completion while VIC21 is masked. Raw sees the level,
     * the CPU does not; enabling then entering WFI refreshes it at zero time. */
    m.bus.write32(m.bus.ctx, S5L8900_I2C0_BASE + I2C_STAT, 0xd0u);
    m.bus.write32(m.bus.ctx, S5L8900_I2C0_BASE + I2C_DS,
                  PCF50635_I2C_ADDR << 1);
    m.bus.write32(m.bus.ctx, S5L8900_I2C0_BASE + I2C_STAT, 0xf0u);
    s5l8900_tick(&m, 0u);
    CHECK((m.vic[0].raw & (1u << S5L8900_IRQ_I2C0)) != 0u &&
          !m.cpu.irq_line, "masked IRQ21 raw/CPU behavior is wrong");

    s5l_vic_write(&m.vic[0], VIC_INTENABLE, 1u << S5L8900_IRQ_I2C0);
    uint64_t ticks_before = m.timer.ticks, accum_before = m.tb_accum;
    CHECK(m.bus.wait_for_interrupt &&
          m.bus.wait_for_interrupt(m.bus.ctx),
          "pending I2C level did not wake WFI");
    CHECK(m.timer.ticks == ticks_before && m.tb_accum == accum_before,
          "I2C WFI wake advanced guest time");
    CHECK(m.cpu.irq_line, "enabled IRQ21 did not reach the CPU");

    m.bus.write32(m.bus.ctx, S5L8900_I2C0_BASE + I2C_INT, I2C_INT_BYTE);
    s5l8900_tick(&m, 0u);
    CHECK((m.vic[0].raw & (1u << S5L8900_IRQ_I2C0)) == 0u &&
          !m.cpu.irq_line, "W1C did not deassert IRQ21 and the CPU");

    /* Finish bus0 and clear its STOP before testing that i2c1 routes only 22. */
    m.bus.write32(m.bus.ctx, S5L8900_I2C0_BASE + I2C_STAT, 0x90u);
    m.bus.write32(m.bus.ctx, S5L8900_I2C0_BASE + I2C_INT, I2C_INT_ALL);
    s5l_vic_write(&m.vic[0], VIC_INTENABLE, 1u << S5L8900_IRQ_I2C1);
    m.bus.write32(m.bus.ctx, S5L8900_I2C1_BASE + I2C_ENABLE, 1u);
    m.bus.write32(m.bus.ctx, S5L8900_I2C1_BASE + I2C_STAT, 0xd0u);
    m.bus.write32(m.bus.ctx, S5L8900_I2C1_BASE + I2C_DS, 0x22u << 1);
    m.bus.write32(m.bus.ctx, S5L8900_I2C1_BASE + I2C_STAT, 0xf0u);
    s5l8900_tick(&m, 0u);
    CHECK((m.vic[0].raw & (1u << S5L8900_IRQ_I2C1)) != 0u &&
          (m.vic[0].raw & (1u << S5L8900_IRQ_I2C0)) == 0u,
          "i2c1 did not route exclusively to VIC0 line 22");
    m.bus.write32(m.bus.ctx, S5L8900_I2C1_BASE + I2C_INT, I2C_INT_ALL);
    m.bus.write32(m.bus.ctx, S5L8900_I2C1_BASE + I2C_STAT, 0x90u);
    m.bus.write32(m.bus.ctx, S5L8900_I2C1_BASE + I2C_INT, I2C_INT_ALL);
    s5l8900_tick(&m, 0u);
    CHECK((m.vic[0].raw & (1u << S5L8900_IRQ_I2C1)) == 0u,
          "IRQ22 remained asserted after acknowledge");

    s5l8900_free(&m);
}

static void test_malformed_runtime_state_cannot_index_callbacks(void) {
    s5l_i2c_t bus;
    s5l_i2c_reset(&bus);
    bus.active = true;
    bus.stat = I2C_STAT_START | I2C_STAT_MODE_MTX;
    bus.sel = INT32_MAX;
    bus.ds = 0x55u;
    s5l_i2c_write(&bus, I2C_CON, I2C_CON_RESUME);
    CHECK(bus.nak && (bus.intstat & I2C_INT_BYTE) != 0u,
          "invalid selected index was not converted into a bounded NAK");
    CHECK(s5l_i2c_read(NULL, I2C_STAT) == 0u &&
          !s5l_i2c_irq(NULL), "NULL controller helpers were unsafe");
    s5l_i2c_write(NULL, I2C_STAT, 0u);
}

static void test_snapshot_mid_transaction_rebinds_callbacks(void) {
    s5l8900_t src, dst;
    CHECK(s5l8900_init(&src, 0u, 1u << 16), "source init failed");
    CHECK(s5l8900_init(&dst, 0u, 1u << 16), "destination init failed");

    s5l_i2c_t *bus = &src.i2c[0];
    s5l_i2c_write(bus, I2C_ENABLE, 1u);
    s5l_i2c_write(bus, I2C_STAT, 0xd0u);
    s5l_i2c_write(bus, I2C_DS, PCF50635_I2C_ADDR << 1);
    s5l_i2c_write(bus, I2C_STAT, 0xf0u);
    s5l_i2c_write(bus, I2C_INT, I2C_INT_BYTE);
    s5l_i2c_write(bus, I2C_DS, 0x40u);
    s5l_i2c_write(bus, I2C_CON, I2C_CON_RESUME);
    s5l_i2c_write(bus, I2C_INT, I2C_INT_BYTE);
    s5l_i2c_write(bus, I2C_DS, 0x5au); /* queued, not yet resumed */

    uint8_t *snapshot = NULL;
    size_t snapshot_len = 0;
    CHECK(snapshot_save_mem(&src, &snapshot, &snapshot_len) == SNAP_OK,
          "could not save mid-transaction snapshot");
    CHECK(snapshot_load_mem(&dst, snapshot, snapshot_len) == SNAP_OK,
          "could not restore mid-transaction snapshot");
    CHECK(dst.i2c[0].slaves[0].ctx == &dst.pmu &&
          dst.i2c[0].slaves[0].ctx != &src.pmu,
          "restore copied a source callback context");

    s5l_i2c_write(&dst.i2c[0], I2C_CON,
                  I2C_CON_ACKEN | I2C_CON_RESUME);
    CHECK(dst.pmu.written[0x40] == 1u && dst.pmu.regs[0x40] == 0x5au,
          "restored transfer did not resume into destination PMU");
    CHECK(src.pmu.written[0x40] == 0u,
          "restored callback mutated source PMU");

    free(snapshot);
    s5l8900_free(&src);
    s5l8900_free(&dst);
}

static void test_snapshot_rejects_invalid_i2c_and_pmu_state(void) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, 0u, 1u << 16), "machine init failed");
    uint8_t *out = NULL;
    size_t out_len = 0;

    m.i2c[0].active = true;
    m.i2c[0].stat = I2C_STAT_START;
    m.i2c[0].sel = (int32_t)m.i2c[0].slave_count;
    CHECK(snapshot_save_mem(&m, &out, &out_len) == SNAP_ERR_CORRUPT,
          "out-of-live-range selected slave was snapshotted");
    m.i2c[0].active = false; m.i2c[0].stat = 0u; m.i2c[0].sel = -1;

    m.i2c[0].unknown_off_count = S5L_I2C_UNKNOWN_OFF + 1u;
    CHECK(snapshot_save_mem(&m, &out, &out_len) == SNAP_ERR_CORRUPT,
          "overflowed unknown-offset count was snapshotted");
    m.i2c[0].unknown_off_count = 0u;

    m.pmu.written[3] = 2u;
    CHECK(snapshot_save_mem(&m, &out, &out_len) == SNAP_ERR_CORRUPT,
          "non-boolean PMU written marker was snapshotted");
    m.pmu.written[3] = 0u;

    m.pmu.tick_accum = m.pmu.tick_hz;
    CHECK(snapshot_save_mem(&m, &out, &out_len) == SNAP_ERR_CORRUPT,
          "out-of-range PMU tick remainder was snapshotted");
    CHECK(out == NULL && out_len == 0u,
          "failed snapshot returned an allocation");
    m.pmu.tick_accum = 0u;

    m.pmu.seconds = UINT64_MAX;
    CHECK(snapshot_save_mem(&m, &out, &out_len) == SNAP_ERR_CORRUPT,
          "out-of-era RTC seconds were snapshotted");
    s5l8900_free(&m);
}

int main(void) {
    printf("S5LBox S5L8900 I2C / PCF50635 tests\n");
    test_reset_and_attachment_are_bounded();
    test_controller_register_edges();
    test_unknown_slave_naks_and_w1c_is_selective();
    test_pmu_multibyte_pointer_and_wrap();
    test_pmu_rtc_and_tick_overflow();
    test_pmu_standby_wake_event_is_one_shot();
    test_unknown_pmu_registers_are_visible_and_bounded();
    test_machine_routes_widths_windows_and_irqs();
    test_malformed_runtime_state_cannot_index_callbacks();
    test_snapshot_mid_transaction_rebinds_callbacks();
    test_snapshot_rejects_invalid_i2c_and_pmu_state();
    printf("  %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
