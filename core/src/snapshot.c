/*
 * S5LBox — full machine snapshot / restore.  See core/include/snapshot.h for
 * what this promises and why it exists.
 *
 * ===========================================================================
 * HOW TO ADD A FIELD  (read this before touching a device struct)
 * ===========================================================================
 * Every field of every struct is named EXACTLY ONCE in this file, inside a
 * "visitor" function — snap_cpu(), snap_uart(), snap_timer(), and so on. A
 * visitor does not know whether it is saving, loading, or merely measuring:
 * the F32()/F64()/FB()/FA32()/FBYTES() macros dispatch on io->mode. That is
 * deliberate. It means save and load cannot drift apart, because they are the
 * same list of fields read in the same order.
 *
 * So, to add a field to a device:
 *
 *   1. Add it to the struct in soc.h (or arm.h) as usual.
 *   2. Add ONE line to that struct's visitor below.
 *   3. Update the corresponding number in the STRUCT SIZE GUARDS block and
 *      bump SNAPSHOT_VERSION in snapshot.h.
 *
 * Step 3 is what makes step 2 hard to forget. The guards are _Static_asserts
 * on sizeof() of every struct that is snapshotted. Add a field and the build
 * BREAKS, with a message naming the visitor you have to extend. C gives no way
 * to enumerate a struct's members, so a size guard is the closest thing to
 * "you cannot compile a snapshot that has forgotten a register" — and a
 * forgotten register is a boot that diverges days later.
 *
 * A field that must NOT be snapshotted (a host pointer: m->ram, nor.data,
 * stub.regs, cpu.bus, the bus vtable) is still accounted for: it is listed in
 * the "deliberately not serialised" comment in its visitor. Silence about a
 * field is always a bug.
 *
 * ===========================================================================
 * FILE FORMAT
 * ===========================================================================
 *   header  (40 bytes, little-endian)
 *       0  char     magic[16]        "S5LBox SNAPSHOT"
 *      16  uint32   version          SNAPSHOT_VERSION, exact match required
 *      20  uint32   header_len       40
 *      24  uint64   payload_len
 *      32  uint64   flags            0
 *   payload (payload_len bytes)      a sequence of sections
 *   trailer (8 bytes)
 *       0  uint64   legacy FNV-1a-style hash over the payload bytes
 *
 *   section:  uint32 tag, uint32 zero, uint64 len, then len bytes
 *   sections, in order: GEOM CPU  MACH RAM  NOR  STUB END
 *
 * The checksum is a TRAILER rather than a header field on purpose: it means
 * the whole file can be written in a single forward pass with no seeking back
 * to patch a header, which keeps this portable to any sink (a file, a socket,
 * an iOS document) and keeps the writer trivial enough to be obviously right.
 * The payload length is known in advance because the visitors can be run in a
 * third "count" mode that measures instead of writing.
 *
 * ===========================================================================
 * RAM ENCODING — the tradeoff, stated
 * ===========================================================================
 * Guest RAM is up to 512 MB and a naive dump writes all of it every time. The
 * scheme here is the simplest one that is impossible to get wrong: RAM is cut
 * into 4 KB pages and every page is CLASSIFIED BY READING IT — 0 = all zero
 * (nothing stored), 1 = all bytes equal (one byte stored), 2 = anything else
 * (stored verbatim). There is no dirty-page tracking, no write barrier, no
 * cooperation required from the MMU or the bus, and therefore no way for a
 * missed write to silently corrupt a snapshot. The cost is one linear scan of
 * RAM per save, which is a few hundred milliseconds against a boot measured in
 * minutes.
 *
 * That was a deliberate choice over a dirty-bitmap scheme: a dirty bitmap
 * would make saves cheaper and would have to be maintained by every path that
 * writes guest memory (bus_write, s5l8900_load, DMA models yet to be written).
 * One forgotten path there produces exactly the class of silent divergence
 * this feature exists to prevent, and the saving is on the cheap side of the
 * loop. Simple and correct wins.
 *
 * With a 433 MB RAM disk resident in DRAM the classifier does not save much —
 * the file is roughly the size of the live data, as it must be. With a bare
 * kernel boot (no -r) most of DRAM is still zero and the file is small.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#include "snapshot.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

/* ===========================================================================
 * STRUCT SIZE GUARDS — see "HOW TO ADD A FIELD" above.
 *
 * Only checked on a 64-bit host, because the numbers include the padding the
 * ABI chose and a 32-bit build lays these structs out differently. The guard
 * is a development aid for the machines this is developed on, not a portable
 * assertion about the format; the FORMAT is byte-exact everywhere because
 * every field is serialised with explicit little-endian primitives.
 * ======================================================================== */
#if defined(__SIZEOF_POINTER__) && __SIZEOF_POINTER__ == 8
#define SNAP_SIZE_GUARD(type, bytes, visitor)                                  \
    _Static_assert(sizeof(type) == (bytes),                                    \
        #type " changed size. A snapshot that silently drops one register is " \
        "a boot that diverges days later, so this is a compile error on "      \
        "purpose: add the new field to " visitor "() in core/src/snapshot.c, " \
        "update this number, and bump SNAPSHOT_VERSION in snapshot.h.")

SNAP_SIZE_GUARD(arm_cp15_t,        64,    "snap_cpu");
/* 68112 = 66032 + the data-read and data-write block caches (2 x 64 x 16),
 * their four host-only accounting counters (32), and the padding their
 * 8-byte alignment adds after fetch_priv. Like the fetch cache they hold HOST
 * pointers, so they are NOT in snap_cpu(), snap_cpu() clears them on read,
 * and the bytes on disk are identical to a file written before either existed
 * -- SNAPSHOT_VERSION therefore does not move. Same exception as level_dirty
 * below, and justified the same way. Measured with the compiler's failed size
 * guard and confirmed by the successful guard below, not assumed from source
 * arithmetic; the padding is exactly why. */
SNAP_SIZE_GUARD(arm_cpu_t,         68112,   "snap_cpu");
SNAP_SIZE_GUARD(s5l_uart_t,        8280,  "snap_uart");
SNAP_SIZE_GUARD(s5l_vic_t,         16,    "snap_vic");
SNAP_SIZE_GUARD(s5l_timer_t,       40,    "snap_timer");
SNAP_SIZE_GUARD(s5l_power_t,       24,    "snap_power");
SNAP_SIZE_GUARD(s5l_mbx_t,         8208,  "snap_mbx");
SNAP_SIZE_GUARD(s5l_clcd_window_t, 24,    "snap_clcd");
SNAP_SIZE_GUARD(s5l_clcd_t,        3368,  "snap_clcd");
SNAP_SIZE_GUARD(s5l_tvout_t,       12304, "snap_tvout");
/* I2C/PMU guards are intentionally adjacent to their visitors. These values
 * describe host ABI layout only; the file format remains field-by-field. */
SNAP_SIZE_GUARD(s5l_i2c_t,         320,   "snap_i2c");
SNAP_SIZE_GUARD(s5l_pcf50635_t,    600,   "snap_pmu");
SNAP_SIZE_GUARD(s5l_wm8991_t,      496,   "snap_codec");
SNAP_SIZE_GUARD(s5l_i2s_t,         104,   "snap_i2s");
SNAP_SIZE_GUARD(s5l_spi_t,         240,   "snap_spi");
/* Four register banks, plus what the board is driving and which lines it
 * drives at all -- see the `driven` note in soc.h. */
SNAP_SIZE_GUARD(s5l_gpioic_t,      224,   "snap_gpioic");
/* The pin block is 4 KiB of page plus the host-side watch table, which is not
 * serialised — see snap_gpio(). */
SNAP_SIZE_GUARD(s5l_gpio_t,        4192,  "snap_gpio");
/* One held-button byte and three counters, padded to 8-byte alignment. */
SNAP_SIZE_GUARD(s5l_buttons_t,     32,    "snap_buttons");
SNAP_SIZE_GUARD(s5l_mtz2_t,        1600,  "snap_mtz2");
SNAP_SIZE_GUARD(s5l_usbotg_t,      4,     "snap_usbotg");
/* Eight channels of five registers (160), the four controller-wide words, the
 * access and unknown-offset accounting, and the work/refusal counters. */
SNAP_SIZE_GUARD(s5l_pl080_chan_t,  40,    "snap_pl080");
SNAP_SIZE_GUARD(s5l_pl080_t,       496,   "snap_pl080");
SNAP_SIZE_GUARD(s5l_nor_entry_t,   12,    "snap_nor");
SNAP_SIZE_GUARD(s5l_nor_t,         208,   "snap_nor");
SNAP_SIZE_GUARD(s5l_stub_t,        56,    "snap_stubs");
/* arm_bus_t's optional direct-write, WFI and privileged-SVC hooks plus their
 * contexts are host-owned runtime configuration. They grow the containing
 * machine ABI but are deliberately excluded from MACH for the same reason as
 * every other bus callback; snapshot_load preserves the live machine's hooks
 * and dedicated privileged-SVC context. The byte format therefore does not
 * change. */
/* 43648 = 42888 + the codec (496) + two I2S windows (2 x 104) + the five
 * physical buttons (32) + the GPIO controller's `driven` mask (28).
 *
 * 43768 = 43760 + `level_dirty` and its padding. That one is NOT in snap_mach()
 * and SNAPSHOT_VERSION does not move for it, which is the exception this guard
 * exists to make you justify: it is derived state, snap_apply() SETS it rather
 * than reading it, and the bytes on disk are identical to a v13 file written
 * before it existed. See its comment in soc.h.
 *
 * 44408 = 43768 + the two PL080 DMA controllers (2 x 320). Unlike level_dirty
 * this one IS in snap_mach() and the byte format DOES change, so
 * SNAPSHOT_VERSION moves with it — see the v14 note. */
/* 44528 = 44496 + the two PL080 config-write counters (2 x 16). These ARE in
 * snap_pl080() and the byte format DOES change, so SNAPSHOT_VERSION moves. */
/* 44544 = 44496 + the two PL080 config-write counters (2 x 16) and the three
 * SPI controllers' dma_arms (3 x 8, no padding). Both ARE in snap_mach() and
 * the byte format DOES change, so SNAPSHOT_VERSION moved to 17. Measured with
 * a sizeof probe rather than arithmetic -- the first two guesses were wrong,
 * which is the entire reason this guard is a compile error. */
