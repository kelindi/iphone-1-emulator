/*
 * S5LBox — S5L8900 CLCD display controller (AppleH1CLCD's hardware).
 *
 * This is the block between a booting kernel and a screen. Nothing here is
 * guessed from a datasheet — no S5L8900 documentation exists — every offset and
 * every bit below is a line of AppleH1CLCD's own ARM code, read out of the
 * AppleH1DisplayDrivers kext inside the shipped kernelcache (text at
 * 0xc0703000, size 0xa000). The driver keeps its mapped register base in
 * `this+0x1fc`, so any `ldr/str [rX, #off]` after `ldr rX,[this,#0x1fc]` is a
 * register access and the offset is directly readable. Addresses in the notes
 * below are virtual addresses in that kext, so each claim can be re-checked.
 *
 * WHY THIS IS A DEVICE MODEL AND NOT A STUB
 *
 * Because one bit has to move on its own. AppleH1CLCD submits a framebuffer
 * swap and then waits for the controller to tell it the frame went out:
 *
 *   0xc0705d38  swap_submit tail
 *       if (shadowMask & 1) return;            // a swap is already pending
 *       write32(reg + 0x18, 1);                // clear any stale frame status
 *       shadowMask |= 1;
 *       write32(reg + 0x14, shadowMask);       // enable the frame interrupt
 *
 *   0xc0705d7c  the interrupt handler
 *       status  = read32(reg + 0x18);
 *       pending = status & shadowMask;
 *       if (pending & 1) {
 *           write32(reg + 0x18, 1);            // acknowledge, write-1-to-clear
 *           ... complete the swap, then if nothing else is queued:
 *           shadowMask &= ~1;
 *           write32(reg + 0x14, shadowMask);
 *       }
 *       if (pending & 0x3f00) {
 *           IOLog("AppleH1CLCD: Graphics underrun interrupt: %08x", pending);
 *           write32(reg + 0x18, 0x3f00);
 *       }
 *
 * Storage that returns what was last written would return 0 from 0x018 forever
 * and the swap would never complete: the UI wedges, permanently, with no error.
 * So this model raises the frame interrupt on a timer, in GUEST time.
 *
 * Two properties of that handler are load-bearing and are pinned by tests:
 *
 *   1. The interrupt LINE must be gated by the hardware mask at 0x014, not just
 *      by the status at 0x018. The handler masks the status with its own
 *      software shadow; if the line were asserted while the shadow's bit 0 is
 *      clear, `pending` would come out 0, the handler would acknowledge
 *      nothing, and it would re-enter forever. That is an interrupt storm, not
 *      a display. (This is the same failure shape as the timer's ack mask.)
 *
 *   2. We must NEVER set the underrun bits 0x3f00. There is no underrun in an
 *      emulator with no real FIFO, and setting them only produces error spam
 *      from a driver that is working correctly.
 *
 * WHAT THE DRIVER DOES, in the order it does it
 *
 *   start_hardware      0xc07059d8
 *       write32(reg + 0x218, 1);
 *       shadowMask = 0x3f00;                   // underruns only, for now
 *       write32(reg + 0x18, 0);                // 0x0d8..0x0ec are NOT touched
 *       write32(reg + 0x14, 0);
 *       write32(reg + 0x1c, 0);
 *       v = read32(reg + 8);
 *       v = (v & ~0x30000001) | 1;
 *       v = (v & ~0x02000000) | 0x01000000;
 *       v = (v & ~0x00330000) | 0x00110000;
 *       write32(reg + 8, v);
 *       write32(reg + 0xc, read32(reg + 0xc) | 0x000f0f0f);
 *
 *   enable path         0xc0705840
 *       write32(reg + 0x20, 1);
 *       write32(reg + 0x14, shadowMask);
 *       ... restore panel timing, then program the windows ...
 *       write32(reg + 0x00, 1);                // scanout starts HERE, last
 *       write32(reg + 0x200, read32(reg + 0x200) | 1);
 *
 *   stop  0xc0705694 / 0xc0705910
 *       shadowMask = 0; write32(reg+0x18, 0); write32(reg+0x14, 0);
 *       write32(reg+0x1c, 0); write32(reg+4, 1);
 *       write32(reg + 0x200, read32(reg + 0x200) & ~1);
 *
 *   window update       0xc0704940
 *       write32(reg + 0xd4, 2);
 *       write32(reg + 0xe8, 0x50001000);
 *       ... then the six registers of each enabled window ...
 *
 * REGISTER SEMANTICS, LABELLED
 *
 *   CONFIRMED (read directly out of the driver's code, cited above):
 *     0x000 start, 0x004 stop, 0x008 display+window enables and the exact
 *     read-modify-write masks, 0x00c FIFO thresholds OR 0x000f0f0f,
 *     0x014 interrupt enable, 0x018 interrupt status write-1-to-clear with
 *     bit 0 = frame and 0x3f00 = underrun, 0x01c cleared on start and stop,
 *     0x020 written 1 just before 0x000, 0x0d4 = 2 per update,
 *     0x0e8 = 0x50001000 per update, 0x200 bit 0 gate,
 *     0x204 read-only status used as `if (((v >> 6) & 3) == 3) defer the swap`
 *     (0xc0705ccc..0xc0705ce0), and the register at 0x218 written 1 at start.
 *     Base 0x38900000 and interrupt line 13 are confirmed from the device tree.
 *
 *   CORRECTED REGISTER LABELS: 0x0d8/0x0dc, 0x0e0/0x0e4 and 0x0e8/0x0ec
 *     are per-window configuration pairs in openiBoot, not panel timings.
 *     AppleH1CLCD also uses 0x0e8 as its update word. The real clock/timing
 *     handoff is VIDCON0/1 and VIDTCON0..3 at 0x200..0x218. The N82 values
 *     planted by s5l_clcd_seed_window0() follow openiBoot's optC and its
 *     recorded divisor-five iPhone 3G handoff (10.8 MHz pixels, inverted
 *     VCLK, 15/15/16 horizontal and 4/4/4 vertical porch/sync).
 *
 *   INFERRED: 0x024 backdrop colour, the 11 video-overlay registers at
 *     0x028..0x054, the 8 colour-matrix registers at 0x1c8..0x1e8, the opaque
 *     registers at 0x208..0x214 and the three gamma LUTs at 0x400/0x800/0xc00
 *     are all write-then-save/restore as far as the driver is concerned. Their
 *     MEANING is inferred from the surrounding code; their BEHAVIOUR (plain
 *     storage) is what the driver actually requires, and that much is certain.
 *
 *   CONFIRMED (was INFERRED): the layout of the RGB window registers at
 *     0x058 + k*0x18, now read out of 0xc0705f00 — the vtable slot
 *     IOMobileFramebuffer::start invokes immediately after start_hardware, and
 *     the most important function in this whole driver:
 *
 *       r2 = regs;
 *       if      (read32(r2+0x08) & 0x40) { pitch=[r2+0x58]; ctl=[r2+0x5c]; fb=[r2+0x60]; geom=[r2+0x64]; }
 *       else if (                & 0x20) { ...0x70 0x74 0x78 0x7c }
 *       else if (                & 0x10) { ...0x88 0x8c 0x90 0x94 }
 *       else if (                & 0x08) { ...0xa0 0xa4 0xa8 0xac }
 *       fourcc = ((ctl >> 8) & 7) >= 6 ? 'ARGB' : '565L';      // 0xc0705ff8
 *       width  = (geom << 5) >> 21;                            // bits[26:16]
 *       height = uxth(geom &~ 0xfc00);                         // bits[9:0]
 *       size   = round_up(pitch * height, 0x1000);
 *       md     = IOMemoryDescriptor::withPhysicalAddress(fb, size, kIODirectionOutIn);
 *       surface = IOCoreSurfaceRoot->createSurface(md, {IOSurfaceIsGlobal:true});
 *
 *     So the window base offsets, the CLCD_CTRL enable bits and their priority
 *     order, the field order, the geometry bit positions, and the fact that
 *     FBADDR is PHYSICAL are all facts now, not readings.
 *
 *     THE CONSEQUENCE IS THE POINT: AppleH1CLCD does not program the display.
 *     It reads whatever iBoot left in these registers and wraps it in the
 *     IOSurface that becomes the screen. If CLCD_CTRL enables no window, the
 *     four `ldrne`s above simply do not execute and the driver builds that
 *     IOSurface out of four uninitialised callee-saved registers. A boot stub
 *     standing in for iBoot MUST call s5l_clcd_seed_window0() — an unseeded
 *     controller is not a blank screen, it is a garbage surface.
 *
 *   STILL INFERRED, and deliberately not acted on: component order in control
 *     bits[17:16]. The driver publishes 'ARGB' whatever those bits hold, so we
 *     have no evidence for what they select. s5l_clcd_window() reports the
 *     field; s5l_clcd_scanout() ignores it rather than inventing a swizzle.
 *
 *   DECIDED, and stated: VIDCON1 preserves the N82 IVCLK polarity bit when the
 *     N82 display-clock handoff is present. Bits [7:6] are the only live-state
 *     bits AppleH1CLCD examines and it defers a swap when both are set. We keep
 *     those bits clear ("not busy") because no scanline engine exists to move
 *     them; inventing 0xc0 would deadlock every swap.
 *
 *   NOT MODELLED, deliberately: the hardware scanout does not read the
 *     framebuffer on its own, gamma is not applied, the video overlay does
 *     nothing, and 0x004 stops the frame interrupt but nothing else. None of
 *     that is invented behaviour — it is absent behaviour.
 *
 *     s5l_clcd_scanout() is the seam the header used to promise: it takes the
 *     window the DRIVER would take (s5l_clcd_active_window(), in the driver's
 *     own 0x40/0x20/0x10/0x08 order) and turns it into RGB. It is a host-side
 *     observation of guest memory, not a device behaviour: nothing the guest
 *     can see changes when it is called, and every byte it returns came out of
 *     guest DRAM. A window it cannot decode is an error return, never a
 *     plausible-looking rectangle.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#include "soc.h"
#include <string.h>

void s5l_clcd_reset(s5l_clcd_t *c) {
    memset(c, 0, sizeof *c);
    /*
     * One frame every S5L_CLCD_REFRESH_HZ of GUEST time. The unit here is
     * timebase ticks, which is what s5l8900_tick feeds us after converting
     * retired instructions at the guest's own CPU:timebase ratio — so the panel
     * refreshes at 60 Hz on the guest's clock, not at 60 Hz of host wall time
     * and not once per N instructions. Getting this wrong in the fast direction
     * is what livelocked the decrementer (see S5L8900_CPU_HZ), and getting it
     * wrong in the slow direction just makes the UI crawl.
     *
     * The machine overrides this from its own tb_hz at init; the default here
     * keeps a standalone s5l_clcd_t honest.
     */
    c->frame_ticks = S5L8900_TB_HZ / S5L_CLCD_REFRESH_HZ;
}

