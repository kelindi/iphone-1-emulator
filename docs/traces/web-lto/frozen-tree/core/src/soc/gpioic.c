/*
 * S5LBox — Samsung S5L8900 GPIO interrupt controller, and the GPIO pin block.
 *
 * Two blocks, one file, because /arm-io/gpio is one device tree node with two
 * reg ranges and one driver reaches both: the interrupt cascade on the
 * 0x39a00000 page and the pin state on the 0x3e400000 page. See the GPIOIC and
 * GPIO sections of core/include/soc.h for the register map, the disassembly
 * behind every offset, and why the two pages are addressed the way they are.
 *
 * WHAT THIS EXISTS FOR. The guest has already armed the touch interrupt: run59
 * measured this controller's group-4 enable register holding 0x08000000, which
 * is bit 27, which is line 155, which is /arm-io/spi1/multi-touch's
 * `interrupts {0x9b,0}`. Against a storage stub that enable was written into a
 * word nobody read and no line could ever be asserted, so a touch report had
 * nowhere to go. This turns the enable into a route.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#include "soc.h"
#include <string.h>

/* ============================================================ interrupts === */

/*
 * The cascade, from /arm-io/gpio's own `interrupts` property:
 *   {0x21, 0x20, 0x1f, 0x03, 0x02, 0x01, 0x00}
 * one entry per interrupt group, in group order. It is descending, which looks
 * like a reversal and is not: the driver asks its provider for interrupt index
 * `group`, so index 4 — VIC line 2 — is the multi-touch group's line.
 *
 * Kept as one table behind one accessor because the same seven numbers are
 * needed by the bus routing, by the WFI wake table and by the tests, and three
 * transcriptions of a descending list is three chances to get one wrong.
 */
static const unsigned GPIOIC_CASCADE[S5L_GPIOIC_GROUPS] = {
    33u, 32u, 31u, 3u, 2u, 1u, 0u
};

unsigned s5l_gpioic_cascade(unsigned group) {
    /* Out of range answers with a line no VIC carries, which every caller —
     * s5l_vic_set_line(), wake_line_enabled() — already drops. */
    if (group >= S5L_GPIOIC_GROUPS) return 32u * S5L8900_VIC_COUNT;
    return GPIOIC_CASCADE[group];
}

void s5l_gpioic_reset(s5l_gpioic_t *g) {
    if (!g) return;
    /* Total, as the I2C and SPI controllers' resets are: valid on a poisoned
     * stack object, and it drops any level the board was driving. */
    memset(g, 0, sizeof *g);
}

static void note_unknown(s5l_gpioic_t *g, uint32_t off) {
    for (unsigned i = 0; i < g->unknown_off_count; i++)
        if (g->unknown_off[i] == off) return;
    if (g->unknown_off_count < S5L_GPIOIC_UNKNOWN_OFF)
        g->unknown_off[g->unknown_off_count++] = off;
}

/* Decode `off` into one of the four banks. Returns false for anything else. */
static bool decode(uint32_t off, uint32_t base, unsigned *group) {
    if (off < base) return false;
    uint32_t rel = off - base;
    if (rel & 3u) return false;
    uint32_t idx = rel >> 2;
    if (idx >= S5L_GPIOIC_GROUPS) return false;
    *group = (unsigned)idx;
    return true;
}

