/*
 * S5LBox — the five physical buttons, and the level-sensitive GPIO lines they
 * arrive on.
 *
 * The property that matters most here is not that a bit changes. It is that
 * NOTHING IS ASSERTED AT REST. Two of the five buttons are wired active low, so
 * the pin block's power-on zero is a volume key held down, and the moment the
 * guest programs INTLEVEL for a level-sensitive line that becomes an interrupt
 * storm on a machine where nobody has touched anything. Several tests below
 * exist only to make that fail loudly.
 *
 * The second is that a press and a release each produce EXACTLY ONE interrupt,
 * through the guest's own auto-flip sequence rather than through anything this
 * model invents. test_the_guests_own_press_and_release_sequence() replays
 * AppleS5L8900XGPIOIC's real instruction order — configure, enable, then per
 * interrupt: read status, flip INTLEVEL, run the handler, acknowledge — and a
 * model that gets the order wrong either loses the release or rebuilds run71's
 * livelock on five more lines.
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

/*
 * THE DEVICE TREE, TRANSCRIBED HERE INDEPENDENTLY of the model's own table, so
 * that changing the model is a test failure rather than a silently agreed
 * mistake. From /device-tree/buttons in the shipped 40,544-byte tree:
 *
 *   button-names 'hold\0menu\0volup\0voldown\0ringerab\0'
 *   interrupts   {45,7} {40,7} {41,5} {42,5} {43,7}
 *   function-button_hold     {..., 'GPIO', 0x1605, 0x00000100}
 *   function-button_menu     {..., 'GPIO', 0x1600, 0x00000100}
 *   function-button_volup    {..., 'GPIO', 0x1601, 0x00000000}
 *   function-button_voldown  {..., 'GPIO', 0x1602, 0x00000000}
 *   function-button_ringerab {..., 'GPIO', 0x1603, 0x00010000}
 */
static const struct {
    const char *name;
    uint16_t    pin;
    unsigned    line;
    unsigned    cell;      /* the second interrupt cell */
    uint32_t    arg;       /* the function property's fourth word */
} DT[S5L_BUTTON_COUNT] = {
    { "hold",     0x1605u, 45u, 7u, 0x00000100u },
    { "menu",     0x1600u, 40u, 7u, 0x00000100u },
    { "volup",    0x1601u, 41u, 5u, 0x00000000u },
    { "voldown",  0x1602u, 42u, 5u, 0x00000000u },
    { "ringerab", 0x1603u, 43u, 7u, 0x00010000u },
};

/* Byte 1 of the argument word, which AppleS5L8900XGPIOFunction reads at
 * 0xc05a45dc and uses as the pin's active level and nothing else. */
static bool dt_active_high(unsigned i) { return ((DT[i].arg >> 8) & 0xffu) != 0u; }

/* ------------------------------------------------------------------------- */

static void test_wiring_is_the_device_trees_own(void) {
    for (unsigned i = 0; i < S5L_BUTTON_COUNT; i++) {
        CHECK(strcmp(s5l_button_name(i), DT[i].name) == 0,
              "button %u is '%s', the tree's name %u is '%s' — the table is "
              "out of order, and index IS the interrupt index the driver asks "
              "its provider for", i, s5l_button_name(i), i, DT[i].name);
        CHECK(s5l_button_pin(i) == DT[i].pin,
              "%s pin 0x%04x, tree says 0x%04x",
              DT[i].name, s5l_button_pin(i), DT[i].pin);
        CHECK(s5l_button_line(i) == DT[i].line,
              "%s line %u, tree says %u",
              DT[i].name, s5l_button_line(i), DT[i].line);
        CHECK(s5l_button_active_high(i) == dt_active_high(i),
              "%s active_high %d, the tree's polarity byte says %d",
              DT[i].name, s5l_button_active_high(i), dt_active_high(i));
        /* Every pin is port 22, and the bit is the pin's low byte. */
        CHECK((s5l_button_pin(i) >> 8) == 22u,
              "%s is not on gpio port 22 but %u",
              DT[i].name, (unsigned)(s5l_button_pin(i) >> 8));
        /* Every line is group 1, which cascades to VIC line 32 — VIC1. */
        CHECK(s5l_button_line(i) / 32u == 1u,
              "%s line %u is not in interrupt group 1",
              DT[i].name, s5l_button_line(i));
    }
    CHECK(s5l_gpioic_cascade(1u) == 32u,
          "group 1 no longer cascades to VIC line 32 but %u — every button "
          "would be routed to a line nobody enabled", s5l_gpioic_cascade(1u));

    /* The exact enable word run86 measured, rebuilt from the tree's own line
     * numbers. INTEN group 1 settled at 0x00002f00 at instruction
     * 238,689,154; anything else here means a line number moved. */
    uint32_t mask = 0;
    for (unsigned i = 0; i < S5L_BUTTON_COUNT; i++)
        mask |= 1u << (s5l_button_line(i) & 31u);
    CHECK(mask == 0x00002f00u,
          "the five lines make group-1 mask 0x%08x, but run86 measured the "
          "guest arming 0x00002f00", mask);

    /* No two buttons share a pin or a line. */
    for (unsigned a = 0; a < S5L_BUTTON_COUNT; a++)
        for (unsigned b = a + 1u; b < S5L_BUTTON_COUNT; b++) {
            CHECK(s5l_button_pin(a) != s5l_button_pin(b),
                  "%s and %s share pin 0x%04x",
                  DT[a].name, DT[b].name, s5l_button_pin(a));
            CHECK(s5l_button_line(a) != s5l_button_line(b),
                  "%s and %s share line %u",
                  DT[a].name, DT[b].name, s5l_button_line(a));
        }

    /* Out of range must not alias onto a real button. Group 0xff is past
     * #gpio-ports and the line is past the controller's 224. */
    CHECK((unsigned)(s5l_button_pin(S5L_BUTTON_COUNT) >> 8) >= S5L_GPIO_PORTS,
          "a button that does not exist named a real gpio port");
    CHECK(s5l_button_line(S5L_BUTTON_COUNT) >= S5L_GPIOIC_LINES,
          "a button that does not exist named a real interrupt line");
    CHECK(!s5l_button_active_high(S5L_BUTTON_COUNT) &&
          !s5l_button_level(S5L_BUTTON_COUNT, true),
          "an out-of-range button claimed an active-high level, which is the "
          "direction that invents a press");
    CHECK(strcmp(s5l_button_name(999u), "?") == 0, "999 was given a name");
}

/*
 * The interrupt cells the guest will program, checked against the two registers
 * run86 measured. This is the decode the whole level-line model rests on:
 * cell bit 0 -> INTTYPE, cell bit 1 -> INTLEVEL.
 */
static void test_the_measured_interrupt_configuration(void) {
    uint32_t type = 0, level = 0;
    for (unsigned i = 0; i < S5L_BUTTON_COUNT; i++) {
        unsigned bit = DT[i].line & 31u;
        type  |= (DT[i].cell & 1u) << bit;
        level |= ((DT[i].cell >> 1) & 1u) << bit;
        /* Every one of the five is level-sensitive AND auto-flip. A momentary
         * button cannot report its release without both. */
        CHECK((DT[i].cell & 1u) != 0u,
              "%s is not level-triggered; its release could never be seen",
              DT[i].name);
        CHECK((DT[i].cell & 4u) != 0u,
              "%s is not auto-flip; the guest would never invert its polarity "
              "and only one of press/release would ever interrupt", DT[i].name);
    }
    CHECK(type == 0x00002f00u,
          "the cells make INTTYPE group 1 = 0x%08x; run86 measured 0x00002f00",
          type);
    CHECK(level == 0x00002900u,
          "the cells make INTLEVEL group 1 = 0x%08x; run86 measured 0x00002900",
          level);

    /*
     * AND THE TWO INDEPENDENT POLARITY FIELDS AGREE. The initial INTLEVEL bit
     * says which level asserts; the function property's byte 1 says which level
     * the platform function calls asserted. They come from different properties
     * and are read by different code, and for the four momentary keys they must
     * be the same or one of the two decodes is wrong.
     */
    for (unsigned i = 0; i < S5L_BUTTON_COUNT; i++) {
        if (i == S5L_BUTTON_RINGERAB) continue;   /* a slider has no rest */
        bool from_intlevel = ((DT[i].cell >> 1) & 1u) != 0u;
        CHECK(from_intlevel == dt_active_high(i),
              "%s: INTLEVEL says asserted-at-%s but the polarity byte says "
              "active %s — the two decodes disagree and one is wrong",
              DT[i].name, from_intlevel ? "high" : "low",
              dt_active_high(i) ? "high" : "low");
    }
}

