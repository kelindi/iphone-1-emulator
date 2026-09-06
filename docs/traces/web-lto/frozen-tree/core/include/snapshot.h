/*
 * S5LBox — full machine snapshot / restore.
 *
 * WHY THIS EXISTS. Reaching the current frontier of the boot costs ~2 billion
 * interpreted instructions, i.e. minutes of wall clock per debugging iteration.
 * docs/dynarec.md §11.2 concludes that the fix for that loop is snapshotting,
 * not a JIT: "Serialise arm_cpu_t, guest RAM, and every device's state to a
 * file, and restore it." This is that.
 *
 * WHAT IT PROMISES. A restored machine is bit-for-bit the machine that was
 * saved: every CPU register including all banked modes, CPSR and the SPSRs,
 * the whole cp15 struct, the exclusive monitor, the VFP control registers,
 * guest RAM, the NOR contents and its scanned directory, every device's
 * registers and counters and pending-interrupt state, and the machine's own
 * diagnostic counters. Continuing a restored machine must produce exactly the
 * output continuing the original would have produced; if it does not, this
 * file has a missing field and that is a bug, not a tolerance.
 *
 * WHAT IT DOES NOT COVER. Only the emulated machine (`s5l8900_t`). Host-side
 * state belonging to a *tool* — bootkernel's trace ring, its milestone hit
 * counts, its sampled profile — is not machine state and is not saved; a
 * restored process starts those counters fresh. Nothing the guest can observe
 * lives there.
 *
 * FAILURE POLICY. This core's rule is "trap what you don't implement, never
 * guess". A snapshot that half-loads is worse than one that refuses, because
 * the divergence it causes surfaces a billion instructions later. So: the
 * magic, the version, the payload length and a checksum over the whole payload
 * are all verified BEFORE any byte of the machine is touched, and any failure
 * is reported rather than papered over.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#ifndef S5LBOX_SNAPSHOT_H
#define S5LBOX_SNAPSHOT_H

#include <stddef.h>
#include <stdint.h>
#include "soc.h"

/* 16 bytes, stored without a terminator. */
#define SNAPSHOT_MAGIC     "S5LBox SNAPSHOT"
#define SNAPSHOT_MAGIC_LEN 16u

/*
 * Bump this whenever the serialised form changes in any way — a new field, a
 * reordered field, a changed section. There is deliberately no compatibility
 * shim: an old snapshot restored into a newer emulator would be a machine with
 * one register quietly holding the wrong value, which is the single most
 * expensive class of bug this feature exists to prevent.
 */
/* v2: the VFP register file (arm_cpu_t.vfp_s, s0-s31 aliasing d0-d15) joined
 *     the CPU section when real VFPv2 arithmetic was implemented. */
/* v3: both I2C controllers and the PCF50635 PMU/RTC joined MACH. Old
 *     checkpoints cannot safely invent an in-flight transfer or RTC state. */
/* v4: the three-bank TV-out controller and its VSYNC phase joined MACH. */
/* v5: the Synopsys DWC2 USB OTG block's PCGCCTL joined MACH. Its GHWCFG straps
 *     are build constants rather than state, so PCGCCTL is the whole of it. */
/* v6: the two SPI controllers joined MACH, with their transmit and receive
 *     FIFOs. A transfer can be in flight across a checkpoint — that is the
 *     whole point of the model, since the guest sleeps inside one — so the FIFO
 *     bytes and levels are state and the payload genuinely grows. The stub
 *     windows for spi0 and spi1 also disappear from GEOM, which a v5 file would
 *     not agree with either. */
/* v7: the two halves of /arm-io/gpio and the multi-touch controller joined
 *     MACH. This one could not have been avoided by any encoding choice: the
 *     GPIO interrupt controller's pending latch and enable masks are guest
 *     state that decides whether a touch report can reach the CPU, the pin
 *     block is 4 KiB of guest-written levels one of which is the touch
 *     controller's reset line, and the Z2's own protocol position — which
 *     packet it is part way through, and whether it is still answering as its
 *     bootloader — is state a checkpoint cannot invent. The `gpio` and
 *     `gpioic` stub windows also disappear from GEOM, so a v6 file would fail
 *     the geometry check even if the payload happened to line up. */
/* v8: the touch controller's HBPP bookkeeping changed shape. One `hbpp` bool
 *     became `in_reset` plus a monotonic `hbpp_answered`, and a `reset_bytes`
 *     counter joined it. The byte format therefore differs even where the host
 *     ABI size does not, and a v7 file restored into a v8 build would land a
 *     stale claim in the wrong field -- which decides whether the driver stays
 *     attached or pushes 54 KB of firmware at a device that cannot take it. */