/* Is `off` inside [first, last] on a 4-byte grid? */
static inline bool in_range(uint32_t off, uint32_t first, uint32_t last) {
    return off >= first && off <= last && (off & 3u) == 0u;
}

static s5l_clcd_window_t *window_at(s5l_clcd_t *c, uint32_t off, uint32_t *sub) {
    if (off < CLCD_WIN_FIRST) return NULL;
    uint32_t rel = off - CLCD_WIN_FIRST;
    uint32_t k   = rel / CLCD_WIN_STRIDE;
    if (k >= CLCD_WIN_COUNT) return NULL;
    *sub = rel % CLCD_WIN_STRIDE;
    return &c->win[k];
}

/* The three gamma LUTs are contiguous: 0x400-0x7fc, 0x800-0xbfc, 0xc00-0xffc. */
static uint32_t *gamma_at(s5l_clcd_t *c, uint32_t off) {
    if (off < CLCD_GAMMA0 || off >= CLCD_GAMMA0 + 3u * CLCD_GAMMA_SIZE) return NULL;
    if (off & 3u) return NULL;
    uint32_t rel = off - CLCD_GAMMA0;
    return &c->gamma[rel / CLCD_GAMMA_SIZE][(rel % CLCD_GAMMA_SIZE) / 4u];
}

