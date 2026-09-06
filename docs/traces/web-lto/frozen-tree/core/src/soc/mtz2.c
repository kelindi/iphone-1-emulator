/*
 * S5LBox — the multi-touch controller on spi1 chip select 0.
 *
 * /arm-io/spi1/multi-touch, `compatible "multi-touch,n82"`, reg[0] = 0 (the
 * chip select — there is no `chip-select` property anywhere in the shipped
 * tree), `interrupts {0x9b, 0}` = GPIO interrupt line 155, `function-reset`
 * GPIO 0x0606 and `function-power_ldo` GPIO 0x0701.
 *
 * ===========================================================================
 * THE ONE THING THAT MUST NOT BE GOT BACKWARDS
 * ===========================================================================
 * AppleMultitouchZ2SPI probes this device with isInHBPP() — 0xc0441008,
 * reached by virtual dispatch through slot 0x4d0 of the vtable at 0xc0449f40,
 * never by a direct bl — and TWO CALLERS WANT OPPOSITE ANSWERS:
 *
 *   finishStarting() 0xc0442670 takes `beq 0xc0442714` on FALSE, prints
 *   "Could not detect HBPP. Returning false from finishStarting()" and
 *   DETACHES. That is the state run61 measured against the null device and the
 *   whole reason this file exists. It needs TRUE.
 *
 *   attemptToBootloadDevice 0xc04414c4 probes first thing
 *   (`ldr pc,[r3,#0x4d0]` at 0xc04414dc). On TRUE it logs "attempting to
 *   bootload device" and pushes 54,156 bytes of firmware — run65 watched it
 *   reach "MTSPIBootloader_Z2::bootloadDevice() / sending preconstructed
 *   firmware bytes". On FALSE it logs "not in HBPP, so skipping bootload"
 *   (literal 0xc04486f0, referenced only from this function's pool at
 *   0xc04415e4) and returns 0 — which the retry loop at 0xc043a980 counts as
 *   one failure, printing "Bootload attempt %d of %d failed". It needs FALSE.
 *
 * The two sites are BYTE-IDENTICAL on the wire, so the discrimination has to be
 * stateful. It is one monotonic bit, `hbpp_answered`, and getting the state it
 * keys on right took four wrong designs and two boots:
 *
 *   1. "Watch the reset line: the bootload probe follows resetDevice(TRUE)."
 *      Both sites reset. Worthless as a discriminator on its own.
 *
 *   2. "Accept the first probe, decline the rest." MEASURED WRONG. A run with
 *      `--call-probe-kernel 0xc0441008 --call-probe-kernel 0xc0442670` shows
 *      finishStarting entered once, at instruction 220,700,146, and isInHBPP
 *      entered once, at 316,898,121 with lr = 0xc04426cc. But spi1 carried TWO
 *      byte-identical 16-byte `1A A1 18 E1…` transfers, at 309.53M and 316.91M.
 *      The first is resetDevice's DUMMY TRANSFER (0xc0440d7c builds the same
 *      sixteen bytes and logs "initiating dummy transfer"), whose answer is
 *      discarded. A plain probe counter spends itself there and hands
 *      finishStarting the rejection.
 *
 *   3. "Never decline, then." That is what shipped in 098ce49, and run65 showed
 *      where it leads: the bootload probe answered TRUE and the driver began
 *      pushing firmware. Correct behaviour for an unprogrammed part; not what
 *      this model can survive.
 *
 *   4. "A reset restores HBPP, so a reset must also re-arm the claim." No. The
 *      guest pulses this line at every site, so re-arming the claim makes site
 *      2 identical to site 1 — which is bug 3 again. A reset restores the
 *      part's STATE; it does not un-ask the question the host already asked.
 *
 * WHAT ACTUALLY SEPARATES THE TWO EXCHANGES IS THE RESET LINE'S LEVEL, and the
 * guest's own log says so:
 *
 *      mtlog: enabling power
 *      mtlog: ensuring S_CLK is high
 *      mtlog: initiating dummy transfer      <- reset still ASSERTED
 *      mtlog: Deasserting reset line
 *      mtlog: checking if in HBPP            <- reset RELEASED
 *
 * confirmed against the GPIO trace to the instruction: fsel write 0x0006060e
 * (group 6, bit 6, level 0) at 220,635,069 asserts; the dummy runs at
 * 309.53M; 0x0006060f releases at 309,541,162; the probe runs at 316.91M;
 * 0x0006060e asserts again at 316,965,809. So the line is ACTIVE LOW, the
 * dummy is entirely inside the asserted window and the probe entirely outside
 * it.
 *
 * A part held in its reset pin drives nothing. Modelling that one physical
 * fact makes the dummy answer sixteen zeros — which the driver discards, as it
 * always did — and leaves `hbpp_answered` for the exchange that is actually
 * read. No magic constant, no counting of transfers, and every element of it
 * is visible in the guest's own log.
 *
 * `hbpp_answered` is still a lie: a real unprogrammed Z2 answers TRUE forever
 * and expects firmware. It is a bounded, named, single-bit lie that costs three
 * cosmetic log lines, and the alternative is in the note at the end of this
 * comment.
 *
 * ANSWERING TRUE ONCE ALSO CHOOSES THE FRAME OPCODE. The true return runs
 * `strb r3, [r4, #0x1bc]` at 0xc04426fc, and deviceReadResultData at
 * 0xc0441324 rejects 0xEB unless this+0x1bc is non-zero. So this device's Z2
 * frame opcode is downstream of the HBPP answer rather than independent of it.
 *
 * THE ALTERNATIVE, AND WHY NOT YET. Implementing the HBPP sink — accepting the
 * 54,156 bytes and afterwards reporting firmware resident — removes the lie
 * entirely.
 *
 * IT IS BLOCKED BY DMA AFTER ALL, and this paragraph used to say the opposite.
 * The old text read: "It is NOT blocked by DMA, which was the standing
 * objection: MTSPIBootloader_Z2 pushes through the ordinary SPI entry
 * `v[0x368]` at 0xc0444ff8, 0xc0445224 and 0xc04454d0." Those three sites are
 * real, and they are all in the CALIBRATION senders. The FIRMWARE sender is
 * 0xc0445144 and it has a fourth site the survey missed — `v[0x360]` at
 * 0xc04451ec — chosen at 0xc04451b0 on `bootloader->0x2c`, which the guest's
 * own console announces as "AppleMultitouchZ2SPI: using DMA for bootloading".
 *
 * run103 measured which arm is taken, and it is not close:
 *
 *     0xc04451c4  the DMA path        15 captures
 *     0xc04451ec  v[0x360], the DMA transfer   15
 *     0xc04451f4  the PIO path         0
 *     0xc0445224  v[0x368], the PIO transfer    0
 *
 * fifteen being three bootload attempts times the five retries each makes. The
 * transfer then RETURNS SUCCESS — 0xc044525c, the success branch, fires all
 * fifteen times — and the acknowledgement after it still does not answer
 * 0x4BC1. The device never saw the packet: its framer is still waiting for a
 * sixteen-octet probe, so it loops the `1A A1` back and the driver reads
 * 0x1AA1. That much is measured and stands.
 *
 * WHY the packet never arrives has been wrong twice, and both readings are
 * retracted here rather than quietly edited away.
 *
 *   - "core/src/soc/spi.c has no DMA path at all" was the first. The SPI model
 *     is not the obstacle: across every run the PL080 has refused nothing
 *     (`flow 0 width 0 chain 0 softreq 0 endian 0`), and soc.h documents
 *     /arm-io/spi1's asymmetric widths — four bytes in, one out — as modes it
 *     runs, while the shipped tree only ever uses flow control 0-3.
 *
 *   - "the SPI controller holds no DMA channel, so v[0x360] refuses with
 *     kIOReturnDMAError" was the second, and run112 killed it: the refusal at
 *     0xc05a69ec is captured ZERO times and the success path 41, with a real
 *     TX channel (0xc0e19400) in 40 of 41 calls. The RX channel is null, but
 *     the request never asks for RX DMA, which is correct for a sender.
 *
 * What is left is the transfer itself: the channel is aimed at SPI_TXDATA and
 * carries transfer size 0 with no Enable bit. Whether the controller's global
 * enable is ever written is being measured properly for the first time — the
 * old report inferred it from the register's resting value, which cannot tell
 * "never written" from "written and cleared".
 *
 * So the work is in the SPI controller, not here. What also blocks it is that
 * the bootloader's own multi-stage protocol is unread, its failure mode is a
 * hang inside commandSleep rather than an error, and the feedback loop is an
 * 18-minute boot because the timer does not fire until instruction 1.11e9. It
 * is a fidelity upgrade, not a prerequisite: nothing above the bootloader can
 * tell a part that was programmed from one that says it was.
 *
 * ===========================================================================
 * THE WIRE PROTOCOL
 * ===========================================================================
 * Full duplex, one byte out for one byte in, framed by length. Command frames
 * are 16 bytes:
 *
 *     [0]      opcode
 *     [1..4]   parameters (report id in [1] for 0xE3/0xE6/0xE7)
 *     [5..13]  zero
 *     [14..15] LE16 sum16 of [0..13]
 *
 * CONFIRMED at 0xc0443288-0xc044329c, which builds GET_REPORT_INFO as
 * `tx[0]=0xE3; tx[1]=id; tx[14..15] = LE16(0xE3 + id)` — the plain byte sum of
 * a buffer that memset zeroed, with no seed and no complement.
 *
 * The driver validates a response two ways and only two ways
 * (0xc04433e4-0xc0443438 for 0xE3, 0xc04409b8-0xc04409f0 for 0xE1):
 *
 *     rx[0] == tx[0]                      else kIOReturnNotResponding
 *     LE16(rx[14..15]) == sum16(rx, 14)   else an error
 *
 * Both are satisfied by answering IN THE SAME TRANSFER: byte 0 of the reply is
 * the byte being received at that instant, and every later byte can depend on
 * every byte already received — including the report id at [1], which arrives
 * one slot before the first byte of the reply that needs it. The driver sends
 * 0xE3 twice with an IODelay(0x19) between (0xc04432ec and 0xc0443378) and
 * reads only the second, so answering both identically is correct and the
 * two-phase shape costs this model nothing.
 *
 * WE NEVER VALIDATE THE HOST'S CHECKSUM, and that is not laziness. The
 * control-read stage-2 packet ships a STALE one: 0xc0441ff8 stores tx[2] = 1
 * and the transfer at 0xc0442030 follows with no recomputation, so the frame
 * carries sum16 computed while tx[2] was still zero. 0xE5 by contrast does
 * recompute. A checksum-strict device would reject every control read the
 * driver makes, and whether real silicon checks it cannot be decided from
 * driver code.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#include "soc.h"
#include <string.h>

uint16_t s5l_mtz2_sum16(const uint8_t *p, unsigned n) {
    uint16_t s = 0;
    if (!p) return 0u;
    while (n--) s = (uint16_t)(s + *p++);
    return s;
}

/* ============================================================ the reports ===
 *
 * ALL GEOMETRY COMES FROM THE DEVICE. The device tree carries none of it — no
 * row count, no column count, no surface size — so every number below is a
 * choice this project makes, and each one is justified where it is set in
 * s5l_mtz2_reset().
 */