/* 112576 = 111536 + the CPU's data-read block cache and counters, which this
 * struct contains. Not serialised, byte format unchanged; see the arm_cpu_t
 * note. 120792 adds one host-only pointer for the signed-static decode cache.
 * 121840 adds the CPU's host-only data-write block cache/counters (1040) and
 * the bus's host_ram_write callback (8). 121952 adds the exact-PC pre-step
 * hook, its context, bounded target table, rejection filter and host-only
 * counters (112). 121968 adds the two host-only User-mode interpreter tick
 * batching counters (16). 122032 adds the per-machine host-only MBX work
 * ledger (64). 122088 adds the interactive WFI sleep callback/context, four
 * host-only evidence counters and the transient run-slice yield (56). Like
 * the bus callbacks, snapshot_load preserves this live host policy; none of
 * it belongs to guest state or the stream. The optional active host clock adds
 * another callback/context, seven uint64_t values and transient anchor flag;
 * it follows the same policy. 122192 adds three uint64_t witnesses identifying
 * the last rejected 2D batch inside the already host-only MBX ledger (24).
 * 123312 adds four bounded host-only MBX3D rejection witnesses (4 x 280).
 * They retain transient decoder evidence only and remain outside snap_mach().
 * 123336 adds uart4's two host-peer callbacks and shared context (24), also
 * live host wiring and deliberately outside snap_mach().
 * 125384 adds sixteen bounded accepted-render witnesses (16 x 128) to the
 * same host-only MBX ledger. They classify completed work and retain no guest
 * state, so a restore preserves the destination process's live evidence.
 * 125704 adds eight per-framebuffer target aggregates (8 x 40), likewise
 * host-only, so a short witness ring cannot hide which completed surface later
 * became the active CLCD scanout.
 * 125992 adds the third SPI controller's serialized state. This changes the
 * snapshot payload, so SNAPSHOT_VERSION moves to 34.
 *
 * v35 adds the Z1 controller protocol state, pending frame and geometry after
 * the existing machine touch state. Transaction receive bytes are transient:
 * restore resumes between transactions, while the protocol and queued frame
 * state remain serialized.
 * v36 adds the Z1 input-detection control report state.
 * v37 replaces that one report with the complete Z1 parameter table.
 * v38 adds held-contact stream state to Z1. */
SNAP_SIZE_GUARD(s5l_mtz1_t,        1512,   "snap_mtz1");
/* +8 bytes are host-only clock carry policy/debt, explicitly excluded
 * from snap_mach. Native byte-identity contracts pin the unchanged v39 format. */
SNAP_SIZE_GUARD(s5l8900_t,         127496, "snap_mach");
#endif

/* ---------------------------------------------------------------- the IO --- */

#define TAG(a,b,c,d) (((uint32_t)(a)) | ((uint32_t)(b) << 8) | \
                      ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))
#define TAG_GEOM TAG('G','E','O','M')
#define TAG_CPU  TAG('C','P','U',' ')
#define TAG_MACH TAG('M','A','C','H')
#define TAG_RAM  TAG('R','A','M',' ')
#define TAG_NOR  TAG('N','O','R',' ')
#define TAG_STUB TAG('S','T','U','B')
#define TAG_END  TAG('E','N','D',' ')

#define SNAP_HEADER_LEN 40u
#define SNAP_PAGE       4096u

typedef enum { SN_COUNT = 0, SN_SAVE, SN_LOAD, SN_VALIDATE } sn_mode_t;

typedef struct {
    sn_mode_t mode;
    /* sink for SN_SAVE: exactly one of f / (buf,cap) is used */
    FILE     *f;
    uint8_t  *buf;
    size_t    cap;
    /* source for SN_LOAD: exactly one of f / in */
    const uint8_t *in;
    size_t         in_len;
    uint64_t  pos;          /* bytes produced/consumed so far (all modes)   */
    uint64_t  hash;         /* legacy payload hash over bytes passed through */
    bool      defer_hash;   /* capture owns a frozen copy; finish it in slices */
    snapshot_status_t err;
} sn_io_t;

static bool sn_reading(const sn_io_t *io) {
    return io->mode == SN_LOAD || io->mode == SN_VALIDATE;
}

/* Historical v1/v2 FNV-1a-style hash. The offset below is not the standard
 * FNV-1a-64 basis; changing it would invalidate existing snapshots, so it is
 * deliberately retained and documented as format state. This is a corruption
 * check, not a security primitive. */
#define FNV64_OFFSET 1469598103934665603ull
#define FNV64_PRIME  1099511628211ull

static void sn_hash(sn_io_t *io, const uint8_t *p, size_t n) {
    if (io->defer_hash) return;
    uint64_t h = io->hash;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= FNV64_PRIME; }
    io->hash = h;
}

static void sn_raw(sn_io_t *io, void *p, size_t n) {
    if (io->err != SNAP_OK) return;
    switch (io->mode) {
        case SN_COUNT:
            break;
        case SN_SAVE:
            if (io->f) {
                if (fwrite(p, 1, n, io->f) != n) { io->err = SNAP_ERR_IO; return; }
            } else {
                if (io->pos > io->cap || n > io->cap - (size_t)io->pos) {
                    io->err = SNAP_ERR_NOMEM; return;
                }
                memcpy(io->buf + io->pos, p, n);
            }
            sn_hash(io, p, n);
            break;
        case SN_LOAD:
        case SN_VALIDATE:
            if (io->f) {
                if (fread(p, 1, n, io->f) != n) { io->err = SNAP_ERR_TRUNCATED; return; }
            } else {
                if (io->pos > io->in_len || n > io->in_len - (size_t)io->pos) {
                    io->err = SNAP_ERR_TRUNCATED; return;
                }
                memcpy(p, io->in + io->pos, n);
            }
            sn_hash(io, p, n);
            break;
    }
    io->pos += n;
}

/* Consume bytes without assigning them anywhere. Validation uses this for the
 * large host-owned RAM/NOR/stub buffers so a dry run cannot touch live state. */
static void sn_discard(sn_io_t *io, size_t n) {
    uint8_t scratch[4096];
    while (n && io->err == SNAP_OK) {
        size_t chunk = n < sizeof scratch ? n : sizeof scratch;
        sn_raw(io, scratch, chunk);
        n -= chunk;
    }
}

/* --- little-endian primitives. Every field goes through one of these, so the
 * file is identical on any host regardless of struct padding or byte order. */

static void sn_u8(sn_io_t *io, uint8_t *v) { sn_raw(io, v, 1); }

/* The codec's register file is the only 16-bit state in the machine. It gets a
 * primitive of its own rather than being widened to u32: 128 registers stored
 * four bytes wide would put 256 bytes of guaranteed zero in every checkpoint,
 * and a narrowing conversion on load is exactly the silent-truncation bug the
 * per-width primitives exist to make impossible. */
static void sn_u16(sn_io_t *io, uint16_t *v) {
    uint8_t b[2];
    if (io->mode == SN_SAVE) {
        b[0] = (uint8_t)*v; b[1] = (uint8_t)(*v >> 8);
    }
    sn_raw(io, b, 2);
    if (sn_reading(io) && io->err == SNAP_OK)
        *v = (uint16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8));
}

