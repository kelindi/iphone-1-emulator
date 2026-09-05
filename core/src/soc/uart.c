/*
 * S5LBox — S5L8900 UART.
 *
 * This is the first device that makes the emulator *observable*: whatever the
 * guest writes to UTXH is captured, which is how iBoot and the XNU kernel will
 * later tell us how far they got.
 *
 * It is now also the first device that makes the emulator *reachable*: the
 * receive FIFO below is the host->guest direction docs/networking.md §6 needs
 * for Route D, and the only thing that can put a byte in it is a host calling
 * s5l_uart_rx_push() between run slices. Every register rule the receive path
 * implements is quoted from the UART block in soc.h; read that first.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#include "soc.h"
#include <string.h>

/*
 * UTRSTAT's LIVE half, named as Apple names it in pexpert/pexpert/arm/
 * apple_uart_regs.h: receive_buffer_data_ready, transmit_buffer_empty,
 * transmitter_empty. Status with a source behind it, derived on every read. We
 * report the transmitter permanently drained so guest spin-loops of the form
 * "wait until TX empty, then write" always make progress.
 */
#define UTRSTAT_RX_READY     (1u << 0)
#define UTRSTAT_TX_BUF_EMPTY (1u << 1)
#define UTRSTAT_TX_EMPTY     (1u << 2)

/*
 * UTRSTAT's LATCHED half. Only UTRSTAT_RX_INT (soc.h) is ever set — see the UART
 * block there for why the other four are not, and for what asserting 0x100 in
 * particular would set running. UTRSTAT_LATCHED is the whole of the set the
 * driver's enable mask at this->0x9c can hold, and so the whole of what an
 * enable may arm; it is written out rather than derived so that adding a bit is
 * a decision somebody makes here.
 */
#define UTRSTAT_LATCHED      0x178u      /* 0x8|0x10|0x20|0x40|0x100  */

/*
 * Each enable sits eight bits above the status bit it gates: UCON 0x1000 enables
 * UTRSTAT 0x10, 0x800 enables 0x8, and so on up to 0x10000 for 0x100. All five
 * pairs come out of the one function at 0xc065f0e4 that writes both registers.
 */
#define UCON_ENABLE_SHIFT    8u

/* UFSTAT: bits[3:0] receive count, bit 8 receive full, bits[7:4] transmit
 * count, bit 9 transmit full. The transmitter drains instantly, so its two
 * fields are permanently zero. */
#define UFSTAT_RX_FULL       (1u << 8)

void s5l_uart_reset(s5l_uart_t *u) {
    /* Still every byte, including rx_irq_suppressed. Preserving the flag here
     * was tried and reverted: reset's one caller is s5l8900_init(), which runs
     * on storage that has not been initialised yet, so reading any field before
     * the memset is reading uninitialised memory — and core/tests/test_uart4.c
     * pins reset as "a statement about every byte", which a preserved field
     * would quietly falsify. The ordering requirement lives in
     * s5l_uart_set_rx_irq()'s contract instead: call it AFTER init. */
    memset(u, 0, sizeof *u);
}

void s5l_uart_set_rx_irq(s5l_uart_t *u, bool enabled) {
    if (u) u->rx_irq_suppressed = !enabled;
}

unsigned s5l_uart_rx_space(const s5l_uart_t *u) {
    if (!u || u->rx_count >= UART_RX_FIFO) return 0u;
    return UART_RX_FIFO - u->rx_count;
}

/* Which latched causes UCON has armed. Nothing in this model ever latches a bit
 * outside UTRSTAT_LATCHED, so the mask is documentation as much as a guard. */
static uint32_t utrstat_enables(const s5l_uart_t *u) {
    return (u->ucon >> UCON_ENABLE_SHIFT) & UTRSTAT_LATCHED;
}

bool s5l_uart_rx_irq(const s5l_uart_t *u) {
    /* The suppression is applied HERE and nowhere else, so that every other
     * observable — UTRSTAT's levels and its latch, UFSTAT's count, what URXH
     * dequeues, what a W1C store clears — is bit for bit what it is with the
     * interrupt on. That is what makes the two runs a controlled pair: the VIC
     * line is the only difference between them. */
    if (!u || u->rx_irq_suppressed) return false;
    /*
     * The line, gated. A driver that has not set UCON's enable never sees this
     * cause, which is how the hardware works and is also the only reason a
     * silent gate is safe to model: the enable and the filter's mask are set by
     * the same straight-line code at 0xc065f0e4, so a driver that reads the bit
     * has enabled it.
     */
    return (u->utrstat_pending & utrstat_enables(u)) != 0u;
}