unsigned s5l_mtz2_report(const s5l_mtz2_t *dev, uint8_t id, uint8_t *out) {
    s5l_mt_geom_t geom;
    if (!dev || !out) return 0u;
    geom.family_id = dev->family_id;
    geom.endianness = dev->endianness;
    geom.rows = dev->rows;
    geom.columns = dev->columns;
    geom.bcd_version = dev->bcd_version;
    geom.buttons = dev->buttons;
    geom.surface_width = dev->surface_width;
    geom.surface_height = dev->surface_height;
    return s5l_mt_report(&geom, id, out);
#if 0
    memset(out, 0, MTZ2_REPORT_MAX);
    switch (id) {
        case MTZ2_REPORT_FAMILY_ID:
            /* 0xc0438670 reads data[0] and publishes it as "Family ID". */
            out[0] = dev->family_id;
            return 1u;
        case MTZ2_REPORT_GEOMETRY:
            /*
             * 0xc04386e0: data[0] -> "Endianness" (also cached at this+0x1a0),
             * data[1] -> "Sensor Rows", data[2] -> "Sensor Columns", and
             * data[3..4] -> "bcdVersion" assembled BIG-endian
             * (`orr r3, data[4], data[3] lsl #8`). Note the byte order: this
             * is the one field in the whole protocol that is not little-endian.
             */
            out[0] = dev->endianness;
            out[1] = dev->rows;
            out[2] = dev->columns;
            out[3] = (uint8_t)(dev->bcd_version >> 8);
            out[4] = (uint8_t)(dev->bcd_version & 0xffu);
            return 5u;
        case MTZ2_REPORT_BUTTONS:
            /* 0xc043898c reads data[0] and publishes it as "Buttons". */
            out[0] = dev->buttons;
            return 1u;
        case MTZ2_REPORT_SURFACE:
            /*
             * 0xc04388dc requires length >= 8 (`cmp r3,#7 / bls`) and reads two
             * UNALIGNED little-endian 32-bit words, `ldr r2,[sp,#5]` and
             * `ldr r8,[r2,#5]`, publishing them as "Sensor Surface Width" and
             * "Sensor Surface Height". A shorter report is dropped silently.
             */
            out[0] = (uint8_t)(dev->surface_width);
            out[1] = (uint8_t)(dev->surface_width >> 8);
            out[2] = (uint8_t)(dev->surface_width >> 16);
            out[3] = (uint8_t)(dev->surface_width >> 24);
            out[4] = (uint8_t)(dev->surface_height);
            out[5] = (uint8_t)(dev->surface_height >> 8);
            out[6] = (uint8_t)(dev->surface_height >> 16);
            out[7] = (uint8_t)(dev->surface_height >> 24);
            return 8u;
        case MTZ2_REPORT_REGION_DESC:
            /*
             * THE BYTES THE NORMALISER READS — and, measured rather than
             * argued, NOT the reason every touch lands in the same place.
             * Both halves of that sentence are load-bearing, so both are set
             * out here.
             *
             * 0xc043880c publishes this blob verbatim as OSData under "Sensor
             * Region Descriptor" and READS NO FIELD OF IT. Userspace is the
             * consumer. _mt_CachePropertiesForDevice parses it into a
             * sixteen-entry array of SEVEN-byte records at device+0x74 (the
             * count is hardwired to 16 at 0x33cf7ef8, the destination set at
             * 0x33cf7f00), and _mt_DefineSurfaceGrid hands record 1 —
             * `blob + 7`, stored at 0x33d01dc8 — to _alg_InitRowColXYConvert
             * (0x33d01010) as `desc`. That function builds two 66-entry tables
             * INDEXED FROM MINUS ONE — a real, deliberately computed entry and
             * not an overrun; both loops start at `mvn r5,#0`, at 0x33d01040
             * and 0x33d01088 — and then derives the four bounds every contact
             * is normalised against:
             *
             *     Xmax = grid[0x26] + colTable[desc[5] - 1]   0x33d01124
             *     Xmin = colTable[0] - grid[0x24]             0x33d01130
             *     Ymax = grid[0x2a] + rowTable[desc[2] - 1]   0x33d01104
             *     Ymin = rowTable[0] - grid[0x28]             0x33d01110
             *
             * with colTable[i] = (i - grid[0x3c]) * grid[0x34]*100 / grid[0x38]
             * and rowTable[i] = i * grid[0x2c]*100 / grid[0x30]. For this part
             * 0x33d01154 sets 56 and 11, 36 and 7, and all four margins to
             * 0x4b, and leaves grid[0x3c] zero — so the tables are i*5600/11
             * and i*3600/7 and every margin is 75.
             *
             * SO desc[2] IS THE ROW COUNT AND desc[5] THE COLUMN COUNT, and at
             * zero both tables are read at index -1 and BOTH SPANS GO NEGATIVE.
             * The clamp at 0x33d00f2c then returns the maximum for every
             * coordinate on every frame, which is the pinned normalised 1.0
             * run98 measured at plugin+0x2114 across a 208-pixel drag.
             *
             * ALL OF THAT IS NOW READ OUT OF A GUEST rather than inferred.
             * run96's snapshot at instruction 3.5e9 puts the device object at
             * VA 0x007e9000 and its grid at +0x168:
             *
             *     grid+0x148 Xmax -434     grid+0x14a Xmin  -75
             *     grid+0x14c Ymax -439     grid+0x14e Ymin  -75
             *     grid+0x150 -509 = colTable[-1]
             *     grid+0x154 -514 = rowTable[-1]
             *
             * AND THE LEVER IS NOT IN THIS REPORT. The same snapshot has desc
             * holding `01 00 00 01 00 00 00`, which is not this device's answer
             * at all — it is the exact fingerprint of the BACK-FILL at
             * 0x33d01dd0, which fires when `blob[7] == 0` and writes desc[0]=1,
             * desc[3]=1, desc[2]=(byte)rows and desc[5]=(byte)cols from its own
             * arguments. Those arguments are device+0x20 and device+0x24, which
             * are the "Sensor Rows" and "Sensor Columns" IORegistry properties
             * (fetched at 0x33cf7d48 and 0x33cf7d64), and in that snapshot both
             * are ZERO — as are "Family ID" and "bcdVersion", while "Sensor
             * Surface Width" and "Sensor Surface Height" hold 5000 and 7500,
             * which are not this device's 4800 and 7200 but MultitouchSupport's
             * OWN FALLBACKS for a property it could not read (0x33cf8098 and
             * 0x33cf80a0). Every int32 property this model publishes through
             * reports 0xD1 and 0xD3 is missing from the registry entry
             * userspace queries. THAT is the fault, it is upstream of this
             * report, and it cannot be repaired from here.
             *
             * WHY THESE BYTES ARE SET ANYWAY, stated as justification and not
             * as a result: the back-fill only runs when desc[0] is zero. A part
             * that answers with a real descriptor suppresses it and supplies
             * the counts itself, which is what silicon does, and it makes this
             * model's geometry independent of a property path that is at this
             * moment broken. The record is written in EXACTLY the shape the
             * back-fill would have produced with correct arguments, because
             * that is the only descriptor layout anything in this system is on
             * record as accepting; inventing a different one would be a guess
             * standing where a measurement is available. It has not yet been
             * shown to change what a guest does — while the property path is
             * broken this blob does not reach userspace either.
             *
             * NOT ESTABLISHED: the remaining bytes of a record; what records 2
             * and 3 are for, though grid+0x10 and grid+0x0c point at them; and
             * whether [2] and [5] are electrode counts or table indices that
             * merely coincide with them on a 10x15 panel. desc[5] at blob
             * offset 12 is what makes this reply fourteen bytes and so forces
             * the 0xE7 long control read.
             */
            out[MTZ2_REGION_RECORD + 0u] = 1u;
            out[MTZ2_REGION_RECORD + 2u] = dev->rows;
            out[MTZ2_REGION_RECORD + 3u] = 1u;
            out[MTZ2_REGION_RECORD + 5u] = dev->columns;
            return MTZ2_REGION_RECORD * MTZ2_REGION_RECORDS;
        case MTZ2_REPORT_REGION_PARAM:
            /*
             * 0xc0438874 publishes this one as OSData under "Sensor Region
             * Param" and, like the descriptor, reads no field of it. Unlike the
             * descriptor, no userspace consumer of it has been found either, so
             * eight zero bytes stays: it is a well-formed answer of the right
             * shape, and inventing contents for a blob whose reader is unknown
             * is the mistake this file has already paid for once.
             */
            return 8u;
        default:
            /* A report this device does not have. getReportInfo answers with a
             * length of zero, which is what a real device does for an
             * unimplemented id, and the driver moves on. */
            return 0u;
    }
#endif
}