uint32_t s5l_clcd_read(s5l_clcd_t *c, uint32_t off) {
    switch (off) {
        case CLCD_ENABLE:    return c->enable;
        case CLCD_DISABLE:   return c->disable;
        case CLCD_CTRL:      return c->ctrl;
        case CLCD_FIFO:      return c->fifo;
        case CLCD_INTMASK:   return c->intmask;
        /* Raw status. The driver masks it with its own shadow, so this must be
         * the unmasked latch — handing back status & mask here would hide a
         * frame the driver had already armed. */
        case CLCD_INTSTATUS: return c->intstatus;
        case CLCD_REG1C:     return c->reg1c;
        case CLCD_PREENABLE: return c->preenable;
        case CLCD_BACKDROP:  return c->backdrop;
        case CLCD_UPDATE:    return c->update;
        case CLCD_UPDATE2:   return c->update2;
        case CLCD_GATE:      return c->gate;

        /*
         * MUST NOT read as 0xC0 (or anything with both bits [7:6] set):
         *   0xc0705ccc  ldr r3,[r3,#0x204]; lsr r3,r3,#6; and r3,r3,#3
         *   0xc0705cdc  cmp r3,#3; beq <defer the swap>
         * so a value of 0xC0 makes AppleH1CLCD postpone every swap it is ever
         * asked to do. Preserve optC's N82 IVCLK polarity bit when VIDCON0
         * selects the display clock, while leaving those live-state bits clear.
         */
        case CLCD_STATUS:
            return (c->gate & 0xc0u) == 0x40u ? CLCD_N82_VIDCON1 : 0u;
        default: break;
    }
    if (in_range(off, CLCD_VIDEO_FIRST, CLCD_VIDEO_LAST))
        return c->video[(off - CLCD_VIDEO_FIRST) / 4u];
    if (in_range(off, CLCD_CSC_FIRST, CLCD_CSC_LAST))
        return c->csc[(off - CLCD_CSC_FIRST) / 4u];
    if (in_range(off, CLCD_OPAQUE_FIRST, CLCD_OPAQUE_LAST))
        return c->opaque[(off - CLCD_OPAQUE_FIRST) / 4u];
    if (off == CLCD_WINCFG0 || off == CLCD_WINCFG0 + 4u ||
        off == CLCD_WINCFG0 + 8u || off == CLCD_WINCFG0 + 12u)
        return c->wincfg_aux[(off - CLCD_WINCFG0) / 4u];
    if (off == CLCD_WINCFG2_AUX) return c->wincfg_aux[4];
    {
        uint32_t sub;
        s5l_clcd_window_t *w = window_at(c, off, &sub);
        if (w) switch (sub) {
            case CLCD_WIN_PITCH:     return w->stride;
            case CLCD_WIN_CONTROL:   return w->control;
            case CLCD_WIN_FBADDR:    return w->fbaddr;
            case CLCD_WIN_GEOMETRY:  return w->geometry;
            case CLCD_WIN_LINEWORDS: return w->linewords;
            case CLCD_WIN_POSITION:  return w->position;
            default: return 0;
        }
    }
    {
        uint32_t *g = gamma_at(c, off);
        if (g) return *g;
    }
    return 0;
}