bool s5l_uart_rx_push(s5l_uart_t *u, uint8_t byte) {
    if (!u) return false;
    if (u->rx_count >= UART_RX_FIFO) {
        /* Refuse and count. Growing the FIFO would model a UART nobody built;
         * blocking would stall the CPU thread, which docs/networking.md §8.3
         * forbids outright. A dropped byte is a lost frame and the peer's
         * restart timer is what recovers it. */
        u->rx_dropped++;
        return false;
    }
    u->rx[(unsigned)(u->rx_head + u->rx_count) % UART_RX_FIFO] = byte;
    u->rx_count++;
    u->rx_pushed++;
    /*
     * THE EDGE. Every arrival latches, not just the empty->non-empty one: after
     * an acknowledge the driver may have drained only part of the FIFO, and a
     * latch that only fired on the transition would leave the remainder with
     * nothing to announce it. Setting a bit that is already set is a no-op, so
     * two bytes still cost the driver one acknowledge — the latch is a status
     * bit, not a count of arrivals.
     */
    u->utrstat_pending |= UTRSTAT_RX_INT;
    return true;
}

uint32_t s5l_uart_read(s5l_uart_t *u, uint32_t off) {
    switch (off) {
        case UART_ULCON:   return u->ulcon;
        case UART_UCON:    return u->ucon;
        case UART_UFCON:   return u->ufcon;
        case UART_UMCON:   return u->umcon;
        case UART_UBRDIV:  return u->ubrdiv;
        case UART_UTRSTAT:
            /* Levels derived now, latch as it stands. Reading does NOT clear
             * the latch: the filter at 0xc065eecc reads this register, masks it
             * and writes the result BACK, and that store is the acknowledge. */
            return UTRSTAT_TX_EMPTY | UTRSTAT_TX_BUF_EMPTY |
                   (u->rx_count ? UTRSTAT_RX_READY : 0u) |
                   u->utrstat_pending;
        case UART_UERSTAT: return 0;
        case UART_UFSTAT:
            /* The count field is four bits wide and the depth is sixteen, so a
             * full FIFO reports 0 in the field and 1 in the full bit. That is
             * not a truncation bug: it is why the full bit exists. */
            return ((uint32_t)u->rx_count & 0x0fu) |
                   (u->rx_count >= UART_RX_FIFO ? UFSTAT_RX_FULL : 0u);
        case UART_UMSTAT:  return 0;
        case UART_URXH: {
            if (u->rx_count == 0u) {
                /* No byte has arrived. Zero is the only answer that does not
                 * invent one; the counter is what tells a reader afterwards
                 * that the guest polled an empty port rather than receiving
                 * a stream of NULs. */
                u->rx_underruns++;
                return 0;
            }
            uint8_t b = u->rx[u->rx_head];
            u->rx_head = (uint8_t)((u->rx_head + 1u) % UART_RX_FIFO);
            u->rx_count--;
            u->rx_reads++;
            return b;
        }
        default:           return 0;
    }
}

void s5l_uart_write(s5l_uart_t *u, uint32_t off, uint32_t val) {
    switch (off) {
        case UART_ULCON:  u->ulcon  = val; break;
        case UART_UCON:   u->ucon   = val; break;
        case UART_UFCON:  u->ufcon  = val; break;
        case UART_UMCON:  u->umcon  = val; break;
        case UART_UBRDIV: u->ubrdiv = val; break;
        case UART_UTXH:
            if (u->tx_len < UART_TX_BUFFER - 1) u->tx[u->tx_len++] = (char)(val & 0xff);
            break;
        case UART_UTRSTAT:
            /*
             * THE ACKNOWLEDGE, write-one-to-clear — Apple's own
             * apple_uart_ack_irq() builds a zero word, sets one status bit and
             * stores it, and the filter at 0xc065eecc stores back what it read
             * ANDed with its enable mask.
             *
             * It clears the named pending bits and NOTHING else. The levels are
             * not stored in the latch at all, so a store with bit 0 set — which
             * the filter cannot even produce, since 0x1 can never enter its mask
             * — is structurally incapable of stranding a queued byte.
             *
             * And it clears 0x10 whether or not the FIFO still holds data. That
             * is the whole repair: this line is lowered by the acknowledge and
             * by nothing else, because the filter returns 0 for a receive cause
             * and IOFilterInterruptEventSource::disableInterruptOccurred then
             * returns without masking. A latch re-armed from a non-empty FIFO
             * would re-raise a level nobody masks, which is exactly the storm
             * run94 measured. The remaining bytes are announced by UTRSTAT bit
             * 0, which is live and which the driver's drain loop reads.
             */
            u->utrstat_pending &= ~val;
            break;
        default: break;
    }
}