/* ========================================================== packet framing === */

/* The wire length L of the pending report: payload plus its two checksum
 * bytes, and zero when nothing is pending. This is the number the length read
 * answers with and the number the data read's transfer size is derived from. */
static unsigned wire_len(const s5l_mtz2_t *dev) {
    return dev->frame_len ? (unsigned)dev->frame_len + 2u : 0u;
}

/* How many bytes the packet starting with `op` occupies. Zero means "not an
 * opcode this device answers". */
/* Record one register access. Bounded: the ring stops at its capacity rather
 * than wrapping, because the FIRST accesses of a bootload are the ones that
 * decide it and a driver that loops would otherwise overwrite them with the
 * third identical cycle. See reg_log_* in soc.h. */
static void mtz2_reg_log(s5l_mtz2_t *dev, uint32_t addr, uint32_t val,
                         bool is_write) {
    const unsigned cap = (unsigned)(sizeof dev->reg_log_addr /
                                    sizeof dev->reg_log_addr[0]);
    if (dev->reg_log_n >= cap) return;
    dev->reg_log_addr[dev->reg_log_n]  = addr;
    dev->reg_log_val[dev->reg_log_n]   = val;
    dev->reg_log_write[dev->reg_log_n] = is_write ? 1u : 0u;
    dev->reg_log_n++;
}

static unsigned frame_len(const s5l_mtz2_t *dev, uint8_t op) {
    switch (op) {
        case MTZ2_OP_CMD_STATUS:
        case MTZ2_OP_DEVICE_INFO:
        case MTZ2_OP_REPORT_INFO:
        case MTZ2_OP_WRITE_SHORT:
        case MTZ2_OP_WRITE_LONG:
        case MTZ2_OP_READ_SHORT:
        case MTZ2_OP_READ_LONG:
        case MTZ2_OP_WAKE:
            return MTZ2_FRAME_LEN;
        case MTZ2_OP_REQ_WAKEUP:
            return 2u;   /* {0x19, 0xC1} */
        case MTZ2_OP_HBPP:
            /*
             * The HBPP loopback probe, framed as a packet like any other
             * command rather than handled by putting the whole device into a
             * loopback mode. That distinction matters twice over. It means a
             * probe cannot swallow a following command, and it means the
             * ordinary protocol keeps working while the device is in HBPP — so
             * a loopback mode would have echoed every GET_REPORT_INFO frame
             * back at the driver.
             *
             * It also must be framed rather than skipped a byte at a time: the
             * probe pattern `1A A1 18 E1 ...` CONTAINS 0xE1, so a device that
             * treated 0x1A as an unknown byte would reach the 0xE1 at offset 3
             * and start parsing the rest of the probe as a GET_CMD_STATUS
             * frame, ending twelve bytes into a frame that never completes.
             *
             * BUT THE LENGTH IS NOT SIXTEEN ANY MORE, and it cannot be read off
             * the bytes. `1A A1` alone is the acknowledgement after a DATA
             * packet or a calibration request; `1A A1 18 E1 18 E1 18 E1` is the
             * one after a register read; the sixteen-octet form is the probe.
             * The middle one is a strict PREFIX of the last, so only the
             * command that came before separates them — see `atn_len`, which
             * the previous packet set, and docs/multitouch.md §6.8.
             */
            return dev->atn_len ? dev->atn_len : MTZ2_ATN_PROBE;
        case MTZ2_OP_HBPP_IDLE:
            /* 18 E1. See soc.h: without this the framer eats the 0x18 as an
             * unknown octet and reads the 0xE1 as GET_CMD_STATUS, whose
             * sixteen octets swallow the DATA header that follows. */
            return 2u;
        case MTZ2_OP_HBPP_CALIB:
            return 2u;   /* 1F 01 — "requesting calibration", 0xc0445750     */
        case MTZ2_OP_HBPP_RDREG:
            return 8u;   /* 1C 73 <addr:4> <sum:2>, 0xc0440adc              */
        case MTZ2_OP_HBPP_EXEC:
            return 12u;  /* 1D 53 …, `mov r4,#0xc` at 0xc04449a8            */
        case MTZ2_OP_HBPP_WRREG:
            return MTZ2_FRAME_LEN;   /* 1E 33 <addr><mask><val><sum>        */
        case MTZ2_OP_HBPP_DATA:
            /*
             * 14 + 4W, where W is the big-endian word count at [2..3]. Not
             * known until [3] arrives, so the framer starts this packet at its
             * minimum and lengthens it there, exactly as a frame read is
             * lengthened when tx[2] lands. Ten is the header alone: a
             * zero-length DATA packet is still a complete, well-formed one.
             */
            (void)dev;
            return 14u;
        case MTZ2_OP_FRAME_Z1:
        case MTZ2_OP_FRAME_Z2:
            /*
             * A frame read has TWO lengths and the byte that chooses between
             * them — tx[2] — has not arrived yet. Start as the LENGTH read,
             * which is always sixteen bytes (0xc0442440 loads `r4 = 0x10` and
             * never changes it), and let the framer lengthen the packet to
             * L + 5 when tx[2] turns out to be 1. Starting the other way round
             * is not possible: L is not known to the host until the length read
             * has answered, so the sixteen-byte form is the one a device with
             * no context must assume.
             */
            (void)dev;
            return MTZ2_FRAME_LEN;
        default:
            return 0u;
    }
}

/*
 * How many payload bytes fit in the control-read frame currently being framed:
 * everything between the prologue at rx[3] and the two checksum bytes the
 * frame ends with.
 *
 * ONE RULE FOR BOTH FORMS, and that is a fact about the driver rather than a
 * convenience. The short form's frame is a fixed sixteen bytes, so this is
 * 16 - 3 - 2 = 11 — which is exactly MTZ2_PAYLOAD_MAX and exactly the
 * `cmp r1,#0xb` cut at 0xc0442e10. The long form's frame is length + 5, so it
 * is `length`. The 0xE6 form IS the 0xE7 form evaluated at eleven, so a second
 * composer would only be able to disagree with this one.
 */
static unsigned payload_room(const s5l_mtz2_t *dev) {
    unsigned len = dev->len;
    return len > MTZ2_PAYLOAD_AT + 2u ? len - MTZ2_PAYLOAD_AT - 2u : 0u;
}

/* Finish a reply: zero the tail and stamp the little-endian checksum. */
static void seal(s5l_mtz2_t *dev) {
    if (dev->len < 3u) return;
    uint16_t sum = s5l_mtz2_sum16(dev->rsp, (unsigned)(dev->len - 2u));
    dev->rsp[dev->len - 2u] = (uint8_t)(sum & 0xffu);
    dev->rsp[dev->len - 1u] = (uint8_t)(sum >> 8);
}

/*
 * Build the reply once the parameter byte has arrived.
 *
 * Called from byte 1, which is the last moment at which every byte the reply
 * needs is already in hand and the first byte that depends on it — rx[2] — has
 * not yet been driven.
 */
