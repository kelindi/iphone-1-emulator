/*
 * S5LBox — S5L8900 timer block.
 *
 * Two independent things live here, and conflating them is the mistake that
 * kept this kernel silent:
 *
 *   1. A free-running 64-bit counter at 0x080/0x084. This is the backing store
 *      for mach_absolute_time(). It counts unconditionally. If it reads zero
 *      forever, every delay loop and every timeout in the kernel waits forever,
 *      and the boot dies quietly in a spin lock rather than with a panic.
 *
 *   2. Timer 4, a down-counter the kernel arms as its decrementer to get a
 *      periodic FIQ. On expiry it latches TIMER4_IRQ_BITS, which is exactly
 *      the mask the kernel's own FIQ handler writes back to acknowledge.
 *
 * The register semantics below are inferred from what the real xnu-1357 kernel
 * does with this block, not from a datasheet — no public S5L8900 documentation
 * exists. Where behaviour is unobserved it is left inert rather than invented.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#include "soc.h"
#include <string.h>

void s5l_timer_reset(s5l_timer_t *t) { memset(t, 0, sizeof *t); }

uint32_t s5l_timer_read(s5l_timer_t *t, uint32_t off) {
    switch (off) {
        case TIMER_TICKSLOW:   return (uint32_t)t->ticks;
        case TIMER_TICKSHIGH:  return (uint32_t)(t->ticks >> 32);
        case TIMER_CONFIG:     return t->config;
        case TIMER4_CONFIG:    return t->t4_config;
        case TIMER4_STATE:     return t->t4_state;
        case TIMER4_COUNTBUF:  return t->t4_count;
        case TIMER4_COUNTBUF2: return t->t4_count2;
        case TIMER4_VALUE:     return t->t4_value;
        case TIMER_IRQLATCH:   return t->irqlatch;
        case TIMER_IRQSTATUS:  return t->irqlatch;
        default:               return 0;
    }
}

void s5l_timer_write(s5l_timer_t *t, uint32_t off, uint32_t val) {
    switch (off) {
        case TIMER_CONFIG:     t->config = val; break;
        case TIMER4_CONFIG:    t->t4_config = val; break;

        case TIMER4_STATE:
            t->t4_state = val;
            /* The kernel writes start|update together; the update bit is what
             * loads the buffered count into the live counter. */
            if (val & TIMER4_STATE_UPDATE) t->t4_value = t->t4_count;
            break;

        case TIMER4_COUNTBUF:
            /* set_decrementer writes the next deadline here and expects it to
             * take effect without a separate update, so load the live counter
             * as well as the buffer. */
            t->t4_count = val;
            t->t4_value = val;
            break;

        case TIMER4_COUNTBUF2: t->t4_count2 = val; break;

        /* Write-1-to-clear. The FIQ handler acknowledges with TIMER4_IRQ_BITS;
         * failing to drop the latch here leaves the line asserted so the
         * handler re-enters immediately, which presents as a hang. */
        case TIMER_IRQACK:
        case TIMER_IRQLATCH:   t->irqlatch &= ~val; break;
        case TIMER_IRQSTATUS:  t->irqlatch = 0; break;

        default: break;
    }
}

bool s5l_timer_tick(s5l_timer_t *t, uint32_t ticks) {
    t->ticks += ticks;                        /* unconditional: this is time */

    if ((t->t4_state & TIMER4_STATE_START) && ticks) {
        /*
         * Advance algebraically.  WFI can legitimately move guest time by a
         * complete decrementer interval in one call; iterating once per
         * timebase tick would turn a safe idle fast-forward back into another
         * busy loop.
         */
        uint32_t value = t->t4_value;

        if (value == 0) value = t->t4_count;  /* reload before the first tick */
        if (value != 0) {
            if (ticks < value) {
                t->t4_value = value - ticks;
            } else {
                /*
                 * One-shot: the kernel re-arms via set_decrementer (count
                 * buffer + start|update) on every expiry.  Auto-reloading
                 * here would re-latch the FIQ while the handler is still
                 * running whenever the handler takes longer than the armed
                 * interval, and the 10-tick minimum deadline then livelocks.
                 */
                t->irqlatch |= TIMER4_IRQ_BITS;
                t->t4_value = 0;
                t->t4_state &= ~TIMER4_STATE_START;
            }
        }
    }
    return t->irqlatch != 0;
}