/*
 * THE PENDING LATCH IS EDGE-TRIGGERED, AND THAT WAS MEASURED, NOT CHOSEN.
 *
 * An earlier revision of this file made it LEVEL-sensitive: the write-one-to-
 * clear cleared the bit and then re-asserted it from whatever the board was
 * still driving. The reasoning was that a level latch cannot lose the second
 * report of a device that raises one mid-acknowledge, and that a device which
 * does deassert cannot tell the difference. The first half is true. The second
 * half is false, and run71 measured exactly how false:
 *
 *   HOT PAGE 0x39a00000, offset 0x0b0 (INTSTAT group 4):
 *       reads 1,193,122   writes 1,193,123   lastval 0x08000000
 *   @1799986776 R 0x39a000b0 val 0x08000000  pc 0xc05a44dc
 *   @1799986797 W 0x39a000b0 val 0x08000000  pc 0xc05a44e8
 *   @1799987195 R 0x39a000b0 val 0x08000000  pc 0xc05a44dc   <- 419 later
 *   @1799987216 W 0x39a000b0 val 0x08000000  pc 0xc05a44e8
 *
 * One touch report was queued at instruction 1,300,000,000 and the attention
 * line came up. The GPIO interrupt controller's filter read the pending word,
 * wrote it back to acknowledge, and our re-latch undid the acknowledge inside
 * the same store — so it re-entered every ~419 instructions and did so
 * 1,193,122 times, for the whole remaining half-billion instructions of the
 * run. `IOWorkLoop::signalWorkAvailable` was reached every time and the work
 * loop's thread was never scheduled, so AppleMultitouchZ2SPI's handler never
 * ran, never issued the SPI frame read, and the report was still pending at
 * the cap. A livelock, not a lost interrupt.
 *
 * The driver settles it a second way: it writes INTEN for this group exactly
 * TWICE in the whole boot (offset 0x0d0, writes 2), so it does not mask the
 * line while servicing it. A controller whose pending bit survived its own
 * acknowledge would be unusable by this driver, which means the real part's
 * does not.
 *
 * So: the write-one-to-clear CLEARS, and only a RISING edge on the incoming
 * line sets a pending bit again. A device that holds its line up until it is
 * serviced -- which is exactly what the Z2's attention line does -- gets one
 * interrupt per report, which is what it wants.
 *
 * THAT IS THE EDGE LINE. A LEVEL LINE IS A DIFFERENT MACHINE.
 *
 * INTLEVEL and INTTYPE used to be stored and never consulted, on the stated
 * grounds that "every `interrupts` property's second cell is zero". That was
 * only ever true of the lines that had been modelled. /device-tree/buttons
 * uses cells 7 and 5 for all five of its lines, and both have INTTYPE bit 0
 * set, which getInterruptType at 0xc05a429c proves is IOKit's own
 * kIOInterruptTypeLevel. See the register map in core/include/soc.h for the
 * full decode and the panic string that names bit 2 "auto-flip".
 *
 * For a line whose INTTYPE bit is set:
 *
 *   pending  <=  (raw bit == INTLEVEL bit)
 *
 * evaluated whenever anything on either side of that comparison moves while
 * the line is enabled. It is a SET-only rule. The latch is still a latch: only
 * the guest's write-one-to-clear clears it, so a condition that goes away on
 * its own does not silently un-report an interrupt the guest has already been
 * given.
 *
 * That is why this file re-evaluates in five places rather than one — a device
 * moving a line is only the most obvious of them:
 *
 *   set_line          the board moved the wire
 *   write(INTSTAT)    the guest acknowledged; if the enabled condition still
 *                     holds, a real level line asserts again immediately
 *   write(INTLEVEL)   the guest changed which level asserts. This one is the
 *                     whole mechanism, not an edge case — see below
 *   write(INTTYPE)    the guest changed whether this line is level at all
 *   write(INTEN)      masking lets an active line be acknowledged without an
 *                     IRQ-handler livelock; unmasking rechecks it immediately
 *
 * AUTO-FLIP, and why a button needs it. handleInterrupt at 0xc05a4358 reads a
 * software shadow of the device tree's cell bit 2 and, for a line that has it,
 * does `eor r2, r0, r6` on the INTLEVEL word and writes it back — inverting
 * that one line's polarity — BEFORE dispatching the child handler and BEFORE
 * acknowledging. So one wire reports both transitions: a press asserts, the
 * flip makes the asserted condition false, the acknowledge therefore STICKS,
 * and the release asserts again against the flipped polarity. Model the
 * INTLEVEL write without re-evaluating and half of that disappears; model the
 * acknowledge without the flip having happened first and it is run71's
 * livelock again, on five more lines.
 *
 * Auto-flip itself is NOT modelled here, and deliberately: it is something the
 * GUEST does, with a store this file already sees. Reproducing it in the model
 * as well would mean the polarity flipped twice per interrupt.
 *
 * The EDGE path is untouched, including its rising-edge sense. There is an
 * argument from the polarity byte that a cell-0 line is really active low in
 * silicon and therefore falling-edge; it is unobservable, because no cell-0
 * line in this machine has both a pin the guest reads and an interrupt, and
 * changing it would break the one proven input path this emulator has.
 */