/* v9: the touch controller gained a report to deliver. The pending payload,
 *     its length, the frame sequence number, the framer's latched tx[2] and
 *     the power-line level all joined MACH, and the reply buffer grew from 40
 *     bytes to MTZ2_PAYLOAD_LIMIT + 7 because a data read is L + 5 bytes long.
 *     No encoding choice could have avoided this: a checkpoint taken between
 *     the length read and the data read has already told the guest a length,
 *     and a v8 file has no payload to answer the second half of that exchange
 *     with. Every field after the Z2's in MACH also shifts, so a v8 file read
 *     as v9 would misparse the USB OTG block onwards rather than fail. */
/* v10: uart4 joined MACH. The PPP line is a second full s5l_uart_t — five
 *     configuration registers, an 8 KiB transmit capture and its length — and
 *     it is serialised immediately after uart0, at the very front of the
 *     section. That placement is what makes the bump unavoidable rather than
 *     merely tidy: EVERY field after it moves by 8,224 bytes, so a v9 file
 *     read as v10 would hand uart0's capture to uart4, then read the VICs out
 *     of the middle of a console log and carry on — it would MISPARSE, not
 *     fail, and the first symptom would be an interrupt mask a billion
 *     instructions later. The capture also cannot be reconstructed: it is the
 *     only record of what the guest transmitted before the checkpoint, and the
 *     milestone this port exists for is a six-byte sequence at its head. */
/* v11: the WM8991 codec and both I2S windows joined MACH. The codec is
 *      serialised immediately after the PMU — the two I2C slaves adjacent, in
 *      the order they are attached — and the I2S pair immediately after it, so
 *      EVERY field from the SPI controllers onwards moves by 680 bytes — 488
 *      for the codec and 96 for each window. That placement is what makes this
 *      a bump rather than a free append: a v10
 *      file read as v11 would hand the PMU's tail to the codec, then read spi0
 *      out of the middle of a register file and carry on. It would MISPARSE,
 *      not fail, and the first symptom would be a touch controller whose FIFO
 *      level came from a codec register — a divergence that surfaces a billion
 *      instructions later, which is the exact failure this format exists to
 *      prevent.
 *
 *      The codec's state also cannot be reconstructed by re-probing. Its
 *      transfer position is genuinely in flight across a checkpoint: the stock
 *      controller sets the register pointer in one I2C transaction and reads in
 *      the NEXT, so a snapshot taken between them has already told the guest
 *      which register it is about to read, and `second_byte` records whether
 *      the MSB of that register has already gone out on the wire. A file with
 *      neither would resume the read byte-swapped. `written[]` matters for the
 *      same reason it does on the PMU: it is what separates "this register
 *      holds zero because the guest wrote zero" from "nobody has ever touched
 *      it", and only the latter is worth recording in a census.
 *
 *      The two I2S windows are seven stored words each and no more, because the
 *      driver never reads one back — see the I2S block in soc.h. They are still
 *      state: they are the configuration a resumed transfer would run under.
 *      Their windows additionally appear in GEOM, so a v10 file would fail the
 *      geometry check even if the payload happened to line up. */
/* v12: the board's five physical buttons joined MACH, immediately after the
 *      GPIO pin block, so every field from the touch controller onwards moves
 *      by 32 bytes. Which switches are held cannot be recovered from anything
 *      else in the file: the pin levels alone do not say, because two of the
 *      five are wired active low, so a RELEASED volume key and a PRESSED Home
 *      button are the same pin level and only this byte tells them apart. A
 *      v11 file read as v12 would therefore misparse the Z2 onwards as well as
 *      losing the state, which is why this is a hard break and not a field
 *      that could have been defaulted.
 *
 *      The refusal counter travels with it deliberately. A restored run that
 *      silently zeroed it would report a host that had been told "no" a
 *      thousand times as one that had never asked — and "the guest never armed
 *      the line" is exactly the diagnosis those refusals exist to carry.
 *
 *      The GPIO interrupt controller grew a `driven` mask in the same change,
 *      immediately after `raw`, for the same class of reason: it records which
 *      of the 224 lines have a device on the end of them at all, and a level
 *      line's pending condition is evaluated only for those. A file without it
 *      would restore a machine in which every level line was undriven and so
 *      permanently silent — or, defaulted the other way, one in which the seven
 *      unmodelled level lines asserted forever. That second one is not
 *      hypothetical: it is what run87 measured before the mask existed,
 *      668,039 acknowledges of a group-2 pending word the guest could never
 *      clear, and no progress past instruction ~96 million.
 *
 * v13: both UARTs grew a RECEIVE FIFO — sixteen bytes, a head, a count and four
 *      counters — and snap_uart() is the first visitor in MACH, so every field
 *      from the VICs onwards moves by 56 bytes twice. A hard break, and not one
 *      that could have been defaulted: the bytes in that FIFO are bytes the
 *      host's PPP peer has ALREADY transmitted and will never transmit again,
 *      because its restart timer (RFC 1661 §4.6) counted them as delivered. A
 *      v12 file read as v13 would misparse every device after the console; a
 *      v13 file restored with the FIFO zeroed would resume a link that stalls
 *      for one restart interval and then renegotiates, which looks exactly like
 *      a bug in the peer.
 *
 *      The four counters travel for the reason every other refusal counter in
 *      this format does: rx_dropped separates "the host never sent a byte" from
 *      "the host sent one into a full FIFO and it is gone", and rx_reads
 *      separates both from "the guest's driver never read URXH". Those are
 *      three different failures with three different next steps and nothing
 *      else in the machine can tell them apart.
 *
 * v14: the two PL080 DMA controllers, /arm-io/dmac0 and /arm-io/dmac1. Appended
 *      at the END of MACH, after usbotg, so nothing before them moves — but the
 *      payload is 640 bytes longer and a v13 reader would run off the end of
 *      the section, so this is still a hard break rather than a free append.
 *
 *      It could not have been defaulted. A DMA channel's five registers ARE the
 *      transfer in flight: source, destination, the remaining count in
 *      Control[11:0] and the address of the next linked-list item. Restoring
 *      them as zero would resume a guest whose driver is waiting on a channel
 *      that is no longer enabled, no longer has anything queued, and will never
 *      raise the terminal count it is blocked on — and AppleARMPL080DMAC's
 *      queryDMACommand spins on the Active bit with no deadline, so that guest
 *      does not fail, it stops.
 *
 *      The counters travel for the same reason v13's do. bytes_moved is the
 *      only thing in this machine that can distinguish "the guest never
 *      programmed a transfer" from "it programmed one and this model refused
 *      it", and refused_flow/width/chain/softreq/endian name which refusal. */
