/*
 * S5LBox — the two ARM PrimeCell PL080 DMA controllers, /arm-io/dmac0 and
 * /arm-io/dmac1.
 *
 * The register map, the bit positions and every claim about what the guest's
 * AppleARMPL080DMAC does with them are derived in the PL080 block of soc.h,
 * with the disassembly address for each. This file carries the reasoning that
 * belongs to the implementation rather than to the map: WHEN bytes move, HOW
 * FAR a chain is followed, and what a refusal costs.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#include "soc.h"
#include <string.h>

/*
 * The one design decision worth arguing about is that transfers happen in
 * s5l_pl080_run(), which s5l8900_tick() calls, and NOT inside the store that
 * sets the channel-enable bit.
 *
 * Doing it in the store would work — bus_write() is plain C and would simply
 * recurse — but it would put a whole audio buffer's worth of device writes
 * inside one guest instruction's bus access, interleaved into the device trace
 * between that access's entry and its value, and it would make the DMAC the
 * only device in this machine that can re-enter the bus. Running from the tick
 * costs nothing in latency: s5l8900_tick()'s early-out cannot skip the tick
 * after a device store, because that store sets `level_dirty`.
 *
 * A channel is therefore enabled by one instruction and has completed by the
 * time the next one retires. That is faster than the part — see the burst note
 * in soc.h — but it is the same ORDER the guest observes: the terminal-count
 * status bit and the VIC line it drives are both set before the driver's next
 * load can see them.
 */

void s5l_pl080_reset(s5l_pl080_t *d) {
    if (!d) return;
    /* Total, like every other device here: valid on a stack object holding any
     * prior byte pattern. Reset state is all-zero on real hardware too — the
     * controller comes up disabled with every channel disabled, which is why
     * the driver's power-on path writes 0x030 = 1 before anything else. */
    memset(d, 0, sizeof *d);
}

static void note_unknown(s5l_pl080_t *d, uint32_t off) {
    for (unsigned i = 0; i < d->unknown_off_count; i++)
        if (d->unknown_off[i] == off) return;
    if (d->unknown_off_count < S5L_PL080_UNKNOWN_OFF)
        d->unknown_off[d->unknown_off_count++] = off;
}

/* Which channel a per-channel offset belongs to, or -1 if it is not one. */
static int chan_of(uint32_t off, uint32_t *reg) {
    if (off < PL080_CHAN_BASE) return -1;
    uint32_t rel = off - PL080_CHAN_BASE;
    uint32_t idx = rel / PL080_CHAN_STRIDE;
    if (idx >= S5L_PL080_CHANNELS) return -1;
    uint32_t r = rel % PL080_CHAN_STRIDE;
    if (r > PL080_CH_CFG) return -1;   /* 0x14-0x1c: reserved, not a register */
    if (reg) *reg = r;
    return (int)idx;
}

/* The masked terminal-count status: a raw bit only reaches 0x004 (and through
 * it 0x000) when that channel's Configuration has ITC set. Both halves are
 * live in the shipped driver — it starts a channel with template | 0x8001. */
static uint32_t masked_tc(const s5l_pl080_t *d) {
    uint32_t v = 0;
    for (unsigned i = 0; i < S5L_PL080_CHANNELS; i++)
        if ((d->raw_tc & (1u << i)) && (d->ch[i].cfg & PL080_CFG_ITC))
            v |= 1u << i;
    return v;
}

static uint32_t masked_err(const s5l_pl080_t *d) {
    uint32_t v = 0;
    for (unsigned i = 0; i < S5L_PL080_CHANNELS; i++)
        if ((d->raw_err & (1u << i)) && (d->ch[i].cfg & PL080_CFG_IE))
            v |= 1u << i;
    return v;
}

static uint32_t enabled_chans(const s5l_pl080_t *d) {
    uint32_t v = 0;
    for (unsigned i = 0; i < S5L_PL080_CHANNELS; i++)
        if (d->ch[i].cfg & PL080_CFG_EN) v |= 1u << i;
    return v;
}

bool s5l_pl080_irq(const s5l_pl080_t *d) {
    if (!d) return false;
    return (masked_tc(d) | masked_err(d)) != 0u;
}

