/*
 * S5LBox — S5L8900 GPIO interrupt controller and GPIO pin block tests.
 *
 * The property that matters most here is not that a register stores a word. It
 * is that the ONE line the guest has already armed reaches the CPU: run59
 * measured this controller's group-4 enable holding 0x08000000, which is bit
 * 27, which is line 155, which is the multi-touch attention line — and the
 * whole chain from that bit to cpu.irq_line runs through two numbers that are
 * easy to get wrong and impossible to notice being wrong. 155 must decode as
 * group 4 bit 27 with a stride of 32, not as a pin; and group 4 must cascade to
 * VIC line 2, the fifth entry of a DESCENDING device-tree array. Get either one
 * wrong and every register still reads back correctly, every count still
 * increments, and a touch simply never arrives. Several tests below exist only
 * to make those two numbers fail loudly.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
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

/* The seven cascade lines, transcribed here from the device tree rather than
 * from the model, so a change to the model's table is a test failure and not a
 * silently agreed mistake. /arm-io/gpio interrupts =
 *   {0x21, 0x20, 0x1f, 0x03, 0x02, 0x01, 0x00} */
static const unsigned DT_CASCADE[S5L_GPIOIC_GROUPS] = {
    33u, 32u, 31u, 3u, 2u, 1u, 0u
};

/* ------------------------------------------------------------------------- */

static void test_reset_is_total_and_null_safe(void) {
    s5l_gpioic_t g, expected;
    memset(&g, 0x5a, sizeof g);
    memset(&expected, 0, sizeof expected);
    s5l_gpioic_reset(&g);
    CHECK(memcmp(&g, &expected, sizeof g) == 0,
          "reset did not totally initialize a poisoned controller");

    s5l_gpio_t p, pexpected;
    memset(&p, 0x5a, sizeof p);
    memset(&pexpected, 0, sizeof pexpected);
    s5l_gpio_reset(&p);
    CHECK(memcmp(&p, &pexpected, sizeof p) == 0,
          "reset did not totally initialize a poisoned pin block");

    /* Every entry point is public and must survive a NULL object. */
    CHECK(s5l_gpioic_read(NULL, GPIOIC_INTSTAT) == 0u, "NULL read unsafe");
    s5l_gpioic_write(NULL, GPIOIC_INTEN, 1u);
    s5l_gpioic_set_line(NULL, 155u, true);
    CHECK(!s5l_gpioic_line(NULL, 155u), "NULL line query unsafe");
    CHECK(!s5l_gpioic_group_irq(NULL, 4u), "NULL group query unsafe");
    CHECK(s5l_gpio_read(NULL, 0u, 4u) == 0u, "NULL pin read unsafe");
    s5l_gpio_write(NULL, 0u, 1u, 4u);
    CHECK(!s5l_gpio_pin(NULL, 0x0606u), "NULL pin query unsafe");
    CHECK(!s5l_gpio_watch(NULL, 0x0606u, NULL, NULL), "NULL watch unsafe");
    s5l_gpio_reset(NULL);
    s5l_gpioic_reset(NULL);
}

/*
 * The cascade, pinned against the device tree's own array and not against the
 * model's copy of it. Group 4 is checked separately and by literal, because it
 * is the one that carries touch and because a reversed transcription of a
 * descending list gives group 4 the line 31 that belongs to group 2 — a value
 * that is still a legal, still-unclaimed VIC line, so nothing else would
 * complain.
 */
static void test_cascade_is_the_device_trees_own_array(void) {
    for (unsigned g = 0; g < S5L_GPIOIC_GROUPS; g++)
        CHECK(s5l_gpioic_cascade(g) == DT_CASCADE[g],
              "group %u cascades to VIC line %u, expected %u",
              g, s5l_gpioic_cascade(g), DT_CASCADE[g]);

    CHECK(s5l_gpioic_cascade(4u) == 2u,
          "the multi-touch group does not cascade to VIC line 2 but to %u",
          s5l_gpioic_cascade(4u));

    /* Two of the seven are on VIC1, which is what forces the routing to split
     * the flat number instead of hardcoding vic[0]. */
    CHECK(s5l_gpioic_cascade(0u) >= 32u && s5l_gpioic_cascade(1u) >= 32u,
          "groups 0 and 1 are no longer on VIC1: %u %u",
          s5l_gpioic_cascade(0u), s5l_gpioic_cascade(1u));

    /* No two groups may share a line, or one group's source would silently
     * acknowledge another's. */
    for (unsigned a = 0; a < S5L_GPIOIC_GROUPS; a++)
        for (unsigned b = a + 1u; b < S5L_GPIOIC_GROUPS; b++)
            CHECK(s5l_gpioic_cascade(a) != s5l_gpioic_cascade(b),
                  "groups %u and %u share VIC line %u",
                  a, b, s5l_gpioic_cascade(a));

    CHECK(s5l_gpioic_cascade(S5L_GPIOIC_GROUPS) >= 32u * S5L8900_VIC_COUNT,
          "a group that does not exist was given a routable line");
    CHECK(s5l_gpioic_cascade(999u) >= 32u * S5L8900_VIC_COUNT,
          "a wildly out-of-range group was given a routable line");
}

/*
 * The register file. Four banks of seven, at the driver's own page-relative
 * offsets, and nothing at the eighth slot of any bank: the group count is
 * /arm-io/gpio's `#interrupt-groups` and a model that answered an eighth group
 * would let the init loop's bound go unnoticed.
 */
