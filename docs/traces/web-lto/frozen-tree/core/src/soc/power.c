/*
 * S5LBox — S5L8900 power-gate controller.
 *
 * This block is what stood between a booting kernel and a booting OS. Apple's
 * AppleS5L8900XPowerController::start writes the domains it wants gated and
 * then spins reading STATE until it agrees:
 *
 *     write(OFFCTRL, 0x12fc);
 *     do { s = read(STATE); } while ((s & 0x12fc) != 0x12fc);
 *
 * With the window unmodelled, STATE read 0 forever: 3,887,707 reads, about a
 * quarter of a 200M-instruction boot, and start() never returned — so the
 * controller never registered and nothing downstream could power-gate anything.
 *
 * A storage stub cannot fix this, which is the whole reason this is a device
 * model rather than a stub: STATE is never written by the guest, so "return the
 * last value written" returns zero just as forever. The behaviour that matters
 * is the coupling — ONCTRL and OFFCTRL are the only things that move STATE.
 *
 * Polarity is taken from the driver's own generic gate routine, not guessed:
 *   power up:   write(ONCTRL,  1<<n); wait until STATE bit n is CLEAR
 *   power down: write(OFFCTRL, 1<<n); wait until STATE bit n is SET
 * so a set bit means "this domain is gated off". Nothing here self-clears and
 * nothing toggles on its own; STATE moves only in response to those two writes.
 *
 * The upper half of the page (0x80 and above) is a different block entirely —
 * the GPIO interrupt controller — and is deliberately not handled here.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#include "soc.h"
#include <string.h>

void s5l_power_reset(s5l_power_t *p) {
    memset(p, 0, sizeof *p);
    /*
     * Reset value is not observable: start() forces STATE before the first
     * read, so nothing in this boot depends on it. The device tree's
     * power-gate-defaults is the most faithful choice available, and 0 also
     * works. Recorded as a stated assumption rather than a silent constant.
     */
    p->state = S5L_POWER_GATE_DEFAULTS;
}

uint32_t s5l_power_read(s5l_power_t *p, uint32_t off) {
    switch (off) {
        case POWER_CONFIG0:  return p->cfg0;
        case POWER_CONFIG1:  return p->cfg1;
        /* SETSTATE mirrors STATE. XNU 3.1.3 never reads it; other firmware
         * (openiBoot) polls it instead of STATE, so mirroring costs nothing
         * and avoids a second silent-zero trap. */
        case POWER_SETSTATE: return p->state;
        case POWER_STATE:    return p->state;
        case POWER_SRAM:     return p->sram;
        case POWER_CFG24:    return p->cfg24;
        case POWER_CFG28:    return p->cfg28;
        default:             return 0;
    }
}

void s5l_power_write(s5l_power_t *p, uint32_t off, uint32_t val) {
    switch (off) {
        case POWER_CONFIG0: p->cfg0 = val; break;
        case POWER_CONFIG1: p->cfg1 = val; break;

        /* ONCTRL ungates: write-1-to-clear the corresponding STATE bits. */
        case POWER_ONCTRL:  p->state &= ~(val & S5L_POWER_DOMAIN_MASK); break;
        /* OFFCTRL gates: write-1-to-set. */
        case POWER_OFFCTRL: p->state |=  (val & S5L_POWER_DOMAIN_MASK); break;

        /* STATE is read-only; it moves only via ONCTRL/OFFCTRL. */
        case POWER_STATE:   break;

        case POWER_SRAM:    p->sram  = val; break;
        case POWER_CFG24:   p->cfg24 = val; break;
        case POWER_CFG28:   p->cfg28 = val; break;
        default: break;
    }
}