void s5l_clcd_write(s5l_clcd_t *c, uint32_t off, uint32_t val) {
    switch (off) {
        /*
         * Start. The driver writes this LAST, after the windows are programmed,
         * so this is the edge on which scanout — and therefore the frame
         * interrupt — begins. Restart the frame accumulator so the first VBL
         * after a start is a whole frame away rather than immediate.
         */
        case CLCD_ENABLE:
            c->enable = val;
            if (val & 1u) { c->scanning = true; c->frame_accum = 0; }
            return;

        /* Stop. Scanout ends; the latched status is left alone because this is
         * not a write-1-to-clear register and the driver clears status
         * separately at 0x018. */
        case CLCD_DISABLE:
            c->disable = val;
            if (val & 1u) c->scanning = false;
            return;

        case CLCD_CTRL:      c->ctrl      = val; return;
        case CLCD_FIFO:      c->fifo      = val; return;
        case CLCD_INTMASK:   c->intmask   = val; return;

        /* Write-1-to-clear. Anything we fail to clear here leaves line 13
         * asserted after the handler returns, which is an interrupt storm. */
        case CLCD_INTSTATUS: c->intstatus &= ~val; return;

        case CLCD_REG1C:     c->reg1c     = val; return;
        case CLCD_PREENABLE: c->preenable = val; return;
        case CLCD_BACKDROP:  c->backdrop  = val; return;
        case CLCD_UPDATE:
            /*
             * The head of a window update, and the only per-FRAME event this
             * model can see. Counting it is what makes instructions-per-frame
             * measurable: nothing in this project has ever measured that, and
             * every estimate of what frame rate is reachable -- with a JIT or
             * without one -- rests on it. See the frame_* block in bootkernel.
             */
            if (val == 2u) c->updates++;
            c->update = val;
            return;
        case CLCD_UPDATE2:   c->update2   = val; return;
        case CLCD_GATE:      c->gate      = val; return;
        /* Read-only: the driver never writes it, and inventing a way for it to
         * move would be inventing behaviour. */
        case CLCD_STATUS:    return;
        default: break;
    }
    if (in_range(off, CLCD_VIDEO_FIRST, CLCD_VIDEO_LAST)) {
        c->video[(off - CLCD_VIDEO_FIRST) / 4u] = val; return;
    }
    if (in_range(off, CLCD_CSC_FIRST, CLCD_CSC_LAST)) {
        c->csc[(off - CLCD_CSC_FIRST) / 4u] = val; return;
    }
    if (in_range(off, CLCD_OPAQUE_FIRST, CLCD_OPAQUE_LAST)) {
        c->opaque[(off - CLCD_OPAQUE_FIRST) / 4u] = val; return;
    }
    if (off == CLCD_WINCFG0 || off == CLCD_WINCFG0 + 4u ||
        off == CLCD_WINCFG0 + 8u || off == CLCD_WINCFG0 + 12u) {
        c->wincfg_aux[(off - CLCD_WINCFG0) / 4u] = val; return;
    }
    if (off == CLCD_WINCFG2_AUX) { c->wincfg_aux[4] = val; return; }
    {
        uint32_t sub;
        s5l_clcd_window_t *w = window_at(c, off, &sub);
        if (w) { switch (sub) {
            case CLCD_WIN_PITCH:     w->stride    = val; return;
            case CLCD_WIN_CONTROL:   w->control   = val; return;
            case CLCD_WIN_FBADDR:    w->fbaddr    = val; return;
            case CLCD_WIN_GEOMETRY:  w->geometry  = val; return;
            case CLCD_WIN_LINEWORDS: w->linewords = val; return;
            case CLCD_WIN_POSITION:  w->position  = val; return;
            default: return;
        } }
    }
    {
        uint32_t *g = gamma_at(c, off);
        if (g) { *g = val; return; }
    }
}