static void sn_u32(sn_io_t *io, uint32_t *v) {
    uint8_t b[4];
    if (io->mode == SN_SAVE) {
        b[0] = (uint8_t)*v; b[1] = (uint8_t)(*v >> 8);
        b[2] = (uint8_t)(*v >> 16); b[3] = (uint8_t)(*v >> 24);
    }
    sn_raw(io, b, 4);
    if (sn_reading(io) && io->err == SNAP_OK)
        *v = (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
             ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

static void sn_u64(sn_io_t *io, uint64_t *v) {
    uint8_t b[8];
    if (io->mode == SN_SAVE)
        for (unsigned i = 0; i < 8; i++) b[i] = (uint8_t)(*v >> (8 * i));
    sn_raw(io, b, 8);
    if (sn_reading(io) && io->err == SNAP_OK) {
        uint64_t x = 0;
        for (unsigned i = 0; i < 8; i++) x |= (uint64_t)b[i] << (8 * i);
        *v = x;
    }
}

static void sn_bool(sn_io_t *io, bool *v) {
    uint8_t b = (io->mode == SN_SAVE) ? (*v ? 1u : 0u) : 0u;
    sn_u8(io, &b);
    if (sn_reading(io) && io->err == SNAP_OK) {
        if (b > 1u) io->err = SNAP_ERR_CORRUPT;
        else *v = b != 0;
    }
}

static void sn_u32a(sn_io_t *io, uint32_t *a, size_t n) {
    for (size_t i = 0; i < n; i++) sn_u32(io, &a[i]);
}

static void sn_size(sn_io_t *io, size_t *v) {   /* size_t as a fixed u64 */
    uint64_t x = (io->mode == SN_SAVE) ? (uint64_t)*v : 0;
    sn_u64(io, &x);
    if (sn_reading(io) && io->err == SNAP_OK) {
        if (x > (uint64_t)SIZE_MAX) io->err = SNAP_ERR_CORRUPT;
        else *v = (size_t)x;
    }
}

/* The field macros the visitors use. `io` is captured by name deliberately:
 * every visitor takes a parameter called `io`, so a field line is just the
 * field. */
#define F8(x)       sn_u8  (io, &(x))
#define F16(x)      sn_u16 (io, &(x))
#define F32(x)      sn_u32 (io, &(x))
#define F64(x)      sn_u64 (io, &(x))
#define FB(x)       sn_bool(io, &(x))
#define FSZ(x)      sn_size(io, &(x))
#define FA32(a, n)  sn_u32a(io, (a), (n))
#define FBYTES(p,n) sn_raw (io, (p), (n))

/* ===========================================================================
 * THE VISITORS.  One per struct. Every mutable field appears exactly once.
 * ======================================================================== */

static void snap_cp15(sn_io_t *io, arm_cp15_t *p) {
    F32(p->sctlr);      F32(p->actlr);      F32(p->cpacr);
    F32(p->ttbr0);      F32(p->ttbr1);      F32(p->ttbcr);
    F32(p->dacr);
    F32(p->dfsr);       F32(p->ifsr);
    F32(p->dfar);       F32(p->ifar);
    F32(p->fcse_pid);   F32(p->context_id);
    F32(p->tpidrurw);   F32(p->tpidruro);   F32(p->tpidrprw);
}

/*
 * The CPU. This list is the reset path in arm_interp.c (arm_reset) read field
 * by field; if a future register is added there it must be added here, and the
 * size guard above will insist.
 *
 * Deliberately NOT serialised: `bus`, a host pointer into the machine struct.
 * snapshot_load re-points it at the live machine's bus, which is what lets a
 * tool wrap the bus callbacks and still restore underneath the wrapper.
 */
static void snap_cpu(sn_io_t *io, arm_cpu_t *c) {
    FA32(c->r, 16);
    F32(c->cpsr);
    snap_cp15(io, &c->cp15);
    FA32(c->spsr,      ARM_BANK_COUNT);
    FA32(c->bank_r13,  ARM_BANK_COUNT);
    FA32(c->bank_r14,  ARM_BANK_COUNT);
    FA32(c->fiq_r8_12, 5);
    FA32(c->usr_r8_12, 5);
    F64(c->cycles);
    FB (c->abort_pending);
    F32(c->abort_fsr);
    F32(c->abort_far);
    FB (c->irq_line);
    FB (c->fiq_line);
    FB (c->excl_valid);
    F32(c->excl_addr);
    F32(c->vfp_fpexc);
    F32(c->vfp_fpscr);
    /* s0-s31. d0-d15 alias these and so need no separate entry — that is the
     * point of storing the file once (see arm_cpu_t.vfp_s). */
    FA32(c->vfp_s, 32);
    /*
     * THE TLB IS DELIBERATELY NOT STORED, and the size guard above is what
     * forces anyone adding to arm_cpu_t to read this rather than assume it.
     *
     * It is a cache whose every entry is derivable from the page tables in
     * guest RAM, which the snapshot DOES carry. Restoring it empty therefore
     * loses nothing: the first access to each page walks and refills, and a
     * walk and a hit return the same physical address and the same fault code
     * by construction. Storing it would add 12 KB per snapshot to save work
     * that costs microseconds to redo.
     *
     * Clearing on restore is also the SAFE direction. A stored TLB could
     * outlive the tables it was derived from if a snapshot were ever restored
     * onto edited memory, and a stale translation is a wild store that
     * surfaces nowhere near its cause. An empty one cannot be wrong.
     *
     * The counters travel, because they are measurements of the run rather
     * than state of the machine, and a restored run that reported zero hits
     * would misdescribe what it did.
     */
    if (sn_reading(io)) {
        memset(c->tlb, 0, sizeof c->tlb);
        /* And generation 1, for the same reason arm_reset does: an entry at
         * generation 0 in a table whose counter is also 0 is a false hit. */
        c->tlb_gen = 1u;
    }
    F64(c->tlb_hits); F64(c->tlb_misses); F64(c->tlb_flushes);
    /*
     * THE FETCH-BLOCK CACHE IS NOT SERIALISED, deliberately, and this is the
     * one field in the CPU where writing it out would be actively unsafe:
     * fetch_host is a HOST address. Restored into another process -- or the
     * same process after the RAM allocation moved -- it would point at
     * whatever now lives there, and the interpreter would execute it as guest
     * instructions with no fault and no diagnostic.
     *
     * So nothing is written and the fields are cleared on read. The stream's
     * bytes are unchanged; a restored machine simply refills the cache from
     * its own RAM on its first fetch. See the fetch_* block in arm.h.
     */
    if (sn_reading(io)) {
        c->fetch_host = NULL;
        c->fetch_blk  = 0u;
        c->fetch_gen  = 0u;
        c->fetch_priv = false;
        /* The data block caches hold host addresses for the same reason and
         * are unsafe to carry across a restore for the same reason. */
        memset(c->dread, 0, sizeof c->dread);
        memset(c->dwrite, 0, sizeof c->dwrite);
    }
}

/* The UART's whole transmit capture is saved, not just its used prefix: the
 * buffer is 8 KB, the simplicity is worth more than the bytes, and a restored
 * machine then compares byte-identical to the original including the slack. */
static void snap_uart(sn_io_t *io, s5l_uart_t *u) {
    F32(u->ulcon); F32(u->ucon); F32(u->ufcon); F32(u->umcon); F32(u->ubrdiv);
    FBYTES(u->tx, UART_TX_BUFFER);
    FSZ(u->tx_len);
    /*
     * The receive FIFO travels too, and it is not optional bookkeeping: a
     * checkpoint taken mid-negotiation holds bytes the host peer has already
     * transmitted and will never transmit again — its restart timer counts them
     * as delivered. A restore that dropped them would resume a link that stalls
     * for one restart interval and then renegotiates, which is indistinguishable
     * from a peer bug. Whole array, not just the used part, for the same reason
     * the transmit capture is whole: a restored machine then compares
     * byte-identical to the original including the slack.
     */
    FBYTES(u->rx, UART_RX_FIFO);
    F8(u->rx_head); F8(u->rx_count);
    F64(u->rx_pushed); F64(u->rx_dropped);
    F64(u->rx_reads);  F64(u->rx_underruns);
    if (sn_reading(io) && io->err == SNAP_OK &&
        (u->tx_len >= (size_t)UART_TX_BUFFER ||
         u->rx_count > UART_RX_FIFO || u->rx_head >= UART_RX_FIFO))
        io->err = SNAP_ERR_CORRUPT;     /* the writer keeps a NUL slot free */
    /*
     * UTRSTAT's latched half is DERIVED here rather than serialised, which is
     * why the receive interrupt landing did not move SNAPSHOT_VERSION and why
     * every checkpoint written before it still loads.
     *
     * The two ways of being wrong are not symmetric. Restoring the latch clear
     * over a non-empty FIFO loses an edge, and the bytes then wait for an
     * arrival that may never come — the peer already counted them as delivered.
     * Restoring it set over a FIFO the guest had already been told about costs
     * one spurious interrupt into a driver that will find real data waiting for
     * it and drain it. So a non-empty FIFO re-arms, and an empty one does not.
     *
     * This is the same exception `level_dirty` takes in soc.h: derived state
     * that the load path sets rather than reads, with the byte format unchanged.
     * It stops being tenable the day a second cause is latched here, because
     * "the FIFO has bytes in it" cannot re-derive a transmit or an error edge.
     */
    if (sn_reading(io) && io->err == SNAP_OK)
        u->utrstat_pending = u->rx_count ? UTRSTAT_RX_INT : 0u;
}

static void snap_vic(sn_io_t *io, s5l_vic_t *v) {
    F32(v->raw); F32(v->enable); F32(v->select); F32(v->soft);
}

static void snap_timer(sn_io_t *io, s5l_timer_t *t) {
    F64(t->ticks);
    F32(t->config);
    F32(t->t4_config); F32(t->t4_state);
    F32(t->t4_count);  F32(t->t4_count2); F32(t->t4_value);
    F32(t->irqlatch);
}

static void snap_power(sn_io_t *io, s5l_power_t *p) {
    F32(p->state); F32(p->cfg0); F32(p->cfg1);
    F32(p->sram);  F32(p->cfg24); F32(p->cfg28);
}

/*
 * The MBX register file. reset_done travels because it is DEVICE state, not
 * host bookkeeping: a restore that dropped it would put the machine back with
 * AppleMBX believing its reset had completed while the block said otherwise,
 * and the driver would spin on an acknowledgement it had already been given.
 */
static void snap_mbx(sn_io_t *io, s5l_mbx_t *m) {
    for (unsigned i = 0; i < S5L_MBX_SIZE / 4u; i++) F32(m->reg[i]);
    F32(m->status);
    FB(m->reset_done);
    /*
     * The edram, which is guest-visible memory and therefore machine state.
     * It is 16 MB and it is NOT part of s5l_mbx_t's bytes -- the struct holds
     * a pointer the machine owns, exactly as it does for RAM -- so it is
     * streamed here rather than covered by the size guard.
     *
     * Unconditional on purpose: s5l8900_init() allocates it with the RAM and
     * fails the machine if it cannot, so a NULL here is a machine that was
     * never built. Making the field optional would make the snapshot's LENGTH
     * depend on it, and a format whose size varies with a pointer is how a
     * restore silently reads the next section's bytes.
     */
    FBYTES(m->edram, S5L_MBX_EDRAM_SIZE);
}

/*
 * The GPIO interrupt controller. `raw` travels with the rest: it is what the
 * board is driving, not host wiring, and a restore that dropped it would
 * silently deassert every held source — the pending latch would survive but
 * the next write-one-to-clear would clear it for good instead of re-latching.
 */
static void snap_gpioic(sn_io_t *io, s5l_gpioic_t *g) {
    FA32(g->level, S5L_GPIOIC_GROUPS);
    FA32(g->stat,  S5L_GPIOIC_GROUPS);
    FA32(g->en,    S5L_GPIOIC_GROUPS);
    FA32(g->type,  S5L_GPIOIC_GROUPS);
    FA32(g->raw,   S5L_GPIOIC_GROUPS);
    /* Which lines have a device on them travels with what they are driving:
     * a restore that dropped it would turn every level line back into an
     * undriven one and stop delivering its interrupts. */
    FA32(g->driven, S5L_GPIOIC_GROUPS);
    F64(g->unknown_reads); F64(g->unknown_writes);
    FA32(g->unknown_off, S5L_GPIOIC_UNKNOWN_OFF);
    F32(g->unknown_off_count);
    if (sn_reading(io) && io->err == SNAP_OK &&
        g->unknown_off_count > (unsigned)S5L_GPIOIC_UNKNOWN_OFF)
        io->err = SNAP_ERR_CORRUPT;
}

/*
 * The GPIO pin block: 4 KiB of storage and nothing else.
 *
 * Deliberately NOT serialised: `watch`, the host-side subscriptions, exactly
 * as the SPI slave table and the I2C slave table are not. A restored machine
 * keeps the live machine's wiring, which is the only wiring that can be valid
 * — the callbacks point at host objects the snapshot never saw.
 */
static void snap_gpio(sn_io_t *io, s5l_gpio_t *g) {
    FA32(g->regs, S5L_GPIO_WORDS);
}

/*
 * The five physical buttons. Board state, like s5l_gpioic_t.raw and for the
 * same reason: a restore that dropped it would release every held switch
 * silently, and the pin levels it left behind could not be used to rebuild it
 * — two of the five are active low, so a released volume key and a pressed
 * Home button are indistinguishable at the pin.
 *
 * `pressed` is validated on the way IN. It indexes nothing, but a file that
 * claimed a sixth button would restore a machine whose held-button set could
 * never be cleared through the public API, which range-checks `which`.
 */
static void snap_buttons(sn_io_t *io, s5l_buttons_t *b) {
    F8(b->pressed);
    F64(b->sets); F64(b->refused); F64(b->edges);
    if (sn_reading(io) && io->err == SNAP_OK &&
        (b->pressed & (uint8_t)~((1u << S5L_BUTTON_COUNT) - 1u)) != 0u)
        io->err = SNAP_ERR_CORRUPT;
}

static bool mtz2_state_valid(const s5l_mtz2_t *d) {
    /* The framer's invariants, which the model maintains and a file is not
     * trusted to respect: `len` is zero between packets and never longer than
     * the longest reply this device drives, and `pos` is always inside the
     * packet. The HBPP answer is a loopback within the ordinary framing, so it
     * needs no separate case. */
    if (!d || d->len > S5L_MTZ2_RSP) return false;
    /* The report and the line that announces it move together: a pending
     * payload is bounded by the buffer, an asserted attention line means a
     * payload is waiting, and a contact count without a payload is a frame
     * this device could not have built. */
    if (d->frame_len > MTZ2_PAYLOAD_LIMIT) return false;
    if (d->atn != (d->frame_len != 0u)) return false;
    if (d->contacts > MTZ2_CONTACT_MAX) return false;
    if (d->contacts != 0u && d->frame_len == 0u) return false;
    if (d->len == 0u) return d->pos == 0u;
    return d->pos < d->len;
}

/*
 * The touch controller. Its protocol position is real state: the guest can be
 * part way through a packet when a checkpoint is taken. So is `hbpp_mode` —
 * a restore that set it would put an already-programmed part back into its
 * bootloader, and the driver would answer by downloading 54 KB of firmware to
 * something that is already running it. `in_reset` travels for the same
 * reason: it is what tells the dummy transfer apart from the probe.
 *
 * The published geometry travels too, even though nothing writes it after
 * reset: a snapshot must not depend on the build that reads it choosing the
 * same numbers, and a restored machine whose surface size differed from the
 * one the guest already read would put every tap in the wrong place.
 */
static void snap_mtz1(sn_io_t *io, s5l_mtz1_t *d) {
    FB(d->present); FB(d->powered); FB(d->selected); FB(d->atn);
    FB(d->firmware); FB(d->stage_b); FB(d->check_seen); FB(d->atn_pulse);
    FB(d->raw_stream); FB(d->aspeed_stream); FB(d->transaction_overflow);
    F8(d->operating_mode); F8(d->seq); F8(d->last_n);
    FBYTES(d->params, sizeof d->params);
    F32(d->pkt_len); F32(d->rx_len); F16(d->last_checksum);
    F32(d->b_checksum);
    F64(d->octets); F64(d->aspeed_bytes); F64(d->bspeed_bytes);
    F64(d->transactions); F64(d->mode_transitions); F64(d->checks);
    F64(d->executes); F64(d->unknown_ops); F64(d->report_unknown);
    F64(d->frames_queued); F64(d->frames_read);
    F8(d->geom.family_id); F8(d->geom.endianness);
    F8(d->geom.rows); F8(d->geom.columns); F16(d->geom.bcd_version);
    F8(d->geom.buttons); F32(d->geom.surface_width);
    F32(d->geom.surface_height);
    FBYTES(d->pending, MTZ2_PAYLOAD_LIMIT); F8(d->pending_len);
    FBYTES(d->last_contacts, sizeof d->last_contacts);
    F8(d->frame_seq); F32(d->frame_ms);
    FBYTES(d->held, sizeof d->held); F8(d->held_n); FB(d->lift_pending);
    F32(d->tick_acc);
    if (sn_reading(io)) {
        d->rx_len = 0u;
        d->pkt_len = 0u;
        d->transaction_overflow = false;
        d->raw_stream = false;
    }
}

static void snap_mtz2(sn_io_t *io, s5l_mtz2_t *d) {
    FB (d->in_reset); FB(d->hbpp_mode);
    /*
     * `atn_len` and `rdreg_addr` travel because they are the only things that
     * say what the NEXT `1A A1` means, and a restore that guessed would frame
     * an eight-octet acknowledgement as a sixteen-octet probe and desynchronise
     * the bus for the rest of the boot.
     */
    F8 (d->atn_len); F16(d->atn_val); F32(d->rdreg_addr);
    /* The register conversation travels for the same reason the packet list
     * does: a restored machine that lost it would report a bootload's second
     * half as if it were the whole thing. */
    FA32(d->reg_log_addr, 16); FA32(d->reg_log_val, 16);
    FBYTES(d->reg_log_write, 16); F8(d->reg_log_n);
    F32(d->pos); F32(d->len); F8(d->op); F8(d->frame_phase);
    FBYTES(d->req, S5L_MTZ2_BUF);
    FBYTES(d->rsp, S5L_MTZ2_RSP);
    F8 (d->rows); F8(d->columns); F8(d->endianness);
    F8 (d->family_id); F8(d->buttons);
    /* Widened for the wire: the format has 8-, 32- and 64-bit primitives and
     * no 16-bit one, and inventing one for a single field would be a worse
     * trade than two spare bytes. */
    uint32_t bcd = d->bcd_version;
    F32(bcd);
    if (sn_reading(io) && io->err == SNAP_OK) d->bcd_version = (uint16_t)bcd;
    F32(d->surface_width); F32(d->surface_height);
    /*
     * The pending report travels whole. A checkpoint taken between the length
     * read and the data read has already told the guest a length; restoring
     * without the payload would answer the second half of that exchange with a
     * frame the driver's checksum rejects, and restoring without `atn` would
     * leave a report nobody is ever told about.
     */
    FB (d->atn); F8(d->contacts); F8(d->frame_len); F8(d->frame_seq);
    F32(d->frame_ms);
    FBYTES(d->frame, MTZ2_PAYLOAD_LIMIT);
    FB (d->power_level); F64(d->power_edges);
    F64(d->packets); F64(d->hbpp_probes); F64(d->unknown_opcodes);
    F64(d->resets); F64(d->reset_bytes); F64(d->select_edges);
    F64(d->txn_mark);
    for (unsigned i = 0; i < 24u; i++) F32(d->txn_octets[i]);
    F32(d->txn_n);
    FBYTES(d->head, 64); F32(d->head_n);
    F64(d->octets); F64(d->packet_octets);
    /* Derived, not the literal 24 this used to carry. The array grew to 128 on
     * 2026-07-30 and a hardcoded bound here would have saved the first 24 and
     * silently dropped the rest -- a restored machine reporting a shorter
     * packet list than the one it was made from, with nothing to say so. */
    for (unsigned i = 0; i < sizeof d->pkt_op / sizeof d->pkt_op[0]; i++) {
        F8(d->pkt_op[i]); F32(d->pkt_len[i]);
    }
    F32(d->pkt_n);
    F64(d->frames_queued); F64(d->frames_read);
    F64(d->length_reads); F64(d->data_reads); F64(d->injects_refused);
    F8 (d->last_unknown_op);
    if (sn_reading(io) && io->err == SNAP_OK && !mtz2_state_valid(d))
        io->err = SNAP_ERR_CORRUPT;
}

static bool i2c_state_valid(const s5l_i2c_t *b) {
    if (!b || b->slave_count > S5L_I2C_SLAVES ||
        b->unknown_off_count > S5L_I2C_UNKNOWN_OFF ||
        b->ds > 0xffu ||
        (b->stat & I2C_STAT_NAK) != 0u ||
        (b->intstat & ~(I2C_INT_BYTE | I2C_INT_STOP)) != 0u ||
        b->sel < -1 ||
        (b->sel >= 0 && (unsigned)b->sel >= b->slave_count) ||
        (!b->active && (b->sel != -1 || b->reading)) ||
        b->active != ((b->stat & I2C_STAT_START) != 0u) ||
        (b->active &&
         b->reading !=
             ((b->stat & I2C_STAT_MODE) == I2C_STAT_MODE_MRX)) ||
        (b->active && b->sel == -1 && !b->nak))
        return false;
    for (unsigned i = 0; i < b->slave_count; i++) {
        if (b->slaves[i].addr > 0x7fu || !b->slaves[i].start) return false;
        for (unsigned j = 0; j < i; j++)
            if (b->slaves[i].addr == b->slaves[j].addr) return false;
    }
    return true;
}

static bool pmu_state_valid(const s5l_pcf50635_t *p) {
    if (!p || p->seconds < PCF50635_MIN_TIME ||
        p->seconds > PCF50635_MAX_TIME ||
        !p->tick_hz || p->tick_accum >= p->tick_hz ||
        p->unknown_reg_count > PCF50635_UNKNOWN_REGS)
        return false;
    for (unsigned i = 0; i < PCF50635_NREG; i++)
        if (p->written[i] > 1u) return false;
    return true;
}

/*
 * Controller wiring is host state and is deliberately not serialized. `sel`
 * is guest state because a snapshot may be taken between START and STOP; it
 * is encoded without aliasing an int32_t through a uint32_t pointer.
 */
static void snap_i2c(sn_io_t *io, s5l_i2c_t *b) {
    F32(b->con); F32(b->stat); F32(b->add); F32(b->ds);
    F32(b->enable); F32(b->intstat);
    FB(b->nak); FB(b->active); FB(b->reading);

    uint32_t selected = b->sel < 0 ? UINT32_MAX : (uint32_t)b->sel;
    F32(selected);
    if (sn_reading(io) && io->err == SNAP_OK) {
        if (selected == UINT32_MAX) b->sel = -1;
        else if (selected > INT32_MAX) io->err = SNAP_ERR_CORRUPT;
        else b->sel = (int32_t)selected;
    }

    F64(b->starts); F64(b->bytes_tx); F64(b->bytes_rx); F64(b->naks);
    F64(b->unknown_reads); F64(b->unknown_writes);
    FA32(b->unknown_off, S5L_I2C_UNKNOWN_OFF);
    F32(b->unknown_off_count);
    if (sn_reading(io) && io->err == SNAP_OK && !i2c_state_valid(b))
        io->err = SNAP_ERR_CORRUPT;
}

static void snap_pmu(sn_io_t *io, s5l_pcf50635_t *p) {
    FBYTES(p->regs, PCF50635_NREG);
    FBYTES(p->written, PCF50635_NREG);
    F64(p->seconds);
    F32(p->tick_hz); F64(p->tick_accum);
    F8(p->ptr); FB(p->have_ptr); FB(p->reading);
    F64(p->reg_reads); F64(p->reg_writes);
    F64(p->unknown_reads); F64(p->unknown_writes);
    FBYTES(p->unknown_reg, PCF50635_UNKNOWN_REGS);
    F32(p->unknown_reg_count);
    if (sn_reading(io) && io->err == SNAP_OK && !pmu_state_valid(p))
        io->err = SNAP_ERR_CORRUPT;
}

static bool codec_state_valid(const s5l_wm8991_t *c, bool wm8758) {
    if (!c || c->ptr >= WM8991_NREG ||
        c->wlen > WM8991_MAX_WRITE ||
        c->unknown_reg_count > WM8991_UNKNOWN_REGS)
        return false;
    /* Register 0 is the identity and is never stored, so a file claiming a
     * value for it was not written by this model. Everything else is free
     * storage and has no invariant to check beyond the boolean marker. */
    if (c->regs[WM8991_REG_ID] != 0u || c->written[WM8991_REG_ID] != 0u)
        return false;
    /* Bit 5 of register 1 is unimplemented. A stored 1 there could only come
     * from a file this model did not write, and restoring it would silently
     * turn the part into a WM1817 for every subsequent read. */
    if (!wm8758 && (c->regs[WM8991_REG_PWR1] & WM8991_PWR1_PROBE) != 0u) return false;
    /* Same argument for bit 12 of the status register: it is a mirror of the
     * control register, never storage, so a stored 1 is a file this model did
     * not write — and restoring one would make the driver's poll observe a
     * level nothing commanded. */
    if (!wm8758 && (c->regs[WM8991_REG_GPSTAT] & WM8991_GP_BIT) != 0u) return false;
    for (unsigned i = 0; i < WM8991_NREG; i++) {
        if (c->written[i] > 1u) return false;
        if (wm8758 && (c->regs[i] > 0x1ffu ||
            (!c->written[i] && c->regs[i] != 0u) ||
            (!s5l_wm8758_register_valid(i) && c->written[i]))) return false;
    }
    if (wm8758 && (!s5l_wm8758_register_valid(c->ptr) ||
        c->second_byte || c->latch || c->wide_writes ||
        c->reg_reads || c->id_reads || c->probe_bit_writes ||
        c->status_mirror_reads || c->unknown_reg_count)) return false;
    for (unsigned i = 0; i < c->unknown_reg_count; i++)
        if (c->unknown_reg[i] >= WM8991_NREG) return false;
    return true;
}

static bool i2s_state_valid(const s5l_i2s_t *s) {
    return s && s->unknown_off_count <= S5L_I2S_UNKNOWN_OFF;
}

static void snap_codec(sn_io_t *io, s5l_wm8991_t *c, bool wm8758) {
    for (unsigned i = 0; i < WM8991_NREG; i++) F16(c->regs[i]);
    FBYTES(c->written, WM8991_NREG);
    F8(c->ptr); FB(c->reading); FB(c->second_byte); F16(c->latch);
    FBYTES(c->wbuf, WM8991_MAX_WRITE);
    F32(c->wlen);
    F64(c->reg_reads); F64(c->reg_writes);
    F64(c->wide_writes); F64(c->packed_writes);
    F64(c->refused_writes); F64(c->id_reads); F64(c->probe_bit_writes);
    F64(c->status_mirror_reads);
    F64(c->unknown_reads);
    FBYTES(c->unknown_reg, WM8991_UNKNOWN_REGS);
    F32(c->unknown_reg_count);
    if (sn_reading(io) && io->err == SNAP_OK && !codec_state_valid(c, wm8758))
        io->err = SNAP_ERR_CORRUPT;
}

static void snap_i2s(sn_io_t *io, s5l_i2s_t *s) {
    FA32(s->regs, S5L_I2S_REGS);
    F64(s->reads); F64(s->writes);
    F64(s->unknown_reads); F64(s->unknown_writes);
    FA32(s->unknown_off, S5L_I2S_UNKNOWN_OFF);
    F32(s->unknown_off_count);
    if (sn_reading(io) && io->err == SNAP_OK && !i2s_state_valid(s))
        io->err = SNAP_ERR_CORRUPT;
}

static bool spi_state_valid(const s5l_spi_t *s) {
    /* Only what the model itself can produce. The FIFO levels bound the arrays
     * they index, the low `cs` bits bound the slave table, its one high bit is
     * only valid between the two words of lcd0's status read, and the status
     * word carries nothing but the four event latches — the two level fields
     * are computed on read and are never stored. */
    if (!s || s->tx_level > S5L_SPI_FIFO_DEPTH ||
        s->rx_level > S5L_SPI_FIFO_DEPTH ||
        (s->cs & ~(S5L_SPI_CS_ROUTE_MASK |
                   S5L_SPI_LCD_READ_PENDING)) != 0u ||
        (s->cs & S5L_SPI_CS_ROUTE_MASK) >= S5L_SPI_SLAVES ||
        (s->status & ~(uint32_t)SPI_STATUS_EVENTS) != 0u ||
        s->unknown_off_count > S5L_SPI_UNKNOWN_OFF)
        return false;
    if ((s->cs & S5L_SPI_LCD_READ_PENDING) != 0u &&
        ((s->cs & S5L_SPI_CS_ROUTE_MASK) != S5L_SPI0_LCD_CS ||
         s->cnt != 2u || s->words_left != 1u))
        return false;
    return true;
}

/*
 * An SPI controller. The attached devices are host wiring and are deliberately
 * not serialized, exactly as the I2C slave table is not; `cs` travels so an
 * in-flight transfer remains coherent. Its low bits are the route and bit 7 is
 * lcd0's pending-register-read phase. This does not change the byte layout or
 * any previously valid value: an older reader safely rejects a new mid-read
 * value as corrupt, while a new reader interprets an old clear bit as no
 * pending read. snap_apply() reasserts spi0's current low board route so
 * checkpoints made before the lcd0 endpoint existed can resume.
 */
static void snap_spi(sn_io_t *io, s5l_spi_t *s) {
    F32(s->control); F32(s->setup); F32(s->pin);
    F32(s->clkdiv);  F32(s->cnt);   F32(s->idd);
    F32(s->status);  F32(s->words_left);
    FBYTES(s->tx, S5L_SPI_FIFO_DEPTH);
    FBYTES(s->rx, S5L_SPI_FIFO_DEPTH);
    F8(s->tx_level); F8(s->rx_level); F8(s->cs);
    F64(s->words); F64(s->tx_drops); F64(s->rx_underruns);
    /* Travels with the rest: a restored machine whose overrun count reset
     * would under-report exactly the loss this counter exists to make
     * visible. */
    F64(s->rx_overruns); F64(s->dma_arms);
    /* Same reason as rx_overruns: a restored machine that reset these would
     * under-report the very thing they were added to settle -- whether the
     * guest ever reads the receive FIFO, and whether this model ever raises
     * the line that would make it. irq_last is the edge detector; restoring it
     * wrong would only miscount the first edge after a restore, but a counter
     * that is right except at the seam is the kind that gets trusted and then
     * quietly misleads. */
    F64(s->rx_reads); F64(s->irq_rises); FB(s->irq_last);
    F64(s->unknown_reads); F64(s->unknown_writes);
    FA32(s->unknown_off, S5L_SPI_UNKNOWN_OFF);
    F32(s->unknown_off_count);
    if (sn_reading(io) && io->err == SNAP_OK && !spi_state_valid(s))
        io->err = SNAP_ERR_CORRUPT;
}

/*
 * The DWC2 block's only writable register. The GHWCFG straps are constants in
 * core/src/soc/usbotg.c, not fields, so there is nothing else here to serialize
 * — and a restored machine reads the same straps because it is the same build.
 */
static void snap_usbotg(sn_io_t *io, s5l_usbotg_t *u) {
    F32(u->pcgcctl);
}

static bool pl080_state_valid(const s5l_pl080_t *d) {
    /* Only what the model itself can produce. The Active bit is derived on read
     * and is never stored, so a saved Configuration carrying it is a file this
     * build did not write; the unknown-offset cursor bounds its own array. */
    if (!d || d->unknown_off_count > S5L_PL080_UNKNOWN_OFF) return false;
    for (unsigned i = 0; i < S5L_PL080_CHANNELS; i++)
        if (d->ch[i].cfg & PL080_CFG_ACTIVE) return false;
    return true;
}

/*
 * A PL080 DMA controller. Every field travels, including the counters: a
 * restore that reset `bytes_moved` would make "did any audio leave the guest"
 * a question about when the snapshot was taken. There is no host wiring to
 * exclude — s5l_pl080_run() is HANDED the bus rather than holding it, precisely
 * so that this visitor has no pointer to decide about.
 *
 * A channel can be saved mid-chain: the registers describe how far it got, so
 * a restore resumes from the same item with the same remaining count. It cannot
 * be saved mid-ITEM, because s5l_pl080_run() completes every runnable channel
 * inside the tick that started it and a snapshot is taken between ticks.
 */
static void snap_pl080(sn_io_t *io, s5l_pl080_t *d) {
    for (unsigned i = 0; i < S5L_PL080_CHANNELS; i++) {
        F32(d->ch[i].src); F32(d->ch[i].dst); F32(d->ch[i].lli);
        F32(d->ch[i].ctrl); F32(d->ch[i].cfg);
        F64(d->ch[i].runs); F64(d->ch[i].bytes);
    }
    F32(d->config); F32(d->sync); F32(d->raw_tc); F32(d->raw_err);
    F64(d->reads); F64(d->writes);
    F64(d->unknown_reads); F64(d->unknown_writes);
    FA32(d->unknown_off, S5L_PL080_UNKNOWN_OFF);
    F32(d->unknown_off_count);
    F64(d->transfers); F64(d->bytes_moved);
    F64(d->items); F64(d->completions);
    F64(d->refused_flow); F64(d->refused_width); F64(d->refused_chain);
    F64(d->refused_softreq); F64(d->refused_endian);
    F64(d->config_writes); F32(d->config_first);
    if (sn_reading(io) && io->err == SNAP_OK && !pl080_state_valid(d))
        io->err = SNAP_ERR_CORRUPT;
}

static void snap_clcd(sn_io_t *io, s5l_clcd_t *c) {
    F32(c->enable); F32(c->disable); F32(c->ctrl); F32(c->fifo);
    F32(c->intmask); F32(c->intstatus); F32(c->reg1c);
    F32(c->preenable); F32(c->backdrop);
    FA32(c->video, sizeof c->video / sizeof c->video[0]);
    for (unsigned k = 0; k < CLCD_WIN_COUNT; k++) {
        s5l_clcd_window_t *w = &c->win[k];
        F32(w->stride); F32(w->control); F32(w->fbaddr);
        F32(w->geometry); F32(w->linewords); F32(w->position);
    }
    F32(c->update); F32(c->update2);
    FA32(c->wincfg_aux, sizeof c->wincfg_aux / sizeof c->wincfg_aux[0]);
    FA32(c->csc,    sizeof c->csc    / sizeof c->csc[0]);
    F32(c->gate);
    FA32(c->opaque, sizeof c->opaque / sizeof c->opaque[0]);
    FA32(&c->gamma[0][0], 3u * 256u);
    FB (c->scanning);
    F32(c->frame_ticks);
    F32(c->frame_accum);
    F64(c->frames);
    /* Guest window updates travel with the machine for the same reason
     * frames does: a restored run that reset them would report the tail
     * of a boot as though it were the whole thing. */
    F64(c->updates);
}

static bool tvout_state_valid(const s5l_tvout_t *t) {
    if (!t || (t->frame_ticks == 0u
                   ? t->frame_accum != 0u
                   : t->frame_accum >= t->frame_ticks))
        return false;
    for (unsigned bank = 0; bank < S5L_TVOUT_BANK_COUNT; bank++)
        if ((t->regs[bank][0] & TVOUT_READY) != 0u) return false;
    /* tick() and every mixer+SDO timing transition reset the phase.  A stopped
     * timing engine with residual phase cannot be produced by the model and
     * would silently normalize on the first post-restore zero tick.  The
     * independent control block may be stopped while timing remains live. */
    if (!s5l_tvout_running(t) && t->frame_accum != 0u) return false;

    /* These are status latches, not guest storage.  The model can only
     * generate SDO VSYNC bit 0 and deliberately generates no mixer event. */
    if ((t->regs[S5L_TVOUT_BANK_SDO][TVOUT_SDO_IRQ / 4u] &
         ~TVOUT_SDO_VSYNC) != 0u ||
        t->regs[S5L_TVOUT_BANK_MIXER][TVOUT_MIXER_STATUS / 4u] != 0u)
        return false;
    return true;
}

static void snap_tvout(sn_io_t *io, s5l_tvout_t *t) {
    FA32(&t->regs[0][0],
         (size_t)S5L_TVOUT_BANK_COUNT * S5L_TVOUT_BANK_WORDS);
    F32(t->frame_ticks);
    F32(t->frame_accum);
    F64(t->frames);
    if (sn_reading(io) && io->err == SNAP_OK && !tvout_state_valid(t))
        io->err = SNAP_ERR_CORRUPT;
}

/*
 * NOR. The contents are saved as well as the scanned directory: a guest
 * payload can program the flash (that is how an untethered jailbreak persists
 * itself), so the NOR is mutable guest-visible state, not a constant.
 *
 * Deliberately NOT serialised: `data`, the host allocation. Its SIZE is
 * checked against the live machine in the GEOM section before anything is
 * touched, and the bytes are then written through the existing pointer.
 */
static void snap_nor(sn_io_t *io, s5l_nor_t *n) {
    if (io->mode == SN_VALIDATE) sn_discard(io, n->size);
    else                         FBYTES(n->data, n->size);
    for (unsigned i = 0; i < S5L_NOR_MAX_IMAGES; i++) {
        F32(n->images[i].ident);
        F32(n->images[i].offset);
        F32(n->images[i].size);
    }
    F32(n->image_count);
    if (sn_reading(io) && io->err == SNAP_OK &&
        n->image_count > (unsigned)S5L_NOR_MAX_IMAGES)
        io->err = SNAP_ERR_CORRUPT;
    if (sn_reading(io) && io->err == SNAP_OK) {
        for (unsigned i = 0; i < n->image_count; i++) {
            if (n->images[i].size < 20u ||
                (uint64_t)n->images[i].offset + n->images[i].size > n->size) {
                io->err = SNAP_ERR_CORRUPT;
                break;
            }
        }
    }
}

/*
 * The machine's own state, excluding RAM, NOR, the stub backing stores and the
 * CPU (each of which has its own section).
 *
 * Deliberately NOT serialised: `cpu` (own section), `bus` (host function
 * pointers and callback contexts — a tool may have interposed on them),
 * `ram` (host allocation), `pre_step_hook`/its context, targets and counters,
 * and the interpreter tick-batching state/counters, MBX work ledger, uart4 host-peer
 * callbacks/context and WFI pacing callback/context/counters (live host
 * policy/measurement),
 * `nor.data` and `stubs[].regs`/`stubs[].name` (host allocations / string
 * literals). ram_base/ram_size live in GEOM.
 */
static void snap_mach(sn_io_t *io, s5l8900_t *m) {
    snap_uart(io, &m->uart0);
    /* uart4, the PPP line, immediately after the console — the same visitor
     * over a second, independent capture. See SNAPSHOT_VERSION's v10 note for
     * why inserting it here is a version bump and not a free append. */
    snap_uart(io, &m->uart4);
    for (unsigned i = 0; i < S5L8900_VIC_COUNT; i++) snap_vic(io, &m->vic[i]);
    snap_timer(io, &m->timer);
    snap_power(io, &m->power);
    snap_mbx(io, &m->mbx);
    snap_clcd (io, &m->clcd);
    snap_tvout(io, &m->tvout);
    for (unsigned i = 0; i < S5L8900_I2C_COUNT; i++)
        snap_i2c(io, &m->i2c[i]);
    snap_pmu(io, &m->pmu);
    /* The codec immediately after the PMU — the two i2c0 slaves adjacent, in
     * attach order — and the I2S windows after it. See SNAPSHOT_VERSION's v11
     * note for why inserting here is a version bump and not a free append. */
    /* The codec variant is host board wiring, never inferred from mutable
     * register bytes. M68's previously unused all-zero v39 codec state is a
     * valid WM8758 reset state. PMU keeps slot0, so old I2C selections retain
     * their meaning when the new control endpoint occupies slot1. */
    snap_codec(io, &m->codec, m->i2c[0].slave_count == 2u &&
                              m->i2c[0].slaves[1].addr == WM8758_I2C_ADDR);
    for (unsigned i = 0; i < S5L8900_I2S_COUNT; i++)
        snap_i2s(io, &m->i2s[i]);
    for (unsigned i = 0; i < S5L8900_SPI_COUNT; i++)
        snap_spi(io, &m->spi[i]);
    snap_gpioic(io, &m->gpioic);
    snap_gpio(io, &m->gpio);
    /* The board's switches, adjacent to the two blocks they drive. See
     * SNAPSHOT_VERSION's v12 note for why this is a bump and not an append. */
    snap_buttons(io, &m->buttons);
    snap_mtz2(io, &m->mtz2);
    snap_mtz1(io, &m->mtz1);
    snap_usbotg(io, &m->usbotg);
    /* The two DMA controllers. Appended after usbotg rather than placed beside
     * the I2S windows they feed, because MACH is a positional stream: putting
     * them mid-list would move every field after them for no benefit. See
     * SNAPSHOT_VERSION's v14 note. */
    for (unsigned i = 0; i < S5L8900_DMAC_COUNT; i++)
        snap_pl080(io, &m->dmac[i]);

    F64(m->unmapped_reads);
    F64(m->unmapped_writes);
    FA32(m->unmapped_addr, S5L_UNMAPPED_LOG);
    F32(m->unmapped_addr_count);

    FB (m->trace_devices);
    FA32(m->dev_addr,  S5L_DEVLOG);
    FA32(m->dev_value, S5L_DEVLOG);
    for (unsigned i = 0; i < S5L_DEVLOG; i++) FB(m->dev_is_write[i]);
    F32(m->dev_count);

    F32(m->cpu_hz);
    F32(m->tb_hz);
    F64(m->tb_accum);

    F32(m->stub_declare_failures);

    if (sn_reading(io) && io->err == SNAP_OK &&
        (m->unmapped_addr_count > (unsigned)S5L_UNMAPPED_LOG ||
         m->dev_count           > (unsigned)S5L_DEVLOG ||
         m->pmu.tick_hz         != m->tb_hz))
        io->err = SNAP_ERR_CORRUPT;
}

/* ------------------------------------------------------------ sections --- */

/*
 * GEOM: everything that describes the SHAPE of the machine rather than its
 * contents. It is the first section so that a mismatch is caught before a
 * single byte of the live machine has been modified.
 */
static void snap_geom(sn_io_t *io, s5l8900_t *m) {
    uint32_t ram_base = m->ram_base, ram_size = m->ram_size;
    uint32_t nor_size = m->nor.size, stub_count = m->stub_count;
    uint32_t nor_base = m->nor_base;
    uint32_t page = SNAP_PAGE;
    F32(ram_base); F32(ram_size); F32(nor_size); F32(stub_count); F32(page);
    F32(nor_base);

    if (sn_reading(io)) {
        if (io->err != SNAP_OK) return;
        if (ram_base != m->ram_base || ram_size != m->ram_size ||
            nor_size != m->nor.size || stub_count != m->stub_count ||
            nor_base != m->nor_base || page != SNAP_PAGE) {
            io->err = SNAP_ERR_GEOMETRY;
            return;
        }
    }

    /* Each stub window is identified by base/size/name so that a snapshot
     * taken before a peripheral window was added, renamed, or resized is
     * refused rather than restored into the wrong backing store. */
    for (unsigned i = 0; i < m->stub_count && io->err == SNAP_OK; i++) {
        uint32_t base = m->stubs[i].base, size = m->stubs[i].size;
        uint32_t nregs = m->stubs[i].nregs;
        const char *live = m->stubs[i].name ? m->stubs[i].name : "";
        uint32_t nlen = (uint32_t)strlen(live);
        char nm[64];
        if (io->mode == SN_SAVE) snprintf(nm, sizeof nm, "%s", live);
        else                     memset(nm, 0, sizeof nm);
        F32(base); F32(size); F32(nregs); F32(nlen);
        if (io->err == SNAP_OK && nlen >= sizeof nm) { io->err = SNAP_ERR_CORRUPT; return; }
        FBYTES(nm, nlen);
        if (sn_reading(io) && io->err == SNAP_OK) {
            nm[nlen] = '\0';
            if (base != m->stubs[i].base || size != m->stubs[i].size ||
                nregs != m->stubs[i].nregs || strcmp(nm, live) != 0) {
                io->err = SNAP_ERR_GEOMETRY;
                return;
            }
        }
    }
}

static void snap_cpu_sec(sn_io_t *io, s5l8900_t *m) { snap_cpu(io, &m->cpu); }
static void snap_nor_sec(sn_io_t *io, s5l8900_t *m) { snap_nor(io, &m->nor); }

/*
 * RAM, page-classified. See the header comment for why classification is a
 * read-only scan rather than a dirty bitmap.
 *
 *   uint64 npages
 *   uint8  class[npages]     0 = all zero, 1 = all one byte value, 2 = raw
 *   then, in page order: class 1 -> one byte, class 2 -> the page verbatim
 */
/* Test equality in complete machine words, without alignment or aliasing
 * assumptions. Repeating one byte has the same representation in either
 * byte order. The tail never reads beyond a partial final RAM page. */
static uint8_t snap_page_class(const uint8_t *page, size_t len) {
    const uint8_t first = page[0];
    const uint64_t repeated = UINT64_C(0x0101010101010101) * first;
    size_t i = 1;
    while (len - i >= sizeof repeated) {
        uint64_t word;
        memcpy(&word, page + i, sizeof word);
        if (word != repeated) return 2;
        i += sizeof word;
    }
    for (; i < len; i++)
        if (page[i] != first) return 2;
    return first ? 1 : 0;
}

static void snap_ram(sn_io_t *io, s5l8900_t *m) {
    uint64_t npages = ((uint64_t)m->ram_size + SNAP_PAGE - 1u) / SNAP_PAGE;
    uint64_t n = npages;
    F64(n);
    if (io->err != SNAP_OK) return;
    if (sn_reading(io) && n != npages) { io->err = SNAP_ERR_CORRUPT; return; }

    if (io->mode == SN_LOAD) memset(m->ram, 0, m->ram_size);

    for (uint64_t p = 0; p < npages; p++) {
        uint64_t off = p * SNAP_PAGE;
        size_t   len = (size_t)((m->ram_size - off < SNAP_PAGE)
                                ? m->ram_size - off : SNAP_PAGE);
        uint8_t *page = m->ram + off;
        uint8_t  cls = 0;
        if (!sn_reading(io)) {
            cls = snap_page_class(page, len);
        }
        sn_u8(io, &cls);
        if (io->err != SNAP_OK) return;
        if (cls == 0) continue;
        if (cls == 1) {
            uint8_t v = page[0];
            sn_u8(io, &v);
            if (io->mode == SN_LOAD && io->err == SNAP_OK) memset(page, v, len);
        } else if (cls == 2) {
            if (io->mode == SN_VALIDATE) sn_discard(io, len);
            else                         sn_raw(io, page, len);
        } else {
            io->err = SNAP_ERR_CORRUPT;
            return;
        }
    }
}

/* Stub backing stores and their access counters. The identity of each window
 * was already checked in GEOM, so only contents travel here. */
static void snap_stubs(sn_io_t *io, s5l8900_t *m) {
    for (unsigned i = 0; i < m->stub_count; i++) {
        s5l_stub_t *s = &m->stubs[i];
        if (io->mode == SN_VALIDATE) {
            for (uint32_t k = 0; k < s->nregs; k++) {
                uint32_t discard = 0;
                sn_u32(io, &discard);
            }
        } else {
            FA32(s->regs, s->nregs);
        }
        F64(s->reads); F64(s->writes); F64(s->oob);
    }
}

/* --------------------------------------------------------- the payload --- */

typedef void (*snap_fn_t)(sn_io_t *, s5l8900_t *);

static uint64_t snap_measure(snap_fn_t fn, s5l8900_t *m) {
    sn_io_t c = {0};
    c.mode = SN_COUNT;
    fn(&c, m);
    return c.pos;
}

static void snap_section(sn_io_t *io, uint32_t tag, snap_fn_t fn, s5l8900_t *m) {
    if (io->err != SNAP_OK) return;
    uint32_t t = tag, zero = 0;
    uint64_t len = sn_reading(io) ? 0 : snap_measure(fn, m);

    if (io->mode == SN_COUNT) { io->pos += 16 + len; return; }

    sn_u32(io, &t); sn_u32(io, &zero); sn_u64(io, &len);
    if (io->err != SNAP_OK) return;
    if (sn_reading(io)) {
        if (t != tag || zero != 0) { io->err = SNAP_ERR_CORRUPT; return; }
    }
    uint64_t start = io->pos;
    fn(io, m);
    if (io->err != SNAP_OK) return;
    if (io->pos - start != len) io->err = SNAP_ERR_CORRUPT;
}

static void snap_end(sn_io_t *io, s5l8900_t *m) { (void)io; (void)m; }

static void snap_payload(sn_io_t *io, s5l8900_t *m) {
    snap_section(io, TAG_GEOM, snap_geom,    m);
    snap_section(io, TAG_CPU,  snap_cpu_sec, m);
    snap_section(io, TAG_MACH, snap_mach,    m);
    snap_section(io, TAG_RAM,  snap_ram,     m);
    snap_section(io, TAG_NOR,  snap_nor_sec, m);
    snap_section(io, TAG_STUB, snap_stubs,   m);
    snap_section(io, TAG_END,  snap_end,     m);
}

static void snap_header(sn_io_t *io, uint64_t *payload_len) {
    char     magic[SNAPSHOT_MAGIC_LEN];
    uint32_t version = SNAPSHOT_VERSION, hlen = SNAP_HEADER_LEN;
    uint64_t flags = 0;

    if (io->mode == SN_SAVE) memcpy(magic, SNAPSHOT_MAGIC, SNAPSHOT_MAGIC_LEN);
    else                     memset(magic, 0, sizeof magic);

    /* The header is outside the checksummed payload, so it is written with the
     * raw primitives but must not disturb the running hash. */
    uint64_t saved_hash = io->hash;
    sn_raw(io, magic, SNAPSHOT_MAGIC_LEN);
    sn_u32(io, &version);
    sn_u32(io, &hlen);
    sn_u64(io, payload_len);
    sn_u64(io, &flags);
    io->hash = saved_hash;

    if (sn_reading(io) && io->err == SNAP_OK) {
        if (memcmp(magic, SNAPSHOT_MAGIC, SNAPSHOT_MAGIC_LEN) != 0)
            io->err = SNAP_ERR_MAGIC;
        else if (version != SNAPSHOT_VERSION)
            io->err = SNAP_ERR_VERSION;
        else if (hlen != SNAP_HEADER_LEN)
            io->err = SNAP_ERR_CORRUPT;
        else if (flags != 0)
            io->err = SNAP_ERR_CORRUPT;
    }
}

/* ------------------------------------------------------------ save side --- */

static snapshot_status_t snap_write(const s5l8900_t *cm, FILE *f,
                                    uint8_t *buf, size_t cap, size_t *written,
                                    bool defer_hash) {
    /* Casting away const is safe and confined: the visitors take a mutable
     * pointer because the SAME code loads, and in SN_SAVE/SN_COUNT mode not
     * one of them assigns through it. */
    s5l8900_t *m = (s5l8900_t *)(uintptr_t)cm;

    uint64_t payload_len = snap_measure(snap_payload, m);

    sn_io_t io = {0};
    io.mode = SN_SAVE; io.f = f; io.buf = buf; io.cap = cap;
    io.hash = FNV64_OFFSET;
    io.defer_hash = defer_hash;

    snap_header(&io, &payload_len);
    uint64_t body_start = io.pos;
    snap_payload(&io, m);
    if (io.err == SNAP_OK && io.pos - body_start != payload_len)
        io.err = SNAP_ERR_CORRUPT;

    /* Trailer: the checksum of everything after the header. */
    uint64_t h = io.hash, saved = io.hash;
    sn_u64(&io, &h);
    io.hash = saved;

    if (written) *written = (size_t)io.pos;
    return io.err;
}

static bool snap_machine_valid(const s5l8900_t *m) {
    if (!m || !m->ram || !m->ram_size || !m->nor.data || !m->nor.size ||
        m->stub_count > S5L_STUB_MAX ||
        m->uart0.tx_len >= UART_TX_BUFFER ||
        m->uart4.tx_len >= UART_TX_BUFFER ||
        m->uart0.rx_count > UART_RX_FIFO || m->uart0.rx_head >= UART_RX_FIFO ||
        m->uart4.rx_count > UART_RX_FIFO || m->uart4.rx_head >= UART_RX_FIFO ||
        m->nor.image_count > S5L_NOR_MAX_IMAGES ||
        m->unmapped_addr_count > S5L_UNMAPPED_LOG || m->dev_count > S5L_DEVLOG)
        return false;
    for (unsigned i = 0; i < S5L8900_I2C_COUNT; i++)
        if (!i2c_state_valid(&m->i2c[i])) return false;
    if (!pmu_state_valid(&m->pmu)) return false;
    if (!codec_state_valid(&m->codec, m->i2c[0].slave_count == 2u &&
                                    m->i2c[0].slaves[1].addr == WM8758_I2C_ADDR)) return false;
    for (unsigned i = 0; i < S5L8900_I2S_COUNT; i++)
        if (!i2s_state_valid(&m->i2s[i])) return false;
    if (!tvout_state_valid(&m->tvout)) return false;
    for (unsigned i = 0; i < S5L8900_SPI_COUNT; i++)
        if (!spi_state_valid(&m->spi[i])) return false;
    /* Slave callbacks are host wiring, not file bytes. Requiring the board's
     * exact wiring on both save and load makes that omission safe: a snapshot
     * can never be applied to a differently wired machine. */
    bool n82_wiring =
        m->i2c[0].slave_count == 2u &&
        m->i2c[0].slaves[0].addr == PCF50635_I2C_ADDR &&
        m->i2c[0].slaves[0].ctx == &m->pmu &&
        m->i2c[0].slaves[1].addr == WM8991_I2C_ADDR &&
        m->i2c[0].slaves[1].ctx == &m->codec;
    bool m68_wiring =
        m->i2c[0].slave_count == 2u &&
        m->i2c[0].slaves[0].addr == PCF50635_I2C_ADDR &&
        m->i2c[0].slaves[0].ctx == &m->pmu &&
        m->i2c[0].slaves[1].addr == WM8758_I2C_ADDR &&
        m->i2c[0].slaves[1].ctx == &m->codec;
    if ((!n82_wiring && !m68_wiring) ||
        m->i2c[1].slave_count != 0u ||
        m->pmu.tick_hz != m->tb_hz)
        return false;
    /* The SPI board wiring, for the same reason. N82 has the transport-only
     * Merlot panel on spi0 cs1 and Z2 on spi1 cs0. M68 has its Merlot panel on
     * spi1 cs0, its LM2512 display-link transmitter on spi0 cs0, and Z1 on
     * spi2 cs0. Restoring a receive FIFO into a controller
     * with no device behind it would resume a transfer that can never finish,
     * and restoring either touch protocol position behind a different device
     * would resume it into the wrong one. */
    m68_wiring = m68_wiring &&
        m->spi[1].slaves[0].transfer != NULL &&
        m->spi[1].slaves[0].ctx == (void *)&m->spi[1] &&
        m->spi[2].slaves[0].transfer != NULL &&
        m->spi[2].slaves[0].ctx == (void *)&m->mtz1;
    if (!n82_wiring && !m68_wiring) return false;
    if (n82_wiring) {
        if (m->spi[0].slaves[S5L_SPI0_LCD_CS].transfer == NULL ||
            m->spi[0].slaves[S5L_SPI0_LCD_CS].ctx != (void *)&m->spi[0])
            return false;
        for (unsigned cs = 0; cs < S5L_SPI_SLAVES; cs++)
            if (cs != S5L_SPI0_LCD_CS &&
                m->spi[0].slaves[cs].transfer != NULL) return false;
        if (m->spi[1].slaves[0].transfer == NULL ||
            m->spi[1].slaves[0].ctx != (void *)&m->mtz2 ||
            (m->spi[1].cs & S5L_SPI_LCD_READ_PENDING) != 0u) return false;
        for (unsigned cs = 1; cs < S5L_SPI_SLAVES; cs++)
            if (m->spi[1].slaves[cs].transfer != NULL) return false;
        if (!mtz2_state_valid(&m->mtz2)) return false;
    } else {
        if (m->spi[0].slaves[0].transfer == NULL ||
            m->spi[0].slaves[0].ctx != (void *)&m->spi[0] ||
            (m->spi[0].cs & S5L_SPI_LCD_READ_PENDING) != 0u ||
            m->spi[0].cs > 1u)
            return false;
        for (unsigned cs = 1; cs < S5L_SPI_SLAVES; cs++)
            if (m->spi[0].slaves[cs].transfer != NULL) return false;
        if ((m->spi[1].cs & S5L_SPI_LCD_READ_PENDING) != 0u)
            return false;
        for (unsigned cs = 1; cs < S5L_SPI_SLAVES; cs++)
            if (m->spi[1].slaves[cs].transfer != NULL ||
                m->spi[2].slaves[cs].transfer != NULL) return false;
    }
    for (unsigned i = 0; i < m->nor.image_count; i++)
        if (m->nor.images[i].size < 20u ||
            (uint64_t)m->nor.images[i].offset + m->nor.images[i].size > m->nor.size)
            return false;
    for (unsigned i = 0; i < m->stub_count; i++) {
        const s5l_stub_t *s = &m->stubs[i];
        uint64_t want = ((uint64_t)s->size + 3u) / 4u;
        const char *name = s->name ? s->name : "";
        if (!s->size || !s->regs || s->nregs != want || strlen(name) >= 64u)
            return false;
    }
    return true;
}

static char *snapshot_temp_path(const char *path, const void *tag) {
    size_t n = strlen(path);
    if (n > SIZE_MAX - 40u) return NULL;
    char *tmp = malloc(n + 40u);
    if (!tmp) return NULL;
    snprintf(tmp, n + 40u, "%s.tmp.%p", path, tag);
    return tmp;
}

static bool snapshot_replace_file(const char *tmp, const char *path) {
#ifdef _WIN32
    return MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING |
                                  MOVEFILE_WRITE_THROUGH) != 0;
#else
    return rename(tmp, path) == 0;
#endif
}

snapshot_status_t snapshot_save(const s5l8900_t *m, const char *path) {
    if (!m || !path) return SNAP_ERR_IO;
    if (!snap_machine_valid(m)) return SNAP_ERR_CORRUPT;
    char *tmp = snapshot_temp_path(path, m);
    if (!tmp) return SNAP_ERR_NOMEM;
    FILE *f = fopen(tmp, "wb");
    if (!f) { free(tmp); return SNAP_ERR_IO; }
    snapshot_status_t st = snap_write(m, f, NULL, 0, NULL, false);
    if (fclose(f) != 0 && st == SNAP_OK) st = SNAP_ERR_IO;
    if (st == SNAP_OK && !snapshot_replace_file(tmp, path)) st = SNAP_ERR_IO;
    if (st != SNAP_OK) remove(tmp);
    free(tmp);
    return st;
}

snapshot_status_t snapshot_save_mem(const s5l8900_t *m,
                                    uint8_t **out, size_t *out_len) {
    if (!out || !out_len) return SNAP_ERR_IO;
    *out = NULL; *out_len = 0;
    if (!m) return SNAP_ERR_IO;
    if (!snap_machine_valid(m)) return SNAP_ERR_CORRUPT;
    s5l8900_t *mm = (s5l8900_t *)(uintptr_t)m;
    uint64_t payload_len = snap_measure(snap_payload, mm);
    uint64_t total = SNAP_HEADER_LEN + payload_len + 8u;
    if (total > (uint64_t)(size_t)-1) return SNAP_ERR_NOMEM;
    uint8_t *buf = malloc((size_t)total);
    if (!buf) return SNAP_ERR_NOMEM;
    size_t written = 0;
    snapshot_status_t st = snap_write(m, NULL, buf, (size_t)total, &written, false);
    if (st != SNAP_OK || written != (size_t)total) {
        free(buf);
        return st == SNAP_OK ? SNAP_ERR_CORRUPT : st;
    }
    *out = buf; *out_len = written;
    return SNAP_OK;
}

/* A capture copies all guest state at one instant. Only the checksum then
 * remains: it reads this owned copy, so the machine may resume immediately.
 * No incomplete bytes are exposed through snapshot_capture_data(). */
struct snapshot_capture {
    uint8_t *bytes;
    size_t size, cursor;
    uint64_t hash;
};

snapshot_status_t snapshot_capture_begin(const s5l8900_t *m,
                                          snapshot_capture_t **out) {
    if (!out) return SNAP_ERR_IO;
    *out = NULL;
    if (!m) return SNAP_ERR_IO;
    if (!snap_machine_valid(m)) return SNAP_ERR_CORRUPT;
    uint64_t length = snap_measure(snap_payload, (s5l8900_t *)(uintptr_t)m);
    uint64_t total = SNAP_HEADER_LEN + length + 8u;
    if (total > (uint64_t)(size_t)-1) return SNAP_ERR_NOMEM;
    snapshot_capture_t *capture = calloc(1, sizeof *capture);
    if (!capture) return SNAP_ERR_NOMEM;
    capture->bytes = malloc((size_t)total);
    if (!capture->bytes) { free(capture); return SNAP_ERR_NOMEM; }
    capture->size = (size_t)total;
    capture->cursor = SNAP_HEADER_LEN;
    capture->hash = FNV64_OFFSET;
    size_t written = 0;
    snapshot_status_t status = snap_write(m, NULL, capture->bytes,
                                          capture->size, &written, true);
    if (status != SNAP_OK || written != capture->size) {
        snapshot_capture_free(capture);
        return status == SNAP_OK ? SNAP_ERR_CORRUPT : status;
    }
    *out = capture;
    return SNAP_OK;
}

bool snapshot_capture_advance(snapshot_capture_t *capture, size_t max_bytes) {
    if (!capture) return false;
    size_t end = capture->size - 8u;
    size_t count = end - capture->cursor;
    if (count > max_bytes) count = max_bytes;
    sn_io_t io = {0};
    io.hash = capture->hash;
    sn_hash(&io, capture->bytes + capture->cursor, count);
    capture->hash = io.hash;
    capture->cursor += count;
    if (capture->cursor != end) return false;
    for (unsigned i = 0; i < 8u; i++)
        capture->bytes[end + i] = (uint8_t)(capture->hash >> (8u * i));
    return true;
}

const uint8_t *snapshot_capture_data(const snapshot_capture_t *capture,
                                      size_t *length) {
    if (length) *length = 0;
    if (!capture || capture->cursor != capture->size - 8u) return NULL;
    if (length) *length = capture->size;
    return capture->bytes;
}

void snapshot_capture_free(snapshot_capture_t *capture) {
    if (!capture) return;
    free(capture->bytes);
    free(capture);
}

/* ------------------------------------------------------------ load side --- */

/*
 * Loading has three stages. The first verifies framing and the legacy payload
 * hash without touching the machine. The second parses the complete stream
 * into a shallow probe in SN_VALIDATE mode: it checks section lengths,
 * geometry, scalar encodings, NOR metadata and the exact END position while
 * discarding all bulk contents. Only a stream that passes both stages reaches
 * the final applying pass.
 *
 * In-memory loads are therefore transactional for malformed input. A file
 * load can still be interrupted by a genuine read error (or external in-place
 * modification) during the final applying pass; callers must treat that
 * SNAP_ERR_IO/SNAP_ERR_TRUNCATED result as potentially partially applied.
 */
static snapshot_status_t snap_verify_stream(FILE *f, const uint8_t *in,
                                            size_t in_len, uint64_t *payload_len) {
    sn_io_t io = {0};
    io.mode = SN_LOAD; io.f = f; io.in = in; io.in_len = in_len;
    io.hash = FNV64_OFFSET;

    uint64_t plen = 0;
    snap_header(&io, &plen);
    if (io.err != SNAP_OK) return io.err;

    if (plen > UINT64_MAX - SNAP_HEADER_LEN - 8u) return SNAP_ERR_CORRUPT;
    uint64_t total = SNAP_HEADER_LEN + plen + 8u;

    /* A declared length that cannot fit is a truncated file, not a huge one. */
    if (!f && (uint64_t)in_len < total)
        return SNAP_ERR_TRUNCATED;

    uint8_t chunk[65536];
    uint64_t left = plen;
    while (left) {
        size_t want = left > sizeof chunk ? sizeof chunk : (size_t)left;
        sn_raw(&io, chunk, want);
        if (io.err != SNAP_OK) return io.err;
        left -= want;
    }
    uint64_t recorded = 0, computed = io.hash;
    uint64_t saved = io.hash;
    sn_u64(&io, &recorded);
    io.hash = saved;
    if (io.err != SNAP_OK) return io.err;
    if (recorded != computed) return SNAP_ERR_CHECKSUM;

    /* Trailing junk is a sign the file is not what it claims to be. */
    if (!f && (uint64_t)in_len != total)
        return SNAP_ERR_CORRUPT;
    if (f) {
        uint8_t extra;
        if (fread(&extra, 1, 1, f) == 1) return SNAP_ERR_CORRUPT;
        if (ferror(f)) return SNAP_ERR_IO;
    }
    *payload_len = plen;
    return SNAP_OK;
}

/* Parse every section and validate every constrained field into a shallow
 * machine copy. Large host-owned buffers are consumed with sn_discard(), so
 * this pass cannot modify the live machine. A checksummed but structurally
 * hostile stream is therefore rejected before the applying pass starts. */
static snapshot_status_t snap_validate_structure(const s5l8900_t *m, FILE *f,
                                                  const uint8_t *in,
                                                  size_t in_len) {
    s5l8900_t probe = *m;
    sn_io_t io = {0};
    io.mode = SN_VALIDATE; io.f = f; io.in = in; io.in_len = in_len;
    io.hash = FNV64_OFFSET;

    uint64_t plen = 0;
    snap_header(&io, &plen);
    if (io.err != SNAP_OK) return io.err;
    uint64_t body_start = io.pos;
    snap_payload(&io, &probe);
    if (io.err != SNAP_OK) return io.err;
    if (io.pos - body_start != plen) return SNAP_ERR_CORRUPT;
    return SNAP_OK;
}

static snapshot_status_t snap_apply(s5l8900_t *m, FILE *f,
                                    const uint8_t *in, size_t in_len) {
    sn_io_t io = {0};
    io.mode = SN_LOAD; io.f = f; io.in = in; io.in_len = in_len;
    io.hash = FNV64_OFFSET;

    uint64_t plen = 0;
    snap_header(&io, &plen);
    if (io.err != SNAP_OK) return io.err;
    uint64_t body_start = io.pos;
    snap_payload(&io, m);
    if (io.err != SNAP_OK) return io.err;
    if (io.pos - body_start != plen) return SNAP_ERR_CORRUPT;

    /* Host-owned wiring the visitors deliberately never touched, including
     * the privileged-SVC hook and its dedicated context. Re-pointing the CPU
     * at the live machine's bus is what makes restoring underneath an
     * interposed bus (bootkernel's tracing wrapper) work. */
    m->cpu.bus = &m->bus;
    m->bus.ctx = m;
    /* Old checkpoints legitimately contain zero before N82 lcd0 was wired,
     * or one on M68 because the older loader unconditionally selected N82's
     * route. The wiring validator accepts only those known M68 values and
     * forbids a Merlot read phase on its transmitter. Reassert the attached
     * board endpoint's route while retaining a genuine N82 mid-read phase. */
    const uint8_t spi0_route = m->spi[0].slaves[0].transfer ? 0u :
                              S5L_SPI0_LCD_CS;
    m->spi[0].cs = (uint8_t)(spi0_route |
        (m->spi[0].cs & S5L_SPI_LCD_READ_PENDING));
    /* Every device in this machine was just replaced, so nothing the previous
     * tick derived about its levels still holds. Setting rather than restoring
     * this is what keeps it out of the file format — see `level_dirty` in
     * soc.h — and it is the safe direction: it costs one full refresh. */
    m->level_dirty = true;
    m->batch_pending = 0u;
    /* A yield belongs to the host run call that observed the wait, not to the
     * restored guest instant. The callback, context and accumulated evidence
     * remain the live frontend's, exactly like the other host-only fields. */
    m->wfi_pace_yield = false;
    /* A restored guest instant has no relation to the host instant sampled
     * before restore. Preserve policy/evidence, but require a fresh anchor and
     * discard every transient conversion input. */
    m->active_clock_last_host_ns = 0u;
    m->active_clock_guest_ticks_since_sync = 0u;
    m->active_clock_fraction = 0u;
    m->active_clock_debt_ticks = 0u;
    m->active_clock_anchor_valid = false;
    return SNAP_OK;
}

snapshot_status_t snapshot_load(s5l8900_t *m, const char *path) {
    if (!m || !path) return SNAP_ERR_IO;
    if (!snap_machine_valid(m)) return SNAP_ERR_GEOMETRY;
    FILE *f = fopen(path, "rb");
    if (!f) return SNAP_ERR_IO;

    uint64_t plen = 0;
    snapshot_status_t st = snap_verify_stream(f, NULL, 0, &plen);
    if (st != SNAP_OK) { fclose(f); return st; }

    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return SNAP_ERR_IO; }
    st = snap_validate_structure(m, f, NULL, 0);
    if (st != SNAP_OK) { fclose(f); return st; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return SNAP_ERR_IO; }
    st = snap_apply(m, f, NULL, 0);
    fclose(f);
    return st;
}