/*
 * NOTHING IS ASSERTED AT REST, and the volume keys are why this is a test
 * rather than an observation. They are active low, so a machine that left them
 * at the pin block's power-on zero would hand the guest two keys held down for
 * the whole boot the instant it configured them.
 */
static void test_rest_asserts_nothing(void) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, 0u, 1u << 16), "machine init failed");

    for (unsigned i = 0; i < S5L_BUTTON_COUNT; i++) {
        CHECK(!s5l_buttons_held(&m.buttons, i),
              "%s was held at reset", DT[i].name);
        /* The rest LEVEL is the complement of the level a press produces. */
        bool rest = s5l_gpio_pin(&m.gpio, s5l_button_pin(i));
        CHECK(rest == s5l_button_level(i, false),
              "%s rests at pin level %d, expected %d",
              DT[i].name, rest, s5l_button_level(i, false));
        CHECK(rest != s5l_button_level(i, true),
              "%s produces the same pin level pressed and released", DT[i].name);
        CHECK(s5l_gpioic_line(&m.gpioic, s5l_button_line(i)) == rest,
              "%s drives its interrupt line and its pin differently",
              DT[i].name);
    }

    /* The two active-low keys really do rest HIGH, by literal, because that is
     * the whole point and an accidental table edit would still be "consistent". */
    CHECK(s5l_gpio_pin(&m.gpio, 0x1601u) && s5l_gpio_pin(&m.gpio, 0x1602u),
          "the volume keys do not rest high — they are active low, so the pin "
          "block's power-on zero reads as both of them held down");
    CHECK(!s5l_gpio_pin(&m.gpio, 0x1600u) && !s5l_gpio_pin(&m.gpio, 0x1605u),
          "Home or Hold rests high — they are active high, so that is a press");

    /* And the pin word really is the one the guest reads: port 22 -> 0x2c4. */
    CHECK(S5L_GPIO_PIN_REG(22u) == 0x2c4u,
          "port 22's level register is at 0x%03x, not the 0x2c4 the guest's "
          "own accessor computes", S5L_GPIO_PIN_REG(22u));
    CHECK(s5l_gpio_read(&m.gpio, 0x2c4u, 4u) == 0x00000006u,
          "the resting port-22 word is 0x%08x, expected 0x00000006 (the two "
          "active-low volume keys high, everything else low)",
          s5l_gpio_read(&m.gpio, 0x2c4u, 4u));

    /* Now let the guest configure and arm all five exactly as it does, and
     * require that still nothing has latched. */
    for (unsigned i = 0; i < S5L_BUTTON_COUNT; i++) {
        unsigned g = DT[i].line >> 5, b = DT[i].line & 31u;
        uint32_t t = s5l_gpioic_read(&m.gpioic, GPIOIC_INTTYPE + 4u * g);
        s5l_gpioic_write(&m.gpioic, GPIOIC_INTTYPE + 4u * g,
                         (t & ~(1u << b)) | ((DT[i].cell & 1u) << b));
        uint32_t l = s5l_gpioic_read(&m.gpioic, GPIOIC_INTLEVEL + 4u * g);
        s5l_gpioic_write(&m.gpioic, GPIOIC_INTLEVEL + 4u * g,
                         (l & ~(1u << b)) | (((DT[i].cell >> 1) & 1u) << b));
        s5l_gpioic_write(&m.gpioic, GPIOIC_INTSTAT + 4u * g, 1u << b);
        uint32_t e = s5l_gpioic_read(&m.gpioic, GPIOIC_INTEN + 4u * g);
        s5l_gpioic_write(&m.gpioic, GPIOIC_INTEN + 4u * g, e | (1u << b));
    }
    s5l8900_tick(&m, 0u);
    CHECK(s5l_gpioic_read(&m.gpioic, GPIOIC_INTSTAT + 4u) == 0u,
          "arming the five buttons latched 0x%08x with nothing pressed — that "
          "is a phantom key held down for the whole boot",
          s5l_gpioic_read(&m.gpioic, GPIOIC_INTSTAT + 4u));
    CHECK(!s5l_gpioic_group_irq(&m.gpioic, 1u),
          "group 1 asserted with nothing pressed");
    CHECK(!m.cpu.irq_line, "an untouched machine raised an interrupt");

    /* The measured registers, end to end through the model. */
    CHECK(s5l_gpioic_read(&m.gpioic, GPIOIC_INTTYPE + 4u) == 0x00002f00u &&
          s5l_gpioic_read(&m.gpioic, GPIOIC_INTLEVEL + 4u) == 0x00002900u &&
          s5l_gpioic_read(&m.gpioic, GPIOIC_INTEN + 4u) == 0x00002f00u,
          "the guest's own configure/enable sequence did not reproduce run86: "
          "type=0x%08x level=0x%08x en=0x%08x",
          s5l_gpioic_read(&m.gpioic, GPIOIC_INTTYPE + 4u),
          s5l_gpioic_read(&m.gpioic, GPIOIC_INTLEVEL + 4u),
          s5l_gpioic_read(&m.gpioic, GPIOIC_INTEN + 4u));

    s5l8900_free(&m);
}

/* Bring a machine to the state the guest leaves it in: all five configured and
 * armed. Shared, because every behavioural test below starts there. */
static void arm_all(s5l8900_t *m) {
    for (unsigned i = 0; i < S5L_BUTTON_COUNT; i++) {
        unsigned g = DT[i].line >> 5, b = DT[i].line & 31u;
        uint32_t t = s5l_gpioic_read(&m->gpioic, GPIOIC_INTTYPE + 4u * g);
        s5l_gpioic_write(&m->gpioic, GPIOIC_INTTYPE + 4u * g,
                         (t & ~(1u << b)) | ((DT[i].cell & 1u) << b));
        uint32_t l = s5l_gpioic_read(&m->gpioic, GPIOIC_INTLEVEL + 4u * g);
        s5l_gpioic_write(&m->gpioic, GPIOIC_INTLEVEL + 4u * g,
                         (l & ~(1u << b)) | (((DT[i].cell >> 1) & 1u) << b));
        s5l_gpioic_write(&m->gpioic, GPIOIC_INTSTAT + 4u * g, 1u << b);
        uint32_t e = s5l_gpioic_read(&m->gpioic, GPIOIC_INTEN + 4u * g);
        s5l_gpioic_write(&m->gpioic, GPIOIC_INTEN + 4u * g, e | (1u << b));
    }
}

/* Configure the PMU interrupt exactly as `/arm-io/i2c0/pmu` describes it:
 * flat GPIOIC line 85, cell 1 (level-sensitive, active low), cascading from
 * group 2 to VIC0 line 31. The physical clean-shutdown checkpoint has this
 * same bit configured and enabled before the retained reset vector runs. */
static void arm_pmu_irq(s5l8900_t *m) {
    CHECK(S5L_GPIOIC_LINE_PMU == 85u,
          "PMU line drifted from the device tree: %u", S5L_GPIOIC_LINE_PMU);
    unsigned group = S5L_GPIOIC_LINE_PMU >> 5;
    unsigned bit = S5L_GPIOIC_LINE_PMU & 31u;
    uint32_t mask = 1u << bit;

    uint32_t type = s5l_gpioic_read(&m->gpioic,
                                    GPIOIC_INTTYPE + 4u * group);
    s5l_gpioic_write(&m->gpioic, GPIOIC_INTTYPE + 4u * group,
                     type | mask);
    uint32_t level = s5l_gpioic_read(&m->gpioic,
                                     GPIOIC_INTLEVEL + 4u * group);
    s5l_gpioic_write(&m->gpioic, GPIOIC_INTLEVEL + 4u * group,
                     level & ~mask);
    s5l_gpioic_write(&m->gpioic, GPIOIC_INTSTAT + 4u * group, mask);
    uint32_t enable = s5l_gpioic_read(&m->gpioic,
                                      GPIOIC_INTEN + 4u * group);
    s5l_gpioic_write(&m->gpioic, GPIOIC_INTEN + 4u * group,
                     enable | mask);

    unsigned cascade = s5l_gpioic_cascade(group);
    CHECK(cascade == 31u, "PMU group cascades to VIC line %u", cascade);
    s5l_vic_write(&m->vic[cascade >> 5], VIC_INTENABLE,
                  1u << (cascade & 31u));
    m->level_dirty = true;
    s5l8900_tick(m, 0u);

    CHECK((m->gpioic.driven[group] & mask) != 0u &&
          s5l_gpioic_line(&m->gpioic, S5L_GPIOIC_LINE_PMU),
          "idle PMU did not drive INT_N high");
    CHECK(!s5l_gpioic_pending(&m->gpioic, S5L_GPIOIC_LINE_PMU) &&
          !m->cpu.irq_line,
          "inactive PMU interrupt asserted at rest");
}