/*
 * Re-evaluate every LEVEL-configured line of one group and latch the ones whose
 * asserting condition holds. SET-ONLY: see the note above. Edge lines are not
 * touched at all here, which is what keeps the whole currently-working world —
 * multi-touch included, whose cell is 0 — running through set_line's rising
 * edge and nothing else.
 */
static void relatch_level(s5l_gpioic_t *g, unsigned group) {
    /*
     * `raw ^ level` is 0 in the bits where the wire equals the selected
     * polarity, so its complement is "asserting". It is masked three times:
     *
     *   `type`   keeps only the lines the guest asked to be level-sensitive.
     *   `driven` keeps only the lines this machine has a device on.
     *   `en`     suppresses a masked line until the guest unmasks it.
     *
     * THE SECOND MASK IS NOT DEFENSIVE. It was measured. Without it, run87
     * spent its whole budget in an interrupt storm: /arm-io/i2c0/als and
     * /arm-io/i2c0/pmu both have interrupt cell 1, which is LEVEL with
     * INTLEVEL 0 — assert while LOW. Before the PMU interrupt was wired, this
     * machine modelled neither device, so their `raw` bit sat at the array's
     * initial zero and `0 == 0` made them permanently asserted. The PMU now
     * drives its real line; ALS remains absent, and every line is still
     * undriven between reset and its device's first refresh. The guest read and
     * acknowledged group 2's pending word 668,039 times and never got past
     * instruction ~96 million when that distinction was missing.
     *
     * An undriven line is not a line at level zero. It is a line with nothing
     * on the end of it, and the model has to be able to say so — which is what
     * `driven` is for, and why it is set by s5l_gpioic_set_line() for a FALSE
     * level as well as a true one. A device stating "my line is low" is
     * driving it; the absence of a device is not.
     *
     * The edge path never needed this: an undriven line has no rising edge, so
     * it silently never latched. That is exactly why this was invisible until a
     * level line existed.
     */
    g->stat[group] |= ~(g->raw[group] ^ g->level[group]) &
                      g->type[group] & g->driven[group] & g->en[group];
}

uint32_t s5l_gpioic_read(s5l_gpioic_t *g, uint32_t off) {
    unsigned group;
    if (!g) return 0u;
    if (decode(off, GPIOIC_INTLEVEL, &group)) return g->level[group];
    if (decode(off, GPIOIC_INTSTAT,  &group)) return g->stat[group];
    if (decode(off, GPIOIC_INTEN,    &group)) return g->en[group];
    if (decode(off, GPIOIC_INTTYPE,  &group)) return g->type[group];
    g->unknown_reads++;
    note_unknown(g, off);
    return 0u;
}

void s5l_gpioic_write(s5l_gpioic_t *g, uint32_t off, uint32_t val) {
    unsigned group;
    if (!g) return;
    if (decode(off, GPIOIC_INTLEVEL, &group)) {
        /* Configuration. Stored whole: the driver only ever reaches it through
         * a read-modify-write of one bit, so storing the word it hands back is
         * the same thing and needs no bit interpretation. */
        g->level[group] = val;
        /* And this is the auto-flip store. The guest has just changed which
         * level asserts, so a level line can start or stop asserting without
         * any wire having moved. Re-evaluating here is what makes a button
         * report its RELEASE as well as its press. */
        relatch_level(g, group);
        return;
    }
    if (decode(off, GPIOIC_INTSTAT, &group)) {
        /*
         * WRITE-ONE-TO-CLEAR, and nothing else. This is not a guess: the enable
         * path at 0xc05a56a0 writes exactly `1 << bit` here immediately before
         * enabling that one line, which is only meaningful as "discard the
         * stale pending bit for the line I am about to arm". A register that
         * SET pending bits instead would have the driver raising an interrupt
         * every time it enabled a line.
         */
        /* And for an EDGE line it CLEARS. It does not re-latch from what is
         * still driven — see the note above, and run71's 1,193,122
         * acknowledges. */
        g->stat[group] &= ~val;
        /* A LEVEL line is the opposite, and that is not a contradiction: it is
         * what "level-sensitive" means. If an enabled wire still matches the
         * selected polarity, the condition is still true and the part asserts.
         * This does not reintroduce run71 for the buttons, because the guest's
         * own auto-flip has already inverted the polarity by the time it
         * acknowledges — handleInterrupt flips at 0xc05a4384 and acknowledges
         * a level line at 0xc05a4418, in that order. The PMU line uses the
         * other valid route: its child disables line 85 before acknowledging,
         * queues PMU work, then unmasks after that work has deasserted INT_N. */
        relatch_level(g, group);
        return;
    }
    if (decode(off, GPIOIC_INTEN, &group)) {
        /*
         * A plain mask, not a set/clear pair. The driver keeps its own shadow
         * word per group at object+0x88 and stores the whole shadow here on
         * both paths — `orr` then store when enabling (0xc05a56b0-0xc05a56d8),
         * `bic` then store when disabling (0xc05a5748-0xc05a5788) — and the
         * init loop stores a bare zero (0xc05a51a8). A write-one-to-set model
         * would make that disable path a no-op and leave every line armed.
         */
        g->en[group] = val;
        /* A level that remained active while masked asserts as soon as the
         * guest unmasks it. Conversely, writing zero above makes a following
         * W1C stick until the deferred device handler has cleared the source. */
        relatch_level(g, group);
        return;
    }
    if (decode(off, GPIOIC_INTTYPE, &group)) {
        g->type[group] = val;
        /* A line the guest has just declared level-sensitive is level-sensitive
         * NOW, not after the next time its wire moves. initVector writes this
         * register before INTLEVEL (0xc05a55e4 then 0xc05a5608), so on the
         * first configure this evaluates against a polarity bit that is about
         * to change — which is why INTLEVEL re-evaluates too, and why both
         * being SET-only rather than assignments matters. */
        relatch_level(g, group);
        return;
    }
    g->unknown_writes++;
    note_unknown(g, off);
}