static void test_register_file_decodes_four_banks_of_seven(void) {
    s5l_gpioic_t g;
    s5l_gpioic_reset(&g);

    for (unsigned k = 0; k < S5L_GPIOIC_GROUPS; k++) {
        s5l_gpioic_write(&g, GPIOIC_INTLEVEL + 4u * k, 0x1000u + k);
        s5l_gpioic_write(&g, GPIOIC_INTEN     + 4u * k, 0x3000u + k);
        s5l_gpioic_write(&g, GPIOIC_INTTYPE   + 4u * k, 0x4000u + k);
    }
    for (unsigned k = 0; k < S5L_GPIOIC_GROUPS; k++) {
        CHECK(s5l_gpioic_read(&g, GPIOIC_INTLEVEL + 4u * k) == 0x1000u + k,
              "INTLEVEL group %u did not read back", k);
        CHECK(s5l_gpioic_read(&g, GPIOIC_INTEN + 4u * k) == 0x3000u + k,
              "INTEN group %u did not read back", k);
        CHECK(s5l_gpioic_read(&g, GPIOIC_INTTYPE + 4u * k) == 0x4000u + k,
              "INTTYPE group %u did not read back", k);
    }

    /* The four banks must not alias each other. */
    CHECK(g.level[0] == 0x1000u && g.en[0] == 0x3000u && g.type[0] == 0x4000u,
          "two banks share storage: level=%08x en=%08x type=%08x",
          g.level[0], g.en[0], g.type[0]);

    /* One past each bank is not a register. 0x80 + 4*7 == 0x9c, and so on. */
    uint64_t before = g.unknown_reads;
    static const uint32_t past[] = {
        GPIOIC_INTLEVEL + 4u * S5L_GPIOIC_GROUPS,
        GPIOIC_INTSTAT  + 4u * S5L_GPIOIC_GROUPS,
        GPIOIC_INTEN    + 4u * S5L_GPIOIC_GROUPS,
        GPIOIC_INTTYPE  + 4u * S5L_GPIOIC_GROUPS,
    };
    for (unsigned i = 0; i < sizeof past / sizeof past[0]; i++)
        CHECK(s5l_gpioic_read(&g, past[i]) == 0u,
              "offset 0x%03x answered as an eighth group", past[i]);
    CHECK(g.unknown_reads == before + 4u,
          "an eighth group was decoded rather than counted (%llu unknown)",
          (unsigned long long)g.unknown_reads);

    /* Unaligned offsets inside a bank are not registers either. */
    before = g.unknown_reads;
    CHECK(s5l_gpioic_read(&g, GPIOIC_INTEN + 2u) == 0u,
          "an unaligned offset was decoded");
    CHECK(g.unknown_reads == before + 1u, "an unaligned read was not counted");

    /* Unknown traffic is remembered, distinctly and boundedly. */
    s5l_gpioic_reset(&g);
    for (unsigned i = 0; i < 4u * S5L_GPIOIC_UNKNOWN_OFF; i++)
        s5l_gpioic_write(&g, 0x400u + 4u * i, i);
    CHECK(g.unknown_writes == 4u * S5L_GPIOIC_UNKNOWN_OFF,
          "unknown writes were not all counted: %llu",
          (unsigned long long)g.unknown_writes);
    CHECK(g.unknown_off_count == S5L_GPIOIC_UNKNOWN_OFF,
          "the unknown-offset log is not bounded at %u: %u",
          (unsigned)S5L_GPIOIC_UNKNOWN_OFF, g.unknown_off_count);
    for (unsigned i = 0; i < 8u; i++)
        s5l_gpioic_write(&g, 0x400u, 0u);
    CHECK(g.unknown_off_count == S5L_GPIOIC_UNKNOWN_OFF,
          "a repeated offset was logged again");
}

/*
 * INTSTAT is WRITE-ONE-TO-CLEAR. This is the mutation that looks most harmless
 * and is worst: a model that SET pending bits would make the driver's own
 * enable path — which writes exactly `1 << bit` to 0xA0 immediately before
 * arming that line, to discard a stale pending — raise a spurious interrupt on
 * every single enable, for every driver in the system.
 */
