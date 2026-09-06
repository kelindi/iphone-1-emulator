/*
 * S5LBox — the iPhone1,2's five physical buttons.
 *
 * Home (the tree calls it `menu`), Power/Hold, Volume Up, Volume Down and the
 * ringer/silent slider. There is no button chip: the board wires five switches
 * to five GPIO pins on port 22 and five lines of the GPIO interrupt
 * controller's group 1, and com.apple.driver.AppleM68Buttons is the driver.
 * This file is therefore a model of WIRING, and every number in it comes from
 * /device-tree/buttons or from that driver's own code.
 *
 * The full evidence — the device tree node, the interrupt-cell decode, the
 * polarity byte and its independent corroboration, the 14 ms debounce, and the
 * HID usages the guest dispatches — is in the buttons section of
 * core/include/soc.h. Read that first; this file assumes it.
 *
 * WHAT THIS EXISTS FOR. The GPIO interrupt controller landed with a working
 * level/edge latch and nothing but the touch controller to drive it, and
 * run86 measured the guest arming all five of these lines at instruction
 * 238,689,154 and then waiting forever: INTEN group 1 settles at 0x00002f00,
 * which is bits 8, 9, 10, 11 and 13, which is lines 40, 41, 42, 43 and 45,
 * which is exactly this node's `interrupts` array. Five armed lines with
 * nothing on the other end. This puts something on the other end.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#include "soc.h"
#include <string.h>

/*
 * THE WIRING TABLE, IN THE DEVICE TREE'S OWN ORDER.
 *
 * `button-names` is 'hold\0menu\0volup\0voldown\0ringerab\0' and `interrupts`
 * is {45,7} {40,7} {41,5} {42,5} {43,7}; they are parallel arrays, because
 * AppleM68Buttons walks the names in order and asks its provider for interrupt
 * index i (0xc065a6b0). So index 0 is `hold` on line 45, and this table is in
 * that order so that a reader can lay it against `dtwalk.py buttons` and check
 * it a row at a time. Sorting it into a nicer order would still compile.
 *
 * `active_high` is byte 1 of the fourth word of each `function-button_*`
 * property, which AppleS5L8900X's platform-function read path at 0xc05a45d8
 * uses as the pin's polarity and nothing else. It is NOT a guess: the same
 * byte is 0 for /arm-io/spi1/multi-touch's `function-reset`, a pin this
 * project had already established as active low from the driver's behaviour.
 */
static const struct {
    const char *name;
    uint16_t    pin;          /* device-tree platform-function GPIO id     */
    uint8_t     line;         /* flat GPIO interrupt controller line index */
    bool        active_high;  /* byte 1 of the function property's 4th word */
    bool        inverts;      /* AppleM68Buttons inverts it again, 0xc065ab14 */
} WIRING[S5L_BUTTON_COUNT] = {
    /* name        pin      line  act_hi  drv_inv   device-tree 4th word */
    { "hold",     0x1605u,  45u,  true,   false },  /* 0x00000100 */
    { "menu",     0x1600u,  40u,  true,   false },  /* 0x00000100 */
    { "volup",    0x1601u,  41u,  false,  false },  /* 0x00000000 */
    { "voldown",  0x1602u,  42u,  false,  false },  /* 0x00000000 */
    { "ringerab", 0x1603u,  43u,  false,  true  },  /* 0x00010000 */
};

const char *s5l_button_name(unsigned which) {
    if (which >= S5L_BUTTON_COUNT) return "?";
    return WIRING[which].name;
}

uint16_t s5l_button_pin(unsigned which) {
    /* Group 0xff does not exist — #gpio-ports is 25 — so every pin accessor
     * already drops this rather than aliasing onto a real pin. */
    if (which >= S5L_BUTTON_COUNT) return 0xffffu;
    return WIRING[which].pin;
}

unsigned s5l_button_line(unsigned which) {
    /* Past the controller's 224 lines, which s5l_gpioic_set_line() refuses. */
    if (which >= S5L_BUTTON_COUNT) return S5L_GPIOIC_LINES;
    return WIRING[which].line;
}

bool s5l_button_active_high(unsigned which) {
    /* A button that does not exist is not active high. The answer is only ever
     * combined with a `which` that has already been range-checked, but an
     * out-of-range true here would make the out-of-range REST level high,
     * which is the direction that invents a press. */
    if (which >= S5L_BUTTON_COUNT) return false;
    return WIRING[which].active_high;
}

bool s5l_button_driver_inverts(unsigned which) {
    if (which >= S5L_BUTTON_COUNT) return false;
    return WIRING[which].inverts;
}