void s5l_gpioic_set_line(s5l_gpioic_t *g, unsigned line, bool level) {
    if (!g || line >= S5L_GPIOIC_LINES) return;
    unsigned group = line >> 5;
    uint32_t bit   = 1u << (line & 31u);
    bool was = (g->raw[group] & bit) != 0u;
    bool first = (g->driven[group] & bit) == 0u;
    if (level) g->raw[group] |= bit;
    else       g->raw[group] &= ~bit;
    /* Something is on the end of this wire. See relatch_level() for why that is
     * a different fact from "the wire is low", and what it cost to learn. */
    g->driven[group] |= bit;

    if (g->type[group] & bit) {
        /*
         * LEVEL. Either transition can start an assertion, because which level
         * asserts is the guest's choice and it inverts that choice after every
         * interrupt on an auto-flip line. A line that has not moved cannot
         * change the answer, so this is gated on the change — and on the very
         * first drive, which is a change from "nothing on the wire" and is not
         * visible in `raw` — and stays free of the per-tick refresh cost.
         */
        if (level != was || first) relatch_level(g, group);
        return;
    }
    /*
     * EDGE, and A RISING EDGE latches. Two consequences worth stating, because
     * callers refresh this every tick with the current level:
     *   - a line that is merely STILL high sets nothing, which is what stops
     *     the acknowledge/re-assert livelock run71 measured;
     *   - deasserting does NOT clear the latch, so a pulse shorter than the
     *     guest's polling interval is still delivered. Only the guest's
     *     write-one-to-clear clears it.
     */
    if (level && !was) g->stat[group] |= bit;
}

bool s5l_gpioic_consume_autoflip_level(s5l_gpioic_t *g, unsigned line,
                                       bool level) {
    if (!g || line >= S5L_GPIOIC_LINES) return false;
    unsigned group = line >> 5;
    uint32_t bit = 1u << (line & 31u);
    if (!(g->type[group] & bit)) return false;

    /*
     * This transition has already been reported through a second hardware
     * path, so putting it through set_line() would deliver it twice. Preserve
     * the facts the next transition depends on instead: something drives the
     * wire, the wire is at `level`, no interrupt for this consumed edge is
     * pending, and AUTO-FLIP now selects the opposite electrical level.
     *
     * The last assignment deliberately does not call relatch_level(). The
     * opposite level is non-asserting by construction; calling it would be
     * harmless but would obscure the stronger invariant this helper exists to
     * establish.
     */
    g->driven[group] |= bit;
    if (level) g->raw[group] |= bit;
    else       g->raw[group] &= ~bit;
    g->stat[group] &= ~bit;
    if (level) g->level[group] &= ~bit;
    else       g->level[group] |= bit;
    return true;
}