static void test_intstat_is_write_one_to_clear_not_write_one_to_set(void) {
    s5l_gpioic_t g;
    s5l_gpioic_reset(&g);

    /* Writing ones into an empty latch must leave it empty. */
    s5l_gpioic_write(&g, GPIOIC_INTSTAT + 4u * 4u, 0xffffffffu);
    CHECK(s5l_gpioic_read(&g, GPIOIC_INTSTAT + 4u * 4u) == 0u,
          "writing to INTSTAT set pending bits: 0x%08x",
          s5l_gpioic_read(&g, GPIOIC_INTSTAT + 4u * 4u));

    /* A real source latches, and only the matching bit clears it. */
    s5l_gpioic_set_line(&g, 155u, true);
    CHECK(s5l_gpioic_read(&g, GPIOIC_INTSTAT + 4u * 4u) == 0x08000000u,
          "line 155 did not latch as group 4 bit 27");
    s5l_gpioic_set_line(&g, 155u, false);      /* source withdrawn... */
    CHECK(s5l_gpioic_read(&g, GPIOIC_INTSTAT + 4u * 4u) == 0x08000000u,
          "deasserting the source cleared the latch — a pulse shorter than "
          "the guest's polling interval would be lost");

    s5l_gpioic_write(&g, GPIOIC_INTSTAT + 4u * 4u, 0x00000001u);
    CHECK(s5l_gpioic_read(&g, GPIOIC_INTSTAT + 4u * 4u) == 0x08000000u,
          "clearing the wrong bit cleared the latch");
    s5l_gpioic_write(&g, GPIOIC_INTSTAT + 4u * 4u, 0x08000000u);
    CHECK(s5l_gpioic_read(&g, GPIOIC_INTSTAT + 4u * 4u) == 0u,
          "write-one-to-clear did not clear the latch");

    /*
     * AND AN ACKNOWLEDGE STICKS, even while the source is still driving.
     *
     * This is the one that run71 measured. A latch that re-asserted from the
     * still-high line undid its own acknowledge inside the same store: the
     * GPIO interrupt controller's filter re-entered every ~419 instructions
     * and did so 1,193,122 times over half a billion instructions, the touch
     * driver's work loop never ran, and the queued report was never read.
     * The driver writes this group's INTEN exactly twice in a whole boot, so
     * it does not mask the line while servicing it -- a controller that
     * behaved the other way would be unusable by it.
     */
    s5l_gpioic_set_line(&g, 155u, true);         /* raw was low: an edge */
    CHECK(s5l_gpioic_read(&g, GPIOIC_INTSTAT + 4u * 4u) == 0x08000000u,
          "a rising edge did not latch");
    s5l_gpioic_set_line(&g, 155u, true);         /* already high: no edge */
    s5l_gpioic_write(&g, GPIOIC_INTSTAT + 4u * 4u, 0xffffffffu);
    CHECK(s5l_gpioic_read(&g, GPIOIC_INTSTAT + 4u * 4u) == 0u,
          "acknowledging a still-asserted source did not stick — that is the "
          "1,193,122-iteration livelock run71 spent half a billion "
          "instructions in");

    /* A device that drops its line and raises it again is a NEW interrupt. */
    s5l_gpioic_set_line(&g, 155u, false);
    CHECK(s5l_gpioic_read(&g, GPIOIC_INTSTAT + 4u * 4u) == 0u,
          "deasserting set a pending bit");
    s5l_gpioic_set_line(&g, 155u, true);
    CHECK(s5l_gpioic_read(&g, GPIOIC_INTSTAT + 4u * 4u) == 0x08000000u,
          "a second report after the first was serviced did not latch");
    s5l_gpioic_set_line(&g, 155u, false);
    s5l_gpioic_write(&g, GPIOIC_INTSTAT + 4u * 4u, 0xffffffffu);
    CHECK(s5l_gpioic_read(&g, GPIOIC_INTSTAT + 4u * 4u) == 0u,
          "a withdrawn source could not be acknowledged");

    /* Groups are independent. */
    s5l_gpioic_set_line(&g, 0u, true);
    s5l_gpioic_set_line(&g, 155u, true);
    s5l_gpioic_write(&g, GPIOIC_INTSTAT, 0xffffffffu);
    CHECK(s5l_gpioic_read(&g, GPIOIC_INTSTAT + 4u * 4u) == 0x08000000u,
          "acknowledging group 0 disturbed group 4");
}

/*
 * Line 155 is an interrupt-controller index with a stride of 32, not a pin.
 * The failure this pins is the one the handoff warns about by name: computing
 * group*8+bit, which puts 155 in group 19 — a group that does not exist — and
 * makes the enable the guest has already written unreachable.
 */
static void test_line_155_is_group_four_bit_twenty_seven(void) {
    s5l_gpioic_t g;
    s5l_gpioic_reset(&g);

    CHECK(S5L_GPIOIC_LINE_MULTITOUCH == 155u,
          "the multi-touch line constant moved");
    CHECK(155u / 32u == 4u && 155u % 32u == 27u,
          "155 does not decompose as group 4 bit 27 at stride 32");

    s5l_gpioic_set_line(&g, S5L_GPIOIC_LINE_MULTITOUCH, true);
    CHECK(g.raw[4] == 0x08000000u,
          "line 155 drove group 4 raw = 0x%08x, expected 0x08000000",
          g.raw[4]);
    for (unsigned k = 0; k < S5L_GPIOIC_GROUPS; k++)
        if (k != 4u)
            CHECK(g.raw[k] == 0u && g.stat[k] == 0u,
                  "line 155 also drove group %u", k);
    CHECK(s5l_gpioic_line(&g, 155u), "line 155 did not read back as driven");
    CHECK(!s5l_gpioic_line(&g, 154u) && !s5l_gpioic_line(&g, 156u),
          "line 155 drove a neighbouring line");

    /* This is exactly the enable word run59 measured. */
    CHECK((1u << 27) == 0x08000000u, "bit 27 is not 0x08000000");

    /* Every line in range is reachable and distinct; nothing outside is. */
    s5l_gpioic_reset(&g);
    for (unsigned line = 0; line < S5L_GPIOIC_LINES; line++) {
        s5l_gpioic_set_line(&g, line, true);
        CHECK(g.raw[line / 32u] == (1u << (line % 32u)),
              "line %u did not drive exactly one bit of group %u",
              line, line / 32u);
        s5l_gpioic_set_line(&g, line, false);
        g.stat[line / 32u] = 0u;
    }
    s5l_gpioic_set_line(&g, S5L_GPIOIC_LINES, true);
    s5l_gpioic_set_line(&g, 0xffffffffu, true);
    for (unsigned k = 0; k < S5L_GPIOIC_GROUPS; k++)
        CHECK(g.raw[k] == 0u && g.stat[k] == 0u,
              "an out-of-range line wrapped into group %u", k);
    CHECK(!s5l_gpioic_line(&g, S5L_GPIOIC_LINES),
          "an out-of-range line read back as driven");
}