bool s5l_clcd_running(const s5l_clcd_t *c) {
    return c && c->scanning &&
           (c->ctrl & CLCD_CTRL_ENABLE) != 0u &&
           (c->gate & 1u) != 0u;
}

bool s5l_clcd_tick(s5l_clcd_t *c, uint32_t ticks) {
    if (s5l_clcd_running(c) && c->frame_ticks) {
        /*
         * Accumulate rather than test-and-reset against a running counter, so a
         * long tick cannot swallow a frame boundary and a short one cannot
         * double-count it. Frames that fall inside a single large tick are
         * coalesced into one asserted line: the status bit is a level, not a
         * count, and the driver only completes one swap per interrupt. The
         * visibility counter still records every elapsed frame boundary.
         */
        uint64_t total = (uint64_t)c->frame_accum + ticks;
        if (total >= c->frame_ticks) {
            uint64_t elapsed = total / c->frame_ticks;
            c->frame_accum = (uint32_t)(total % c->frame_ticks);
            /* Bit 0 only. Never CLCD_INT_UNDERRUN — there is no FIFO here to
             * underrun, and asserting it only makes a correct driver log
             * errors. */
            c->intstatus |= CLCD_INT_FRAME;
            c->frames += elapsed;
        } else {
            c->frame_accum = (uint32_t)total;
        }
    }
    /* Gate on the hardware mask, not on the status alone. See the header
     * comment: the handler ANDs the status with its own shadow, so a line
     * asserted for a masked-off source makes it acknowledge nothing and
     * re-enter forever. */
    return (c->intstatus & c->intmask) != 0u;
}