bool s5l_gpioic_line(const s5l_gpioic_t *g, unsigned line) {
    if (!g || line >= S5L_GPIOIC_LINES) return false;
    return (g->raw[line >> 5] & (1u << (line & 31u))) != 0u;
}

bool s5l_gpioic_pending(const s5l_gpioic_t *g, unsigned line) {
    if (!g || line >= S5L_GPIOIC_LINES) return false;
    return (g->stat[line >> 5] & (1u << (line & 31u))) != 0u;
}

bool s5l_gpioic_group_irq(const s5l_gpioic_t *g, unsigned group) {
    if (!g || group >= S5L_GPIOIC_GROUPS) return false;
    return (g->stat[group] & g->en[group]) != 0u;
}

/* ============================================================== pin block === */

void s5l_gpio_reset(s5l_gpio_t *g) {
    if (!g) return;
    memset(g, 0, sizeof *g);
}

/*
 * Honest byte-addressable storage over the whole page, assembled a byte at a
 * time exactly as the storage stub this replaces did — so an 8- or 16-bit
 * access updates the correct lanes and nothing can index past the array. The
 * page really is 4 KiB of registers we have not decoded; pretending otherwise
 * by answering an undecoded offset with a fabricated value is the failure the
 * stub design exists to avoid.
 */
uint32_t s5l_gpio_read(const s5l_gpio_t *g, uint32_t off, unsigned bytes) {
    uint32_t v = 0;
    if (!g || !bytes || bytes > 4u) return 0u;
    if ((uint64_t)off + bytes > (uint64_t)S5L8900_DEV_SIZE) return 0u;
    for (unsigned i = 0; i < bytes; i++) {
        uint32_t at = off + i;
        v |= ((g->regs[at >> 2] >> ((at & 3u) * 8u)) & 0xffu) << (i * 8u);
    }
    return v;
}

/*
 * Tell every watcher whose pin's level differs from the snapshot in `before`.
 * One implementation, shared by the guest's fsel store and by the board's
 * s5l_gpio_drive(), so a watcher cannot be told about one and not the other.
 */
static void notify(s5l_gpio_t *g, const uint32_t *before) {
    for (unsigned w = 0; w < S5L_GPIO_WATCHERS; w++) {
        s5l_gpio_watch_t *watch = &g->watch[w];
        if (!watch->armed || !watch->changed) continue;
        unsigned group = (unsigned)(watch->pin >> 8);
        if (group >= S5L_GPIO_PORTS) continue;
        uint32_t mask = 1u << (watch->pin & 0x1fu);
        uint32_t now  = g->regs[S5L_GPIO_PIN_REG(group) >> 2];
        if (((before[group] ^ now) & mask) == 0u) continue;
        watch->changed(watch->ctx, (now & mask) != 0u);
    }
}

/* True if the byte at `off` belongs to some group's read-only level register. */
static bool is_pin_reg(uint32_t off) {
    uint32_t word = off & ~3u;
    if (word < 4u || ((word - 4u) % 32u) != 0u) return false;
    return ((word - 4u) / 32u) < S5L_GPIO_PORTS;
}

