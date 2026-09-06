/*
 * S5LBox — Synopsys DesignWare USB 2.0 OTG (DWC2) hardware-configuration
 * registers.
 *
 * This is not a USB controller. It is the four registers a stock driver reads
 * to find out what silicon it is talking to, and nothing else — no transfer,
 * FIFO, endpoint, DMA, PHY or interrupt behaviour is emulated, and no interrupt
 * is ever asserted. It exists because leaving the block unmodelled was not the
 * neutral choice it looks like.
 *
 * ---------------------------------------------------------------- the panic
 * With nothing behind 0x38400000, every read of it returned the zero an
 * unmapped access returns, and the guest believed it. AppleSynopsysOTG2 then
 * panicked in AppleSynopsysOTGDevice::provideEndpointIDsForConfiguration
 * ("ran of OUT endpoints", AppleSynopsysOTGDevice.cpp:533) at retired
 * instruction 8,728,148,009 — the same instruction on every run, which is what
 * made it a cap on the boot rather than a flake.
 *
 * The endpoint layout it panics about is derived one function earlier, in
 * AppleSynopsysOTGDevice::findMaxEndpoints (kernel VA 0xc048c424). Reading that
 * disassembly gives the exact MMIO the driver performs, in order:
 *
 *     1. read  0x38400E00   PCGCCTL
 *     2. write 0x38400E00   value &= ~3        ; ungate the clocks
 *     3. read  0x38400048   GHWCFG2
 *     4. read  0x38400044   GHWCFG1
 *     5. read  0x38400E00   PCGCCTL
 *     6. write 0x38400E00   value |= 1         ; gate them again
 *
 * and the derivation it runs on what it read, which simulating the
 * disassembly reproduces exactly:
 *
 *     NumDevEps = (GHWCFG2 >> 10) & 0xF
 *     in = out = 0 ; ep = 1
 *     while (NumDevEps >= in + out) {
 *         d = (GHWCFG1 >> (2*ep)) & 3     // 00=BIDIR 01=IN 10=OUT 11=abort
 *         d==0 -> in++, out++ ;  d==1 -> in++ ;  d==2 -> out++ ;  d==3 -> fail
 *         max_endpoint = ep ; ep++
 *     }
 *     num_endpoints = in + out + 2
 *
 * All-zero registers give in=1, out=1, max_endpoint=1, num_endpoints=4 —
 * self-inconsistent, and precisely the "in EPs: 1 out, EPs: 1, max_endpoint: 1,
 * num_endpoints: 4" the panicking run printed. So the zero was not an absence
 * of information; it was wrong information the driver had no way to doubt.
 *
 * ------------------------------------------------------- the values, honestly
 * These are a legal and sufficient DWC2 configuration. They are NOT measured
 * from real S5L8900 silicon: we have no dump of this part's configuration
 * registers, and this file does not pretend to one. What can be defended is
 * that each value is a configuration the hardware family really ships and that
 * satisfies the guest's own stated requirements.
 *
 * GHWCFG1 = 0 — every endpoint bidirectional.
 *   This is what most real DWC2 instantiations report, and it is the only class
 *   of value that is safe against a bug in this driver. The loop above never
 *   bounds `ep`: with any unidirectional encoding only one of `in`/`out`
 *   advances per iteration, so `ep` runs past 15 and overflows the driver's
 *   15-byte endpoint lists. An all-bidirectional strap advances both counters
 *   every iteration and keeps `ep` small.
 *
 * GHWCFG2 = 0x228de550 — the well-known DWC2 configuration word with
 *   NumDevEps=9. Decoded: OtgMode=0, OtgArch=2 (internal DMA), HSPhyType=1
 *   (UTMI+), NumDevEps=9, NumHstChnl=7, PerioSupport=1, DynFifoSizing=1.
 *   Through the loop above it yields in=5, out=5, max_endpoint=5,
 *   num_endpoints=12.
 *
 *   5 IN / 5 OUT is enough for this guest, and that is read from the guest
 *   rather than assumed: its own USB configuration plist in the rootfs
 *   (iPhone1,2, standardMuxPTPEthernet) needs 2 OUT / 3 IN pipes in the default
 *   configuration and 3 OUT / 4 IN in the worst case. So every configuration it
 *   can select fits, with margin.
 *
 * GHWCFG4 = 0 — the driver reads it and uses only bit 25. Zero is what the
 *   unmodelled window already returned, so this register changes nothing
 *   today; it is modelled explicitly so that the value is a stated choice
 *   rather than the side effect of a missing device.
 *
 * GHWCFG3 (0x04c) is deliberately absent. The driver's traced accesses do not
 * include it, so it stays in the same accept-and-discard default as the rest of
 * the page instead of acquiring an invented value.
 *
 * -------------------------------------------------------------------- PCGCCTL
 * Storage, and only storage. The guest's sequence is read / clear bits 0 and 1 /
 * ... / read / set bit 0, so what it needs is a word that remembers what it was
 * given. Nothing here gates a clock, and nothing self-clears: this model has no
 * clock to gate, and inventing a bit that changes on its own is exactly what
 * turned an unmodelled window into a panic in the first place.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#include "soc.h"
#include <string.h>

void s5l_usbotg_reset(s5l_usbotg_t *u) {
    memset(u, 0, sizeof *u);
}

uint32_t s5l_usbotg_read(const s5l_usbotg_t *u, uint32_t off) {
    switch (off) {
        /* Hardware straps: the same word on every read, whatever was written. */
        case USBOTG_GHWCFG1: return S5L_DWC2_GHWCFG1;
        case USBOTG_GHWCFG2: return S5L_DWC2_GHWCFG2;
        case USBOTG_GHWCFG4: return S5L_DWC2_GHWCFG4;

        case USBOTG_PCGCCTL: return u->pcgcctl;

        /* Everything else in the page reads zero — unchanged from what an
         * unmapped access answered before this window existed. Modelling one
         * register does not license inventing the other 1020. */
        default: return 0;
    }
}

void s5l_usbotg_write(s5l_usbotg_t *u, uint32_t off, uint32_t val) {
    switch (off) {
        case USBOTG_PCGCCTL: u->pcgcctl = val; break;

        /* GHWCFG* are read-only configuration straps. A write is accepted by
         * the bus and discarded, as it is on the real block; making them
         * writable would let a guest talk itself back into the panic. */
        case USBOTG_GHWCFG1:
        case USBOTG_GHWCFG2:
        case USBOTG_GHWCFG4: break;

        default: break;
    }
}