snapshot_status_t snapshot_load_mem(s5l8900_t *m, const uint8_t *buf, size_t len) {
    if (!m || !buf) return SNAP_ERR_IO;
    if (!snap_machine_valid(m)) return SNAP_ERR_GEOMETRY;
    uint64_t plen = 0;
    snapshot_status_t st = snap_verify_stream(NULL, buf, len, &plen);
    if (st != SNAP_OK) return st;
    st = snap_validate_structure(m, NULL, buf, len);
    if (st != SNAP_OK) return st;
    return snap_apply(m, NULL, buf, len);
}

const char *snapshot_strerror(snapshot_status_t st) {
    switch (st) {
        case SNAP_OK:            return "ok";
        case SNAP_ERR_IO:        return "I/O error";
        case SNAP_ERR_MAGIC:     return "not an S5LBox snapshot (bad magic)";
        case SNAP_ERR_VERSION:   return "snapshot format version mismatch";
        case SNAP_ERR_TRUNCATED: return "snapshot file is truncated";
        case SNAP_ERR_CHECKSUM:  return "snapshot payload failed its checksum";
        case SNAP_ERR_CORRUPT:   return "snapshot is structurally corrupt";
        case SNAP_ERR_GEOMETRY:  return "snapshot machine layout does not match "
                                        "this machine (RAM size/base, NOR size, "
                                        "or the stub windows differ)";
        case SNAP_ERR_NOMEM:     return "out of memory";
        default:                 return "unknown snapshot error";
    }
}