uint32_t s5l_pl080_read(s5l_pl080_t *d, uint32_t off) {
    if (!d) return 0u;
    d->reads++;

    uint32_t r;
    int c = chan_of(off, &r);
    if (c >= 0) {
        switch (r) {
        case PL080_CH_SRC:  return d->ch[c].src;
        case PL080_CH_DST:  return d->ch[c].dst;
        case PL080_CH_LLI:  return d->ch[c].lli;
        case PL080_CH_CTRL: return d->ch[c].ctrl;
        case PL080_CH_CFG:
            /*
             * The Active bit is derived, not stored, and it is derived as
             * ZERO. It means "this channel's FIFO still holds data", and this
             * model has no channel FIFO: a transfer either has not started or
             * has fully landed on the bus. Getting this wrong is not a cosmetic
             * error — queryDMACommand at 0xc070efb4 spins on `tst r0,#0x20000`
             * with no iteration bound and no deadline, so a stuck bit here is a
             * guest that never returns.
             */
            return d->ch[c].cfg & ~PL080_CFG_ACTIVE;
        default: break;
        }
        /* 0x14-0x1c inside a channel block: reserved, and chan_of already
         * rejected it. Fall through to the unknown log. */
    }

    switch (off) {
    case PL080_INTSTATUS:       return masked_tc(d) | masked_err(d);
    case PL080_INTTCSTATUS:     return masked_tc(d);
    case PL080_INTERRSTATUS:    return masked_err(d);
    case PL080_RAWINTTCSTATUS:  return d->raw_tc;
    case PL080_RAWINTERRSTATUS: return d->raw_err;
    case PL080_ENBLDCHNS:       return enabled_chans(d);
    case PL080_CONFIG:          return d->config;
    case PL080_SYNC:            return d->sync;
    /*
     * 0x008 and 0x010 are write-only on the part and the driver never reads
     * either. They are named here rather than left to the unknown log so that
     * a read of one is recorded as the anomaly it would be, and answered with
     * the zero a write-only register reads as, not with a status word that
     * would look like a second copy of 0x004.
     */
    case PL080_INTTCCLEAR:
    case PL080_INTERRCLEAR:
        d->unknown_reads++;
        note_unknown(d, off);
        return 0u;
    default: break;
    }

    d->unknown_reads++;
    note_unknown(d, off);
    return 0u;
}

void s5l_pl080_write(s5l_pl080_t *d, uint32_t off, uint32_t val) {
    if (!d) return;
    d->writes++;

    uint32_t r;
    int c = chan_of(off, &r);
    if (c >= 0) {
        switch (r) {
        case PL080_CH_SRC:  d->ch[c].src  = val; return;
        case PL080_CH_DST:  d->ch[c].dst  = val; return;
        case PL080_CH_LLI:  d->ch[c].lli  = val; return;
        case PL080_CH_CTRL: d->ch[c].ctrl = val; return;
        case PL080_CH_CFG:
            /* Active is read-only; a guest that writes a 1 there does not get
             * to claim the channel FIFO has data. Everything else — including
             * the Halt bit, the flow-control field and the two interrupt masks
             * — is stored exactly as written. */
            d->ch[c].cfg = val & ~PL080_CFG_ACTIVE;
            return;
        default: break;
        }
    }

    switch (off) {
    case PL080_INTTCCLEAR:
        /* Write-one-to-clear against the RAW status, which is what makes the
         * driver's filter terminate: it reads 0x000 and writes that exact word
         * back here (0xc070f16c, 0xc070f18c). */
        d->raw_tc &= ~val;
        return;
    case PL080_INTERRCLEAR:
        d->raw_err &= ~val;
        return;
    case PL080_CONFIG:
        if (val & PL080_CONFIG_ENDIAN) d->refused_endian++;
        if (d->config_writes == 0u) d->config_first = val;
        d->config_writes++;
        d->config = val;
        return;
    case PL080_SYNC:
        d->sync = val;
        return;
    case PL080_SOFTBREQ:
    case PL080_SOFTSREQ:
    case PL080_SOFTLBREQ:
    case PL080_SOFTLSREQ:
        /* A software DMA request only means something to burst/single pacing,
         * which this model does not have. Counted rather than stored: storing
         * it would let a read back a value that never had an effect. */
        d->refused_softreq++;
        return;
    /* Read-only status. A write is not an error the part reports, but it is a
     * thing the stock driver never does, so it is logged like any anomaly. */
    case PL080_INTSTATUS:
    case PL080_INTTCSTATUS:
    case PL080_INTERRSTATUS:
    case PL080_RAWINTTCSTATUS:
    case PL080_RAWINTERRSTATUS:
    case PL080_ENBLDCHNS:
        d->unknown_writes++;
        note_unknown(d, off);
        return;
    default: break;
    }

    d->unknown_writes++;
    note_unknown(d, off);
}

/* --------------------------------------------------------- the engine --- */

