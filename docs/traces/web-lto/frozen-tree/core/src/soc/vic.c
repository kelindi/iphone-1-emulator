/*
 * S5LBox — S5L8900 vectored interrupt controller (PL190-style).
 *
 * Devices assert numbered lines; the controller masks them by the enable
 * register and routes each to IRQ or FIQ. Its outputs drive arm_cpu_t's
 * irq_line/fiq_line, which arm_step samples before every fetch.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#include "soc.h"
#include <string.h>

static inline uint32_t pending(const s5l_vic_t *v) { return v->raw | v->soft; }

void s5l_vic_reset(s5l_vic_t *v) { memset(v, 0, sizeof *v); }

bool s5l_vic_irq(const s5l_vic_t *v) {
    return (pending(v) & v->enable & ~v->select) != 0;
}
bool s5l_vic_fiq(const s5l_vic_t *v) {
    return (pending(v) & v->enable & v->select) != 0;
}

void s5l_vic_set_line(s5l_vic_t *v, unsigned line, bool level) {
    if (line > 31) return;
    if (level) v->raw |= (1u << line);
    else       v->raw &= ~(1u << line);
}

/*
 * VICADDRESS (0xF00) read — the PL192 vectored-dispatch entry point.
 *
 * AppleARMPL192VIC's interrupt handler does NOT read IRQSTATUS to find the
 * pending source. It reads VICADDRESS, and decodes the returned word as a
 * global source number in bits[30:0] with bit 31 as a "valid" tag:
 *     source = ret & 0x7fffffff;  bit = ret & 0x1f
 * then dispatches through its vector table for that source. A read of 0 decodes
 * to source 0, which the driver treats as spurious: it acknowledges and returns
 * without clearing anything — so a real interrupt (the self-IPI on line 4) that
 * reads back as source 0 re-fires forever. That was the boot-stopping storm.
 *
 * On real hardware the returned value is VICVECTADDR[activeSource], which the
 * driver programmed as (source | 0x80000000). With priorities unprogrammed on
 * this platform, the active source is simply the lowest-numbered pending line.
 * base_source places this VIC in the daisy chain: 0 for VIC0, 32 for VIC1.
 */
uint32_t s5l_vic_vectaddr(const s5l_vic_t *v, unsigned base_source) {
    uint32_t irq = pending(v) & v->enable & ~v->select;
    if (!irq) return 0;                 /* nothing pending -> spurious source 0 */
    unsigned bit = 0;
    while (!(irq & (1u << bit))) bit++;  /* lowest set bit = default priority */
    return (base_source + bit) | 0x80000000u;
}

uint32_t s5l_vic_read(s5l_vic_t *v, uint32_t off) {
    switch (off) {
        case VIC_IRQSTATUS: return pending(v) & v->enable & ~v->select;
        case VIC_FIQSTATUS: return pending(v) & v->enable & v->select;
        case VIC_RAWINTR:   return pending(v);
        case VIC_INTSELECT: return v->select;
        case VIC_INTENABLE: return v->enable;
        case VIC_SOFTINT:   return v->soft;
        default:            return 0;
    }
}

void s5l_vic_write(s5l_vic_t *v, uint32_t off, uint32_t val) {
    switch (off) {
        case VIC_INTSELECT:    v->select  = val;   break;
        case VIC_INTENABLE:    v->enable |= val;   break;  /* write 1 to set   */
        case VIC_INTENCLEAR:   v->enable &= ~val;  break;  /* write 1 to clear */
        case VIC_SOFTINT:      v->soft   |= val;   break;
        case VIC_SOFTINTCLEAR: v->soft   &= ~val;  break;
        /*
         * VICADDRESS write is the end-of-interrupt: on hardware it drops the
         * in-service priority latch. We recompute pending on every read and the
         * source is cleared at its device (SOFTINTCLEAR for the IPI), so there
         * is no priority state to unwind — an accepted no-op is correct.
         * The per-source vector-address registers (0x100-0x17C) are written by
         * the driver's initVector; we synthesise VICADDRESS directly, so those
         * are accepted and ignored too. Both are named here rather than hitting
         * the default so it is clear they are handled, not forgotten.
         */
        case VIC_VECTADDR:     break;   /* EOI */
        default:
            if (off >= VIC_VECTADDR0 && off < VIC_VECTADDR0 + 32u * 4u) break;
            break;
    }
}