/* The group output is OR(STAT & EN): neither half alone asserts anything. */
static void test_group_output_is_pending_and_enabled(void) {
    s5l_gpioic_t g;
    s5l_gpioic_reset(&g);

    s5l_gpioic_set_line(&g, 155u, true);
    CHECK(!s5l_gpioic_group_irq(&g, 4u),
          "a pending line with no enable asserted the group");

    s5l_gpioic_write(&g, GPIOIC_INTEN + 4u * 4u, 0x00000001u);
    CHECK(!s5l_gpioic_group_irq(&g, 4u),
          "the wrong enable bit asserted the group");

    s5l_gpioic_write(&g, GPIOIC_INTEN + 4u * 4u, 0x08000000u);
    CHECK(s5l_gpioic_group_irq(&g, 4u),
          "pending and enabled did not assert the group");

    /* Enabling with nothing pending asserts nothing. */
    s5l_gpioic_write(&g, GPIOIC_INTEN + 4u * 5u, 0xffffffffu);
    CHECK(!s5l_gpioic_group_irq(&g, 5u),
          "an enable with no pending source asserted a group");

    /* INTEN is a plain mask, not write-one-to-set: the driver's disable path
     * stores a whole shadow word with one bit cleared, and a set-only register
     * would make disabling impossible. */
    s5l_gpioic_write(&g, GPIOIC_INTEN + 4u * 4u, 0u);
    CHECK(s5l_gpioic_read(&g, GPIOIC_INTEN + 4u * 4u) == 0u &&
          !s5l_gpioic_group_irq(&g, 4u),
          "INTEN could not be cleared — write-one-to-set would leave every "
          "line the driver ever armed permanently armed");

    CHECK(!s5l_gpioic_group_irq(&g, S5L_GPIOIC_GROUPS),
          "a group that does not exist asserted");
}

/*
 * The guest's exact enable sequence, transcribed from AppleS5L8900XGPIOIC at
 * 0xc05a5678-0xc05a56dc, run against the model. This is the sequence that
 * produced run59's measurement, so reproducing it must reproduce that value.
 */
static void test_the_drivers_own_enable_sequence(void) {
    s5l_gpioic_t g;
    s5l_gpioic_reset(&g);

    /* Init: 0xc05a51a8 zeroes INTEN for every group, count from
     * #interrupt-groups. */
    for (unsigned k = 0; k < S5L_GPIOIC_GROUPS; k++)
        s5l_gpioic_write(&g, GPIOIC_INTEN + 4u * k, 0u);

    /* Configure: read-modify-write of INTTYPE and INTLEVEL from the device
     * tree's second interrupt cell, which for multi-touch is zero. */
    unsigned line = 155u, group = line >> 5, bit = line & 0x1fu;
    uint32_t type  = s5l_gpioic_read(&g, GPIOIC_INTTYPE + 4u * group);
    s5l_gpioic_write(&g, GPIOIC_INTTYPE + 4u * group,
                     (type & ~(1u << bit)) | (0u << bit));
    uint32_t level = s5l_gpioic_read(&g, GPIOIC_INTLEVEL + 4u * group);
    s5l_gpioic_write(&g, GPIOIC_INTLEVEL + 4u * group,
                     (level & ~(1u << bit)) | (0u << bit));

    /* Enable: clear the stale pending bit, then store the whole shadow. */
    s5l_gpioic_write(&g, GPIOIC_INTSTAT + 4u * group, 1u << bit);
    uint32_t shadow = 0u;
    shadow |= 1u << bit;
    s5l_gpioic_write(&g, GPIOIC_INTEN + 4u * group, shadow);

    CHECK(s5l_gpioic_read(&g, GPIOIC_INTEN + 4u * 4u) == 0x08000000u,
          "the driver's own sequence did not produce run59's measured "
          "group-4 enable of 0x08000000, but 0x%08x",
          s5l_gpioic_read(&g, GPIOIC_INTEN + 4u * 4u));
    CHECK(!s5l_gpioic_group_irq(&g, 4u),
          "arming a line asserted it — the stale-pending clear became a set");

    /* Disable: 0xc05a5748 clears the shadow bit and stores the shadow. */
    shadow &= ~(1u << bit);
    s5l_gpioic_write(&g, GPIOIC_INTEN + 4u * group, shadow);
    CHECK(s5l_gpioic_read(&g, GPIOIC_INTEN + 4u * 4u) == 0u,
          "the driver's own disable sequence left the line armed");
}

/*
 * End to end through the bus: a store into the window the machine decodes, an
 * assertion on the model, and cpu.irq_line. Both VICs are exercised, because
 * two of the seven groups are the first sources this machine has ever routed
 * to VIC1 and s5l_vic_set_line() silently drops any line above 31.
 */