bool s5l_clcd_seed_window0(s5l_clcd_t *c, uint32_t fb_phys,
                           uint32_t width, uint32_t height,
                           uint32_t stride, uint32_t format, uint32_t order) {
    if (!c || width == 0 || width > 0x400u ||
        height == 0 || height > 0x3ffu ||
        format > CLCD_FMT_MASK || order > CLCD_ORDER_MASK ||
        (stride & 3u) != 0u) {
        return false;
    }

    uint32_t bytes_per_pixel = CLCD_FMT_IS_32BPP(format) ? 4u : 2u;
    uint64_t row_bytes = (uint64_t)width * bytes_per_pixel;

    /*
     * AppleH1CLCD does not describe only the bytes touched by visible pixels.
     * It passes round_up(stride * height, 0x1000) to
     * IOMemoryDescriptor::withPhysicalAddress.  Validate exactly that mapping:
     * the multiply and round-up must remain representable by the driver's
     * 32-bit size argument, and the complete page-rounded physical span must
     * stay in the 32-bit guest address space.
     */
    const uint64_t page_mask = UINT64_C(0x0fff);
    uint64_t allocation_bytes = (uint64_t)stride * height;
    if ((uint64_t)stride < row_bytes ||
        allocation_bytes > UINT32_MAX) {
        return false;
    }
    uint64_t allocation_span =
        (allocation_bytes + page_mask) & ~page_mask;
    if (allocation_span == 0u || allocation_span > UINT32_MAX ||
        (uint64_t)fb_phys + allocation_span > UINT64_C(0x100000000)) {
        return false;
    }

    s5l_clcd_window_t *w = &c->win[0];
    w->stride    = stride;
    w->control   = ((format & CLCD_FMT_MASK)   << CLCD_FMT_SHIFT) |
                   ((order  & CLCD_ORDER_MASK) << CLCD_ORDER_SHIFT);
    w->fbaddr    = fb_phys;
    w->geometry  = (width << 16) | height;
    w->linewords = stride / 4u;
    w->position  = 0;

    /* Display on, window 0 on. These are the same bits start_hardware forces,
     * so the driver's read-modify-write leaves them set. */
    c->ctrl |= CLCD_CTRL_ENABLE | CLCD_CTRL_WIN0;

    /*
     * Supply the N82 clock and panel geometry instead of planting only a
     * framebuffer. openiBoot's optC is:
     *
     *   320x480, H back/front/sync 15/15/16, V 4/4/4,
     *   inverted VCLK, 54 MHz display clock / 5, scanout enabled.
     *
     * The emulator's VBL cadence does not depend on these registers, but
     * AppleH1CLCD preserves/restores them across power transitions. VIDTCON2
     * follows the requested window geometry so the public seed helper never
     * exposes contradictory active-area and window dimensions; production N82
     * callers request the native 320x480.
     */
    c->gate = CLCD_N82_VIDCON0;
    c->opaque[(CLCD_VIDTCON0 - CLCD_OPAQUE_FIRST) / 4u] = 0x00030303u;
    c->opaque[(CLCD_VIDTCON1 - CLCD_OPAQUE_FIRST) / 4u] = 0x000e0e0fu;
    c->opaque[(CLCD_VIDTCON2 - CLCD_OPAQUE_FIRST) / 4u] =
        ((width - 1u) << 16) | (height - 1u);
    c->opaque[(CLCD_VIDTCON3 - CLCD_OPAQUE_FIRST) / 4u] = 0x00000001u;

    /* openiBoot initializes the three window-configuration words to 0x1000
     * and their paired auxiliaries to zero before programming the window. The
     * 0xe8 word later becomes AppleH1CLCD's 0x50001000 update command. */
    c->wincfg_aux[0] = 0x00001000u;
    c->wincfg_aux[1] = 0u;
    c->wincfg_aux[2] = 0x00001000u;
    c->wincfg_aux[3] = 0u;
    c->update2 = 0x00001000u;
    c->wincfg_aux[4] = 0u;

    /*
     * iBoot leaves the panel lit and scanning. Starting scanout here means the
     * frame interrupt is already running when the kernel arrives, which is what
     * the hardware would be doing — and it is safe, because the line is gated
     * by the interrupt mask at 0x014 and that is still zero until a driver
     * writes it. Nothing can fire at the guest before it asks for it.
     */
    c->enable   = 1;
    c->scanning = true;
    c->frame_accum = 0;
    return true;
}

/* The driver's own enable-bit order: window 0 first, then 1, 2, 3. */
static const uint32_t ENABLE_BIT[CLCD_WIN_COUNT] = {
    CLCD_CTRL_WIN0, CLCD_CTRL_WIN1, CLCD_CTRL_WIN2, CLCD_CTRL_WIN3
};

bool s5l_clcd_window(const s5l_clcd_t *c, unsigned k,
                     uint32_t *fb_phys, uint32_t *width, uint32_t *height,
                     uint32_t *stride, uint32_t *format, uint32_t *order) {
    if (k >= CLCD_WIN_COUNT) return false;
    if (!(c->ctrl & ENABLE_BIT[k])) return false;

    const s5l_clcd_window_t *w = &c->win[k];
    if (fb_phys) *fb_phys = w->fbaddr;
    if (width)   *width   = (w->geometry >> 16) & 0x7ffu;
    if (height)  *height  =  w->geometry        & 0x3ffu;
    if (stride)  *stride  = w->stride;
    if (format)  *format  = (w->control >> CLCD_FMT_SHIFT)   & CLCD_FMT_MASK;
    if (order)   *order   = (w->control >> CLCD_ORDER_SHIFT) & CLCD_ORDER_MASK;
    return true;
}