static void compose(s5l_mtz2_t *dev) {
    uint8_t body[MTZ2_REPORT_MAX];
    unsigned n;
    memset(dev->rsp, 0, sizeof dev->rsp);

    if (dev->op == MTZ2_OP_HBPP) {
        /*
         * `1A A1` is three different transactions and only its LENGTH — which
         * the framer already settled from the command before it — says which.
         *
         * THE PROBE, sixteen octets. In HBPP the answer is not built here at
         * all: it is the incoming byte returned as it arrives by
         * mtz2_transfer(), because a loopback has no buffer. Once programmed
         * the declining answer is sixteen zeros, opcode byte included and
         * deliberately NOT an echo — the driver tests BE16(rx[0..1]) and
         * BE16(rx[2..3]) against a set containing neither 0x0000 nor 0x1A00,
         * so this is a definite rejection, and it is exactly the sixteen zeros
         * the null device produced in run61.
         */
        if (dev->len == MTZ2_ATN_PROBE) return;

        /*
         * THE ACKNOWLEDGEMENTS, and the one number the bootload turns on.
         * 0xc0445284 compares the halfword against 0x00004bc1 and abandons the
         * send after five tries, so a part that answers anything else cannot
         * be programmed. Assembled big-endian at 0xc0440d5c.
         */
        {
            const uint16_t ack = dev->atn_val ? dev->atn_val
                                              : (uint16_t)MTZ2_HBPP_ATN_OK;
            dev->rsp[0] = (uint8_t)(ack >> 8);
            dev->rsp[1] = (uint8_t)(ack & 0xffu);
        }
        if (dev->len == MTZ2_ATN_MEMREAD) {
            /*
             * A MemRead's value rides at rx[2..5] in the same middle-endian
             * order as every other 32-bit field here — 0xc0440c5c assembles it
             * as (rx[2]<<8 | rx[3]) | (rx[4]<<8 | rx[5]) << 16.
             *
             * Only one register has a value anything reads. performCalibSeq
             * compares 0x10008ffc against 0x5A020028 and, ON A MATCH, skips
             * its four register writes and sets the byte that later routes
             * attemptToBootloadDevice into "*** ERROR: Disabling touch ***".
             * So this must not be that value. Everything else reads zero,
             * which is what an unwritten register on a just-powered part holds.
             */
            uint32_t v = dev->rdreg_addr == MTZ2_HBPP_VERSION_REG
                             ? (uint32_t)MTZ2_HBPP_VERSION : 0u;
            dev->rsp[2] = (uint8_t)(v >> 8);
            dev->rsp[3] = (uint8_t)(v);
            dev->rsp[4] = (uint8_t)(v >> 24);
            dev->rsp[5] = (uint8_t)(v >> 16);
        }
        return;                     /* an acknowledgement carries no sum */
    }

    dev->rsp[0] = dev->op;          /* the driver's rx[0] == tx[0] test */

    switch (dev->op) {
        case MTZ2_OP_HBPP_DATA:
        case MTZ2_OP_HBPP_RDREG:
        case MTZ2_OP_HBPP_WRREG:
        case MTZ2_OP_HBPP_CALIB:
        case MTZ2_OP_HBPP_EXEC:
            /*
             * The host TALKS in these; it does not listen. Every one of them is
             * followed either by an ATN_ACK that carries the answer or, for the
             * execute, by nothing at all — 0xc0444a00 returns success without
             * looking. Driving zeros is therefore the whole of the reply, and
             * inventing a status byte here would be a number no code reads.
             */
            memset(dev->rsp, 0, sizeof dev->rsp);
            return;
        case MTZ2_OP_REPORT_INFO:
            /*
             * 0xc0443440-0xc0443460: rx[2] is stored as the record's first byte
             * and is only ever tested against zero, and rx[3..4] is the
             * report's length as a little-endian 16-bit word. rx[1] is never
             * loaded at all — its meaning is not determinable from the driver,
             * so it stays zero rather than being invented.
             */
            n = s5l_mtz2_report(dev, dev->req[1], body);
            dev->rsp[2] = 0u;
            dev->rsp[3] = (uint8_t)(n & 0xffu);
            dev->rsp[4] = (uint8_t)(n >> 8);
            break;
        case MTZ2_OP_READ_SHORT:
        case MTZ2_OP_READ_LONG: {
            /*
             * The payload begins at rx[3] in BOTH control-read forms and the
             * two bytes before it are never loaded (0xc0441e18 for 0xE7,
             * `add r0,r0,#3`; the short form's is at 0xc04420e0). The only
             * thing that differs is how much room there is, which
             * payload_room() reads off the frame the framer has settled on —
             * so a long read composed before its length has arrived simply
             * writes the eleven bytes a short frame holds and is composed again
             * when tx[4] lands. Nothing here needs to know which form it is.
             */
            unsigned room;
            n = s5l_mtz2_report(dev, dev->req[1], body);
            room = payload_room(dev);
            if (n > room) n = room;
            memcpy(&dev->rsp[MTZ2_PAYLOAD_AT], body, n);
            break;
        }
        case MTZ2_OP_FRAME_Z1:
        case MTZ2_OP_FRAME_Z2: {
            unsigned L = wire_len(dev);
            /*
             * rx[1] is the low byte of L in BOTH forms, and that is what lets
             * one composer serve both. The length read wants LE16 L at
             * rx[1..2] (0xc04425b0: `rx[1] | rx[2] << 8`); the data read leaves
             * rx[1] free, constrained only by the five-byte sum. Driving the
             * same byte either way means position 1 — which goes out on the
             * wire BEFORE tx[2] discloses which read this is — never has to
             * know.
             */
            dev->rsp[1] = (uint8_t)(L & 0xffu);
            if (dev->frame_phase == 1u) {
                /*
                 * THE DATA READ. rx[2..3] is LE16 L again — a different
                 * position from the length read's, which is not a
                 * transcription slip: 0xc0441370 loads rx[3] and rx[2] where
                 * 0xc04425b0 loads rx[2] and rx[1]. rx[4] then carries whatever
                 * makes the first five bytes sum to zero mod 256, which is the
                 * only header check the driver makes (0xc0441330, `sum(rx,5)`
                 * must be 0) — the usual trailing sum16 covers the payload
                 * instead.
                 */
                dev->rsp[2] = (uint8_t)(L & 0xffu);
                dev->rsp[3] = (uint8_t)(L >> 8);
                dev->rsp[4] = (uint8_t)(0u - (uint8_t)(dev->rsp[0] +
                                                       dev->rsp[1] +
                                                       dev->rsp[2] +
                                                       dev->rsp[3]));
                if (dev->frame_len) {
                    uint16_t sum;
                    memcpy(&dev->rsp[5], dev->frame, dev->frame_len);
                    sum = s5l_mtz2_sum16(dev->frame, dev->frame_len);
                    dev->rsp[5u + dev->frame_len]      = (uint8_t)(sum & 0xffu);
                    dev->rsp[5u + dev->frame_len + 1u] = (uint8_t)(sum >> 8);
                }
                return;    /* its own header check byte and payload checksum */
            }
            /*
             * THE LENGTH READ. rx[0] only has to satisfy `(rx[0] & 0xF0) ==
             * 0xE0` (0xc0442558), which the echoed opcode does, and the
             * ordinary trailing sum16 over the first fourteen bytes applies.
             * L == 0 lands here too and is a complete, valid answer: the caller
             * tests it at 0xc04430bc and stops without reading data.
             */
            dev->rsp[2] = (uint8_t)(L >> 8);
            break;
        }
        default:
            /* GET_CMD_STATUS, GET_DEVICE_INFO, the control writes and the wake:
             * an echoed opcode, a zero body meaning "no error", and a valid
             * checksum is the whole of a successful answer. */
            break;
    }
    seal(dev);
}

/* =============================================================== the wire === */

/*
 * The byte this device drives while receiving `out` at position `pos`.
 *
 * An HBPP probe is a LOOPBACK: the answer is the byte arriving at that instant,
 * not a byte prepared earlier, which is exactly why echoing passes the driver's
 * test — the first two words it transmits, 0x1AA1 and 0x18E1, are both in the
 * accepted set. Everything else is answered from the composed reply.
 */
static uint8_t drive(const s5l_mtz2_t *dev, uint8_t out, unsigned pos) {
    /*
     * The PROBE loops back; the two ACKNOWLEDGEMENTS do not. All three start
     * `1A A1` and the framer has already decided which this is (see atn_len),
     * so the length is what separates them here — there is nothing in the
     * bytes to separate them by.
     *
     * A part that has been programmed answers the probe from its composed
     * reply instead, which is sixteen zeros: that is exactly the "Could not
     * detect HBPP. Response: 0x00 …" the driver prints, and it is what a real
     * Z2 running application firmware would produce.
     */
    if (dev->op == MTZ2_OP_HBPP && dev->len == MTZ2_ATN_PROBE &&
        dev->hbpp_mode)
        return out;

    /* The idle attention word loops back too: it is the same signalling as the
     * probe, cut to one word, and a device that answered zeros to it would
     * fail the same accept-set test. */
    if (dev->op == MTZ2_OP_HBPP_IDLE && dev->hbpp_mode) return out;
    return pos < S5L_MTZ2_RSP ? dev->rsp[pos] : 0u;
}

/* Is `op` one of the two frame-read opcodes? */
static bool is_frame_op(uint8_t op) {
    return op == MTZ2_OP_FRAME_Z1 || op == MTZ2_OP_FRAME_Z2;
}