static void test_machine_routes_the_cascade_to_both_vics(void) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, 0u, 1u << 16), "machine init failed");
    void *c = m.bus.ctx;

    /* The window really is decoded, at the driver's page-relative offsets. */
    m.bus.write32(c, S5L8900_GPIOIC_PAGE + GPIOIC_INTEN + 4u * 4u, 0x08000000u);
    CHECK(m.gpioic.en[4] == 0x08000000u,
          "a store to 0x39a000d0 did not reach group 4's enable (%08x)",
          m.gpioic.en[4]);
    CHECK(m.bus.read32(c, S5L8900_GPIOIC_PAGE + GPIOIC_INTEN + 4u * 4u) ==
          0x08000000u, "group 4's enable did not read back through the bus");
    CHECK(m.unmapped_reads == 0u && m.unmapped_writes == 0u,
          "the gpioic window is not decoded: %llu unmapped reads, %llu writes",
          (unsigned long long)m.unmapped_reads,
          (unsigned long long)m.unmapped_writes);

    /* The power controller still owns the bottom of the same page. */
    m.bus.write32(c, S5L8900_POWER_BASE + POWER_ONCTRL, 0x4u);
    CHECK(m.gpioic.unknown_writes == 0u,
          "a power-controller store leaked into the interrupt controller");

    /*
     * Assert the touch line and follow it all the way to the CPU. It is driven
     * through the device rather than by calling s5l_gpioic_set_line() directly,
     * because line 155 belongs to the touch controller now: s5l8900_tick()
     * refreshes it from s5l_mtz2_irq() on every tick, so a level poked in by
     * hand would be withdrawn again by the next one.
     */
    m.mtz2.atn = true;
    s5l8900_tick(&m, 0u);
    CHECK((m.vic[0].raw & (1u << 2)) != 0u,
          "group 4 did not raise VIC0 line 2: raw=0x%08x", m.vic[0].raw);
    CHECK(!m.cpu.irq_line, "the line reached the CPU with the VIC masked");

    m.bus.write32(c, S5L8900_VIC0_BASE + VIC_INTENABLE, 1u << 2);
    s5l8900_tick(&m, 0u);
    CHECK(m.cpu.irq_line, "an enabled, pending touch interrupt did not reach "
          "the CPU");

    /* VICADDRESS must name the cascade line, not the GPIO line. */
    CHECK(m.bus.read32(c, S5L8900_VIC0_BASE + VIC_VECTADDR) == (2u | 0x80000000u),
          "VICADDRESS reported 0x%08x for the touch cascade",
          m.bus.read32(c, S5L8900_VIC0_BASE + VIC_VECTADDR));

    /*
     * Acknowledge at the source while it is still driving: the interrupt must
     * STAY DOWN. This is the assertion that changed, and run71 is why. With
     * the old level-re-latch the line came straight back and the guest's
     * filter re-entered every ~419 instructions for 1,193,122 iterations,
     * so AppleMultitouchZ2SPI's work loop never ran and the report it was
     * being told about was never read. The device still has the report; the
     * driver has been told once, which is all it needs.
     */
    m.bus.write32(c, S5L8900_GPIOIC_PAGE + GPIOIC_INTSTAT + 4u * 4u,
                  0x08000000u);
    s5l8900_tick(&m, 0u);
    CHECK(!m.cpu.irq_line,
          "acknowledging a still-asserted source left the line up — that is "
          "the livelock, not a delivered interrupt");
    CHECK(s5l_gpioic_line(&m.gpioic, S5L_GPIOIC_LINE_MULTITOUCH),
          "the source stopped driving just because it was acknowledged");
    /* Withdraw, let the machine propagate it, and only then acknowledge —
     * which is the order a handler that drains the device produces. */
    m.mtz2.atn = false;
    s5l8900_tick(&m, 0u);
    m.bus.write32(c, S5L8900_GPIOIC_PAGE + GPIOIC_INTSTAT + 4u * 4u,
                  0x08000000u);
    s5l8900_tick(&m, 0u);
    CHECK(!m.cpu.irq_line && (m.vic[0].raw & (1u << 2)) == 0u,
          "a withdrawn and acknowledged source left line 2 asserted");

    /* Group 0 cascades to line 33, which is VIC1 bit 1. */
    m.bus.write32(c, S5L8900_GPIOIC_PAGE + GPIOIC_INTEN, 0x00000001u);
    s5l_gpioic_set_line(&m.gpioic, 0u, true);
    s5l8900_tick(&m, 0u);
    CHECK((m.vic[1].raw & (1u << 1)) != 0u,
          "group 0 did not raise VIC1 line 1 (flat 33): vic1 raw=0x%08x",
          m.vic[1].raw);
    CHECK((m.vic[0].raw & (1u << 1)) == 0u,
          "group 0's line 33 was also applied to VIC0 bit 1");
    m.bus.write32(c, S5L8900_VIC1_BASE + VIC_INTENABLE, 1u << 1);
    s5l8900_tick(&m, 0u);
    CHECK(m.cpu.irq_line, "a VIC1 GPIO group could not reach the CPU");

    s5l8900_free(&m);
}

/* The two windows are device windows now, so neither may still be a stub and
 * nothing may be shadowed. */
static void test_windows_replace_the_two_stubs(void) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, 0u, 1u << 16), "machine init failed");
    CHECK(m.stub_declare_failures == 0u,
          "%u stub declarations were refused — a window meant to be named is "
          "unmapped instead", m.stub_declare_failures);

    for (unsigned i = 0; i < m.stub_count; i++)
        CHECK(strcmp(m.stubs[i].name, "gpio") != 0 &&
              strcmp(m.stubs[i].name, "gpioic") != 0,
              "%s is still a storage stub as well as a model", m.stubs[i].name);

    s5l_window_t w[S5L_WINDOW_MAX];
    unsigned n = s5l8900_windows(&m, w, S5L_WINDOW_MAX);
    CHECK(n <= S5L_WINDOW_MAX,
          "%u windows exist but the buffer bound is %u — a window is invisible",
          n, (unsigned)S5L_WINDOW_MAX);
    bool saw_gpio = false, saw_gpioic = false;
    for (unsigned i = 0; i < n && i < S5L_WINDOW_MAX; i++) {
        if (!strcmp(w[i].name, "gpio"))   saw_gpio = true;
        if (!strcmp(w[i].name, "gpioic")) saw_gpioic = true;
        for (unsigned j = 0; j < i; j++)
            CHECK(!s5l8900_overlaps(w[i].base, w[i].size, w[j].base, w[j].size),
                  "%s overlaps %s", w[i].name, w[j].name);
    }
    CHECK(saw_gpio && saw_gpioic, "a GPIO window is not on the bus");
    s5l8900_free(&m);
}

/* The store the platform expert makes to drive one pin. */
static uint32_t fsel_word(unsigned group, unsigned bit, unsigned level) {
    return ((uint32_t)group << 16) | ((uint32_t)bit << 8) | 0xeu | (level & 1u);
}