bool s5l_button_level(unsigned which, bool pressed) {
    /*
     * BOTH INVERSIONS, COMPOSED, ONCE, HERE.
     *
     * `pressed == active_high` reads oddly and is exactly right for a plain
     * key: an active-high one is high when pressed and low when not, an
     * active-low one is the reverse. The ringer needs the second XOR because
     * AppleM68Buttons inverts it again before dispatching, so the two cancel
     * and the level that makes the guest say "muted" is simply HIGH — which is
     * the same thing as saying the dispatched Phone Mute value is the raw pin.
     *
     * Composing them anywhere but here would mean two files had to agree about
     * the ringer, and the one that was wrong would be the one nobody read.
     */
    if (which >= S5L_BUTTON_COUNT) return false;
    return pressed == (WIRING[which].active_high != WIRING[which].inverts);
}

void s5l_buttons_reset(s5l_buttons_t *b) {
    if (!b) return;
    /*
     * Total, as every other reset in this directory is: valid on a poisoned
     * stack object. Zero means every button released and the slider unmuted.
     *
     * Zero here is NOT zero on the wire. `volup` and `voldown` are active low,
     * so released means their pins sit HIGH, and a machine that leaves them at
     * the pin block's power-on zero has handed the guest two volume keys held
     * down from the moment it programs INTLEVEL. s5l_buttons_apply() is what
     * establishes the real rest levels and s5l8900_init() calls it.
     */
    memset(b, 0, sizeof *b);
}

bool s5l_buttons_held(const s5l_buttons_t *b, unsigned which) {
    if (!b || which >= S5L_BUTTON_COUNT) return false;
    return (b->pressed & (1u << which)) != 0u;
}

void s5l_buttons_apply(const s5l_buttons_t *b, s5l_gpio_t *gpio,
                       s5l_gpioic_t *ic) {
    if (!b) return;
    for (unsigned i = 0; i < S5L_BUTTON_COUNT; i++) {
        bool level = s5l_button_level(i, s5l_buttons_held(b, i));
        /*
         * BOTH ENDS OF THE SAME SWITCH, and they are two different numbering
         * spaces that happen to line up on this board: pin 0x1600+k is port 22
         * bit k, and line 40+k is group 1 bit 8+k. The device tree states the
         * two independently and this table copies both rather than deriving
         * one from the other, because the correspondence is a property of the
         * board's copper and not of an arithmetic rule.
         *
         * The pin is what the guest's driver actually READS, through its
         * platform function; the line is what interrupts it. A model with only
         * one of them either never wakes the driver or wakes it to read a pin
         * that never moved.
         */
        if (gpio) s5l_gpio_drive(gpio, WIRING[i].pin, level);
        if (ic)   s5l_gpioic_set_line(ic, WIRING[i].line, level);
    }
}

bool s5l_buttons_set(s5l_buttons_t *b, s5l_gpio_t *gpio, s5l_gpioic_t *ic,
                     unsigned which, bool pressed) {
    if (!b) return false;
    if (which >= S5L_BUTTON_COUNT) { b->refused++; return false; }

    /*
     * FOUR-WAY HONESTY, and each refusal is a different fact about the board.
     *
     * Already in that state. Not a refusal and not an edge: a host holding a
     * button down is not making a request that can fail. Answered before the
     * two conditions below on purpose — a repeated "still pressed" must not be
     * turned into a refusal merely because the guest is mid-service.
     */
    bool now = s5l_buttons_held(b, which);
    if (now == pressed) { b->sets++; return true; }

    /*
     * The guest has not armed this line. AppleM68Buttons NEVER polls: its only
     * sampling path (0xc065a2a8) runs off a 14 ms one-shot timer that only an
     * interrupt arms (0xc065a31c). So a transition on a line whose INTEN bit
     * is clear is one the guest provably cannot observe, and reporting success
     * for it would make "the driver has not started" look like "the driver
     * ignored me". This is the button's `!hbpp_answered`.
     */
    if (!ic || !(ic->en[WIRING[which].line >> 5] &
                 (1u << (WIRING[which].line & 31u)))) {
        b->refused++;
        return false;
    }

    /*
     * The previous transition is still pending. Accepting now would be a real
     * loss, not merely early: with the polarity still where the last assertion
     * left it, driving the wire back collapses the press and the release into
     * the one latched bit the guest has yet to look at, and when it finally
     * samples the pin it sees the button exactly as it was. Neither half is
     * reported. Refusing makes the host pace itself and leaves the reason in a
     * counter.
     */
    if (s5l_gpioic_pending(ic, WIRING[which].line)) { b->refused++; return false; }

    if (pressed) b->pressed |=  (uint8_t)(1u << which);
    else         b->pressed &= (uint8_t)~(1u << which);
    b->sets++;
    b->edges++;

    /*
     * Take effect NOW, between two guest instructions, exactly as
     * s5l_mtz2_set_contacts() raises the attention line inside the call. The
     * per-tick s5l_buttons_apply() would get there too, but "the press landed
     * at the instruction the host chose" is a property worth having: it is
     * what makes a scheduled press reproducible.
     */
    s5l_gpio_drive(gpio, WIRING[which].pin,
                   s5l_button_level(which, pressed));
    s5l_gpioic_set_line(ic, WIRING[which].line,
                        s5l_button_level(which, pressed));
    return true;
}