/*
 * The guest's own service routine for one auto-flip level line, in
 * AppleS5L8900XGPIOIC::handleInterrupt's real instruction order:
 *
 *   read INTSTAT            0xc05a430c
 *   (no early ack: level)   0xc05a4340 is conditional on the edge shadow
 *   INTLEVEL ^= 1 << bit    0xc05a4384   <- the auto-flip
 *   child handler           0xc05a43d0
 *   write INTSTAT 1 << bit  0xc05a4418   <- the late ack
 *
 * Returns whether the line was pending when it started, i.e. whether the guest
 * would have been interrupted at all.
 */
static bool guest_services(s5l8900_t *m, unsigned line) {
    unsigned g = line >> 5, b = line & 31u;
    uint32_t stat = s5l_gpioic_read(&m->gpioic, GPIOIC_INTSTAT + 4u * g);
    if (!(stat & (1u << b))) return false;
    uint32_t lvl = s5l_gpioic_read(&m->gpioic, GPIOIC_INTLEVEL + 4u * g);
    s5l_gpioic_write(&m->gpioic, GPIOIC_INTLEVEL + 4u * g, lvl ^ (1u << b));
    s5l_gpioic_write(&m->gpioic, GPIOIC_INTSTAT + 4u * g, 1u << b);
    return true;
}

/*
 * A PRESS AND A RELEASE, ONE INTERRUPT EACH, through the guest's own sequence.
 * This is the test the whole level-line change exists for.
 */
static void test_the_guests_own_press_and_release_sequence(void) {
    for (unsigned i = 0; i < S5L_BUTTON_COUNT; i++) {
        s5l8900_t m;
        CHECK(s5l8900_init(&m, 0u, 1u << 16), "machine init failed");
        arm_all(&m);
        unsigned line = DT[i].line;

        CHECK(s5l_buttons_set(&m.buttons, &m.gpio, &m.gpioic, i, true),
              "%s: an armed line refused a press", DT[i].name);
        CHECK(s5l_gpioic_pending(&m.gpioic, line),
              "%s: a press did not latch its line", DT[i].name);
        /* And ONLY its line. */
        CHECK(s5l_gpioic_read(&m.gpioic, GPIOIC_INTSTAT + 4u) ==
              (1u << (line & 31u)),
              "%s: a press latched 0x%08x, not just bit %u",
              DT[i].name, s5l_gpioic_read(&m.gpioic, GPIOIC_INTSTAT + 4u),
              line & 31u);
        /* The pin the driver will read says pressed. */
        CHECK(s5l_gpio_pin(&m.gpio, DT[i].pin) == s5l_button_level(i, true),
              "%s: the pin does not read as pressed", DT[i].name);

        /* The guest services it. The acknowledge must STICK — the auto-flip
         * has already made the asserting condition false. */
        CHECK(guest_services(&m, line), "%s: the guest saw no interrupt",
              DT[i].name);
        s5l8900_tick(&m, 0u);
        CHECK(!s5l_gpioic_pending(&m.gpioic, line),
              "%s: the acknowledge did not stick while the button was still "
              "held — that is run71's livelock on a button", DT[i].name);
        /* Still held, and the pin still says so. A driver that samples 14 ms
         * later must find it down. */
        CHECK(s5l_buttons_held(&m.buttons, i) &&
              s5l_gpio_pin(&m.gpio, DT[i].pin) == s5l_button_level(i, true),
              "%s: servicing the interrupt released the button", DT[i].name);

        /* RELEASE. The flipped polarity is what catches it. */
        CHECK(s5l_buttons_set(&m.buttons, &m.gpio, &m.gpioic, i, false),
              "%s: the release was refused", DT[i].name);
        CHECK(s5l_gpioic_pending(&m.gpioic, line),
              "%s: THE RELEASE PRODUCED NO INTERRUPT. Without the auto-flip "
              "being modelled, one wire reports only one of the two edges and "
              "the button is held down forever.", DT[i].name);
        CHECK(s5l_gpio_pin(&m.gpio, DT[i].pin) == s5l_button_level(i, false),
              "%s: the pin does not read as released", DT[i].name);

        CHECK(guest_services(&m, line), "%s: no interrupt for the release",
              DT[i].name);
        s5l8900_tick(&m, 0u);
        CHECK(!s5l_gpioic_pending(&m.gpioic, line),
              "%s: the release acknowledge did not stick", DT[i].name);
        /* And back to a quiet machine, with the polarity where it started. */
        CHECK(s5l_gpioic_read(&m.gpioic, GPIOIC_INTLEVEL + 4u) == 0x00002900u,
              "%s: two flips did not return INTLEVEL to 0x00002900 but 0x%08x",
              DT[i].name, s5l_gpioic_read(&m.gpioic, GPIOIC_INTLEVEL + 4u));
        CHECK(!m.cpu.irq_line, "%s: the machine did not go quiet", DT[i].name);
        CHECK(m.buttons.edges == 2u, "%s: %llu edges for one press and one "
              "release", DT[i].name, (unsigned long long)m.buttons.edges);

        s5l8900_free(&m);
    }
}

/* Each button is independent: pressing one must not move another's pin or
 * line. The five share one 32-bit pin word and one interrupt group, which is
 * exactly the arrangement in which an off-by-one is invisible. */
static void test_buttons_are_independent(void) {
    for (unsigned i = 0; i < S5L_BUTTON_COUNT; i++) {
        s5l8900_t m;
        CHECK(s5l8900_init(&m, 0u, 1u << 16), "machine init failed");
        arm_all(&m);
        CHECK(s5l_buttons_set(&m.buttons, &m.gpio, &m.gpioic, i, true),
              "%s refused", DT[i].name);
        for (unsigned j = 0; j < S5L_BUTTON_COUNT; j++) {
            if (j == i) continue;
            CHECK(!s5l_buttons_held(&m.buttons, j),
                  "pressing %s also held %s", DT[i].name, DT[j].name);
            CHECK(s5l_gpio_pin(&m.gpio, DT[j].pin) == s5l_button_level(j, false),
                  "pressing %s moved %s's pin", DT[i].name, DT[j].name);
            CHECK(!s5l_gpioic_pending(&m.gpioic, DT[j].line),
                  "pressing %s latched %s's line", DT[i].name, DT[j].name);
        }
        s5l8900_free(&m);
    }
}

/*
 * THE RINGER IS INVERTED TWICE AND THE TWO CANCEL. The platform function
 * reports `raw ^ (b1 ^ 1)` with b1 = 0, and AppleM68Buttons then inverts the
 * ringer again at 0xc065ab14, so the Phone Mute value it dispatches is the RAW
 * pin level. This model's `pressed` is what the guest reports, so muted must
 * drive the pin HIGH — and a model that composed only one of the two would
 * silence the phone by moving the slider to the position the guest calls
 * unmuted, silently.
 */