/*
 * The pin block, and the asymmetry that is the whole point of it: a pin is READ
 * at group*32 + 4 and DRIVEN by a single word store to fsel at 0x320. An
 * earlier revision of the specification for this model had writes going to the
 * level register, which would have produced a model in which the guest's real
 * store — the only offset on this page run59 ever saw it touch — changed
 * nothing and no watcher ever fired, silently.
 */
static void test_pins_are_read_at_group32_and_driven_through_fsel(void) {
    s5l_gpio_t p;
    s5l_gpio_reset(&p);

    /* The four ids this machine actually cares about, from the device tree. */
    struct { uint16_t pin; unsigned group, bit; const char *what; } K[] = {
        { 0x0606u, 6u,  6u, "multi-touch function-reset"     },
        { 0x0701u, 7u,  1u, "multi-touch function-power_ldo" },
        { 0x1800u, 24u, 0u, "spi1 function-spi_cs0"          },
        { 0x0001u, 0u,  1u, "lcd0 function-reset"            },
    };
    for (unsigned i = 0; i < sizeof K / sizeof K[0]; i++) {
        uint32_t off = S5L_GPIO_PIN_REG(K[i].group);
        CHECK(off == K[i].group * 32u + 4u,
              "%s: pin register offset arithmetic changed", K[i].what);
        CHECK((unsigned)(K[i].pin >> 8) == K[i].group &&
              (unsigned)(K[i].pin & 0xffu) == K[i].bit,
              "%s: 0x%04x does not split as group %u bit %u",
              K[i].what, K[i].pin, K[i].group, K[i].bit);

        s5l_gpio_reset(&p);
        s5l_gpio_write(&p, S5L_GPIO_FSEL, fsel_word(K[i].group, K[i].bit, 1u),
                       4u);
        CHECK(s5l_gpio_pin(&p, K[i].pin),
              "%s (0x%04x) was not driven high by an fsel store",
              K[i].what, K[i].pin);
        CHECK(s5l_gpio_read(&p, off, 4u) == (1u << K[i].bit),
              "%s: the level register at 0x%03x reads 0x%08x",
              K[i].what, off, s5l_gpio_read(&p, off, 4u));
        s5l_gpio_write(&p, S5L_GPIO_FSEL, fsel_word(K[i].group, K[i].bit, 0u),
                       4u);
        CHECK(!s5l_gpio_pin(&p, K[i].pin), "%s was not driven low", K[i].what);

        /* The level register is read-only: a direct store must change nothing,
         * or the model could not tell which path the guest actually uses. */
        s5l_gpio_write(&p, off, 0xffffffffu, 4u);
        CHECK(!s5l_gpio_pin(&p, K[i].pin) && s5l_gpio_read(&p, off, 4u) == 0u,
              "%s: a direct store to the level register drove the pin",
              K[i].what);
        /* And nothing reads its level from group*32. */
        s5l_gpio_write(&p, K[i].group * 32u, 0xffffffffu, 4u);
        CHECK(!s5l_gpio_pin(&p, K[i].pin),
              "%s read its level from group*32 instead of group*32+4",
              K[i].what);
    }

    /* Groups 0..24 exist; 25 does not, and neither does bit 32. */
    s5l_gpio_reset(&p);
    for (unsigned grp = 0; grp < S5L_GPIO_PORTS; grp++) {
        s5l_gpio_write(&p, S5L_GPIO_FSEL, fsel_word(grp, 2u, 1u), 4u);
        CHECK(s5l_gpio_pin(&p, (uint16_t)((grp << 8) | 2u)),
              "group %u bit 2 was not driven", grp);
    }
    CHECK(!s5l_gpio_pin(&p, (uint16_t)((S5L_GPIO_PORTS << 8) | 2u)),
          "a group past #gpio-ports answered");
    s5l_gpio_write(&p, S5L_GPIO_FSEL, fsel_word(S5L_GPIO_PORTS, 2u, 1u), 4u);
    s5l_gpio_write(&p, S5L_GPIO_FSEL, fsel_word(3u, 32u, 1u), 4u);
    CHECK(s5l_gpio_read(&p, S5L_GPIO_PIN_REG(3u), 4u) == 0x00000004u,
          "an out-of-range group or bit wrapped into a real pin: 0x%08x",
          s5l_gpio_read(&p, S5L_GPIO_PIN_REG(3u), 4u));

    /* A function nibble that is not a driven output changes no pin. */
    s5l_gpio_reset(&p);
    s5l_gpio_write(&p, S5L_GPIO_FSEL, (6u << 16) | (6u << 8) | 0x1u, 4u);
    CHECK(!s5l_gpio_pin(&p, 0x0606u),
          "a non-output function nibble drove a pin");
    /* Nor does a partial store, which cannot carry group, bit and function. */
    s5l_gpio_write(&p, S5L_GPIO_FSEL, 0x0fu, 1u);
    CHECK(!s5l_gpio_pin(&p, 0x0606u), "a byte store to fsel drove a pin");

    /* Honest storage over the rest of the page, at byte granularity, including
     * fsel itself — which the storage stub this replaces used to hold. */
    s5l_gpio_reset(&p);
    s5l_gpio_write(&p, S5L_GPIO_FSEL, 0xdeadbeefu, 4u);
    CHECK(s5l_gpio_read(&p, S5L_GPIO_FSEL, 4u) == 0xdeadbeefu,
          "fsel did not read back");
    CHECK(s5l_gpio_read(&p, S5L_GPIO_FSEL + 1u, 1u) == 0xbeu,
          "a byte read of the middle of a word gave 0x%02x",
          s5l_gpio_read(&p, S5L_GPIO_FSEL + 1u, 1u));
    s5l_gpio_write(&p, 0x800u, 0x1234u, 2u);
    CHECK(s5l_gpio_read(&p, 0x800u, 4u) == 0x00001234u,
          "a halfword store hit the wrong lanes: 0x%08x",
          s5l_gpio_read(&p, 0x800u, 4u));

    /* Nothing may index past the page. */
    CHECK(s5l_gpio_read(&p, S5L8900_DEV_SIZE - 1u, 4u) == 0u,
          "an access straddling the end of the page was served");
    s5l_gpio_write(&p, S5L8900_DEV_SIZE - 1u, 0xffffffffu, 4u);
    CHECK(s5l_gpio_read(&p, S5L8900_DEV_SIZE - 4u, 4u) == 0u,
          "an out-of-range store wrote inside the page");
}