static uint8_t mtz2_transfer(void *ctx, uint8_t out) {
    s5l_mtz2_t *dev = ctx;
    if (!dev) return 0u;

    /*
     * Held in reset: drive nothing, remember nothing.
     *
     * This is one physical fact and it is what makes the whole skip work.
     * resetDevice clocks a 16-byte dummy transfer while the line is down —
     * `1A A1 18 E1…`, byte-identical to the real probe, answer discarded — and
     * then releases the line and probes. Swallowing the dummy here is what
     * leaves the one-time claim in `hbpp_answered` for the exchange whose
     * answer is actually read. Note the framer is deliberately NOT advanced:
     * a part in reset has no protocol position either.
     */
    dev->octets++;

    /* The stream itself, once the probes are behind us. See `head` in soc.h:
     * every theory so far has been about where the image ENDS, and none about
     * what is actually in it. */
    if (dev->hbpp_probes >= 2u && !dev->in_reset &&
        dev->head_n < sizeof dev->head)
        dev->head[dev->head_n++] = out;

    if (dev->in_reset) {
        dev->reset_bytes++;
        (void)out;
        return 0u;
    }

    if (dev->len == 0u) {
        /* Between packets: this byte is an opcode or it is nothing. */
        unsigned n = frame_len(dev, out);
        if (!n) {
            /* Not an opcode. Stay idle and answer zero, so the next byte is
             * read as an opcode again rather than as this one's payload. */
            dev->unknown_opcodes++;
            dev->last_unknown_op = out;
            return 0u;
        }
        dev->op  = out;
        dev->len = n;
        dev->pos = 0u;
        dev->packet_octets++;
        /* 0xFF, not 0: tx[2] == 0 is the LENGTH read, so a cleared field would
         * make every non-frame packet look like one. */
        dev->frame_phase = 0xffu;
        memset(dev->req, 0, sizeof dev->req);
        dev->req[0] = out;
        compose(dev);              /* enough for a parameterless opcode */
        dev->pos = 1u;
        /* Byte 0 of a command reply is the opcode itself — the driver's
         * rx[0] == tx[0] test — and byte 0 of an HBPP reply is the loopback. */
        return drive(dev, out, 0u);
    }

    dev->packet_octets++;
    if (dev->pos < S5L_MTZ2_BUF) dev->req[dev->pos] = out;
    /* The parameter byte has landed; rebuild before driving the first byte
     * that can depend on it. */
    if (dev->pos == 1u) compose(dev);
    /*
     * tx[2] has landed, which for a frame read is the byte that says whether
     * this is the length read or the data read. Latch it and lengthen the
     * packet BEFORE driving position 2, which is the first byte whose value
     * differs between the two forms. Any value other than 1 is treated as the
     * length read, because that is the sixteen-byte form and reading sixteen
     * bytes of a shorter packet is the failure that cannot be recovered from.
     */
    if (dev->pos == 2u && is_frame_op(dev->op)) {
        dev->frame_phase = out;
        if (out == 1u) {
            unsigned total = wire_len(dev) + 5u;
            dev->len = total;
            dev->data_reads++;
        } else {
            dev->length_reads++;
        }
        compose(dev);
    }
    /*
     * tx[4] has landed, which completes the LE16 frame length a LONG CONTROL
     * READ carries at tx[3..4], and tx[2] one byte earlier has already said
     * which of its two stages this is. Stage 1 is sixteen bytes like every
     * other command (`mov r5,#0x10` at 0xc0441be8); stage 2 is length + 5
     * (`add r5,r8,#5` at 0xc0441d18), and that is the frame the payload and
     * its checksum have to fit.
     *
     * THE LENGTH COMES FROM THE HOST AND NOT FROM THE REPORT, and that is the
     * one design decision here. A slave clocks out exactly as many bytes as the
     * master clocks in; the master's tx[3..4] is a direct statement of how many
     * that will be, and it is the only number that can keep the framer in step.
     * Deriving it instead from s5l_mtz2_report()'s own length would be the same
     * number in every reachable case — the driver passes back the length this
     * device announced through GET_REPORT_INFO — but a disagreement would
     * desynchronise the bus for the rest of the boot with nothing to read it
     * off. The frame read above has no such byte to follow and therefore no
     * choice; this one does.
     *
     * Position 4 is early enough for every frame the driver can ask for: the
     * short form is not routed here at all, so `length` is at least 12 and the
     * frame at least 17, and even a degenerate length of 0 leaves a 5-byte
     * frame that ends after this byte rather than before it.
     */
    if (dev->pos == 4u && dev->op == MTZ2_OP_READ_LONG && dev->req[2] == 1u) {
        unsigned total = ((unsigned)dev->req[3] |
                          ((unsigned)dev->req[4] << 8)) + 5u;
        /*
         * Longer than this device can drive is unreachable by construction —
         * the host only ever asks for a length s5l_mtz2_report() announced, and
         * MTZ2_REPORT_MAX is a small fraction of the reply buffer. Clamping
         * rather than trusting it keeps the framer inside the invariant
         * mtz2_state_valid() enforces on every snapshot, so a model bug that
         * ever did announce an unservable report shows up as a desynchronised
         * bus and not as a buffer overrun.
         */
        if (total > S5L_MTZ2_RSP) total = S5L_MTZ2_RSP;
        dev->len = total;
        compose(dev);
    }
    /*
     * tx[3] completes the DATA packet's word count at [2..3], big-endian, and
     * that is the only thing on the wire that says how long this packet runs:
     * 14 + 4W, ten octets of header plus the payload plus a four-octet sum.
     *
     * The payload itself is neither stored nor checked. This model is not a
     * Z2 core and cannot execute what it is handed; what it owes the driver is
     * to CONSUME exactly the right number of octets so the next packet starts
     * where the host thinks it does. A desynchronised bus here would look like
     * a corrupted firmware image, which is the least diagnosable failure this
     * device has.
     */
    if (dev->pos == 3u && dev->op == MTZ2_OP_HBPP_DATA) {
        unsigned words = ((unsigned)dev->req[2] << 8) | (unsigned)out;
        dev->len = 14u + 4u * words;
    }

    uint8_t v = drive(dev, out, dev->pos);
    dev->pos++;
    if (dev->pos >= dev->len) {
        if (dev->pkt_n < sizeof dev->pkt_op / sizeof dev->pkt_op[0]) {
            dev->pkt_op[dev->pkt_n]  = dev->op;
            dev->pkt_len[dev->pkt_n] = dev->len;
            dev->pkt_n++;
        }
        dev->packets++;
        /*
         * End of packet: settle what the NEXT `1A A1` will mean, and whether
         * this part is still a bootloader.
         *
         * Every command that is followed by an acknowledgement says which kind
         * it expects; everything else leaves the probe. This is the one place
         * the ambiguity of §6.8 is resolved, and it is resolved by ordering
         * rather than by content because the content cannot do it.
         */
        switch (dev->op) {
            case MTZ2_OP_HBPP:
                if (dev->len == MTZ2_ATN_PROBE) dev->hbpp_probes++;
                else                            dev->hbpp_atn_acks++;
                dev->atn_len = MTZ2_ATN_PROBE;
                dev->atn_val = MTZ2_HBPP_ATN_OK;
                break;
            case MTZ2_OP_HBPP_DATA:
                dev->hbpp_data_packets++;
                dev->hbpp_data_bytes += dev->len > 14u ? dev->len - 14u : 0u;
                dev->atn_len = MTZ2_ATN_SHORT;
                dev->atn_val = MTZ2_HBPP_ATN_OK;
                break;
            case MTZ2_OP_HBPP_RDREG:
                dev->hbpp_reg_reads++;
                /* The address, middle-endian: 0xc0440a90-0xc0440ab0 writes it
                 * as [a>>8, a, a>>24, a>>16] and every other 32-bit field in
                 * this protocol uses the same order. */
                dev->rdreg_addr = ((uint32_t)dev->req[2] << 8) |
                                  (uint32_t)dev->req[3] |
                                  ((uint32_t)dev->req[4] << 24) |
                                  ((uint32_t)dev->req[5] << 16);
                /* Log the ANSWER, not just the question -- see reg_log_* in
                 * soc.h. This is the same value drive() will hand back. */
                mtz2_reg_log(dev, dev->rdreg_addr,
                             dev->rdreg_addr == MTZ2_HBPP_VERSION_REG
                                 ? (uint32_t)MTZ2_HBPP_VERSION : 0u,
                             false);
                dev->atn_len = MTZ2_ATN_MEMREAD;
                dev->atn_val = MTZ2_HBPP_ATN_OK;
                break;
            case MTZ2_OP_HBPP_WRREG:
                dev->hbpp_reg_writes++;
                /* 1E 33 <addr:4> <mask:4> <val:4> <sum:2>, all middle-endian
                 * like every other 32-bit field in this protocol. */
                mtz2_reg_log(dev,
                             ((uint32_t)dev->req[2] << 8) |
                             (uint32_t)dev->req[3] |
                             ((uint32_t)dev->req[4] << 24) |
                             ((uint32_t)dev->req[5] << 16),
                             ((uint32_t)dev->req[10] << 8) |
                             (uint32_t)dev->req[11] |
                             ((uint32_t)dev->req[12] << 24) |
                             ((uint32_t)dev->req[13] << 16),
                             true);
                /*
                 * A WRITE IS ACKNOWLEDGED LIKE EVERY OTHER COMMAND, and this
                 * was MTZ2_ATN_PROBE until run160 measured what that cost.
                 *
                 * §6.2 is explicit that the sender acknowledges after each
                 * packet, and the packet list shows every other opcode getting
                 * the right length -- `30:...` then `1a:2`, `1c:8` then
                 * `1a:8`. Only `1e:16` was followed by `1a:16`, a sixteen-octet
                 * probe answering the probe pattern, where 0xc0445284 is
                 * comparing against the literal 0x4BC1. So the write reported
                 * failure.
                 *
                 * performCalibSeq checks every write and bails on the first
                 * one that fails -- `cmp r0, #0 / beq 0xc0445810` after each
                 * of the four -- and run160 caught exactly that: one write to
                 * 0x10001c04, never the other three at 0x10001c08, 0x1000300c
                 * and 0x1000304c, then the entire bootload again. Three
                 * identical cycles, so deterministic rather than a race.
                 *
                 * The old value was not arbitrary: the rule above is "every
                 * command that is followed by an acknowledgement says which
                 * kind it expects; everything else leaves the probe", and
                 * WRREG had been classed as expecting none. That classification
                 * was the error, not the rule.
                 */
                dev->atn_len = MTZ2_ATN_SHORT;
                /* ...and a DIFFERENT number. See MTZ2_HBPP_ATN_WROK. */
                dev->atn_val = MTZ2_HBPP_ATN_WROK;
                break;
            case MTZ2_OP_HBPP_CALIB:
                dev->hbpp_calibs++;
                dev->atn_len = MTZ2_ATN_SHORT;
                dev->atn_val = MTZ2_HBPP_ATN_OK;
                break;
            case MTZ2_OP_HBPP_EXEC:
                /*
                 * THE PART IS PROGRAMMED. Its answer to this packet is never
                 * examined — 0xc0444a00 returns 1 unconditionally — so the
                 * arrival is the event, and from here the probe answers no,
                 * which is what lets the driver stop treating this as a
                 * bootloader and start interrogating it.
                 */
                dev->hbpp_execs++;
                dev->hbpp_mode = false;
                dev->atn_len = MTZ2_ATN_PROBE;
                dev->atn_val = MTZ2_HBPP_ATN_OK;
                break;
            default:
                dev->atn_len = MTZ2_ATN_PROBE;
                dev->atn_val = MTZ2_HBPP_ATN_OK;
                break;
        }
        /*
         * A COMPLETED data read is what drops the attention line. Not the
         * length read, which is only an enquiry, and not the first byte of the
         * data read either: a report the host began clocking out and abandoned
         * — the select moved, the part was reset — is still pending, and a
         * device that forgot it here would lose the finger-down half of a tap
         * with no trace. The line is a LEVEL and gpioic.c re-latches it every
         * refresh, so holding it up until the last byte is clocked costs at
         * most one extra interrupt in which the guest finds L == 0.
         */
        if (is_frame_op(dev->op) && dev->frame_phase == 1u && dev->frame_len) {
            dev->frames_read++;
            dev->frame_len = 0u;
            dev->contacts  = 0u;
            dev->atn       = false;
            memset(dev->frame, 0, sizeof dev->frame);
        }
        dev->pos = 0u;
        dev->len = 0u;
        dev->frame_phase = 0xffu;
    }
    return v;
}

/* ============================================================== lifecycle === */