static uint32_t bus_load(const arm_bus_t *bus, uint32_t addr, unsigned width) {
    switch (width) {
    case 1u: return bus->read8  ? (uint32_t)bus->read8 (bus->ctx, addr) : 0u;
    case 2u: return bus->read16 ? (uint32_t)bus->read16(bus->ctx, addr) : 0u;
    default: return bus->read32 ? bus->read32(bus->ctx, addr) : 0u;
    }
}

static void bus_store(const arm_bus_t *bus, uint32_t addr, uint32_t v,
                      unsigned width) {
    switch (width) {
    case 1u: if (bus->write8)  bus->write8 (bus->ctx, addr, (uint8_t)v);  break;
    case 2u: if (bus->write16) bus->write16(bus->ctx, addr, (uint16_t)v); break;
    default: if (bus->write32) bus->write32(bus->ctx, addr, v);           break;
    }
}

/*
 * One linked-list item, from the channel registers. Returns false when the
 * channel must stop, and leaves the registers describing exactly how far it
 * got — which is the contract the driver's progress reporting depends on: it
 * reads Control & 0xfff for the remaining count (0xc070f9f8) and the LLI
 * register for which item is next (0xc070f93c).
 */
static bool run_item(s5l_pl080_t *d, unsigned c, const arm_bus_t *bus,
                     s5l_pl080_ready_fn ready, void *ready_ctx,
                     bool *stalled) {
    s5l_pl080_chan_t *ch = &d->ch[c];

    uint32_t sw = (ch->ctrl >> PL080_CTRL_SWIDTH_SHIFT) & PL080_CTRL_WIDTH_MASK;
    uint32_t dw = (ch->ctrl >> PL080_CTRL_DWIDTH_SHIFT) & PL080_CTRL_WIDTH_MASK;
    if (sw > 2u || dw > 2u) {
        /* See refused_width in soc.h. The channel is disabled rather than left
         * enabled-and-stuck, because a channel that stays enabled forever is
         * one the driver will wait on forever. */
        d->refused_width++;
        ch->cfg &= ~PL080_CFG_EN;
        return false;
    }
    unsigned swidth = 1u << sw, dwidth = 1u << dw;

    uint32_t flow = (ch->cfg & PL080_CFG_FLOW_MASK) >> PL080_CFG_FLOW_SHIFT;
    if (flow > 3u) {
        d->refused_flow++;
        ch->cfg &= ~PL080_CFG_EN;
        return false;
    }

    bool si = (ch->ctrl & PL080_CTRL_SI) != 0u;
    bool di = (ch->ctrl & PL080_CTRL_DI) != 0u;

    uint32_t n = ch->ctrl & PL080_CTRL_SIZE_MASK;
    /*
     * Control[11:0] counts SOURCE transfers, and the two widths need not agree
     * — /arm-io/spi1's `dma-channels` template is 0x00089000, four-byte source
     * and one-byte destination, and it is the only entry in the shipped tree
     * where they differ. The driver's own byte arithmetic is what settles which
     * side the count belongs to: at 0xc070e14c it picks DWidth for direction 1
     * and SWidth otherwise — i.e. always the MEMORY side — and then caps a
     * segment at `0xe00 << thatWidth` BYTES (0xc070e2fc) and writes
     * `bytes >> thatWidth` into Control (0xc070e4b4). For spi1 that memory side
     * is the source, so the count is in source transfers.
     *
     * What the part does with the difference is pack through the channel FIFO,
     * so this stages bytes and drains them at the destination width. The byte
     * ORDER is not a choice: on a little-endian bus, unpacking a word into
     * bytes emits the low byte at the low address.
     *
     * A total that is not a whole number of destination transfers would leave
     * a remainder sitting in that FIFO — data this model has nowhere to hold
     * and no Active bit to admit to — so it is refused rather than dropped.
     */
    if (((uint64_t)n * swidth) % dwidth) {
        d->refused_width++;
        ch->cfg &= ~PL080_CFG_EN;
        return false;
    }
    uint64_t dmask = (1ull << (8u * dwidth)) - 1ull;
    uint64_t pend = 0;          /* staged bytes, low address in the low bits */
    unsigned pend_bytes = 0;    /* at most dwidth - 1 + swidth, so under 8    */

    while (n) {
        /*
         * THE REQUEST LINE, asked at a clean boundary and only at one.
         *
         * `pend` holds bytes read from the source and not yet delivered, and it
         * is a local: stopping with anything staged in it would lose those
         * bytes outright. With swidth 4 and dwidth 1 -- which is exactly what
         * /arm-io/spi1 uses -- a partial drain leaves a remainder that is not a
         * whole number of SOURCE transfers, so it cannot be rolled back into
         * the count either. Stalling only when nothing is staged makes the stop
         * lossless by construction rather than by arithmetic.
         *
         * The enable bit is left set and Control[11:0] already carries the
         * remaining count, so the next tick resumes precisely here.
         */
        if (pend_bytes == 0u && ready && !ready(ready_ctx, ch->dst, dwidth)) {
            if (stalled) *stalled = true;
            return false;
        }
        pend |= (uint64_t)bus_load(bus, ch->src, swidth) << (8u * pend_bytes);
        pend_bytes += swidth;
        if (si) ch->src += swidth;
        n--;
        /* Written back every transfer, not once at the end. The channel
         * registers are the only place a reader can see partial progress, and
         * a refusal below this point must not leave a count that never ran. */
        ch->ctrl = (ch->ctrl & ~PL080_CTRL_SIZE_MASK) | n;
        d->transfers++;
        while (pend_bytes >= dwidth) {
            bus_store(bus, ch->dst, (uint32_t)(pend & dmask), dwidth);
            if (di) ch->dst += dwidth;
            pend >>= 8u * dwidth;
            pend_bytes -= dwidth;
            d->bytes_moved += dwidth;
            ch->bytes += dwidth;
        }
    }

    d->items++;
    ch->runs++;
    if (ch->ctrl & PL080_CTRL_I) {
        /* Terminal count. Raw always; whether it reaches the CPU is the ITC
         * mask's business, applied in masked_tc(). */
        d->raw_tc |= 1u << c;
        d->completions++;
    }

    if (ch->lli == 0u) {
        /* End of chain. The part clears the enable bit itself, and the driver
         * reads it back at 0xc070e950 to decide whether the channel still needs
         * programming — so leaving it set would make every later command think
         * the hardware was still busy. */
        ch->cfg &= ~PL080_CFG_EN;
        return false;
    }

    /* Reload from the next item: {SrcAddr, DestAddr, Next, Control}. The low
     * two bits of the LLI register are the AHB master select, which is why the
     * driver strips them from every read-back it uses as an address
     * (0xc070e718, 0xc070e750, 0xc070f954) and not from the one it only
     * compares against zero (0xc070e8f0). */
    uint32_t at = ch->lli & ~3u;
    ch->src  = bus->read32 ? bus->read32(bus->ctx, at + 0u)  : 0u;
    ch->dst  = bus->read32 ? bus->read32(bus->ctx, at + 4u)  : 0u;
    ch->lli  = bus->read32 ? bus->read32(bus->ctx, at + 8u)  : 0u;
    ch->ctrl = bus->read32 ? bus->read32(bus->ctx, at + 12u) : 0u;
    return true;
}

