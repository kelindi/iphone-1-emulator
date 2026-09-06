/*
 * S5LBox -- S5L8900 standard-definition TV-out path.
 *
 * This is deliberately a narrow hardware model.  The shipped Apple driver
 * establishes the three independent run/ready handshakes, SDO's VSYNC
 * W1C/mask pair, and IRQ 30 as swap completion.  Everything else is retained
 * as register storage until firmware evidence justifies a side effect.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#include "soc.h"
#include <string.h>

static bool valid_access(const s5l_tvout_t *t, s5l_tvout_bank_t bank,
                         uint32_t off, unsigned bytes) {
    return t && (unsigned)bank < S5L_TVOUT_BANK_COUNT &&
           (bytes == 1u || bytes == 2u || bytes == 4u) &&
           (uint64_t)off + bytes <= S5L_TVOUT_BANK_SIZE;
}

/* Bit 1 of each block's first word is hardware-owned.  The driver clears run
 * bit 0 and polls ready bit 1 during shutdown.  Derive it at read time so a
 * guest write cannot pin readiness, while retaining unrelated bits (notably
 * mixer bit 2: the start path writes 5). */
static uint32_t visible_word(const s5l_tvout_t *t, s5l_tvout_bank_t bank,
                             uint32_t word) {
    uint32_t v = t->regs[bank][word];
    if (word == 0u) {
        v &= ~TVOUT_READY;
        if ((v & TVOUT_RUN) == 0u) v |= TVOUT_READY;
    }
    return v;
}

bool s5l_tvout_running(const s5l_tvout_t *t) {
    if (!t) return false;
    /*
     * The control/coefficient block has its own run/ready handshake, but is
     * not a persistent scan-timing gate.  The shipped resume path programs
     * that page without setting CTRL+0, then starts MIXER+0 and SDO+0; its
     * IRQ30 filter likewise consults SDO and mixer state, never CTRL+0.
     * Requiring all three reproduced run19's impossible 0/5/1 deadlock.
     */
    return (t->regs[S5L_TVOUT_BANK_MIXER][0] & TVOUT_RUN) != 0u &&
           (t->regs[S5L_TVOUT_BANK_SDO][0] & TVOUT_RUN) != 0u;
}

void s5l_tvout_reset(s5l_tvout_t *t, uint32_t tb_hz) {
    if (!t) return;
    memset(t, 0, sizeof *t);
    t->frame_ticks = tb_hz / S5L_TVOUT_REFRESH_HZ;
    /* The driver's start path explicitly unmasks with zero.  Begin masked so
     * reset/partial setup can never surface a fabricated stale VBlank. */
    t->regs[S5L_TVOUT_BANK_SDO][TVOUT_SDO_IRQMASK / 4u] =
        TVOUT_SDO_MASK_VSYNC;
}

uint32_t s5l_tvout_read(const s5l_tvout_t *t, s5l_tvout_bank_t bank,
                        uint32_t off, unsigned bytes) {
    if (!valid_access(t, bank, off, bytes)) return 0;

    uint32_t v = 0;
    for (unsigned i = 0; i < bytes; i++) {
        uint32_t at = off + i;
        uint32_t word = visible_word(t, bank, at >> 2);
        uint32_t byte = (word >> ((at & 3u) * 8u)) & 0xffu;
        v |= byte << (i * 8u);
    }
    return v;
}

void s5l_tvout_write(s5l_tvout_t *t, s5l_tvout_bank_t bank,
                      uint32_t off, uint32_t val, unsigned bytes) {
    if (!valid_access(t, bank, off, bytes)) return;

    bool was_running = s5l_tvout_running(t);

    for (unsigned i = 0; i < bytes; i++) {
        uint32_t at = off + i;
        uint32_t word_off = at & ~3u;
        uint32_t word_index = at >> 2;
        uint32_t shift = (at & 3u) * 8u;
        uint32_t lane_mask = 0xffu << shift;
        uint32_t lane_value = ((val >> (i * 8u)) & 0xffu) << shift;
        uint32_t *reg = &t->regs[bank][word_index];

        if ((bank == S5L_TVOUT_BANK_SDO && word_off == TVOUT_SDO_IRQ) ||
            (bank == S5L_TVOUT_BANK_MIXER &&
             word_off == TVOUT_MIXER_STATUS)) {
            /* Both status words are W1C.  Only SDO bit 0 is generated here;
             * mixer stays nonasserting until cable-detect behavior is proven. */
            *reg &= ~(lane_value & lane_mask);
        } else {
            *reg = (*reg & ~lane_mask) | (lane_value & lane_mask);
            if (word_index == 0u) *reg &= ~TVOUT_READY;
        }
    }

    /* Entering or leaving the mixer+SDO timing state starts a fresh frame.
     * A control-block transition is independent and must preserve phase.  The
     * pending latch is intentionally NOT cleared here: the shipped driver
     * explicitly W1C-acknowledges it during both start and stop, and status
     * hardware must not silently eat a real completion on a gate change. */
    if (was_running != s5l_tvout_running(t))
        t->frame_accum = 0;
}

bool s5l_tvout_irq(const s5l_tvout_t *t) {
    if (!s5l_tvout_running(t)) return false;
    uint32_t pending =
        t->regs[S5L_TVOUT_BANK_SDO][TVOUT_SDO_IRQ / 4u];
    uint32_t mask =
        t->regs[S5L_TVOUT_BANK_SDO][TVOUT_SDO_IRQMASK / 4u];
    return (pending & TVOUT_SDO_VSYNC) != 0u &&
           (mask & TVOUT_SDO_MASK_VSYNC) == 0u;
}

bool s5l_tvout_tick(s5l_tvout_t *t, uint32_t ticks) {
    if (!t) return false;
    if (!s5l_tvout_running(t) || t->frame_ticks == 0u) {
        t->frame_accum = 0;
        return false;
    }

    uint64_t total = (uint64_t)t->frame_accum + ticks;
    uint64_t elapsed = total / t->frame_ticks;
    t->frame_accum = (uint32_t)(total % t->frame_ticks);
    if (elapsed != 0u) {
        t->frames += elapsed;
        t->regs[S5L_TVOUT_BANK_SDO][TVOUT_SDO_IRQ / 4u] |=
            TVOUT_SDO_VSYNC;
    }
    return s5l_tvout_irq(t);
}

uint32_t s5l_tvout_ticks_to_vsync(const s5l_tvout_t *t) {
    if (!t || !s5l_tvout_running(t) || t->frame_ticks == 0u ||
        s5l_tvout_irq(t) ||
        (t->regs[S5L_TVOUT_BANK_SDO][TVOUT_SDO_IRQMASK / 4u] &
         TVOUT_SDO_MASK_VSYNC) != 0u)
        return 0;

    /* A malformed in-memory phase must not wrap the WFI distance.  One tick
     * lets tick() normalize it and is the earliest conservative boundary. */
    return t->frame_accum < t->frame_ticks
         ? t->frame_ticks - t->frame_accum : 1u;
}