/*
 * What a loss of LDO power erases.
 *
 * The Z2 is flashless: the twelve-octet EXEC packet leaves HBPP only because
 * the guest has just downloaded the application image into volatile memory.
 * Removing power loses that image, so the next powered session starts in
 * HBPP again.  This is deliberately narrower than s5l_mtz2_reset(): geometry
 * describes the physical panel, and the bounded counters/logs are evidence
 * across a sleep/wake cycle rather than device state to throw away.
 *
 * `in_reset` is also left alone.  It is the independent level of the active-
 * low reset pin and the guest keeps it asserted across the LDO transition.
 */
static void mtz2_power_on_reset(s5l_mtz2_t *dev) {
    dev->hbpp_mode   = true;
    dev->atn_len     = MTZ2_ATN_PROBE;
    dev->atn_val     = MTZ2_HBPP_ATN_OK;
    dev->rdreg_addr  = 0u;
    dev->pos         = 0u;
    dev->len         = 0u;
    dev->op          = 0u;
    dev->frame_phase = 0xffu;
    memset(dev->req, 0, sizeof dev->req);
    memset(dev->rsp, 0, sizeof dev->rsp);

    dev->atn      = false;
    dev->contacts = 0u;
    dev->frame_len = 0u;
    dev->frame_seq = 0u;
    dev->frame_ms  = 0u;
    memset(dev->frame, 0, sizeof dev->frame);

    /* Do not make the transaction ledger span a power boundary. */
    dev->txn_mark = dev->octets;
}

void s5l_mtz2_reset(s5l_mtz2_t *dev) {
    if (!dev) return;
    memset(dev, 0, sizeof *dev);

    /*
     * Power-on state. `hbpp_mode` true is the honest one: a Z2 has no flash,
     * so a part that has just been powered IS its bootloader, and it stays one
     * until the twelve-octet execute packet arrives. That is what gives
     * finishStarting() the yes it detaches without, and gives
     * attemptToBootloadDevice() the yes it needs to actually push firmware --
     * which the old one-time claim could not do for both.
     *
     * `in_reset` true is not a stylistic choice: the GPIO pin block powers up
     * all-zero and MTZ2_PIN_RESET is active low, so a part on this board is
     * genuinely held down until the driver releases it. It also matters that
     * the model start here rather than at "released", because the guest's
     * first store to this pin writes level 0 — no change, so no watch callback
     * — and only the later release produces an edge.
     */
    dev->in_reset = true;
    mtz2_power_on_reset(dev);

    /*
     * THE GEOMETRY. None of this is in the device tree, so all of it is our
     * choice, and the choice is made to keep a reported coordinate and a screen
     * pixel in an exact, distortion-free relationship — which is what decides
     * whether a tap lands on the icon under the finger.
     *
     * Surface 4800 x 7200, in hundredths of a millimetre: 48.00 mm by
     * 72.00 mm. Three properties earn it:
     *
     *   - It is EXACTLY 15 times the panel's 320 x 480 in BOTH axes. The two
     *     scale factors are identical, so no aspect distortion is
     *     representable; and 15 is a whole number, so a coordinate in these
     *     units converts to a pixel by an exact integer divide with no
     *     rounding rule to get wrong.
     *   - 15 units per point is 1/15 of a pixel of headroom, so when a later
     *     step reports sub-pixel contact positions nothing here has to change.
     *   - It is within 2.5% of the real iPhone 3G active area (about 49.2 mm by
     *     73.8 mm), so anything that treats these as physical dimensions — a
     *     gesture recogniser converting to millimetres per second, say — gets a
     *     sane answer rather than an absurd one. A pair like 32000 x 48000
     *     would satisfy the first two points and fail this one by claiming a
     *     320 mm wide phone.
     *
     * Sensor 10 columns by 15 rows: the same 2:3 ratio as the surface and the
     * panel, and a ~4.8 mm electrode pitch, which is the right order for this
     * generation of part.
     *
     * TWO OF THOSE THREE JUSTIFICATIONS ARE NOW KNOWN NOT TO APPLY TO WHERE A
     * TAP LANDS, and saying so is worth more than quietly rewriting them.
     * run98 established that the normaliser every contact passes through takes
     * its bounds from the Sensor Region Descriptor's electrode counts and not
     * from this width and height at all — see the bounds block above
     * to_surface(). So the exact-integer-scale and equal-scale arguments buy
     * nothing for tap accuracy; what they still buy is a physically sane
     * "Sensor Surface Width" property for anything that reads report 0xD9 as
     * millimetres, which is the third argument and the one that survives.
     *
     * The ROW AND COLUMN COUNTS, by contrast, turned out to be load-bearing in
     * a way nothing here anticipated: they are the two bytes the descriptor
     * carries, so they set the bounds, so they decide where every touch lands.
     * They keep the values they already had — this is not a number chosen to
     * make an answer come out.
     */
    dev->surface_width  = 4800u;
    dev->surface_height = 7200u;
    dev->columns        = 10u;
    dev->rows           = 15u;

    /*
     * bcdVersion 1.00. Read big-endian out of the report (see
     * s5l_mtz2_report), published as a number and not compared against
     * anything on this boot path.
     */
    dev->bcd_version = 0x0100u;
    /*
     * Endianness 1. The driver caches this byte at this+0x1a0 and hands it to
     * the frame parser, so it selects the byte order of a payload format this
     * model does not yet generate. 1 is the value that selects the
     * little-endian layout an ARM device would produce; the step that reverses
     * _MT_ParsedMultitouchFrameRepCreate is the step that can confirm it, and
     * it is called out here so that it is checked rather than inherited.
     */
    dev->endianness = 1u;
    /*
     * Family ID 1. Arbitrary and stated as arbitrary: nothing on the boot path
     * we can read compares it, the driver only republishes it as a property,
     * and no source we have gives the real n82 value. Zero was avoided because
     * a zero is indistinguishable from an unanswered report.
     */
    dev->family_id = 1u;
    /* No buttons on the digitizer. The home and lock buttons are elsewhere. */
    dev->buttons = 0u;
}

void s5l_mtz2_bind(s5l_mtz2_t *dev, s5l_spi_slave_t *slave) {
    if (!slave) return;
    memset(slave, 0, sizeof *slave);
    if (!dev) return;
    slave->ctx = dev;
    slave->transfer = mtz2_transfer;
}

bool s5l_mtz2_irq(const s5l_mtz2_t *dev) {
    /* The attention line: asserted exactly while a queued report is waiting to
     * be clocked out. A device with no pending contact does not ask to be
     * read, and one whose report the host has already taken stops asking. */
    return dev && dev->atn;
}

void s5l_mtz2_reset_pin(void *ctx, bool level) {
    s5l_mtz2_t *dev = ctx;
    if (!dev) return;
    /*
     * MTZ2_PIN_RESET, group 6 bit 6, ACTIVE LOW — so `level == false` holds the
     * part down. Measured, not assumed: fsel write 0x0006060e (level 0) at
     * instruction 220,635,069 is the guest's "Asserting reset line", and
     * 0x0006060f (level 1) at 309,541,162 is its "Deasserting reset line",
     * with the dummy transfer between them and the probe after.
     *
     * THIS FUNCTION MUST NOT TOUCH `hbpp_answered`, AND THAT IS THE FIX.
     * An earlier revision set it back to "in HBPP" here, reasoning that a part
     * with no firmware resets into its bootloader — which is true, and is
     * exactly why it breaks. The guest pulses this line before EVERY probe
     * site, so re-arming the claim makes the bootload site indistinguishable
     * from the first one, the bootload probe answers affirmatively, and the
     * driver starts pushing 54,156 bytes of firmware. run65 watched it do
     * precisely that. A reset restores the part's state; it does not un-ask a
     * question the host has already had answered.
     *
     * The part does lose its protocol position: a packet half-received when
     * the line drops did not survive.
     */
    dev->in_reset = !level;
    dev->pos = 0u;
    dev->len = 0u;
    dev->frame_phase = 0xffu;
    dev->resets++;
    /*
     * A queued report does NOT survive a reset, and that is the physical
     * answer rather than the convenient one: the part's scan buffer is what
     * holds it, and a part in reset has cleared it. Keeping it would also let a
     * report queued before bring-up arrive after it, dated to a moment the
     * guest has no way to place. The attention line drops with it.
     */
    if (dev->in_reset) {
        dev->frame_len = 0u;
        dev->contacts  = 0u;
        dev->atn       = false;
        memset(dev->frame, 0, sizeof dev->frame);
    }
}

void s5l_mtz2_select_pin(void *ctx, bool level) {
    s5l_mtz2_t *dev = ctx;
    if (!dev) return;
    (void)level;
    dev->select_edges++;
    /* The transaction that just ended, measured rather than assumed. See the
     * txn_octets note in soc.h for why the shape decides the bootload fix. */
    if (dev->txn_n < sizeof dev->txn_octets / sizeof dev->txn_octets[0]) {
        uint64_t n = dev->octets - dev->txn_mark;
        dev->txn_octets[dev->txn_n++] = n > 0xffffffffu ? 0xffffffffu
                                                        : (uint32_t)n;
    }
    dev->txn_mark = dev->octets;

    /*
     * Resynchronise the framer — but only when nothing is in flight.
     *
     * This used to reset unconditionally, and the comment justifying it read
     * "a chip-select edge means no transfer is in flight, so discarding a
     * part-received packet at one is always safe". run148's transaction ledger
     * refutes that. The Z2 bootload arrives as `... 16 0 16 8 54148 0`, and
     * those two brackets are ONE HBPP DATA packet cut in half:
     *
     *       8 octets:  18 e1 | 30 01 34 df 00 00
     *                  ^idle   ^six of the ten header octets
     *   54148 octets:  00 00 01 13 | <54140 payload> | <4-octet sum>
     *                  ^rest of addr, then hdrsum
     *
     * 6 + 54148 = 54154 = 14 + 4*0x34df, exactly what the header declares, and
     * 2 + 54154 = 54156, exactly the firmware image. Discarding at the edge
     * threw the header away and left the framer re-syncing onto ARM payload
     * words, whose 0xe condition nibble reads as this protocol's own command
     * opcodes -- docs/multitouch.md §6.9. That is where `e1:16 ea:16 ea:16
     * e2:16 ...` and the bogus `30:17538` in run148's packet list came from.
     *
     * The exemption is deliberately narrower than "any in-flight packet": it
     * is only the HBPP DATA packet, which is the only case measured. A select
     * edge part-way through an ordinary command frame still discards it --
     * test_framing_survives_without_a_chip_select asserts that, we have no
     * evidence the real driver ever does it, and widening the rule to cover a
     * case nothing has exercised would trade a known-good behaviour for a
     * guess. An unknown opcode leaves `len` at zero and is unaffected either
     * way.
     */
    if (dev->hbpp_mode && dev->op == MTZ2_OP_HBPP_DATA &&
        dev->len && dev->pos && dev->pos < dev->len) return;

    dev->pos = 0u;
    dev->len = 0u;
    dev->frame_phase = 0xffu;
    /* A report half-clocked-out is still pending. See the framer's note on why
     * the attention line falls at the LAST byte of a data read and not the
     * first. */
}