static void test_the_ringer_composes_both_inversions(void) {
    /* Exactly one button is inverted by the driver, and it is this one. */
    for (unsigned i = 0; i < S5L_BUTTON_COUNT; i++)
        CHECK(s5l_button_driver_inverts(i) == (i == S5L_BUTTON_RINGERAB),
              "%s: driver_inverts is %d", DT[i].name,
              s5l_button_driver_inverts(i));

    /* The hardware polarity byte still says active low — the model must not
     * have "fixed" it by editing the wiring. */
    CHECK(!s5l_button_active_high(S5L_BUTTON_RINGERAB),
          "the ringer's hardware polarity byte was changed to active high; the "
          "tree says 0x00010000, whose byte 1 is zero");

    /* Muted drives the pin HIGH, because that is what "dispatched mute == raw
     * pin level" means. */
    CHECK(s5l_button_level(S5L_BUTTON_RINGERAB, S5L_BUTTONS_RINGER_MUTED),
          "the muted position does not drive the ringer pin high, so the guest "
          "would be told the opposite of what the host asked for");
    CHECK(!s5l_button_level(S5L_BUTTON_RINGERAB, !S5L_BUTTONS_RINGER_MUTED),
          "both ringer positions drive the same level");

    /* The four keys are NOT inverted: pressed is the active level. */
    for (unsigned i = 0; i < S5L_BUTTON_COUNT; i++) {
        if (i == S5L_BUTTON_RINGERAB) continue;
        CHECK(s5l_button_level(i, true) == dt_active_high(i),
              "%s: pressed does not drive its active level", DT[i].name);
    }

    /* And rest is unmuted, which is the position that asserts nothing: line 43
     * asserts while HIGH (INTLEVEL bit set), and unmuted is LOW. */
    s5l8900_t m;
    CHECK(s5l8900_init(&m, 0u, 1u << 16), "machine init failed");
    CHECK(!s5l_gpio_pin(&m.gpio, 0x1603u),
          "the ringer rests high, which is the position that asserts its own "
          "level line before the driver has finished starting");
    arm_all(&m);
    CHECK(!s5l_gpioic_pending(&m.gpioic, 43u),
          "the resting ringer latched its line");
    s5l8900_free(&m);
}

/* Every refusal, and each is a different fact about the board. */
static void test_refusals_are_counted_and_real(void) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, 0u, 1u << 16), "machine init failed");

    /* NULL is safe everywhere. */
    CHECK(!s5l_buttons_set(NULL, &m.gpio, &m.gpioic, S5L_BUTTON_MENU, true),
          "a NULL board accepted a press");
    CHECK(!s5l_buttons_held(NULL, S5L_BUTTON_MENU), "NULL held query unsafe");
    s5l_buttons_reset(NULL);
    s5l_buttons_apply(NULL, &m.gpio, &m.gpioic);

    /* Out of range. */
    uint64_t before = m.buttons.refused;
    CHECK(!s5l_buttons_set(&m.buttons, &m.gpio, &m.gpioic,
                           S5L_BUTTON_COUNT, true),
          "a sixth button was accepted");
    CHECK(!s5l_buttons_set(&m.buttons, &m.gpio, &m.gpioic, 999u, true),
          "a wildly out-of-range button was accepted");
    CHECK(m.buttons.refused == before + 2u,
          "out-of-range requests were not counted: %llu",
          (unsigned long long)m.buttons.refused);
    CHECK(m.buttons.pressed == 0u,
          "an out-of-range request set a real button's bit");

    /*
     * THE LINE IS NOT ARMED. The driver never polls — its only sampling path
     * runs off a timer that only an interrupt arms — so this is not caution,
     * it is the truth about what the guest can see.
     */
    before = m.buttons.refused;
    CHECK(!s5l_buttons_set(&m.buttons, &m.gpio, &m.gpioic,
                           S5L_BUTTON_MENU, true),
          "an unarmed line accepted a press; the guest could never have seen "
          "it, and reporting success would make 'the driver has not started' "
          "look like 'the driver ignored me'");
    CHECK(m.buttons.refused == before + 1u, "the refusal was not counted");
    CHECK(!s5l_buttons_held(&m.buttons, S5L_BUTTON_MENU),
          "a refused press was recorded anyway");
    CHECK(!s5l_gpio_pin(&m.gpio, 0x1600u),
          "a refused press moved the pin");
    /* A refusal must not be an accident of the OTHER conditions: arming it
     * alone makes the very same call succeed. */
    arm_all(&m);
    CHECK(s5l_buttons_set(&m.buttons, &m.gpio, &m.gpioic,
                          S5L_BUTTON_MENU, true),
          "the same press was still refused once its line was armed — the "
          "refusal is not testing what it claims to");

    /*
     * THE PREVIOUS EDGE IS UNSERVICED. Menu is now pending. A release now
     * would collapse both halves into the one latched bit.
     */
    before = m.buttons.refused;
    CHECK(s5l_gpioic_pending(&m.gpioic, 40u), "setup: menu is not pending");
    CHECK(!s5l_buttons_set(&m.buttons, &m.gpio, &m.gpioic,
                           S5L_BUTTON_MENU, false),
          "a release was accepted while the press was still unserviced");
    CHECK(m.buttons.refused == before + 1u, "the refusal was not counted");
    CHECK(s5l_buttons_held(&m.buttons, S5L_BUTTON_MENU),
          "a refused release was applied anyway");
    /* Re-asserting the state it is already in is NOT a refusal. */
    before = m.buttons.refused;
    CHECK(s5l_buttons_set(&m.buttons, &m.gpio, &m.gpioic,
                          S5L_BUTTON_MENU, true),
          "holding a button that is already held was refused");
    CHECK(m.buttons.refused == before, "an unchanged state was counted refused");
    /* And once serviced, the release goes through. */
    CHECK(guest_services(&m, 40u), "setup: nothing to service");
    CHECK(s5l_buttons_set(&m.buttons, &m.gpio, &m.gpioic,
                          S5L_BUTTON_MENU, false),
          "the release was still refused after the guest serviced the press");

    /* A NULL controller is "not armed", not a crash. */
    CHECK(!s5l_buttons_set(&m.buttons, &m.gpio, NULL, S5L_BUTTON_VOLUP, true),
          "a press with no interrupt controller was accepted");

    s5l8900_free(&m);
}

/* End to end: a press must reach cpu.irq_line through group 1's cascade, which
 * is VIC line 32 — on VIC1, not VIC0. */
static void test_a_press_reaches_the_cpu(void) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, 0u, 1u << 16), "machine init failed");
    void *c = m.bus.ctx;
    arm_all(&m);

    CHECK(s5l_buttons_set(&m.buttons, &m.gpio, &m.gpioic,
                          S5L_BUTTON_MENU, true), "the press was refused");
    s5l8900_tick(&m, 0u);
    CHECK((m.vic[1].raw & (1u << 0)) != 0u,
          "group 1 did not raise VIC1 line 0 (flat 32): vic1 raw=0x%08x",
          m.vic[1].raw);
    CHECK((m.vic[0].raw & (1u << 0)) == 0u,
          "group 1's line 32 was also applied to VIC0 bit 0");
    CHECK(!m.cpu.irq_line, "the line reached the CPU with the VIC masked");

    m.bus.write32(c, S5L8900_VIC1_BASE + VIC_INTENABLE, 1u << 0);
    s5l8900_tick(&m, 0u);
    CHECK(m.cpu.irq_line, "an enabled, pending Home press did not reach the CPU");

    /* And the guest can read the pin through the bus, at the word its own
     * accessor computes. */
    CHECK((m.bus.read32(c, S5L8900_GPIO_BASE + 0x2c4u) & 1u) != 0u,
          "the Home pin does not read as high through the bus: 0x%08x",
          m.bus.read32(c, S5L8900_GPIO_BASE + 0x2c4u));

    /* Service it the guest's way and the CPU line drops. */
    CHECK(guest_services(&m, 40u), "nothing to service");
    s5l8900_tick(&m, 0u);
    CHECK(!m.cpu.irq_line,
          "acknowledging a serviced button left the CPU line up");
    s5l8900_free(&m);
}

/*
 * The EDGE world is untouched. Every currently-working line in this machine —
 * multi-touch's included — has a second interrupt cell of zero, so it stays on
 * the rising-edge latch and on the early acknowledge, and the level rules
 * introduced for the buttons must not reach it.
 */