#define SNAPSHOT_VERSION   39u

typedef enum {
    SNAP_OK = 0,
    SNAP_ERR_IO,         /* could not open / read / write the file           */
    SNAP_ERR_MAGIC,      /* not a snapshot at all                            */
    SNAP_ERR_VERSION,    /* written by a different version of this format    */
    SNAP_ERR_TRUNCATED,  /* the file ends before the payload does            */
    SNAP_ERR_CHECKSUM,   /* payload does not match its recorded hash         */
    SNAP_ERR_CORRUPT,    /* section framing or a field value is nonsense     */
    SNAP_ERR_GEOMETRY,   /* the machine's RAM/NOR/stub layout does not match */
    SNAP_ERR_NOMEM
} snapshot_status_t;

const char *snapshot_strerror(snapshot_status_t st);

/*
 * Write the entire machine to `path`. The machine is not modified. The bytes
 * are first completed in the same directory and then atomically replace the
 * destination, so a failed save leaves an earlier checkpoint intact.
 */
snapshot_status_t snapshot_save(const s5l8900_t *m, const char *path);

/*
 * Restore `m` from `path`. `m` must already be a live machine built by
 * s5l8900_init() with the SAME ram_base/ram_size and the same set of stub
 * windows; a mismatch is refused with SNAP_ERR_GEOMETRY rather than silently
 * resized, because the alternative is a machine whose physical map does not
 * match the addresses baked into the guest's page tables.
 *
 * Host-owned pointers (RAM allocation, NOR allocation, stub backing stores,
 * the bus vtable and cpu->bus) are preserved; only their CONTENTS are
 * overwritten. That is what lets a tool interpose on the bus (as bootkernel
 * does) and still restore underneath it.
 *
 * Malformed data, checksum failures and geometry mismatches are rejected
 * before the applying pass and leave the machine untouched. A genuine file
 * read failure (or external in-place modification) during that final pass can
 * leave contents partially applied; in-memory loads are transactional for all
 * malformed inputs.
 */
snapshot_status_t snapshot_load(s5l8900_t *m, const char *path);

/* In-memory forms, used by the tests. `*out` is malloc'd and owned by the
 * caller. Identical byte stream to the file forms. */
snapshot_status_t snapshot_save_mem(const s5l8900_t *m,
                                    uint8_t **out, size_t *out_len);
snapshot_status_t snapshot_load_mem(s5l8900_t *m,
                                    const uint8_t *buf, size_t len);

/* Consistent capture with incremental checksum finalization. Begin must run
 * while the guest is stopped; after it returns the guest may run or be freed.
 * Advance reads at most max_bytes from the owned copy, returning true when
 * complete. Data is NULL until then and belongs to capture until free. Final
 * bytes are identical to snapshot_save_mem(), including the v39 checksum. */
typedef struct snapshot_capture snapshot_capture_t;
snapshot_status_t snapshot_capture_begin(const s5l8900_t *m,
                                          snapshot_capture_t **out);
bool snapshot_capture_advance(snapshot_capture_t *capture, size_t max_bytes);
const uint8_t *snapshot_capture_data(const snapshot_capture_t *capture,
                                      size_t *length);
void snapshot_capture_free(snapshot_capture_t *capture);

#endif /* S5LBOX_SNAPSHOT_H */