typedef struct { unsigned calls; bool last; } pinwatch_t;
static void pinwatch_cb(void *ctx, bool level) {
    pinwatch_t *w = ctx;
    w->calls++;
    w->last = level;
}

/*
 * A device watches its own reset line. It must see the EDGE: the Z2 leaves its
 * bootloader on a reset pulse, and a model that only offered the level would
 * find the pin already back where it started by the time the next probe
 * arrived.
 */
static void test_pin_watch_reports_changes_and_only_changes(void) {
    s5l_gpio_t p;
    pinwatch_t w;
    s5l_gpio_reset(&p);
    memset(&w, 0, sizeof w);

    CHECK(!s5l_gpio_watch(&p, 0x0606u, &w, NULL), "a NULL callback was armed");
    CHECK(!s5l_gpio_watch(&p, (uint16_t)(S5L_GPIO_PORTS << 8), &w, pinwatch_cb),
          "a pin outside #gpio-ports was armed");
    CHECK(!s5l_gpio_watch(&p, 0x06ffu, &w, pinwatch_cb),
          "a bit outside a 32-bit register was armed");
    CHECK(s5l_gpio_watch(&p, 0x0606u, &w, pinwatch_cb), "watch refused");
    CHECK(!s5l_gpio_watch(&p, 0x0606u, &w, pinwatch_cb),
          "the same pin was watched twice — one subscriber would be silent");
    for (unsigned i = 1; i < S5L_GPIO_WATCHERS; i++)
        CHECK(s5l_gpio_watch(&p, (uint16_t)(0x0600u + i), &w, pinwatch_cb),
              "watcher %u refused", i);
    CHECK(!s5l_gpio_watch(&p, 0x0a0au, &w, pinwatch_cb),
          "a full watch table accepted another subscriber");

    /* Assert, deassert, and a store that changes nothing. */
    s5l_gpio_write(&p, S5L_GPIO_FSEL, fsel_word(6u, 6u, 1u), 4u);
    CHECK(w.calls == 1u && w.last, "the rising edge was not reported (%u)",
          w.calls);
    s5l_gpio_write(&p, S5L_GPIO_FSEL, fsel_word(6u, 6u, 1u), 4u);
    CHECK(w.calls == 1u, "a store that changed nothing was reported");
    s5l_gpio_write(&p, S5L_GPIO_FSEL, fsel_word(6u, 6u, 0u), 4u);
    CHECK(w.calls == 2u && !w.last, "the falling edge was not reported");

    /* Another pin in the same group must not report as this one. */
    s5l_gpio_write(&p, S5L_GPIO_FSEL, fsel_word(6u, 9u, 1u), 4u);
    CHECK(w.calls == 2u, "a different bit of the same group was reported");
    /* Nor another group. */
    s5l_gpio_write(&p, S5L_GPIO_FSEL, fsel_word(7u, 6u, 1u), 4u);
    CHECK(w.calls == 2u, "an unrelated group's store was reported");

    /* Watchers on other pins are independent and all fire. */
    s5l_gpio_write(&p, S5L_GPIO_FSEL, fsel_word(6u, 1u, 1u), 4u);
    CHECK(w.calls == 3u, "a second watched pin did not report");

    /* Reset drops the subscriptions with everything else. */
    s5l_gpio_reset(&p);
    s5l_gpio_write(&p, S5L_GPIO_FSEL, fsel_word(6u, 6u, 1u), 4u);
    CHECK(w.calls == 3u, "reset kept a stale subscription alive");
}