static void test_edge_lines_are_unchanged(void) {
    s5l_gpioic_t g;
    s5l_gpioic_reset(&g);

    /* Multi-touch: cell 0, so INTTYPE and INTLEVEL both stay zero. */
    s5l_gpioic_write(&g, GPIOIC_INTEN + 4u * 4u, 0x08000000u);
    s5l_gpioic_set_line(&g, 155u, true);
    CHECK(s5l_gpioic_pending(&g, 155u), "the touch line did not latch");
    /* Acknowledging while still asserted STICKS, exactly as before. This is the
     * assertion that run71 cost half a billion instructions to learn, and a
     * level rule that leaked onto an edge line would undo it. */
    s5l_gpioic_write(&g, GPIOIC_INTSTAT + 4u * 4u, 0x08000000u);
    CHECK(!s5l_gpioic_pending(&g, 155u),
          "acknowledging a still-asserted EDGE line re-latched it — that is "
          "run71's 1,193,122-iteration livelock");
    /* A line that is merely still high sets nothing. */
    s5l_gpioic_set_line(&g, 155u, true);
    CHECK(!s5l_gpioic_pending(&g, 155u), "a still-high edge line re-latched");
    /* Deasserting does not clear a latch. */
    s5l_gpioic_set_line(&g, 155u, false);
    s5l_gpioic_set_line(&g, 155u, true);
    s5l_gpioic_set_line(&g, 155u, false);
    CHECK(s5l_gpioic_pending(&g, 155u),
          "a pulse shorter than the guest's polling interval was lost");

    /*
     * An INTLEVEL write must not disturb an edge line. The buttons' auto-flip
     * writes the whole group word, and group 4 would be written the same way
     * by any driver doing a read-modify-write.
     */
    s5l_gpioic_write(&g, GPIOIC_INTSTAT + 4u * 4u, 0xffffffffu);
    s5l_gpioic_write(&g, GPIOIC_INTLEVEL + 4u * 4u, 0xffffffffu);
    CHECK(s5l_gpioic_read(&g, GPIOIC_INTSTAT + 4u * 4u) == 0u,
          "writing INTLEVEL latched edge lines: 0x%08x",
          s5l_gpioic_read(&g, GPIOIC_INTSTAT + 4u * 4u));
    /* But once a line is declared LEVEL, the same write does latch it. */
    s5l_gpioic_write(&g, GPIOIC_INTLEVEL + 4u * 4u, 0u);
    s5l_gpioic_write(&g, GPIOIC_INTTYPE + 4u * 4u, 0x08000000u);
    CHECK(s5l_gpioic_read(&g, GPIOIC_INTSTAT + 4u * 4u) == 0x08000000u,
          "a line declared level-sensitive while its wire already matched "
          "INTLEVEL did not latch");
}

/* The level rules themselves, at the register level and away from any button. */
static void test_level_lines_relatch_on_every_input(void) {
    s5l_gpioic_t g;
    s5l_gpioic_reset(&g);
    const unsigned line = 40u, bit = 8u;
    const uint32_t mask = 1u << bit;

    /*
     * A device drives it low FIRST, which is the real order: the board presents
     * its levels at machine reset and the guest's driver configures the line
     * afterwards. An undriven line would assert nothing at all no matter how it
     * were configured — see test_an_undriven_level_line_never_asserts().
     */
    s5l_gpioic_set_line(&g, line, false);
    CHECK(!s5l_gpioic_pending(&g, line),
          "driving an EDGE line low latched something");

    /* Configure it active-low while still masked. The electrical condition is
     * true, but INTEN is deliberately part of a level interrupt's assertion:
     * the guest masks a deferred line specifically so its W1C can stick. */
    s5l_gpioic_write(&g, GPIOIC_INTTYPE + 4u, mask);
    CHECK(!s5l_gpioic_pending(&g, line),
          "a masked level line asserted during configuration");

    /* Set the real rest polarity, discard stale state, then arm it in the same
     * order as enableVector at 0xc05a56b0. */
    s5l_gpioic_write(&g, GPIOIC_INTLEVEL + 4u, mask);
    s5l_gpioic_write(&g, GPIOIC_INTSTAT + 4u, mask);
    s5l_gpioic_write(&g, GPIOIC_INTEN + 4u, mask);
    CHECK(!s5l_gpioic_pending(&g, line),
          "the driver's own arm sequence left the resting line asserted");

    /* The wire goes high: assert. */
    s5l_gpioic_set_line(&g, line, true);
    CHECK(s5l_gpioic_pending(&g, line), "a level line did not assert");

    /* Acknowledging while the condition still holds RE-ASSERTS. That is what
     * level-sensitive means, and it is why the guest flips first. */
    s5l_gpioic_write(&g, GPIOIC_INTSTAT + 4u, mask);
    CHECK(s5l_gpioic_pending(&g, line),
          "acknowledging a level line whose condition still holds cleared it "
          "for good — a real level interrupt would come straight back");

    /* The PMU child takes this route instead of auto-flip: mask line 85, then
     * acknowledge it so handleInterrupt can return and deferred PMU work can
     * clear INT_N. Re-enabling while still active must assert again. */
    s5l_gpioic_write(&g, GPIOIC_INTEN + 4u, 0u);
    s5l_gpioic_write(&g, GPIOIC_INTSTAT + 4u, mask);
    CHECK(!s5l_gpioic_pending(&g, line),
          "acknowledging a masked level line immediately re-latched it");
    s5l_gpioic_write(&g, GPIOIC_INTEN + 4u, mask);
    CHECK(s5l_gpioic_pending(&g, line),
          "unmasking a still-active level line did not reassert it");

    /* Flip the polarity and the condition goes away, so the acknowledge
     * sticks. This pair is the whole auto-flip mechanism. */
    s5l_gpioic_write(&g, GPIOIC_INTLEVEL + 4u, 0u);
    s5l_gpioic_write(&g, GPIOIC_INTSTAT + 4u, mask);
    CHECK(!s5l_gpioic_pending(&g, line),
          "the acknowledge did not stick after the polarity flip");

    /*
     * AND THE FLIP ITSELF CAN ASSERT, with no wire having moved. The wire is
     * high and the polarity is "assert while low"; flipping it back makes the
     * condition true on the spot. A level input is a continuous comparison,
     * not a memory of the last transition, and a model that only re-evaluated
     * when a device moved a line would miss this — silently, because the guest
     * writes that register itself after every single interrupt.
     */
    s5l_gpioic_write(&g, GPIOIC_INTSTAT + 4u, mask);
    CHECK(!s5l_gpioic_pending(&g, line), "setup: the line did not go quiet");
    s5l_gpioic_write(&g, GPIOIC_INTLEVEL + 4u, mask);
    CHECK(s5l_gpioic_pending(&g, line),
          "flipping the polarity onto a wire that already matched it asserted "
          "nothing — the assertion condition is not being re-evaluated on an "
          "INTLEVEL write");

    /* Put it back, and prove the acknowledge re-asserts while the condition
     * holds — the INTSTAT re-evaluation, isolated. */
    s5l_gpioic_write(&g, GPIOIC_INTSTAT + 4u, mask);
    CHECK(s5l_gpioic_pending(&g, line),
          "acknowledging a level line whose condition still holds did not "
          "re-assert it");
    s5l_gpioic_write(&g, GPIOIC_INTLEVEL + 4u, 0u);
    s5l_gpioic_write(&g, GPIOIC_INTSTAT + 4u, mask);
    CHECK(!s5l_gpioic_pending(&g, line), "setup: the line did not go quiet");

    /* The wire goes low: assert against the flipped polarity. */
    s5l_gpioic_set_line(&g, line, false);
    CHECK(s5l_gpioic_pending(&g, line),
          "the opposite transition did not assert after the flip");

    /* A latch is still a latch: a condition that goes away on its own does not
     * un-report an interrupt the guest has already been given. */
    s5l_gpioic_set_line(&g, line, true);
    CHECK(s5l_gpioic_pending(&g, line),
          "a level line un-latched itself when its wire moved away");

    /* And the re-evaluation is SET-only: it must never clear. */
    s5l_gpioic_write(&g, GPIOIC_INTLEVEL + 4u, mask);
    s5l_gpioic_write(&g, GPIOIC_INTTYPE + 4u, mask);
    CHECK(s5l_gpioic_pending(&g, line),
          "re-evaluating cleared a bit instead of only setting");

    /* Other lines in the group are untouched throughout. */
    CHECK((s5l_gpioic_read(&g, GPIOIC_INTSTAT + 4u) & ~mask) == 0u,
          "a level line latched its neighbours: 0x%08x",
          s5l_gpioic_read(&g, GPIOIC_INTSTAT + 4u));
}

/*
 * AN UNDRIVEN LEVEL LINE ASSERTS NOTHING, EVER. This is the one run87 paid for.
 *
 * Eleven nodes hang off /arm-io/gpio and this machine now models five of them.
 * /arm-io/i2c0/als `interrupts {73,1}` remains unmodelled: cell 1 is LEVEL with
 * INTLEVEL 0, "assert while low", and an unconnected line whose raw bit sits at
 * the array's initial zero satisfies that condition forever. The PMU formerly
 * had the same problem; it is now a real driven line and is tested separately.
 * The guest read and acknowledged group 2's pending word 668,039 times and
 * never got past instruction ~96 million when undriven lines asserted.
 *
 * The fix is that an undriven line is not a line at level zero, and the test is
 * that configuring one exactly as the guest does asserts nothing at all.
 */