void s5l_gpio_write(s5l_gpio_t *g, uint32_t off, uint32_t val, unsigned bytes) {
    uint32_t before[S5L_GPIO_PORTS];
    if (!g || !bytes || bytes > 4u) return;
    if ((uint64_t)off + bytes > (uint64_t)S5L8900_DEV_SIZE) return;

    /* Snapshot every pin word so a watcher is told about a real change and not
     * merely about a store. */
    for (unsigned i = 0; i < S5L_GPIO_PORTS; i++)
        before[i] = g->regs[S5L_GPIO_PIN_REG(i) >> 2];

    for (unsigned i = 0; i < bytes; i++) {
        uint32_t at = off + i;
        /* The level registers are read-only. Dropping the store rather than
         * storing it is what makes the fsel path the ONLY way a pin moves, so
         * a model that quietly accepted both could not be wrong about which
         * one the guest actually uses. */
        if (is_pin_reg(at)) continue;
        uint32_t shift = (at & 3u) * 8u;
        uint32_t *reg = &g->regs[at >> 2];
        *reg = (*reg & ~(0xffu << shift)) |
               (((val >> (i * 8u)) & 0xffu) << shift);
    }

    /*
     * The function-select store is how a pin is driven:
     *   write32(base + 0x320, (group << 16) | (bit << 8) | 0xE | level)
     * One 32-bit store, no read-modify-write, and 0x320 is the only offset on
     * this page run59 observed the guest touch at all.
     *
     * A word store only: a byte or halfword cannot carry the group, the bit and
     * the function together, so treating a partial store as a pin command would
     * be acting on a value the guest has not finished assembling.
     */
    if (off == S5L_GPIO_FSEL && bytes == 4u) {
        unsigned group = (val >> 16) & 0xffu;
        unsigned bit   = (val >> 8) & 0xffu;
        if (group < S5L_GPIO_PORTS && bit < 32u &&
            ((val >> 1) & 0x7u) == S5L_GPIO_FUNC_OUT) {
            uint32_t *level = &g->regs[S5L_GPIO_PIN_REG(group) >> 2];
            if (val & 1u) *level |=  (1u << bit);
            else          *level &= ~(1u << bit);
        }
    }

    notify(g, before);
}

/*
 * The board drives an input pin. The level register is read-only to the GUEST
 * — s5l_gpio_write() drops every store to it — but it is not read-only to the
 * wire, and something has to put an input's level there. Kept as its own entry
 * point on purpose: if a guest store and a board level reached the same code,
 * the model could no longer answer "which one moved this pin", which is the
 * question the read-only level register was built to settle.
 */
void s5l_gpio_drive(s5l_gpio_t *g, uint16_t pin, bool level) {
    uint32_t before[S5L_GPIO_PORTS];
    unsigned group = (unsigned)(pin >> 8);
    unsigned bit   = (unsigned)(pin & 0xffu);
    if (!g || group >= S5L_GPIO_PORTS || bit > 31u) return;

    uint32_t *reg = &g->regs[S5L_GPIO_PIN_REG(group) >> 2];
    /*
     * The pin is already there, so there is nothing to tell anyone: this call
     * can move at most the one bit below, and notify() only fires a watcher
     * whose own pin differs from the snapshot. Returning here is not a
     * shortcut past a side effect, it is skipping a 25-word snapshot and a
     * watcher sweep whose every comparison is guaranteed equal.
     *
     * Worth its own branch because s5l_buttons_apply() re-drives five
     * unchanged switches on every full refresh, so this is five snapshots of
     * the whole pin map per tick that exist only to prove nothing happened.
     * See `level_dirty` in soc.h for what the refresh costs and why it runs.
     */
    if (((*reg >> bit) & 1u) == (level ? 1u : 0u)) return;

    for (unsigned i = 0; i < S5L_GPIO_PORTS; i++)
        before[i] = g->regs[S5L_GPIO_PIN_REG(i) >> 2];

    if (level) *reg |=  (1u << bit);
    else       *reg &= ~(1u << bit);

    /* A watcher is a subscription to the PIN, and the pin moved. It still only
     * fires on a real change, which is what makes s5l_buttons_apply() safe to
     * call on every tick. */
    notify(g, before);
}

bool s5l_gpio_pin(const s5l_gpio_t *g, uint16_t pin) {
    unsigned group = (unsigned)(pin >> 8);
    if (!g || group >= S5L_GPIO_PORTS) return false;
    return (g->regs[S5L_GPIO_PIN_REG(group) >> 2] &
            (1u << (pin & 0x1fu))) != 0u;
}

bool s5l_gpio_watch(s5l_gpio_t *g, uint16_t pin, void *ctx,
                    void (*changed)(void *ctx, bool level)) {
    if (!g || !changed) return false;
    if ((unsigned)(pin >> 8) >= S5L_GPIO_PORTS) return false;
    if ((pin & 0xffu) > 31u) return false;
    for (unsigned i = 0; i < S5L_GPIO_WATCHERS; i++)
        if (g->watch[i].armed && g->watch[i].pin == pin) return false;
    for (unsigned i = 0; i < S5L_GPIO_WATCHERS; i++)
        if (!g->watch[i].armed) {
            g->watch[i].ctx = ctx;
            g->watch[i].pin = pin;
            g->watch[i].changed = changed;
            g->watch[i].armed = true;
            return true;
        }
    return false;
}