void s5l_mtz2_power_pin(void *ctx, bool level) {
    s5l_mtz2_t *dev = ctx;
    if (!dev) return;
    /*
     * MTZ2_PIN_POWER, group 7 bit 1, ACTIVE HIGH.
     *
     * The device tree gives `function-power_ldo {phandle, 'GPIO', 0x0701,
     * 0x00000101}`; the reset line beside it is `{..., 0x0606, 0x00010001}`
     * and /arm-io/spi1's chip select is `{..., 0x1800, 0x00000001}`. The
     * AppleS5L8900X platform-function implementation at 0xc05a459c/0xc05a45d8
     * reads byte 1 as the polarity: one is active-high and zero active-low.
     * The reset property's zero agrees with the independently measured reset
     * sequence, so this is decoded evidence rather than a convenient guess.
     *
     * A LEVEL WRITE WITH NO EDGE IS NOT A RESET. GPIO watchers normally report
     * only edges, but keeping that rule here prevents a repeated write of 1
     * from erasing firmware. A real edge, in either direction, ends the current
     * protocol conversation; after power returns the flashless part is in HBPP
     * and must be downloaded again. The guest holds reset low while power is
     * disabled, so that existing reset gate is what keeps the SPI bus quiet.
     */
    if (dev->power_level == level) return;
    dev->power_level = level;
    dev->power_edges++;
    mtz2_power_on_reset(dev);
}

/* ======================================================== host injection ===
 *
 * THE PAYLOAD FORMAT.
 *
 * The kext parses ONE byte of the payload — 0xc0438a1c tests
 * `payload[0] == 0x50` and diverts that case to a separate 8-byte status
 * decoder at 0xc043d1ac — and otherwise hands the buffer verbatim to
 * `IODataQueue::enqueue(payload, (len + 3) & ~3)`. Everything else is decided
 * by userspace, and the format below was read out of userspace by two
 * independent readers that were given the same question and no access to each
 * other's answer:
 *
 *   A. MultitouchSupport.framework's own parser, inside the shared cache at
 *      load address 0x33cf6000. `_mt_HandleMultitouchFrame` (0x33cfb3ec)
 *      switches on payload[0]; 0xCC and 0xCE go to `_MTProcess_0xCC_Data`
 *      (0x33cfba60).
 *   B. MultitouchHID.plugin, a different binary (VA == file offset), whose
 *      MTSimpleHIDManager consumes what A produces.
 *
 * They agree, independently, on: the dispatch being on payload[0]; the header
 * being counter / button state / contact count at [1] / [2] / [3]; the
 * per-contact stride of THIRTY-TWO for this frame type (`lsl r2, r3, #5` at
 * 0x33cfbbcc); the record beginning id / stage / fingerID / handID(s8); X as a
 * 32-bit value at +4 shifted right by 8 and Y the same at +8; Z total at +0x14;
 * the ellipse major and minor at +0x1c and +0x1e; coordinates in HUNDREDTHS OF
 * A MILLIMETRE, which is the same unit report 0xD9's surface size is already
 * published in; and the eight path stages 0..7 being NotTracking, StartInRange,
 * HoverInRange, MakeTouch, Touching, BreakTouch, LingerInRange, OutOfRange.
 *
 * Reader A additionally verified the byte-order selector, which settles a
 * question this file has carried as an open one since the geometry was chosen:
 * `_mt_SwapInt32DeviceToHost` (0x33cfb8e0) is `cmp r0,#1 / beq done / rev`,
 * with r0 taken from the device's cached `Endianness` property. So ONE means
 * "no swap" and every other value byte-swaps — and zero is NOT "native". The
 * model's `endianness = 1` is right, and it is right for a reason now rather
 * than by inheritance.
 *
 * WHY 0xCC AND NOT 0x44 OR 0x74. The parser accepts four contact-carrying
 * families and the device chooses by writing payload[0]; reader A's V2 (0x44)
 * table is the more capable one but only ONE reader produced it, while the
 * 0xCC record layout is what both readers derived separately. Where a wrong
 * guess costs an 18-minute boot, the doubly-attested shape is worth more than
 * the richer one. 0xCC also needs no negotiated header length and no stride
 * field: its header is a constant ten bytes and its stride a constant 32, so
 * there is no field whose default the two readings could differ about.
 *
 * WHAT REJECTS A FRAME, and it is a short list — there is no magic number and
 * no checksum anywhere in userspace: an unknown payload[0]; a length at or
 * below 9; a length below header + count * stride. Our shortest payload is
 * 10 + 32 = 42 bytes, and the kext rounds the enqueued length UP to a multiple
 * of four, so what userspace measures is 44 and both bounds hold with room.
 */

/* ================================================== the normaliser's bounds ===
 *
 * THE SURFACE A CONTACT IS MEASURED AGAINST IS NOT THE ONE REPORT 0xD9
 * PUBLISHES, and getting that backwards is what run98 measured.
 *
 * _mt_FillMTContactDirectFromBinary (0x33cfdac4) normalises each raw coordinate
 * as `(v - min) / (max - min)`, and the four bounds come from
 * _alg_InitRowColXYConvert (0x33d01010), which builds them out of the Sensor
 * Region Descriptor's electrode counts and never looks at 0xD9's width and
 * height at all. See s5l_mtz2_report()'s MTZ2_REPORT_REGION_DESC case for the
 * derivation and for what about it is not established; the two numbers this
 * device's 10 columns and 15 rows produce are:
 *
 *     x in [-75, 4656]     span 4731     14.784375 units per pixel across 320
 *     y in [-75, 7275]     span 7350     15.3125   units per pixel down   480
 *
 * TWO CONSEQUENCES, and the second is the reason this arithmetic changed rather
 * than only the descriptor:
 *
 *   - The spans are not equal, and neither is 15x the panel. The device's own
 *     two statements about its width — 4800 hundredths of a millimetre in
 *     report 0xD9 and 4731 implied by the descriptor — DISAGREE by 1.5%. That
 *     disagreement is real and is left visible rather than papered over: 0xD9's
 *     value has the exact-integer-scale property this file argues for at
 *     length, no consumer of it has been found on the boot path, and changing
 *     it would be a second unmeasured guess in the same breath as the first.
 *     The ellipse below still scales through 0xD9 for the same reason: it is a
 *     LENGTH in hundredths of a millimetre, which is precisely the quantity
 *     "Sensor Surface Width" names.
 *
 *   - Mapping the panel onto 0..4800 and then normalising against -75..4656
 *     does not merely offset the result, it runs OFF THE END. Pixel 319 came
 *     out at 4792, above an Xmax of 4656, so the clamp at 0x33d00f2c pinned it
 *     — the right five pixels of the screen were unreachable, on top of a ~+6
 *     px offset and a 1.5% stretch everywhere else. Mapping onto the bounds
 *     themselves removes all three at once.
 */
#define MTZ2_SURFACE_MARGIN 75

/*
 * The far edge of each axis, from the same table _alg_InitRowColXYConvert
 * indexes: 5600/11 units per column step and 3600/7 per row step — about
 * 5.09 mm and 5.14 mm of electrode pitch — plus a margin of 75 beyond the
 * outermost electrode, mirrored by the -75 at the near edge.
 *
 * A device with no electrodes on an axis would index the table at -1 and hand
 * the guest the negative span that caused all this. It cannot happen — reset()
 * sets both counts — so the guard here reads as index 0 and yields a small
 * POSITIVE span, which is a degenerate surface rather than an inverted one.
 */
static int32_t mtz2_axis_max(unsigned electrodes, unsigned num, unsigned den) {
    unsigned i = electrodes ? electrodes - 1u : 0u;
    return (int32_t)MTZ2_SURFACE_MARGIN + (int32_t)(i * num / den);
}
static int32_t mtz2_x_max(const s5l_mtz2_t *dev) {
    return mtz2_axis_max(dev->columns, 5600u, 11u);
}
static int32_t mtz2_y_max(const s5l_mtz2_t *dev) {
    return mtz2_axis_max(dev->rows, 3600u, 7u);
}

static int32_t to_surface(uint32_t pixel, uint32_t span_pixels,
                          int32_t lo, int32_t hi, bool flip);
static void put16(uint8_t *p, uint16_t v);
static void put32(uint8_t *p, uint32_t v);