static void test_an_undriven_level_line_never_asserts(void) {
    /* The real ones, transcribed from the tree rather than invented. */
    static const struct { const char *node; unsigned line, cell; } UNDRIVEN[] = {
        { "/arm-io/i2c0/als",           73u, 1u },
        { "/arm-io/i2c0/accelerometer",163u, 1u },
        { "/arm-io/i2c0/accelerometer",156u, 1u },
        { "/arm-io/i2c0/audio0",        44u, 3u },
        { "/baseband",                  75u, 5u },
    };

    s5l8900_t m;
    CHECK(s5l8900_init(&m, 0u, 1u << 16), "machine init failed");

    for (unsigned i = 0; i < sizeof UNDRIVEN / sizeof UNDRIVEN[0]; i++) {
        unsigned g = UNDRIVEN[i].line >> 5, b = UNDRIVEN[i].line & 31u;
        /* The guest's own initVector, then enableVector. */
        uint32_t t = s5l_gpioic_read(&m.gpioic, GPIOIC_INTTYPE + 4u * g);
        s5l_gpioic_write(&m.gpioic, GPIOIC_INTTYPE + 4u * g,
                         (t & ~(1u << b)) | ((UNDRIVEN[i].cell & 1u) << b));
        uint32_t l = s5l_gpioic_read(&m.gpioic, GPIOIC_INTLEVEL + 4u * g);
        s5l_gpioic_write(&m.gpioic, GPIOIC_INTLEVEL + 4u * g,
                         (l & ~(1u << b)) | (((UNDRIVEN[i].cell >> 1) & 1u) << b));
        s5l_gpioic_write(&m.gpioic, GPIOIC_INTSTAT + 4u * g, 1u << b);
        uint32_t e = s5l_gpioic_read(&m.gpioic, GPIOIC_INTEN + 4u * g);
        s5l_gpioic_write(&m.gpioic, GPIOIC_INTEN + 4u * g, e | (1u << b));

        CHECK(!s5l_gpioic_pending(&m.gpioic, UNDRIVEN[i].line),
              "%s line %u (cell %u) asserted with nothing on the end of it — "
              "that is run87's 668,039-acknowledge storm",
              UNDRIVEN[i].node, UNDRIVEN[i].line, UNDRIVEN[i].cell);
    }

    /* And the acknowledge sticks, which is the property the storm violated:
     * acknowledge, and it must not come straight back. */
    for (unsigned i = 0; i < sizeof UNDRIVEN / sizeof UNDRIVEN[0]; i++) {
        unsigned g = UNDRIVEN[i].line >> 5, b = UNDRIVEN[i].line & 31u;
        s5l_gpioic_write(&m.gpioic, GPIOIC_INTSTAT + 4u * g, 1u << b);
        CHECK(!s5l_gpioic_pending(&m.gpioic, UNDRIVEN[i].line),
              "%s line %u re-latched on acknowledge", UNDRIVEN[i].node,
              UNDRIVEN[i].line);
    }
    s5l8900_tick(&m, 0u);
    CHECK(!m.cpu.irq_line,
          "a machine with five buttons at rest and five unmodelled level lines "
          "raised an interrupt");
    s5l8900_free(&m);

    /*
     * And the distinction is real in both directions: the SAME line, once a
     * device drives it low, does assert. "Undriven" must not be a way of
     * quietly disabling level lines.
     */
    s5l_gpioic_t g2;
    s5l_gpioic_reset(&g2);
    s5l_gpioic_write(&g2, GPIOIC_INTTYPE + 4u * 2u, 1u << 9);   /* line 73 */
    s5l_gpioic_write(&g2, GPIOIC_INTLEVEL + 4u * 2u, 0u);       /* assert low */
    s5l_gpioic_write(&g2, GPIOIC_INTEN + 4u * 2u, 1u << 9);
    CHECK(!s5l_gpioic_pending(&g2, 73u), "an undriven line asserted");
    s5l_gpioic_set_line(&g2, 73u, false);   /* a device says: my line is low */
    CHECK(s5l_gpioic_pending(&g2, 73u),
          "a device DRIVING its line low did not assert an active-low level "
          "line — `driven` has become a way of disabling level lines");
    /* Driving it high deasserts the condition; the latch survives until W1C. */
    s5l_gpioic_set_line(&g2, 73u, true);
    s5l_gpioic_write(&g2, GPIOIC_INTSTAT + 4u * 2u, 1u << 9);
    CHECK(!s5l_gpioic_pending(&g2, 73u), "the acknowledge did not stick");
}

/* The board drives an input pin; the guest cannot, and a watcher sees both. */
typedef struct { unsigned calls; bool last; } pinwatch_t;
static void pinwatch_cb(void *ctx, bool level) {
    pinwatch_t *w = ctx;
    w->calls++;
    w->last = level;
}

static void test_the_board_drives_inputs_and_the_guest_cannot(void) {
    s5l_gpio_t p;
    pinwatch_t w;
    s5l_gpio_reset(&p);
    memset(&w, 0, sizeof w);

    CHECK(s5l_gpio_watch(&p, 0x1600u, &w, pinwatch_cb), "watch refused");

    s5l_gpio_drive(&p, 0x1600u, true);
    CHECK(s5l_gpio_pin(&p, 0x1600u), "the board could not drive an input high");
    CHECK(w.calls == 1u && w.last, "the board's edge was not reported (%u)",
          w.calls);
    s5l_gpio_drive(&p, 0x1600u, true);
    CHECK(w.calls == 1u, "re-driving an unchanged level reported an edge — "
          "s5l_buttons_apply() runs every tick and would flood a watcher");
    s5l_gpio_drive(&p, 0x1600u, false);
    CHECK(!s5l_gpio_pin(&p, 0x1600u) && w.calls == 2u && !w.last,
          "the falling edge was not reported");

    /* A guest store to the level register still changes nothing, which is what
     * keeps the two ends of the wire distinguishable. */
    s5l_gpio_drive(&p, 0x1600u, true);
    s5l_gpio_write(&p, 0x2c4u, 0u, 4u);
    CHECK(s5l_gpio_pin(&p, 0x1600u),
          "a guest store to the level register overrode the board");

    /*
     * AND THE GUEST'S OWN READ PATH DOES NOT CLOBBER IT. getPinLevel writes
     * (pin << 8) | 0 to fsel before every read, to force the pin to function 0.
     * Function 0 is not a driven output, so it must move nothing.
     */
    s5l_gpio_write(&p, S5L_GPIO_FSEL, ((uint32_t)0x1600u << 8), 4u);
    CHECK(s5l_gpio_pin(&p, 0x1600u),
          "the guest's own pin-read preamble drove the board's input low — "
          "every button would read as released the moment it was sampled");

    /* Out of range drives nothing and does not wrap. */
    s5l_gpio_reset(&p);
    s5l_gpio_drive(&p, (uint16_t)(S5L_GPIO_PORTS << 8), true);
    s5l_gpio_drive(&p, 0x16ffu, true);
    s5l_gpio_drive(NULL, 0x1600u, true);
    for (unsigned g = 0; g < S5L_GPIO_PORTS; g++)
        CHECK(s5l_gpio_read(&p, S5L_GPIO_PIN_REG(g), 4u) == 0u,
              "an out-of-range drive wrapped into port %u", g);
}