/*
 * Exactly the selection AppleH1CLCD makes at 0xc0705f10: read CLCD_CTRL, take
 * the first of 0x40/0x20/0x10/0x08 that is set. This is a real decision, not a
 * convenience — the driver reads the chosen window's four registers and builds
 * the IOSurface that becomes the screen out of them, so whichever window this
 * function names is the one that has to hold the picture.
 */
uint32_t s5l_clcd_active_window(const s5l_clcd_t *c) {
    for (unsigned k = 0; k < CLCD_WIN_COUNT; k++)
        if (c->ctrl & ENABLE_BIT[k]) return k;
    return CLCD_WIN_NONE;
}

bool s5l_clcd_scanout(const s5l_clcd_t *c, unsigned k,
                      const uint8_t *ram, uint32_t ram_base, size_t ram_len,
                      uint8_t *rgb, size_t rgb_len,
                      uint32_t *out_w, uint32_t *out_h) {
    uint32_t fb, w, h, stride, fmt, order;
    if (!ram || !rgb) return false;
    if (!s5l_clcd_window(c, k, &fb, &w, &h, &stride, &fmt, &order)) return false;
    if (!w || !h) return false;

    /*
     * The driver classifies depth and nothing finer (see CLCD_FMT_IS_32BPP in
     * soc.h and the jump table it cites), so this decodes on depth and nothing
     * finer either. A 16-bit window is 5-6-5 because that is the FourCC the
     * driver publishes for it; a 32-bit window is the driver's 'ARGB', which in
     * a little-endian word is 0xAARRGGBB and therefore B,G,R,A in memory.
     *
     * `order` is deliberately NOT acted on. The driver publishes 'ARGB' whatever
     * bits [17:16] hold, so we have no evidence for what they change; acting on
     * them would be inventing a swizzle. It is reported through
     * s5l_clcd_window() so a caller can see it, and that is all.
     */
    const uint32_t bpp = CLCD_FMT_IS_32BPP(fmt) ? 4u : 2u;

    /* A stride that cannot hold one row means the programming is wrong, and a
     * wrong picture drawn confidently is worse than no picture. */
    if (stride < (uint64_t)w * bpp) return false;
    if ((uint64_t)w * h * 3u > rgb_len) return false;

    /*
     * The window's framebuffer address is PHYSICAL (the driver hands it to
     * IOMemoryDescriptor::withPhysicalAddress). Resolve it against guest DRAM
     * and require the WHOLE source rectangle to be inside: a window pointing at
     * MMIO, or one row past the end of RAM, is a fault to report, not a black
     * band to paint. The last row only needs w*bpp bytes, not a whole stride.
     */
    if (fb < ram_base) return false;
    uint64_t off  = (uint64_t)fb - ram_base;
    uint64_t need = (uint64_t)(h - 1u) * stride + (uint64_t)w * bpp;
    if (off + need > ram_len) return false;

    for (uint32_t y = 0; y < h; y++) {
        const uint8_t *src = ram + off + (uint64_t)y * stride;
        uint8_t       *dst = rgb + (size_t)y * w * 3u;
        for (uint32_t x = 0; x < w; x++) {
            if (bpp == 4u) {
                /* 0xAARRGGBB little-endian: B,G,R,A. */
                dst[0] = src[2]; dst[1] = src[1]; dst[2] = src[0];
            } else {
                uint32_t p = (uint32_t)src[0] | ((uint32_t)src[1] << 8);
                uint32_t r5 = (p >> 11) & 0x1fu;
                uint32_t g6 = (p >> 5)  & 0x3fu;
                uint32_t b5 =  p        & 0x1fu;
                /* Replicate the high bits into the low ones so full-scale in
                 * stays full-scale out; a plain shift would cap white at 0xf8. */
                dst[0] = (uint8_t)((r5 << 3) | (r5 >> 2));
                dst[1] = (uint8_t)((g6 << 2) | (g6 >> 4));
                dst[2] = (uint8_t)((b5 << 3) | (b5 >> 2));
            }
            src += bpp;
            dst += 3;
        }
    }
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
    return true;
}