/* Both blocks are guest state and must survive a checkpoint. */
static void test_snapshot_carries_both_blocks(void) {
    s5l8900_t src, dst;
    CHECK(s5l8900_init(&src, 0u, 1u << 16), "source init failed");
    CHECK(s5l8900_init(&dst, 0u, 1u << 16), "destination init failed");

    src.bus.write32(src.bus.ctx,
                    S5L8900_GPIOIC_PAGE + GPIOIC_INTEN + 4u * 4u, 0x08000000u);
    src.bus.write32(src.bus.ctx,
                    S5L8900_GPIOIC_PAGE + GPIOIC_INTLEVEL + 4u * 2u, 0x55u);
    src.bus.write32(src.bus.ctx,
                    S5L8900_GPIOIC_PAGE + GPIOIC_INTTYPE + 4u * 3u, 0xaau);
    s5l_gpioic_set_line(&src.gpioic, S5L_GPIOIC_LINE_MULTITOUCH, true);
    src.bus.write32(src.bus.ctx, S5L8900_GPIO_BASE + S5L_GPIO_FSEL,
                    fsel_word(6u, 6u, 1u));

    uint8_t *blob = NULL;
    size_t blob_len = 0;
    CHECK(snapshot_save_mem(&src, &blob, &blob_len) == SNAP_OK,
          "could not save");
    CHECK(snapshot_load_mem(&dst, blob, blob_len) == SNAP_OK,
          "could not restore");

    CHECK(memcmp(dst.gpioic.en, src.gpioic.en, sizeof src.gpioic.en) == 0 &&
          memcmp(dst.gpioic.stat, src.gpioic.stat, sizeof src.gpioic.stat) == 0 &&
          memcmp(dst.gpioic.level, src.gpioic.level, sizeof src.gpioic.level) == 0 &&
          memcmp(dst.gpioic.type, src.gpioic.type, sizeof src.gpioic.type) == 0,
          "the interrupt controller's registers did not survive");
    CHECK(memcmp(dst.gpioic.raw, src.gpioic.raw, sizeof src.gpioic.raw) == 0,
          "the asserted source did not survive — the next acknowledge would "
          "clear it for good instead of re-latching");
    CHECK(memcmp(dst.gpio.regs, src.gpio.regs, sizeof src.gpio.regs) == 0,
          "the pin block did not survive");

    /* And the restored machine still routes it. */
    dst.bus.write32(dst.bus.ctx, S5L8900_VIC0_BASE + VIC_INTENABLE, 1u << 2);
    s5l8900_tick(&dst, 0u);
    CHECK(dst.cpu.irq_line,
          "the restored machine did not re-raise the pending touch interrupt");

    free(blob);
    s5l8900_free(&src);
    s5l8900_free(&dst);
}

/*
 * A file is untrusted input, so a count the model can never reach must be
 * refused on the way IN. The bound matters: unknown_off_count indexes a fixed
 * array, and a restored machine that believed a larger one would walk past it
 * on the next unknown access.
 */
static void test_snapshot_rejects_impossible_gpioic_state(void) {
    s5l8900_t src, dst;
    uint8_t *blob = NULL;
    size_t blob_len = 0;
    CHECK(s5l8900_init(&src, 0u, 1u << 16), "source init failed");
    CHECK(s5l8900_init(&dst, 0u, 1u << 16), "destination init failed");

    src.gpioic.unknown_off_count = S5L_GPIOIC_UNKNOWN_OFF + 1u;
    CHECK(snapshot_save_mem(&src, &blob, &blob_len) == SNAP_OK,
          "could not save the poisoned machine");
    if (blob) {
        CHECK(snapshot_load_mem(&dst, blob, blob_len) == SNAP_ERR_CORRUPT,
              "an overflowed unknown-offset count was restored");
        free(blob);
    }
    s5l8900_free(&dst);
    s5l8900_free(&src);
}

/*
 * The wake table must declare all seven cascade lines, and must declare them as
 * VIC lines. An entry written as { "multitouch", 155, ... } silently answers
 * "cannot wake" for the whole life of the machine, because wake_line_enabled()
 * rejects anything at or above 32*S5L8900_VIC_COUNT and says nothing.
 */
static void test_wake_sources_declare_the_cascade_not_line_155(void) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, 0u, 1u << 16), "machine init failed");

    const s5l_wake_source_t *src = NULL;
    unsigned n = s5l8900_wake_sources(&src);
    for (unsigned g = 0; g < S5L_GPIOIC_GROUPS; g++) {
        const s5l_wake_source_t *found = NULL;
        for (unsigned i = 0; i < n; i++)
            if (src[i].line == s5l_gpioic_cascade(g) &&
                strncmp(src[i].name, "gpio", 4) == 0) found = &src[i];
        CHECK(found != NULL,
              "no wake source declares group %u's VIC line %u",
              g, s5l_gpioic_cascade(g));
        if (found) {
            CHECK(found->next_edge != NULL, "group %u's source is silent", g);
            uint32_t at = 0xdeadbeefu;
            CHECK(found->next_edge(&m, &at) == S5L_WAKE_NEVER,
                  "group %u claimed a future edge it cannot have", g);
            CHECK(at == 0xdeadbeefu,
                  "group %u wrote a tick count while answering NEVER", g);
        }
    }
    for (unsigned i = 0; i < n; i++)
        CHECK(src[i].line < 32u * S5L8900_VIC_COUNT,
              "wake source %s declares line %u, which wake_line_enabled() "
              "rejects silently", src[i].name, src[i].line);

    /* And they must not degrade the reduction. */
    m.vic[0].enable = 0xffffffffu;
    m.vic[1].enable = 0xffffffffu;
    m.timer.t4_state = TIMER4_STATE_START;
    m.timer.t4_count = m.timer.t4_value = 40u;
    uint32_t at = 0xdeadbeefu;
    CHECK(s5l8900_next_wake(&m, src, n, &at) == S5L_WAKE_AT && at == 40u,
          "the GPIO sources changed the reduction: ticks=%u", at);

    s5l8900_free(&m);
}

int main(void) {
    printf("S5LBox S5L8900 GPIO interrupt controller tests\n");
    test_reset_is_total_and_null_safe();
    test_cascade_is_the_device_trees_own_array();
    test_register_file_decodes_four_banks_of_seven();
    test_intstat_is_write_one_to_clear_not_write_one_to_set();
    test_line_155_is_group_four_bit_twenty_seven();
    test_group_output_is_pending_and_enabled();
    test_the_drivers_own_enable_sequence();
    test_machine_routes_the_cascade_to_both_vics();
    test_windows_replace_the_two_stubs();
    test_pins_are_read_at_group32_and_driven_through_fsel();
    test_pin_watch_reports_changes_and_only_changes();
    test_snapshot_carries_both_blocks();
    test_snapshot_rejects_impossible_gpioic_state();
    test_wake_sources_declare_the_cascade_not_line_155();
    printf("  %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