static void test_power_wakes_standby_through_retained_reset(void) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, S5L8900_SDRAM_BASE, 1u << 16),
          "machine init failed");
    arm_all(&m);
    arm_pmu_irq(&m);
    unsigned pmu_group = S5L_GPIOIC_LINE_PMU >> 5;
    uint32_t pmu_bit = 1u << (S5L_GPIOIC_LINE_PMU & 31u);
    m.pmu.regs[PCF50635_OOCSHDWN] = PCF50635_OOCSHDWN_GO_STANDBY;
    m.pmu.written[PCF50635_OOCSHDWN] = 1u;
    m.cpu.r[0] = 0x11111111u;
    m.cpu.r[15] = 0xc0061eb0u; /* XNU's deliberate post-quiesce branch */
    m.cpu.cpsr = ARM_MODE_USR;
    m.cpu.cp15.sctlr = 0x00c5187du;
    m.cpu.cp15.ttbr0 = 0x12344000u;
    m.cpu.cycles = UINT64_C(17667762932);
    m.wfi_pace_yield = true;
    m.active_clock_last_host_ns = 99u;
    m.active_clock_guest_ticks_since_sync = 88u;
    m.active_clock_fraction = 77u;
    m.active_clock_anchor_valid = true;

    CHECK(!s5l8900_set_button(&m, S5L_BUTTON_COUNT, true),
          "nonexistent sleeping button was accepted");
    CHECK(m.buttons.refused == 1u, "malformed sleeping input was not counted");

    CHECK(s5l8900_set_button(&m, S5L_BUTTON_MENU, true),
          "non-wake button was allowed to block the sleeping input FIFO");
    CHECK(m.cpu.r[15] == 0xc0061eb0u &&
          s5l_pcf50635_in_standby(&m.pmu),
          "Home incorrectly woke or mutated the sleeping CPU");
    CHECK(!s5l_buttons_held(&m.buttons, S5L_BUTTON_MENU) &&
          !s5l_gpioic_pending(&m.gpioic, s5l_button_line(S5L_BUTTON_MENU)),
          "unobservable sleeping Home press reached the GPIO path");

    CHECK(s5l8900_set_button(&m, S5L_BUTTON_HOLD, true),
          "Power did not wake the standby machine");
    CHECK(!s5l_pcf50635_in_standby(&m.pmu) &&
          (m.pmu.regs[PCF50635_INT2] & PCF50635_INT2_ONKEYR) != 0u,
          "Power wake did not latch the PMU ONKEY reason");
    CHECK(!s5l_gpioic_line(&m.gpioic, S5L_GPIOIC_LINE_PMU) &&
          s5l_gpioic_pending(&m.gpioic, S5L_GPIOIC_LINE_PMU) &&
          m.cpu.irq_line,
          "PMU ONKEY did not assert active-low GPIO line 85 through VIC0");
    CHECK(m.cpu.r[15] == S5L8900_SDRAM_BASE,
          "warm reset PC=%08x, expected retained vector %08x",
          m.cpu.r[15], S5L8900_SDRAM_BASE);
    CHECK(m.cpu.r[0] == 0u && m.cpu.cp15.sctlr == 0u &&
          m.cpu.cp15.ttbr0 == 0u,
          "Power wake did not reset general/translation CPU state");
    CHECK(m.cpu.cpsr ==
          (ARM_MODE_SVC | ARM_CPSR_I | ARM_CPSR_F | ARM_CPSR_A),
          "warm reset CPSR=%08x", m.cpu.cpsr);
    CHECK(m.cpu.cycles == UINT64_C(17667762932),
          "host retired-instruction timeline restarted at %llu",
          (unsigned long long)m.cpu.cycles);
    CHECK(!m.wfi_pace_yield && !m.active_clock_anchor_valid &&
          m.active_clock_last_host_ns == 0u &&
          m.active_clock_guest_ticks_since_sync == 0u &&
          m.active_clock_fraction == 0u,
          "warm reset retained stale host-clock state");
    unsigned hold_line = s5l_button_line(S5L_BUTTON_HOLD);
    uint32_t hold_bit = 1u << (hold_line & 31u);
    unsigned hold_group = hold_line >> 5;
    CHECK(s5l_buttons_held(&m.buttons, S5L_BUTTON_HOLD) &&
          s5l_gpioic_line(&m.gpioic, hold_line) &&
          !s5l_gpioic_pending(&m.gpioic, hold_line) &&
          (m.gpioic.level[hold_group] & hold_bit) == 0u &&
          s5l_gpio_pin(&m.gpio, s5l_button_pin(S5L_BUTTON_HOLD)) ==
              s5l_button_level(S5L_BUTTON_HOLD, true),
          "PMU wake did not consume its GPIO edge and retain the held wire");

    CHECK(!s5l8900_set_button(&m, S5L_BUTTON_HOLD, false),
          "Power release overtook the guest's PMU wake-reason read");
    m.pmu.regs[PCF50635_INT2] = 0u; /* the clear-on-read tested in test_i2c */
    m.level_dirty = true;            /* a real I2C read dirties the machine */
    s5l8900_tick(&m, 0u);
    CHECK(s5l_gpioic_line(&m.gpioic, S5L_GPIOIC_LINE_PMU) &&
          s5l_gpioic_pending(&m.gpioic, S5L_GPIOIC_LINE_PMU),
          "clearing PMU status did not deassert INT_N while retaining its latch");
    s5l_gpioic_write(&m.gpioic, GPIOIC_INTSTAT + 4u * pmu_group, pmu_bit);
    m.level_dirty = true;
    s5l8900_tick(&m, 0u);
    CHECK(!s5l_gpioic_pending(&m.gpioic, S5L_GPIOIC_LINE_PMU) &&
          !m.cpu.irq_line,
          "acknowledging deasserted PMU INT_N left the CPU interrupted");
    CHECK(s5l8900_set_button(&m, S5L_BUTTON_HOLD, false),
          "queued Power release was not drainable after the PMU read");
    CHECK(s5l_gpioic_pending(&m.gpioic, hold_line),
          "post-wake Power release produced no guest-visible interrupt");
    CHECK(guest_services(&m, hold_line),
          "guest could not service the post-wake Power release");
    s5l8900_tick(&m, 0u);
    CHECK(!s5l_buttons_held(&m.buttons, S5L_BUTTON_HOLD) &&
          !s5l_gpioic_pending(&m.gpioic, hold_line) &&
          (m.gpioic.level[hold_group] & hold_bit) != 0u,
          "Power did not return to its released, press-armed state");
    CHECK(m.buttons.sets == 3u && m.buttons.edges == 2u &&
          m.buttons.refused == 2u,
          "wake press/release evidence drifted: sets=%llu edges=%llu refused=%llu",
          (unsigned long long)m.buttons.sets,
          (unsigned long long)m.buttons.edges,
          (unsigned long long)m.buttons.refused);
    s5l8900_free(&m);
}

static void test_restore_wakes_standby_without_a_button(void) {
    s5l8900_t m;
    CHECK(!s5l8900_wake_from_standby(NULL),
          "NULL machine was reported as woken");
    CHECK(s5l8900_init(&m, S5L8900_SDRAM_BASE, 1u << 16),
          "machine init failed");
    arm_all(&m);
    arm_pmu_irq(&m);
    CHECK(!s5l8900_wake_from_standby(&m),
          "a running machine was reported as woken");

    m.pmu.regs[PCF50635_OOCSHDWN] = PCF50635_OOCSHDWN_GO_STANDBY;
    m.pmu.written[PCF50635_OOCSHDWN] = 1u;
    m.cpu.r[0] = 0x22222222u;
    m.cpu.r[15] = 0xc0062300u;
    m.cpu.cpsr = ARM_MODE_USR;
    m.cpu.cp15.sctlr = 0x00c5187du;
    m.cpu.cp15.ttbr0 = 0x56788000u;
    m.cpu.cycles = UINT64_C(18123456789);
    m.wfi_pace_yield = true;
    m.active_clock_last_host_ns = 199u;
    m.active_clock_guest_ticks_since_sync = 188u;
    m.active_clock_fraction = 177u;
    m.active_clock_anchor_valid = true;

    uint8_t pressed = m.buttons.pressed;
    uint64_t sets = m.buttons.sets;
    uint64_t edges = m.buttons.edges;
    uint64_t refused = m.buttons.refused;
    uint32_t gpio_level[S5L_GPIOIC_GROUPS];
    uint32_t gpio_stat[S5L_GPIOIC_GROUPS];
    memcpy(gpio_level, m.gpioic.level, sizeof gpio_level);
    memcpy(gpio_stat, m.gpioic.stat, sizeof gpio_stat);

    CHECK(s5l8900_wake_from_standby(&m),
          "restored PMU standby state did not wake");
    CHECK(!s5l_pcf50635_in_standby(&m.pmu) &&
          (m.pmu.regs[PCF50635_INT2] & PCF50635_INT2_ONKEYR) != 0u,
          "restore wake did not latch the PMU ONKEY reason");
    CHECK(m.cpu.r[15] == S5L8900_SDRAM_BASE && m.cpu.r[0] == 0u &&
          m.cpu.cp15.sctlr == 0u && m.cpu.cp15.ttbr0 == 0u,
          "restore wake did not enter the retained-RAM reset vector cleanly");
    CHECK(m.cpu.cpsr ==
          (ARM_MODE_SVC | ARM_CPSR_I | ARM_CPSR_F | ARM_CPSR_A),
          "restore wake CPSR=%08x", m.cpu.cpsr);
    CHECK(m.cpu.cycles == UINT64_C(18123456789),
          "restore wake restarted the retired-instruction timeline at %llu",
          (unsigned long long)m.cpu.cycles);
    CHECK(!m.wfi_pace_yield && !m.active_clock_anchor_valid &&
          m.active_clock_last_host_ns == 0u &&
          m.active_clock_guest_ticks_since_sync == 0u &&
          m.active_clock_fraction == 0u,
          "restore wake retained stale host-clock state");
    CHECK(m.buttons.pressed == pressed && m.buttons.sets == sets &&
          m.buttons.edges == edges && m.buttons.refused == refused,
          "restore wake manufactured a host button transition");
    CHECK(memcmp(m.gpioic.level, gpio_level, sizeof gpio_level) == 0,
          "restore wake changed a GPIO polarity configuration");
    for (unsigned group = 0; group < S5L_GPIOIC_GROUPS; group++) {
        uint32_t expected = gpio_stat[group];
        if (group == (S5L_GPIOIC_LINE_PMU >> 5))
            expected |= 1u << (S5L_GPIOIC_LINE_PMU & 31u);
        CHECK(m.gpioic.stat[group] == expected,
              "restore wake changed GPIO group %u status: %08x vs %08x",
              group, m.gpioic.stat[group], expected);
    }
    CHECK(!s5l_gpioic_line(&m.gpioic, S5L_GPIOIC_LINE_PMU) &&
          s5l_gpioic_pending(&m.gpioic, S5L_GPIOIC_LINE_PMU) &&
          m.cpu.irq_line,
          "restore wake did not assert the PMU's real active-low interrupt");
    CHECK(!s5l8900_wake_from_standby(&m),
          "an already-woken machine accepted a second wake");
    s5l8900_free(&m);
}