/*
 * A panel pixel placed inside [lo, hi], at the CENTRE of the pixel rather than
 * at its near corner — which is what stops a tap on pixel 0 from reading as
 * "just off the edge" to anything that rounds, and what makes the guest's own
 * conversion land back on the pixel it came from for every one of the 320 and
 * 480. Written as (2*pixel + 1) half-steps so there is ONE divide and therefore
 * one rounding rule to get wrong instead of two.
 *
 * The result is SIGNED and really can be negative — pixel 0 is 7.4 units inside
 * a lower bound of -75, so it lands at -68. The wire field is a 32-bit value
 * the parser arithmetic-shifts right by eight, so a negative coordinate
 * round-trips; a uint16 would have clamped it to zero and quietly moved the
 * left edge of the screen inward.
 *
 * `flip` exists because the two coordinate systems disagree about which way Y
 * runs. MultitouchHID converts a normalised contact to a pixel row with
 * `py = H * (1 - norm.y)`, i.e. it treats device Y as increasing UPWARD — the
 * trackpad convention this whole stack came from — while a panel pixel row
 * increases downward. Reader B read that conversion; reader A read only the
 * normalisation either side of it, so this one step is SINGLY attested and is
 * called out as such. It is also the step a mutation test pins, because a
 * mirrored Y is the failure that would look exactly like "the tap went
 * somewhere else" rather than like a bug.
 */
static int32_t to_surface(uint32_t pixel, uint32_t span_pixels,
                          int32_t lo, int32_t hi, bool flip) {
    uint32_t span, step;
    if (!span_pixels || hi <= lo) return lo;
    span = (uint32_t)(hi - lo);
    step = (2u * pixel + 1u) * span / (2u * span_pixels);
    return flip ? hi - (int32_t)step : lo + (int32_t)step;
}

static void put16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)((v >> 8) & 0xffu);
    p[2] = (uint8_t)((v >> 16) & 0xffu);
    p[3] = (uint8_t)((v >> 24) & 0xffu);
}

unsigned s5l_mtz2_encode(const s5l_mtz2_t *dev, const s5l_mt_contact_t *c,
                         unsigned n, uint8_t seq, uint32_t ms, uint8_t *out) {
    s5l_mt_geom_t geom;
    if (!dev) return 0u;
    (void)mtz2_x_max;
    (void)mtz2_y_max;
    (void)to_surface;
    (void)put16;
    (void)put32;
    geom.family_id = dev->family_id;
    geom.endianness = dev->endianness;
    geom.rows = dev->rows;
    geom.columns = dev->columns;
    geom.bcd_version = dev->bcd_version;
    geom.buttons = dev->buttons;
    geom.surface_width = dev->surface_width;
    geom.surface_height = dev->surface_height;
    return s5l_mt_frame_encode(&geom, c, n, seq, ms, out);
#if 0
    unsigned len;
    uint32_t scale_x;
    if (!dev || !out) return 0u;
    /*
     * A contact count of zero is refused, and that is a decision rather than an
     * oversight. The parser accepts a zero-contact frame — ten bytes clears its
     * `len > 9` bound — but it carries nothing a BreakTouch frame has not
     * already said, and allowing it would make "this device has no report" and
     * "this device reports nothing" two states a caller could confuse. Say a
     * finger left by saying so.
     */
    if (n == 0u || n > MTZ2_CONTACT_MAX) return 0u;
    if (!c) return 0u;
    scale_x = dev->surface_width / S5L_MT_PANEL_W;

    memset(out, 0, MTZ2_PAYLOAD_LIMIT);
    /*
     * THE HEADER, a constant ten bytes for this frame type.
     *
     *  [0]     frame type. 0xCC selects _MTProcess_0xCC_Data. It is also not
     *          0x50, which is the one value the KEXT would divert.
     *  [1]     frame counter, wrapping at 8 bits.
     *  [2]     button state. This digitizer has no buttons; report 0xD7 already
     *          says so with the same zero.
     *  [3]     contact count. A count, not a bitmask.
     *  [4..5]  a 16-bit field the parser copies to rep+0x42 and nothing in
     *          either reader's trace consumes. Zero.
     *  [6..9]  LE32 timestamp in milliseconds, at an UNALIGNED offset — the
     *          parser really does load a word at +6.
     */
    out[0] = MTZ2_FRAME_TYPE;
    out[1] = seq;
    out[2] = 0u;
    out[3] = (uint8_t)n;
    put32(&out[6], ms);
    len = MTZ2_FRAME_HEADER + n * MTZ2_CONTACT_STRIDE;

    for (unsigned i = 0; i < n; i++) {
        uint8_t *r = &out[MTZ2_FRAME_HEADER + i * MTZ2_CONTACT_STRIDE];
        int32_t sx, sy;
        /* 1..11 is the path-identifier range _mt_getPathLifeCycle accepts
         * (0x33cfd5f4); this device never reports more than five fingers, so
         * the tighter bound is the honest one to enforce. */
        if (!c[i].id || c[i].id > MTZ2_CONTACT_MAX) return 0u;
        if (c[i].phase > MTZ2_PHASE_OUT_OF_RANGE) return 0u;
        if (c[i].x >= S5L_MT_PANEL_W || c[i].y >= S5L_MT_PANEL_H) return 0u;
        sx = to_surface(c[i].x, S5L_MT_PANEL_W,
                        -MTZ2_SURFACE_MARGIN, mtz2_x_max(dev), false);
        sy = to_surface(c[i].y, S5L_MT_PANEL_H,
                        -MTZ2_SURFACE_MARGIN, mtz2_y_max(dev), true);

        r[0] = c[i].id;            /* path identifier                        */
        r[1] = c[i].phase;         /* path stage; 0..7, see MTZ2_PHASE_*      */
        r[2] = c[i].id;            /* finger id; one finger per path here    */
        r[3] = 1;                  /* hand id, signed. One hand.             */
        /*
         * X and Y. This family stores them as 32-bit values that the parser
         * arithmetic-shifts right by eight, so the low byte is a fractional
         * part: the coordinate written here is the surface value scaled by
         * 256. The cast to uint32_t before the shift is what makes a NEGATIVE
         * coordinate legal — pixel 0 sits at -68, inside the bounds and below
         * zero — since shifting a negative signed value left is undefined while
         * the two's-complement pattern is exactly what the parser's arithmetic
         * right shift undoes. The velocities beside them are shifted right by
         * NINE and are zero, because this device reports position only and a
         * fabricated velocity would be a number the guest could act on.
         */
        put32(&r[4],  (uint32_t)sx << 8);
        put32(&r[8],  (uint32_t)sy << 8);
        put32(&r[12], 0u);         /* velocity X */
        put32(&r[16], 0u);         /* velocity Y */
        /*
         * Z total, which the parser divides by 256 into a dimensionless
         * amplitude. The caller's 0..255 maps linearly onto 0..1. The only
         * property anything downstream was seen to require is that it be
         * greater than zero while the finger is down, which is why a lift
         * carries zero here and says BreakTouch in the stage byte: two
         * independent statements of the same fact, so a consumer that reads
         * either one agrees.
         */
        put16(&r[20], c[i].pressure);
        /* +0x16 and +0x18: fields both readers place but neither names. Zero
         * is a legal value for each and is what a device with nothing to say
         * about them reports. */
        put16(&r[22], 0u);
        put16(&r[24], 0u);
        /* Orientation, scaled by pi * 2^-15. This model reports circular
         * contacts, so it is zero — a real angle would be invented. */
        put16(&r[26], 0u);
        /*
         * The contact ellipse, in the same hundredths of a millimetre as the
         * position: the parser divides both by 100 to get millimetres. These
         * are LENGTHS and not positions, so they get the bare scale factor and
         * NOT to_surface()'s half-pixel centring — an axis is measured from
         * its own zero.
         */
        put16(&r[28], (uint16_t)(c[i].major * scale_x));
        put16(&r[30], (uint16_t)(c[i].minor * scale_x));
    }
    return len;
#endif
}


bool s5l_mtz2_set_contacts(s5l_mtz2_t *dev, const s5l_mt_contact_t *c,
                           unsigned n) {
    uint8_t payload[MTZ2_PAYLOAD_LIMIT];
    unsigned len;
    if (!dev) return false;

    /*
     * THREE-WAY HONESTY. Each refusal is a different fact about the device and
     * every one of them is checkable; none is defensive padding.
     */
    /* Held in reset: a part driving nothing cannot raise an attention line,
     * and this model already refuses to answer a byte in that state. */
    if (dev->in_reset)               { dev->injects_refused++; return false; }
    /*
     * Still a bootloader. A part that has not been programmed is not running
     * the application firmware that scans the panel, so it has no contact to
     * report -- and the driver would throw the frame away in any case:
     * deviceReadResultData at 0xc0441324 REJECTS a 0xEB frame while this+0x1bc
     * is zero (`ldrb r3,[r4,#0x1bc] / cmp r3,#0 / beq 0xc0441408`), and that
     * byte is set by the affirmative probe the bootload begins with.
     */
    if (dev->hbpp_mode)              { dev->injects_refused++; return false; }
    /*
     * One report at a time. A real Z2 overwrites its scan buffer and drops
     * frames the host was too slow for; doing that here would silently discard
     * the finger-DOWN half of a tap and leave a lift the guest cannot explain.
     * Refusing makes the caller pace itself and leaves the reason visible.
     */
    if (dev->frame_len)              { dev->injects_refused++; return false; }

    len = s5l_mtz2_encode(dev, c, n, (uint8_t)(dev->frame_seq + 1u),
                          dev->frame_ms + MTZ2_FRAME_PERIOD_MS, payload);
    if (!len || len > MTZ2_PAYLOAD_LIMIT) { dev->injects_refused++; return false; }

    memcpy(dev->frame, payload, len);
    dev->frame_len = (uint8_t)len;
    dev->contacts  = (uint8_t)n;
    dev->frame_seq++;
    /* Monotone and never zero: the parser logs "timestamp invalid!" on a zero
     * and "time travel, eh?" on a decrease, and posts notification 0x66 to the
     * driver for either. */
    dev->frame_ms += MTZ2_FRAME_PERIOD_MS;
    dev->frames_queued++;
    /* The attention line is a LEVEL and it goes up here. gpioic.c latches it
     * into group 4 bit 27 on the next refresh; it comes down at the last byte
     * of the data read that carries this report. */
    dev->atn = true;
    return true;
}