bool s5l_pl080_run(s5l_pl080_t *d, const arm_bus_t *bus,
                   s5l_pl080_ready_fn ready, void *ready_ctx) {
    if (!d) return false;
    if (!bus) return s5l_pl080_irq(d);
    /* The whole controller is gated by 0x030 bit 0. The driver sets it in the
     * same call that clears every channel, and clears it again when its command
     * refcount reaches zero (0xc070ed7c / 0xc070ee90), so a disabled controller
     * really is the guest saying "nothing may move". */
    if (!(d->config & PL080_CONFIG_EN)) return s5l_pl080_irq(d);

    for (unsigned c = 0; c < S5L_PL080_CHANNELS; c++) {
        s5l_pl080_chan_t *ch = &d->ch[c];
        /*
         * Halt means "accept no further DMA requests and drain what is in the
         * FIFO". With no FIFO there is nothing to drain, so a halted channel
         * simply moves nothing and reads back Active == 0 — which is what
         * 0xc070ef04's halt-then-poll path is waiting to see.
         */
        if ((ch->cfg & (PL080_CFG_EN | PL080_CFG_HALT)) != PL080_CFG_EN)
            continue;

        unsigned items = 0;
        bool stalled = false;
        while (run_item(d, c, bus, ready, ready_ctx, &stalled)) {
            if (++items >= S5L_PL080_MAX_ITEMS) {
                /* See refused_chain in soc.h. Real hardware would follow a
                 * self-referential list forever; a host that did the same would
                 * hang with no diagnostic at all. */
                d->refused_chain++;
                ch->cfg &= ~PL080_CFG_EN;
                break;
            }
        }
    }
    return s5l_pl080_irq(d);
}