/* Held buttons are machine state and must survive a checkpoint. */
static void test_snapshot_carries_the_switches(void) {
    s5l8900_t src, dst;
    CHECK(s5l8900_init(&src, 0u, 1u << 16), "source init failed");
    CHECK(s5l8900_init(&dst, 0u, 1u << 16), "destination init failed");
    arm_all(&src);

    /*
     * A line that s5l8900_init() does NOT drive, driven by hand, so that the
     * `driven` check below compares two machines that genuinely differ. Both
     * machines are freshly initialised and therefore already agree about every
     * line the board drives at reset; without this the check would pass on a
     * build that never serialised the mask at all.
     */
    s5l_gpioic_set_line(&src.gpioic, 100u, false);

    CHECK(s5l_buttons_set(&src.buttons, &src.gpio, &src.gpioic,
                          S5L_BUTTON_HOLD, true), "setup: hold refused");
    CHECK(s5l_buttons_set(&src.buttons, &src.gpio, &src.gpioic,
                          S5L_BUTTON_RINGERAB, S5L_BUTTONS_RINGER_MUTED),
          "setup: the ringer refused");
    (void)s5l_buttons_set(&src.buttons, &src.gpio, &src.gpioic, 99u, true);

    uint8_t *blob = NULL;
    size_t blob_len = 0;
    CHECK(snapshot_save_mem(&src, &blob, &blob_len) == SNAP_OK, "could not save");
    CHECK(snapshot_load_mem(&dst, blob, blob_len) == SNAP_OK,
          "could not restore");

    CHECK(dst.buttons.pressed == src.buttons.pressed,
          "the held switches did not survive: 0x%02x vs 0x%02x",
          dst.buttons.pressed, src.buttons.pressed);
    CHECK(s5l_buttons_held(&dst.buttons, S5L_BUTTON_HOLD),
          "a restored machine released the Hold button silently");
    CHECK(dst.buttons.refused == src.buttons.refused &&
          dst.buttons.edges == src.buttons.edges &&
          dst.buttons.sets == src.buttons.sets,
          "the counters did not survive — a restored run would report a host "
          "that had been told no as one that never asked");

    /*
     * WHICH LINES HAVE A DEVICE ON THEM travels too. A restore that dropped it
     * would leave every level line undriven, so the buttons' own interrupts
     * would stop arriving — silently, because the registers would all still
     * read back correctly and the pins would still move.
     */
    CHECK(memcmp(dst.gpioic.driven, src.gpioic.driven,
                 sizeof src.gpioic.driven) == 0,
          "the driven mask did not survive: group 1 %08x vs %08x",
          dst.gpioic.driven[1], src.gpioic.driven[1]);
    CHECK(src.gpioic.driven[1] != 0u,
          "setup: nothing was driving group 1, so the check above proves "
          "nothing");
    CHECK((dst.gpioic.driven[3] & (1u << 4)) != 0u,
          "line 100, which the source drove and a fresh machine does not, came "
          "back undriven — the mask is not being serialised at all, and every "
          "level line a device brought online during the run would go silent "
          "across a checkpoint");
    /* Serialization preserves the source's exact pre-refresh wire state. The
     * source has not refreshed since init, so neither PMU nor ALS has driven a
     * line yet; restore must not silently invent either one inside the blob. */
    CHECK((dst.gpioic.driven[2] & ((1u << 9) | (1u << 21))) == 0u,
          "the pre-refresh ALS or PMU line came back driven: group 2 %08x",
          dst.gpioic.driven[2]);

    /* And the restored machine still presents them on the wire. */
    s5l8900_tick(&dst, 0u);
    CHECK((dst.gpioic.driven[2] & (1u << 21)) != 0u &&
          (dst.gpioic.driven[2] & (1u << 9)) == 0u &&
          s5l_gpioic_line(&dst.gpioic, S5L_GPIOIC_LINE_PMU),
          "refresh did not drive idle PMU INT_N high while leaving ALS absent");
    CHECK(s5l_gpio_pin(&dst.gpio, 0x1605u) == s5l_button_level(S5L_BUTTON_HOLD, true),
          "the restored machine does not drive the held Hold pin");
    CHECK(s5l_gpio_pin(&dst.gpio, 0x1603u) ==
          s5l_button_level(S5L_BUTTON_RINGERAB, S5L_BUTTONS_RINGER_MUTED),
          "the restored machine does not drive the muted ringer pin");

    free(blob);
    s5l8900_free(&src);
    s5l8900_free(&dst);
}

/* A file is untrusted input: a sixth button must be refused on the way in. */
static void test_snapshot_rejects_a_sixth_button(void) {
    s5l8900_t src, dst;
    uint8_t *blob = NULL;
    size_t blob_len = 0;
    CHECK(s5l8900_init(&src, 0u, 1u << 16), "source init failed");
    CHECK(s5l8900_init(&dst, 0u, 1u << 16), "destination init failed");

    src.buttons.pressed = 1u << S5L_BUTTON_COUNT;
    CHECK(snapshot_save_mem(&src, &blob, &blob_len) == SNAP_OK,
          "could not save the poisoned machine");
    if (blob) {
        CHECK(snapshot_load_mem(&dst, blob, blob_len) == SNAP_ERR_CORRUPT,
              "a held button that does not exist was restored");
        free(blob);
    }
    s5l8900_free(&dst);
    s5l8900_free(&src);
}

int main(void) {
    printf("S5LBox iPhone1,2 physical button tests\n");
    test_wiring_is_the_device_trees_own();
    test_the_measured_interrupt_configuration();
    test_rest_asserts_nothing();
    test_the_guests_own_press_and_release_sequence();
    test_buttons_are_independent();
    test_the_ringer_composes_both_inversions();
    test_refusals_are_counted_and_real();
    test_a_press_reaches_the_cpu();
    test_edge_lines_are_unchanged();
    test_level_lines_relatch_on_every_input();
    test_an_undriven_level_line_never_asserts();
    test_the_board_drives_inputs_and_the_guest_cannot();
    test_power_wakes_standby_through_retained_reset();
    test_restore_wakes_standby_without_a_button();
    test_snapshot_carries_the_switches();
    test_snapshot_rejects_a_sixth_button();
    printf("  %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
