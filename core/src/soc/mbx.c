/*
 * S5LBox -- measured PowerVR MBX register block and its first decoded 2D copy.
 *
 * WHY THIS EXISTS. QuartzCore composites every pixel on the CPU in this VM,
 * and that software path is 40.2% of the instructions in a home-screen swipe.
 * It is not the path a real iPhone takes: CA::WindowServer::MBXServer DEFAULTS
 * to the MBX2D hardware path and only falls back when its IOKit connection is
 * missing. The connection is missing because com.apple.driver.AppleMBX never
 * finishes starting, and it never finishes starting because it waits on a
 * register this VM did not have. So the software rasteriser is not something
 * to high-level-emulate around; it is a symptom of an unmodelled device, and
 * tools/ios3_hle.c's rule is that a model gap gets modelled.
 *
 * WHAT THE DRIVER ACTUALLY WAITS FOR, taken from its own code rather than
 * guessed -- the same standard core/src/soc/power.c held itself to. The reset
 * routine at AppleMBX+0xb400 (0xc0783440) reads:
 *
 *     mov  r2, #1
 *     ldr  r1, [pc, #0x2c]        ; r1 = 0x1020, from the literal pool
 *     blx  ip                     ; write(obj, 0x1020, 1)
 *   loop:
 *     ldr  r0, [r4, #4]
 *     ldr  r1, [pc, #0x14]        ; r1 = 0x1020 again
 *     blx  r5                     ; r0 = read(obj, 0x1020)
 *     tst  r0, #0x10000           ; bit 16
 *     beq  loop                   ; spin while it is CLEAR
 *     mov  r3, #1
 *     strb r3, [r4, #1]           ; mark the device ready, return
 *
 * So: write 1 to 0x1020, then spin until bit 16 reads back set. That is a
 * reset request and its completion acknowledgement, and it is the whole of
 * what this file has evidence for.
 *
 * THE EVIDENCE THAT THE ADDRESS IS RIGHT is independent of the disassembly.
 * r225 ran with the nub matched and counted accesses per page: 0x3b000000
 * took 2 reads and 3 writes, and 0x3b001000 took 157,441,382 reads and 9
 * writes. Offset 0x1020 from the block base falls in the second page, which
 * is the page that spun. Two instruments, one conclusion.
 *
 * WHAT THIS STILL IS NOT. It is not a general PowerVR renderer. The later
 * command consumer implements one packet family whose producer, bit fields,
 * MMU tables and pixels have all been recovered from the shipped binaries and
 * exact cold snapshots. Premultiplied source-over, filtered unity sprites and
 * one uniformly minified form are decoded; unknown formats, perspective,
 * nonuniform scaling, blend equations and geometry are rejected without
 * completion. Everything else remains a fresh question to
 * answer the same way -- observe it, read its producer, then model it. A
 * driver that needs an unknown operation must stop loudly instead of quietly
 * proceeding on fabricated pixels.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#include "soc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * THE INSTRUMENT FOR THE NEXT STALL, and the file predicted needing it: "if the
 * driver later reads the region and depends on an effect that never happened,
 * it will stall or complain at a NEW address". Finding that address is a
 * counting problem, and the existing device log cannot count it -- it keeps the
 * FIRST S5L_DEVLOG accesses, and r225 measured 157 million reads on one page,
 * so the log is full of the reset spin before the driver has even started.
 *
 * A per-offset histogram answers it directly: whatever the driver is spinning
 * on is whatever has a count in the millions once the reset handshake is done.
 * The ring holds the last few accesses so the spin's SHAPE is visible too --
 * one offset read forever is a poll, a repeating group is a command sequence.
 *
 * This is deliberately file-static rather than a member of s5l_mbx_t. It is a
 * diagnostic and not machine state, so it must not enter a snapshot; keeping it
 * out of the struct also keeps sizeof(s5l_mbx_t), its SNAP_SIZE_GUARD and
 * SNAPSHOT_VERSION untouched, and a version bump would invalidate every
 * existing snapshot including one being captured while this was written.
 *
 * It counts and prints. It changes no value the guest can observe.
 */
#define MBX_SLOTS      (S5L_MBX_SIZE / 4u)
#define MBX_RING       64u

/* AppleMBX+0x1188 copies submitted words into this 64 KiB hardware ring. */
#define MBX_2D_COPY_BODY_WORDS 10u
#define MBX_2D_BLEND_BODY_WORDS 12u
#define MBX_2D_COPY_WORDS      16u
#define MBX_2D_BLEND_WORDS     18u
#define MBX_2D_END             0x70000000u
#define MBX_2D_COMMAND_HEADER  S5L_MBX_2D_COMMAND_HEADER
#define MBX_2D_SUBMIT          S5L_MBX_2D_SUBMIT
#define MBX_2D_BLEND_TAG       0x20000004u
#define MBX_2D_BLEND_EQUATION  0x095ff000u
#define MBX_2D_OPAQUE_GLOBAL_FACTORS 0x0d500000u
#define MBX_2D_GLOBAL_ALPHA_MASK     0x000ff000u
#define MBX_2D_BLEND_MODE      0x8002ccccu
#define MBX_2D_FILL_MODE       0x8000f0f0u

/*
 * The command-copy helper advances a software cursor, but the actual submit
 * boundary is a fixed write of 0xf0000000 to ring+0 by AppleMBX+0x1f58. r372
 * caught that same fixed store after packets beginning at +0 and +0x40, so
 * the earlier "rewrite this packet's header" interpretation was accidental:
 * packet zero merely aliases the doorbell.
 *
 * Between those writes the device has to remember where the copied command
 * began. Keep that state in the otherwise unread backing slot for STATUS:
 * s5l_mbx_read() serves the independent `status` field, so this marker is not
 * guest-visible, while snap_mbx() already serialises every reg[] word. That
 * preserves a snapshot taken between the command copy and the submit store
 * without changing the v32 format or stealing a real readable register.
 * r376 then caught two real multi-command submits: eight blended packets and
 * four simple copies were each followed by one fixed doorbell. Preserve both
 * the first ring word and the number of copied heads. A zero count is reserved
 * as the fail-closed overflow marker after 255 heads; the measured batches are
 * far smaller, and pretending to know where a larger one ends would be worse
 * than rejecting it.
 */
#define MBX_2D_PENDING_MAGIC       0xb5c00000u
#define MBX_2D_PENDING_MAGIC_M     0xffc00000u
#define MBX_2D_PENDING_COUNT_SHIFT 14u
#define MBX_2D_PENDING_COUNT_M     0x003fc000u
#define MBX_2D_PENDING_WORD_M      0x00003fffu
/* v32 snapshots made before r376 used a one-head/multiple marker. Decode a
 * saved single head exactly; an old multiple marker has lost the true count
 * and therefore remains a rejection rather than being guessed. */
#define MBX_2D_PENDING_V1_MAGIC    0x4d420000u
#define MBX_2D_PENDING_V1_MAGIC_M  0xffff0000u
#define MBX_2D_PENDING_V1_MULTI    0x00004000u

/* The first proved packet is a 320-pixel BGRA8 surface. There is no decoded
 * destination-stride field in its post-relocation packet, so widening this is
 * not justified until another captured packet supplies that missing fact. */
#define MBX_2D_BGRA_STRIDE     0x500u
#define MBX_2D_WIDTH           320u
#define MBX_2D_HEIGHT          480u
#define MBX_2D_SURFACE_BYTES   (MBX_2D_BGRA_STRIDE * MBX_2D_HEIGHT)
#define MBX_2D_SOURCE_FORMAT_M  0xffff8000u
#define MBX_2D_SOURCE_BGRA8     0x94060000u
#define MBX_2D_SOURCE_ARGB1555  0x94048000u

/* The first tiled render captured in r369. The source is an 8x128 BGRA8
 * texture with a 32-byte stride; the object samples its first column over a
 * 320x96 destination rectangle. Texture coordinates and the tile/object
 * stream below independently encode the same dimensions. */
#define MBX_3D_WIDTH           320u
#define MBX_3D_TOP             20u
#define MBX_3D_HEIGHT          96u
#define MBX_3D_SOURCE_STRIDE   0x20u
#define MBX_3D_TARGET_STRIDE   0x500u
#define MBX_3D_ADDRESS_MASK    0x0003ffffu

/* A FIFO has no readable backing store, but a checkpoint can stop between any
 * two of its writes. Preserve the accepted words in the TA object database,
 * which is the GART allocation the hardware would populate and which guest
 * RAM snapshots already carry. Only the cursor needs private device state.
 * CORE_ID/REVISION reads bypass reg[] and return S5L_MBX_REVISION_ID, so its
 * otherwise unreachable backing slot can hold that cursor just as STATUS's
 * unreachable slot holds the pending 2D batch marker above. */
#define MBX_TA_CAPTURE_MAGIC       0x54400000u
#define MBX_TA_CAPTURE_MAGIC_M     0xfff00000u
#define MBX_TA_CAPTURE_FAILED      0x54500000u
#define MBX_TA_CAPTURE_COUNT_M     0x0000ffffu
#define MBX_TA_CAPTURE_MAX_WORDS   16384u

/* Later _mbx3DCtxQuadCopyPerspective records and live object streams recover
 * the padlock, `Searching...`, and battery status sprites, plus clipped
 * padlock and battery redraws. Their texture-control words encode different
 * padded dimensions/strides. Bit 18 is control rather than GPU address bit 18, so
 * all forms use the same exact 18-bit address field recovered above. */

static uint64_t mbx_rd[MBX_SLOTS];
static uint64_t mbx_wr[MBX_SLOTS];
static uint32_t mbx_last_val[MBX_SLOTS];
static uint32_t mbx_ring_off[MBX_RING];
static uint32_t mbx_ring_val[MBX_RING];
static uint8_t  mbx_ring_wr[MBX_RING];
static uint64_t mbx_ring_n;
static int      mbx_trace_state;   /* 0 unknown, 1 on, -1 off */
static int      mbx_fifo_trace_state;
static uint64_t mbx_2d_candidates;
static uint64_t mbx_2d_completed;
static uint64_t mbx_2d_rejected;
static uint64_t mbx_2d_bytes;
static uint64_t mbx_3d_candidates;
static uint64_t mbx_3d_completed;
static uint64_t mbx_3d_rejected;
static uint64_t mbx_3d_pixels;
static uint64_t mbx_3d_fifo_words;
static uint64_t mbx_3d_fifo_scene_words;

static void mbx_counter_add(uint64_t *counter, uint64_t amount) {
    if (!counter) return;
    *counter = *counter > UINT64_MAX - amount
        ? UINT64_MAX : *counter + amount;
}

static void mbx_trace_dump(void);

static bool mbx_trace_enabled(void) {
    if (mbx_trace_state == 0) {
        const char *e = getenv("S5LBOX_MBX_TRACE");
        mbx_trace_state = (e && *e && *e != '0') ? 1 : -1;
        if (mbx_trace_state == 1) atexit(mbx_trace_dump);
    }
    return mbx_trace_state == 1;
}

static bool mbx_fifo_trace_enabled(void) {
    if (mbx_fifo_trace_state == 0) {
        const char *e = getenv("S5LBOX_MBX_FIFO_TRACE");
        mbx_fifo_trace_state = (e && *e && *e != '0') ? 1 : -1;
    }
    return mbx_fifo_trace_state == 1;
}

static void mbx_trace_dump(void) {
    uint64_t total_r = 0, total_w = 0;
    unsigned i;

    fprintf(stderr, "\n=== MBX access histogram ===\n");
    for (i = 0; i < MBX_SLOTS; i++) {
        if (!mbx_rd[i] && !mbx_wr[i]) continue;
        total_r += mbx_rd[i];
        total_w += mbx_wr[i];
        fprintf(stderr, "  off 0x%04x  reads %-14llu writes %-8llu last-write 0x%08x\n",
                i * 4u, (unsigned long long)mbx_rd[i],
                (unsigned long long)mbx_wr[i], mbx_last_val[i]);
    }
    fprintf(stderr, "  TOTAL reads %llu writes %llu\n",
            (unsigned long long)total_r, (unsigned long long)total_w);
    fprintf(stderr, "  2D packets candidates %llu completed %llu rejected %llu"
                    " bytes-committed %llu\n",
            (unsigned long long)mbx_2d_candidates,
            (unsigned long long)mbx_2d_completed,
            (unsigned long long)mbx_2d_rejected,
            (unsigned long long)mbx_2d_bytes);
    fprintf(stderr, "  3D renders candidates %llu completed %llu rejected %llu"
                    " pixels-blended %llu\n",
            (unsigned long long)mbx_3d_candidates,
            (unsigned long long)mbx_3d_completed,
            (unsigned long long)mbx_3d_rejected,
            (unsigned long long)mbx_3d_pixels);

    fprintf(stderr, "=== MBX last %u accesses (oldest first) ===\n",
            (unsigned)(mbx_ring_n < MBX_RING ? mbx_ring_n : MBX_RING));
    {
        uint64_t start = mbx_ring_n > MBX_RING ? mbx_ring_n - MBX_RING : 0;
        uint64_t k;
        for (k = start; k < mbx_ring_n; k++) {
            unsigned s = (unsigned)(k % MBX_RING);
            fprintf(stderr, "  %s off 0x%04x val 0x%08x\n",
                    mbx_ring_wr[s] ? "W" : "R", mbx_ring_off[s],
                    mbx_ring_val[s]);
        }
    }
    fflush(stderr);
}

static void mbx_trace(uint32_t off, uint32_t val, bool is_write) {
    unsigned s;

    if (!mbx_trace_enabled()) return;

    if (is_write) { mbx_wr[off / 4u]++; mbx_last_val[off / 4u] = val; }
    else            mbx_rd[off / 4u]++;

    s = (unsigned)(mbx_ring_n % MBX_RING);
    mbx_ring_off[s] = off;
    mbx_ring_val[s] = val;
    mbx_ring_wr[s]  = is_write ? 1u : 0u;
    mbx_ring_n++;
}

static uint32_t mbx_load_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t mbx_load_le16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint8_t mbx_expand_5_to_8(uint32_t component) {
    component &= 0x1fu;
    return (uint8_t)((component << 3) | (component >> 2));
}

static uint32_t mbx_edram_word(const s5l_mbx_t *m, uint32_t aperture_off) {
    uint32_t o = aperture_off - S5L_MBX_SIZE;
    return mbx_load_le32(m->edram + o);
}

static uint32_t *mbx_2d_pending_slot(s5l_mbx_t *m) {
    return &m->reg[S5L_MBX_STATUS / 4u];
}

static bool mbx_2d_pending_get(const s5l_mbx_t *m, uint32_t *packet_off,
                               uint32_t *command_count) {
    uint32_t state = m->reg[S5L_MBX_STATUS / 4u];
    bool current =
        (state & MBX_2D_PENDING_MAGIC_M) == MBX_2D_PENDING_MAGIC;
    bool v1 = (state & MBX_2D_PENDING_V1_MAGIC_M) ==
              MBX_2D_PENDING_V1_MAGIC;
    if (!current && !v1) return false;
    if (packet_off)
        *packet_off = S5L_MBX_2D_RING_BASE +
                      (state & MBX_2D_PENDING_WORD_M) * 4u;
    if (command_count) {
        if (current)
            *command_count = (state & MBX_2D_PENDING_COUNT_M) >>
                             MBX_2D_PENDING_COUNT_SHIFT;
        else
            *command_count = (state & MBX_2D_PENDING_V1_MULTI) ? 0u : 1u;
    }
    return true;
}

static void mbx_2d_pending_note(s5l_mbx_t *m, uint32_t packet_off) {
    uint32_t old, count;
    if (mbx_2d_pending_get(m, &old, &count)) {
        if (count == 0u) return; /* already overflowed */
        if ((*mbx_2d_pending_slot(m) & MBX_2D_PENDING_MAGIC_M) !=
            MBX_2D_PENDING_MAGIC) {
            uint32_t words = (old - S5L_MBX_2D_RING_BASE) / 4u;
            *mbx_2d_pending_slot(m) = MBX_2D_PENDING_MAGIC |
                (2u << MBX_2D_PENDING_COUNT_SHIFT) | words;
            return;
        }
        if (count == 255u) {
            /* Count zero with a valid magic is the explicit overflow state. */
            *mbx_2d_pending_slot(m) &= ~MBX_2D_PENDING_COUNT_M;
            return;
        }
        count++;
        *mbx_2d_pending_slot(m) =
            (*mbx_2d_pending_slot(m) & ~MBX_2D_PENDING_COUNT_M) |
            (count << MBX_2D_PENDING_COUNT_SHIFT);
        return;
    }
    uint32_t words = (packet_off - S5L_MBX_2D_RING_BASE) / 4u;
    *mbx_2d_pending_slot(m) = MBX_2D_PENDING_MAGIC |
                              (1u << MBX_2D_PENDING_COUNT_SHIFT) | words;
}

static void mbx_2d_pending_clear(s5l_mbx_t *m) {
    *mbx_2d_pending_slot(m) = 0u;
}

/*
 * Translate one GPU VA through the exact GART layout written by
 * AppleMBXMMU::map at 0xc0783570.
 *
 *   chunk    = gpu_va >> 22                         (one root per 4 MiB)
 *   pte      = root[chunk][(gpu_va >> 12) & 0x3ff]
 *   physical = pte + (gpu_va & 0xfff)
 *
 * The eight root-page physical addresses are registers 0x1000..0x101c. The
 * kext stores raw, page-aligned physical addresses in both roots and PTEs; no
 * flag bits were observed, so accepting them would be guessing. host_ram is
 * also the boundary that keeps a malformed packet out of MMIO.
 */
static bool mbx_gart_span(const s5l_mbx_t *m, const arm_bus_t *bus,
                          uint32_t gpu_va, uint32_t *physical,
                          uint32_t *available, const char **why) {
    uint32_t chunk = gpu_va >> 22;
    if (!bus || !bus->host_ram || chunk >= 8u) {
        if (why) *why = "GPU VA is outside the observed eight-root GART";
        return false;
    }

    uint32_t root = m->reg[(0x1000u / 4u) + chunk];
    uint32_t pte_off = ((gpu_va >> 12) & 0x3ffu) * 4u;
    if (!root || (root & 0xfffu) || root > UINT32_MAX - pte_off) {
        if (why) *why = "GART root is zero, unaligned, or wraps";
        return false;
    }

    const uint8_t *entry = bus->host_ram(bus->ctx, root + pte_off, 4u);
    if (!entry) {
        if (why) *why = "GART PTE is outside plain guest DRAM";
        return false;
    }
    uint32_t pte = mbx_load_le32(entry);
    if (!pte || (pte & 0xfffu)) {
        if (why) *why = "GART PTE is zero or not a raw page base";
        return false;
    }

    uint32_t in_page = gpu_va & 0xfffu;
    uint32_t pa = pte + in_page;
    uint32_t span = 0x1000u - in_page;
    if (!bus->host_ram(bus->ctx, pa, span)) {
        if (why) *why = "GART target is outside plain guest DRAM";
        return false;
    }
    *physical = pa;
    *available = span;
    return true;
}

static bool mbx_gart_validate(const s5l_mbx_t *m, const arm_bus_t *bus,
                              uint32_t gpu_va, uint32_t len,
                              const char **why) {
    while (len) {
        uint32_t pa, span;
        if (!mbx_gart_span(m, bus, gpu_va, &pa, &span, why)) return false;
        if (span > len) span = len;
        gpu_va += span;
        len -= span;
    }
    return true;
}

static bool mbx_gart_read(const s5l_mbx_t *m, const arm_bus_t *bus,
                          uint32_t gpu_va, uint8_t *dst, uint32_t len,
                          const char **why) {
    while (len) {
        uint32_t pa, span;
        if (!mbx_gart_span(m, bus, gpu_va, &pa, &span, why)) return false;
        if (span > len) span = len;
        const uint8_t *src = bus->host_ram(bus->ctx, pa, span);
        if (!src) {
            if (why) *why = "source mapping stopped being plain guest DRAM";
            return false;
        }
        memcpy(dst, src, span);
        dst += span;
        gpu_va += span;
        len -= span;
    }
    return true;
}

/* host_ram_write is deliberately separate from host_ram: it is the frontend's
 * explicit, revocable promise that this plain-RAM range has no write observer
 * to bypass.  The stock machine bus grants it, while bootkernel's scanout
 * interposer leaves it NULL so every word remains visible.  Consume that
 * existing consent a translated page span at a time; a range-specific refusal
 * retains the exact observed bus path. */
static bool mbx_gart_write(const s5l_mbx_t *m, const arm_bus_t *bus,
                           uint32_t gpu_va, const uint8_t *src, uint32_t len,
                           const char **why) {
    if (!bus || !bus->write32 || (gpu_va & 3u) || (len & 3u)) {
        if (why) *why = "destination is not a word-addressable bus range";
        return false;
    }
    while (len) {
        uint32_t pa, span;
        if (!mbx_gart_span(m, bus, gpu_va, &pa, &span, why)) return false;
        if (span > len) span = len;
        if ((pa & 3u) || (span & 3u)) {
            if (why) *why = "translated destination is not word aligned";
            return false;
        }
        uint8_t *direct = bus->host_ram_write
            ? bus->host_ram_write(bus->ctx, pa, span) : NULL;
        if (direct) {
            memcpy(direct, src, span);
        } else {
            for (uint32_t i = 0; i < span; i += 4u)
                bus->write32(bus->ctx, pa + i, mbx_load_le32(src + i));
        }
        src += span;
        gpu_va += span;
        len -= span;
    }
    return true;
}

static uint32_t *mbx_ta_capture_slot(s5l_mbx_t *m) {
    return &m->reg[S5L_MBX_REVISION / 4u];
}

bool s5l_mbx_stage_ta_write(s5l_mbx_t *m, const arm_bus_t *bus,
                            uint32_t off, uint32_t val) {
    if (!m) return false;

    if (off == S5L_MBX_TA_START && val == 1u) {
        *mbx_ta_capture_slot(m) = MBX_TA_CAPTURE_MAGIC;
        return true;
    }
    if (off != S5L_MBX_3D_DATA_FIFO ||
        m->reg[S5L_MBX_TA_START / 4u] != 1u)
        return false;

    uint32_t state = *mbx_ta_capture_slot(m);
    if ((state & MBX_TA_CAPTURE_MAGIC_M) != MBX_TA_CAPTURE_MAGIC)
        return false;
    uint32_t count = state & MBX_TA_CAPTURE_COUNT_M;
    uint32_t object = m->reg[S5L_MBX_TA_OBJECT_DATABASE / 4u];
    uint8_t bytes[4] = {
        (uint8_t)val, (uint8_t)(val >> 8),
        (uint8_t)(val >> 16), (uint8_t)(val >> 24)
    };
    const char *why = NULL;
    if (count >= MBX_TA_CAPTURE_MAX_WORDS || !object ||
        object > UINT32_MAX - count * 4u ||
        !mbx_gart_write(m, bus, object + count * 4u,
                        bytes, sizeof bytes, &why)) {
        *mbx_ta_capture_slot(m) = MBX_TA_CAPTURE_FAILED;
        if (mbx_trace_state == 1) {
            fprintf(stderr, "MBX3D TA capture failed at word %u: %s\n",
                    count, why ? why : "invalid object-database span");
            fflush(stderr);
        }
        return false;
    }
    *mbx_ta_capture_slot(m) = MBX_TA_CAPTURE_MAGIC | (count + 1u);
    return true;
}

static uint32_t mbx_source_over_clamped(uint32_t dst, uint32_t src);
static uint32_t mbx_modulate_vertex_alpha(uint32_t src, uint32_t alpha);

struct mbx_2d_job {
    uint32_t target;
    uint32_t dst_x, dst_y;
    uint32_t width, height;
    uint32_t row_bytes, total;
    uint8_t *pixels;
};

struct mbx_2d_batch_state {
    uint32_t target;
    struct mbx_2d_job *jobs;
    uint32_t staged;
};

static void mbx_2d_job_dispose(struct mbx_2d_job *job) {
    if (!job) return;
    free(job->pixels);
    memset(job, 0, sizeof *job);
}

static bool mbx_2d_ranges_overlap(uint32_t a, uint32_t a_len,
                                  uint32_t b, uint32_t b_len) {
    uint64_t a_end = (uint64_t)a + a_len;
    uint64_t b_end = (uint64_t)b + b_len;
    return (uint64_t)a < b_end && (uint64_t)b < a_end;
}

static bool mbx_2d_batch_source_ok(uint32_t source, uint32_t len,
                                   uint32_t target, const char **why) {
    if (mbx_2d_ranges_overlap(source, len, target, MBX_2D_SURFACE_BYTES)) {
        /* No captured batch reads its own render target. Rejecting that case
         * avoids silently choosing between pre-batch and sequential reads. */
        if (why) *why = "batched source aliases the destination surface";
        return false;
    }
    return true;
}

static bool mbx_2d_batch_target_ok(const struct mbx_2d_batch_state *batch,
                                   uint32_t target, const char **why) {
    if (batch && target != batch->target) {
        if (why) *why = "batched commands do not share one destination surface";
        return false;
    }
    return true;
}

/* Reconstruct the destination state visible to the next staged blend without
 * copying the complete 320x480 render target. Guest RAM still contains the
 * pre-submit image until every command has validated, so read just this
 * rectangle and replay intersecting prior jobs over it in command order. Each
 * prior job stores its post-command pixels; later intersections consequently
 * replace earlier ones exactly as the full-surface shadow did. */
static bool mbx_2d_stage_destination(
        const s5l_mbx_t *m, const arm_bus_t *bus,
        uint32_t target, uint32_t dst_x, uint32_t dst_y,
        uint32_t width, uint32_t height, uint8_t *pixels,
        const struct mbx_2d_batch_state *batch, const char **why) {
    uint32_t row_bytes = width * 4u;
    for (uint32_t row = 0; row < height; row++) {
        uint32_t dst = target + (dst_y + row) * MBX_2D_BGRA_STRIDE +
                       dst_x * 4u;
        if (!mbx_gart_read(m, bus, dst, pixels + row * row_bytes,
                           row_bytes, why))
            return false;
    }
    if (!batch) return true;

    for (uint32_t i = 0; i < batch->staged; i++) {
        const struct mbx_2d_job *prior = &batch->jobs[i];
        uint32_t left = prior->dst_x > dst_x ? prior->dst_x : dst_x;
        uint32_t top = prior->dst_y > dst_y ? prior->dst_y : dst_y;
        uint32_t right = prior->dst_x + prior->width < dst_x + width
            ? prior->dst_x + prior->width : dst_x + width;
        uint32_t bottom = prior->dst_y + prior->height < dst_y + height
            ? prior->dst_y + prior->height : dst_y + height;
        if (left >= right || top >= bottom) continue;

        uint32_t copy_bytes = (right - left) * 4u;
        for (uint32_t y = top; y < bottom; y++) {
            uint8_t *destination = pixels +
                (y - dst_y) * row_bytes + (left - dst_x) * 4u;
            const uint8_t *source = prior->pixels +
                (y - prior->dst_y) * prior->row_bytes +
                (left - prior->dst_x) * 4u;
            memcpy(destination, source, copy_bytes);
        }
    }
    return true;
}

static bool mbx_2d_job_commit(const s5l_mbx_t *m, const arm_bus_t *bus,
                              const struct mbx_2d_job *job,
                              const char **why) {
    for (uint32_t row = 0; row < job->height; row++) {
        uint32_t dst = job->target +
                       (job->dst_y + row) * MBX_2D_BGRA_STRIDE +
                       job->dst_x * 4u;
        if (!mbx_gart_write(m, bus, dst,
                            job->pixels + row * job->row_bytes,
                            job->row_bytes, why))
            return false;
    }
    return true;
}

static bool mbx_stage_simple_copy(s5l_mbx_t *m, const arm_bus_t *bus,
                                  uint32_t packet_off,
                                  const struct mbx_2d_batch_state *batch,
                                  struct mbx_2d_job *job,
                                  const char **why) {
    memset(job, 0, sizeof *job);
    uint32_t w[MBX_2D_COPY_WORDS];
    for (unsigned i = 0; i < MBX_2D_COPY_WORDS; i++)
        w[i] = mbx_edram_word(m, packet_off + i * 4u);

    /* These constants and masks are the stores/literal pool in
     * _pack2DCtxBlitCopy at 0x30e1c2ac..0x30e1c5a4. QuartzCore's
     * RenderMBX2D::map_surface at 0x3123a514..0x3123a5cc maps both IOSurface
     * '1555' and '555L' to source-format bits 0x48000; '565L' maps to the
     * distinct 0x50000 format and remains unsupported. _pack2DCtxBlitColor at
     * 0x30e1aecc..0x30e1aeec independently proves the 0x48000 bit layout:
     * A at 15, R at 14..10, G at 9..5 and B at 4..0. The retained Wallpaper
     * batch uses that exact family with descriptor 0x94048280.
     *
     * Accept coordinates in the measured unity-scale simple-copy mode, and
     * only the two source formats whose layout is now decoded. Do not treat
     * every 16-bit descriptor as interchangeable.
     */
    const uint32_t source_format = w[2] & MBX_2D_SOURCE_FORMAT_M;
    const bool source_bgra8 = source_format == MBX_2D_SOURCE_BGRA8;
    const bool source_argb1555 = source_format == MBX_2D_SOURCE_ARGB1555;
    if ((w[0] != MBX_2D_COMMAND_HEADER && w[0] != MBX_2D_SUBMIT) ||
        (!source_bgra8 && !source_argb1555) ||
        (w[4] & 0xf8002000u) != 0x30000000u ||
        w[5] != 0x60800200u || w[6] != 0x8000ccccu ||
        w[7] != 0xffffffffu ||
        (w[8] & ~0x1fff1fffu) || (w[9] & ~0x1fff1fffu)) {
        if (why)
            *why = "packet is not a decoded BGRA8 or ARGB1555 unity simple-copy form";
        return false;
    }
    if (w[MBX_2D_COPY_BODY_WORDS] != MBX_2D_END) {
        if (why) *why = "packet terminators are incomplete";
        return false;
    }

    uint32_t src_x = (w[4] >> 14) & 0x1fffu;
    uint32_t src_y = w[4] & 0x1fffu;
    uint32_t dst_x = (w[8] >> 16) & 0x1fffu;
    uint32_t dst_y = w[8] & 0x1fffu;
    uint32_t dst_x1 = (w[9] >> 16) & 0x1fffu;
    uint32_t dst_y1 = w[9] & 0x1fffu;
    uint32_t source_stride = w[2] & 0x7fffu;
    const uint32_t source_pixel_bytes = source_bgra8 ? 4u : 2u;
    if (!source_stride || source_stride % source_pixel_bytes != 0u ||
        source_stride > MBX_2D_WIDTH * source_pixel_bytes) {
        if (why)
            *why = "copy source stride is not aligned and bounded for its decoded format";
        return false;
    }
    if (dst_x1 <= dst_x || dst_y1 <= dst_y) {
        if (why) *why = "destination rectangle is empty or reversed";
        return false;
    }
    uint32_t width = dst_x1 - dst_x;
    uint32_t height = dst_y1 - dst_y;
    uint32_t source_pixels_per_row = source_stride / source_pixel_bytes;
    if (dst_x1 > MBX_2D_WIDTH || dst_y1 > MBX_2D_HEIGHT ||
        src_x > source_pixels_per_row ||
        width > source_pixels_per_row - src_x ||
        src_y > MBX_2D_HEIGHT || height > MBX_2D_HEIGHT - src_y) {
        if (why) *why =
            "copy rectangle exceeds its decoded source or 320x480 destination";
        return false;
    }

    uint32_t row_bytes = width * 4u;
    uint32_t source_row_bytes = width * source_pixel_bytes;
    uint32_t total = row_bytes * height;
    if ((w[1] & 3u) || (w[3] & 3u)) {
        if (why) *why = "source or destination GPU base is not word aligned";
        return false;
    }
    if (!mbx_2d_batch_target_ok(batch, w[1], why)) return false;

    /* Validate every source and destination before changing one byte. A bad
     * late PTE must not leave a half-rendered frame and then withhold
     * completion. Batch staging also rejects unmeasured target-as-source
     * semantics before it constructs any private results. */
    for (uint32_t row = 0; row < height; row++) {
        uint64_t src64 = (uint64_t)w[3] +
                         (uint64_t)(src_y + row) * source_stride +
                         (uint64_t)src_x * source_pixel_bytes;
        uint64_t dst64 = (uint64_t)w[1] +
                         (uint64_t)(dst_y + row) * MBX_2D_BGRA_STRIDE +
                         (uint64_t)dst_x * 4u;
        if (src64 + source_row_bytes > (uint64_t)UINT32_MAX + 1u ||
            dst64 + row_bytes > (uint64_t)UINT32_MAX + 1u ||
            (batch && !mbx_2d_batch_source_ok(
                (uint32_t)src64, source_row_bytes, batch->target, why)) ||
            !mbx_gart_validate(m, bus, (uint32_t)src64,
                               source_row_bytes, why) ||
            !mbx_gart_validate(m, bus, (uint32_t)dst64, row_bytes, why))
            return false;
    }

    uint8_t *pixels = malloc(total);
    if (!pixels) {
        if (why) *why = "host allocation for staged pixels failed";
        return false;
    }
    bool ok = true;
    if (source_bgra8) {
        for (uint32_t row = 0; row < height && ok; row++) {
            uint32_t src = w[3] + (src_y + row) * source_stride +
                           src_x * source_pixel_bytes;
            ok = mbx_gart_read(m, bus, src, pixels + row * row_bytes,
                               source_row_bytes, why);
        }
    } else {
        uint8_t *packed = malloc(source_row_bytes);
        if (!packed) {
            free(pixels);
            if (why) *why = "host allocation for packed ARGB1555 row failed";
            return false;
        }
        for (uint32_t row = 0; row < height && ok; row++) {
            uint32_t src = w[3] + (src_y + row) * source_stride +
                           src_x * source_pixel_bytes;
            ok = mbx_gart_read(m, bus, src, packed,
                               source_row_bytes, why);
            for (uint32_t x = 0; x < width && ok; x++) {
                uint16_t argb = mbx_load_le16(packed + x * 2u);
                uint8_t *bgra = pixels + row * row_bytes + x * 4u;
                bgra[0] = mbx_expand_5_to_8(argb);
                bgra[1] = mbx_expand_5_to_8(argb >> 5);
                bgra[2] = mbx_expand_5_to_8(argb >> 10);
                bgra[3] = (argb & 0x8000u) ? 0xffu : 0u;
            }
        }
        free(packed);
    }
    if (!ok) {
        free(pixels);
        return false;
    }
    job->target = w[1];
    job->dst_x = dst_x;
    job->dst_y = dst_y;
    job->width = width;
    job->height = height;
    job->row_bytes = row_bytes;
    job->total = total;
    job->pixels = pixels;
    return true;
}

static bool mbx_stage_rect_fill(s5l_mbx_t *m,
                                const arm_bus_t *bus,
                                uint32_t packet_off,
                                const struct mbx_2d_batch_state *batch,
                                struct mbx_2d_job *job,
                                const char **why) {
    memset(job, 0, sizeof *job);
    uint32_t w[MBX_2D_COPY_WORDS];
    for (unsigned i = 0; i < MBX_2D_COPY_WORDS; i++)
        w[i] = mbx_edram_word(m, packet_off + i * 4u);

    /* _pack2DCtxBlitColor at 0x30e1ae1c..0x30e1b0b4 converts its colour
     * argument to the destination format, stores that complete value in word
     * 7, then packs (x, y) and (x + width, y + height) into words 8 and 9.
     * r408 measured black bounded fills. The Safari Tabs recovery fixture then
     * captured the identical BGRA8 command with opaque white and 0xe0 gray.
     * A retained Settings transition uses zero as a raw transparent surface
     * clear before two copies reconstruct the visible rows. Accept the complete
     * converted BGRA8 word while keeping the raster operation, surface format
     * and bounds closed to measured forms. */
    if ((w[0] != MBX_2D_COMMAND_HEADER && w[0] != MBX_2D_SUBMIT) ||
        w[2] != 0x94060500u || w[3] != 0u || w[4] != 0x30000000u ||
        w[5] != 0x60800200u || w[6] != MBX_2D_FILL_MODE ||
        (w[8] & ~0x1fff1fffu) || (w[9] & ~0x1fff1fffu)) {
        if (why) *why = "packet is not the decoded BGRA8 fill form";
        return false;
    }
    if (w[MBX_2D_COPY_BODY_WORDS] != MBX_2D_END) {
        if (why) *why = "solid-fill packet terminators are incomplete";
        return false;
    }
    uint32_t left = (w[8] >> 16) & 0x1fffu;
    uint32_t top = w[8] & 0x1fffu;
    uint32_t right = (w[9] >> 16) & 0x1fffu;
    uint32_t bottom = w[9] & 0x1fffu;
    if (right <= left || bottom <= top || right > MBX_2D_WIDTH ||
        bottom > MBX_2D_HEIGHT) {
        if (why) *why = "solid-fill rectangle is empty, reversed, or outside 320x480";
        return false;
    }
    if ((w[1] & 3u) ||
        (uint64_t)w[1] + MBX_2D_SURFACE_BYTES >
            (uint64_t)UINT32_MAX + 1u ||
        !mbx_2d_batch_target_ok(batch, w[1], why)) {
        if (why) *why = "solid-fill target is unaligned or differs within a batch";
        return false;
    }

    const uint32_t width = right - left;
    const uint32_t height = bottom - top;
    const uint32_t row_bytes = width * 4u;
    const uint32_t total = row_bytes * height;
    for (uint32_t row = 0; row < height; row++) {
        uint32_t dst = w[1] + (top + row) * MBX_2D_BGRA_STRIDE + left * 4u;
        if (!mbx_gart_validate(m, bus, dst, row_bytes, why))
            return false;
    }

    uint8_t *pixels = malloc(total);
    if (!pixels) {
        if (why) *why = "host allocation for staged solid fill failed";
        return false;
    }
    const uint32_t colour = w[7];
    for (uint32_t i = 0; i < total; i += 4u) {
        pixels[i] = (uint8_t)colour;
        pixels[i + 1u] = (uint8_t)(colour >> 8);
        pixels[i + 2u] = (uint8_t)(colour >> 16);
        pixels[i + 3u] = (uint8_t)(colour >> 24);
    }
    job->target = w[1];
    job->dst_x = left;
    job->dst_y = top;
    job->width = width;
    job->height = height;
    job->row_bytes = row_bytes;
    job->total = total;
    job->pixels = pixels;
    return true;
}

static bool mbx_stage_premultiplied_copy(s5l_mbx_t *m,
                                         const arm_bus_t *bus,
                                         uint32_t packet_off,
                                         const struct mbx_2d_batch_state *batch,
                                         struct mbx_2d_job *job,
                                         const char **why) {
    memset(job, 0, sizeof *job);
    uint32_t w[MBX_2D_BLEND_WORDS];
    for (unsigned i = 0; i < MBX_2D_BLEND_WORDS; i++)
        w[i] = mbx_edram_word(m, packet_off + i * 4u);

    /* _pack2DCtxBlitCopy at 0x30e1c3c4 inserts the two extra words only when
     * ctx+0x35 enables blending. The retained clock/date packets use the
     * 0x095 factors at global alpha 255. QuartzCore's other simple branch in
     * RenderMBX2D::set_tex_blend_mode (0x3123a9c8) is selected for a texture
     * it classifies as opaque and passes fixed 0x0d5 factors plus a variable
     * byte at bits 12..19. An earlier retained source happened to store 0xff
     * in every unused alpha byte. The Safari tab transition instead retains a
     * 320x60 source whose fourth bytes are non-alpha data, proving that the
     * branch is opaque XRGB semantics rather than a BGRA alpha precondition.
     * Force that ignored byte opaque, then apply the measured global-alpha
     * modulation followed by premultiplied source-over. */
    bool premultiplied_over = w[6] == MBX_2D_BLEND_EQUATION;
    bool opaque_global =
        (w[6] & ~MBX_2D_GLOBAL_ALPHA_MASK) ==
        MBX_2D_OPAQUE_GLOBAL_FACTORS;
    if ((w[0] != MBX_2D_COMMAND_HEADER && w[0] != MBX_2D_SUBMIT) ||
        (w[2] & 0xffff8000u) != 0x94060000u ||
        (w[4] & 0xf8002000u) != 0x30000000u ||
        w[5] != MBX_2D_BLEND_TAG ||
        (!premultiplied_over && !opaque_global) ||
        w[7] != 0x60800200u || w[8] != MBX_2D_BLEND_MODE ||
        w[9] != 0xffffffffu ||
        (w[10] & ~0x1fff1fffu) || (w[11] & ~0x1fff1fffu)) {
        if (why) *why = "packet is not a decoded blended-copy form";
        return false;
    }
    if (w[MBX_2D_BLEND_BODY_WORDS] != MBX_2D_END) {
        if (why) *why = "premultiplied packet terminators are incomplete";
        return false;
    }

    uint32_t source_stride = w[2] & 0x7fffu;
    uint32_t src_x = (w[4] >> 14) & 0x1fffu;
    uint32_t src_y = w[4] & 0x1fffu;
    uint32_t dst_x = (w[10] >> 16) & 0x1fffu;
    uint32_t dst_y = w[10] & 0x1fffu;
    uint32_t dst_x1 = (w[11] >> 16) & 0x1fffu;
    uint32_t dst_y1 = w[11] & 0x1fffu;
    if (!source_stride || (source_stride & 3u) ||
        dst_x1 <= dst_x || dst_y1 <= dst_y) {
        if (why) *why = "source stride or destination rectangle is invalid";
        return false;
    }
    uint32_t width = dst_x1 - dst_x;
    uint32_t height = dst_y1 - dst_y;
    if (dst_x1 > MBX_2D_WIDTH || dst_y1 > MBX_2D_HEIGHT ||
        src_x > source_stride / 4u || width > source_stride / 4u - src_x) {
        if (why) *why = "blend rectangle exceeds a measured surface";
        return false;
    }
    if ((w[1] & 3u) || (w[3] & 3u)) {
        if (why) *why = "blend source or destination GPU base is unaligned";
        return false;
    }
    if (!mbx_2d_batch_target_ok(batch, w[1], why)) return false;

    uint32_t row_bytes = width * 4u;
    uint32_t total = row_bytes * height;
    for (uint32_t row = 0; row < height; row++) {
        uint64_t src64 = (uint64_t)w[3] +
                         (uint64_t)(src_y + row) * source_stride +
                         (uint64_t)src_x * 4u;
        uint64_t dst64 = (uint64_t)w[1] +
                         (uint64_t)(dst_y + row) * MBX_2D_BGRA_STRIDE +
                         (uint64_t)dst_x * 4u;
        if (src64 + row_bytes > (uint64_t)UINT32_MAX + 1u ||
            dst64 + row_bytes > (uint64_t)UINT32_MAX + 1u ||
            (batch && !mbx_2d_batch_source_ok(
                (uint32_t)src64, row_bytes, batch->target, why)) ||
            !mbx_gart_validate(m, bus, (uint32_t)src64, row_bytes, why) ||
            !mbx_gart_validate(m, bus, (uint32_t)dst64, row_bytes, why))
            return false;
    }

    uint8_t *source = malloc(total);
    uint8_t *pixels = malloc(total);
    if (!source || !pixels) {
        free(source);
        free(pixels);
        if (why) *why = "host allocation for staged blend failed";
        return false;
    }
    bool ok = true;
    for (uint32_t row = 0; row < height && ok; row++) {
        uint32_t src = w[3] + (src_y + row) * source_stride + src_x * 4u;
        ok = mbx_gart_read(m, bus, src, source + row * row_bytes,
                           row_bytes, why);
    }
    if (ok)
        ok = mbx_2d_stage_destination(m, bus, w[1], dst_x, dst_y,
                                      width, height, pixels, batch, why);
    if (ok && opaque_global && mbx_trace_enabled()) {
        uint32_t nonopaque = 0u;
        uint32_t premultiplication_violations = 0u;
        uint32_t first_nonopaque = 0u;
        uint32_t first_nonopaque_pixel = 0u;
        for (uint32_t i = 0; i < total; i += 4u) {
            uint32_t src = mbx_load_le32(source + i);
            uint32_t alpha = src >> 24;
            if (alpha == 0xffu) continue;
            if (nonopaque == 0u) {
                first_nonopaque = src;
                first_nonopaque_pixel = i / 4u;
            }
            nonopaque++;
            if ((src & 0xffu) > alpha || ((src >> 8) & 0xffu) > alpha ||
                ((src >> 16) & 0xffu) > alpha)
                premultiplication_violations++;
        }
        fprintf(stderr,
                "MBX2D global-alpha source ring+0x%04x equation=%08x "
                "source=%08x target=%08x src=%u,%u dst=%u,%u size=%ux%u "
                "stride=%u nonopaque=%u/%u premul-violations=%u "
                "first[%u]=%08x\n",
                packet_off - S5L_MBX_2D_RING_BASE, w[6], w[3], w[1],
                src_x, src_y, dst_x, dst_y, width, height, source_stride,
                nonopaque, total / 4u, premultiplication_violations,
                first_nonopaque_pixel, first_nonopaque);
    }
    for (uint32_t i = 0; i < total && ok; i += 4u) {
        uint32_t src = mbx_load_le32(source + i);
        uint32_t alpha = src >> 24;
        if (!opaque_global &&
            ((src & 0xffu) > alpha || ((src >> 8) & 0xffu) > alpha ||
             ((src >> 16) & 0xffu) > alpha)) {
            if (why) *why = "2D source is not premultiplied BGRA8";
            ok = false;
            break;
        }
        if (opaque_global) {
            src |= 0xff000000u;
            src = mbx_modulate_vertex_alpha(
                src, (w[6] & MBX_2D_GLOBAL_ALPHA_MASK) >> 12);
        }
        uint32_t blended = mbx_source_over_clamped(
            mbx_load_le32(pixels + i), src);
        pixels[i] = (uint8_t)blended;
        pixels[i + 1u] = (uint8_t)(blended >> 8);
        pixels[i + 2u] = (uint8_t)(blended >> 16);
        pixels[i + 3u] = (uint8_t)(blended >> 24);
    }
    if (!ok) {
        free(source);
        free(pixels);
        return false;
    }
    free(source);
    job->target = w[1];
    job->dst_x = dst_x;
    job->dst_y = dst_y;
    job->width = width;
    job->height = height;
    job->row_bytes = row_bytes;
    job->total = total;
    job->pixels = pixels;
    return true;
}

/* A command is its body followed by one or more 0x70000000 terminator words;
 * the OS 3 producer always pads to 16/18 words, the OS 1.0 producer emits a
 * single terminator and starts the next head immediately after it. */
static uint32_t mbx_2d_packet_words(const s5l_mbx_t *m, uint32_t packet_off,
                                    uint32_t body_words, uint32_t max_words) {
    uint32_t n = body_words;
    while (n < max_words &&
           packet_off + n * 4u < S5L_MBX_APERTURE &&
           mbx_edram_word(m, packet_off + n * 4u) == MBX_2D_END)
        n++;
    return n;
}

static bool mbx_stage_2d_packet(s5l_mbx_t *m, const arm_bus_t *bus,
                                uint32_t packet_off,
                                const struct mbx_2d_batch_state *batch,
                                struct mbx_2d_job *job,
                                uint32_t *packet_words,
                                const char **why) {
    const uint32_t ring_end = S5L_MBX_2D_RING_BASE + S5L_MBX_2D_RING_SIZE;
    if (packet_off < S5L_MBX_2D_RING_BASE ||
        packet_off >= ring_end ||
        packet_off > S5L_MBX_APERTURE - MBX_2D_COPY_WORDS * 4u) {
        if (why) *why = "2D packet head is outside the observed ring";
        return false;
    }

    /* Only packet zero aliases the fixed doorbell. Every other counted head
     * must still be present exactly where the preceding decoded length puts
     * it. AppleMBX+0x2188 copies the whole command contiguously before testing
     * whether its updated cursor reached 64 KiB. r432 caught a command starting
     * at +0xfff8: its first two words remain in the nominal ring and its body
     * continues in the following EDRAM bytes. Only the *next* head wraps to
     * +0x0000. */
    uint32_t expected_head = packet_off == S5L_MBX_2D_RING_BASE
        ? MBX_2D_SUBMIT : MBX_2D_COMMAND_HEADER;
    if (mbx_edram_word(m, packet_off) != expected_head) {
        if (why) *why = "counted command heads are not contiguous and ordered";
        return false;
    }

    if (mbx_edram_word(m, packet_off + 5u * 4u) == MBX_2D_BLEND_TAG) {
        if (packet_off > S5L_MBX_APERTURE - MBX_2D_BLEND_WORDS * 4u) {
            if (why) *why = "blended 2D packet crosses the MBX aperture";
            return false;
        }
        if (!mbx_stage_premultiplied_copy(m, bus, packet_off,
                                           batch, job, why))
            return false;
        *packet_words = mbx_2d_packet_words(m, packet_off,
                                            MBX_2D_BLEND_BODY_WORDS,
                                            MBX_2D_BLEND_WORDS);
    } else if (mbx_edram_word(m, packet_off + 6u * 4u) ==
               MBX_2D_FILL_MODE) {
        if (!mbx_stage_rect_fill(m, bus, packet_off,
                                 batch, job, why))
            return false;
        *packet_words = mbx_2d_packet_words(m, packet_off,
                                            MBX_2D_COPY_BODY_WORDS,
                                            MBX_2D_COPY_WORDS);
    } else {
        if (!mbx_stage_simple_copy(m, bus, packet_off,
                                   batch, job, why))
            return false;
        *packet_words = mbx_2d_packet_words(m, packet_off,
                                            MBX_2D_COPY_BODY_WORDS,
                                            MBX_2D_COPY_WORDS);
    }
    return true;
}

static bool mbx_run_2d_submit(s5l_mbx_t *m, const arm_bus_t *bus,
                              uint32_t packet_off,
                              uint32_t command_count,
                              bool commit,
                              const char **why,
                              uint64_t *committed) {
    *committed = 0u;
    if (!command_count) {
        if (why) *why = "pending batch exceeded 255 heads or came from an old count-less snapshot";
        return false;
    }

    if (command_count == 1u) {
        struct mbx_2d_job job;
        uint32_t packet_words;
        if (!mbx_stage_2d_packet(m, bus, packet_off, NULL,
                                 &job, &packet_words, why))
            return false;
        bool ok = !commit || mbx_2d_job_commit(m, bus, &job, why);
        if (ok) *committed = job.total;
        mbx_2d_job_dispose(&job);
        return ok;
    }

    const uint32_t ring_end = S5L_MBX_2D_RING_BASE + S5L_MBX_2D_RING_SIZE;
    if (packet_off < S5L_MBX_2D_RING_BASE || packet_off >= ring_end ||
        packet_off > S5L_MBX_APERTURE - MBX_2D_COPY_WORDS * 4u) {
        if (why) *why = "batched 2D packet head is outside the observed ring";
        return false;
    }
    uint32_t target = mbx_edram_word(m, packet_off + 4u);
    if ((target & 3u) ||
        (uint64_t)target + MBX_2D_SURFACE_BYTES >
            (uint64_t)UINT32_MAX + 1u) {
        if (why) *why = "batched destination surface is unaligned or wraps";
        return false;
    }

    struct mbx_2d_job *jobs = calloc(command_count, sizeof *jobs);
    if (!jobs) {
        free(jobs);
        if (why) *why = "host allocation for 2D batch jobs failed";
        return false;
    }
    /* Preserve the original fail-closed surface requirement without copying
     * 600 KiB that most batches never read. Individual destination rectangles
     * are validated again while their jobs are staged. */
    if (!mbx_gart_validate(m, bus, target, MBX_2D_SURFACE_BYTES, why)) {
        free(jobs);
        return false;
    }

    struct mbx_2d_batch_state batch = {
        .target = target,
        .jobs = jobs,
        .staged = 0u,
    };

    bool ok = true;
    uint32_t staged = 0u;
    uint32_t cursor = packet_off;
    uint64_t total = 0u;
    for (; staged < command_count; staged++) {
        uint32_t packet_words = 0u;
        if (!mbx_stage_2d_packet(m, bus, cursor, &batch,
                                 &jobs[staged], &packet_words, why)) {
            ok = false;
            break;
        }
        uint32_t next = cursor + packet_words * 4u;
        /* The producer resets the software cursor to zero after a command
         * reaches or crosses 64 KiB; it does not carry the overrun modulo the
         * ring and it does not split the current command. */
        cursor = next >= ring_end ? S5L_MBX_2D_RING_BASE : next;
        total += jobs[staged].total;
        batch.staged = staged + 1u;
    }

    /* Every packet has now parsed, every source pixel is known, every target
     * PTE has validated, and sequential overlaps have been reconstructed from
     * prior staged rectangles. Only now may guest RAM change. Since the handler
     * is synchronous, the validated GART cannot mutate between staging and
     * these ordered writes. Each job retains its post-command rectangle,
     * preserving intermediate overlap semantics and the scanout write
     * observer. */
    if (commit) {
        for (uint32_t i = 0; i < command_count && ok; i++)
            ok = mbx_2d_job_commit(m, bus, &jobs[i], why);
    }

    for (uint32_t i = 0; i < command_count; i++)
        mbx_2d_job_dispose(&jobs[i]);
    free(jobs);
    if (ok) *committed = total;
    return ok;
}

static uint64_t mbx_rejection_reason_hash(const char *reason) {
    uint64_t hash = UINT64_C(14695981039346656037);
    if (!reason) return 0u;
    for (const unsigned char *p = (const unsigned char *)reason; *p; p++) {
        hash ^= *p;
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

bool s5l_mbx_probe_2d_packet(s5l_mbx_t *m, const arm_bus_t *bus,
                             uint32_t packet_off, uint32_t *packet_words,
                             const char **why) {
    if (packet_words) *packet_words = 0u;
    if (why) *why = "unknown rejection";
    if (!m || !bus) {
        if (why) *why = "missing MBX or bus";
        return false;
    }

    struct mbx_2d_job job = {0};
    uint32_t words = 0u;
    bool ok = mbx_stage_2d_packet(m, bus, packet_off, NULL,
                                  &job, &words, why);
    mbx_2d_job_dispose(&job);
    if (ok && packet_words) *packet_words = words;
    return ok;
}

bool s5l_mbx_probe_2d_submit(s5l_mbx_t *m, const arm_bus_t *bus,
                             uint32_t packet_off, uint32_t command_count,
                             uint64_t *reason_hash, const char **why) {
    const char *local_why = "unknown rejection";
    if (reason_hash) *reason_hash = 0u;
    if (why) *why = local_why;
    if (!m || !bus) {
        local_why = "missing MBX or bus";
        if (why) *why = local_why;
        if (reason_hash)
            *reason_hash = mbx_rejection_reason_hash(local_why);
        return false;
    }

    uint64_t staged_bytes = 0u;
    bool ok = mbx_run_2d_submit(m, bus, packet_off, command_count, false,
                                &local_why, &staged_bytes);
    if (why) *why = local_why;
    if (!ok && reason_hash)
        *reason_hash = mbx_rejection_reason_hash(local_why);
    return ok;
}

bool s5l_mbx_process_2d(s5l_mbx_t *m, const arm_bus_t *bus,
                        uint32_t written_off, uint32_t value,
                        s5l_mbx_telemetry_t *telemetry) {
    if (!m || !m->edram || written_off < S5L_MBX_2D_RING_BASE ||
        written_off >= S5L_MBX_2D_RING_BASE + S5L_MBX_2D_RING_SIZE ||
        (written_off & 3u))
        return false;

    /* AppleMBX+0x1188 copies one or more commands per selector-3 submission.
     * Count their heads, but do not execute while any body is still arriving. */
    if (value == MBX_2D_COMMAND_HEADER) {
        mbx_2d_pending_note(m, written_off);
        return false;
    }

    /* AppleMBX+0x1f58 performs this fixed ring+0 write after the complete
     * command sequence. r376 measured the same boundary for 1, 4 and 8 heads. */
    if (written_off != S5L_MBX_2D_RING_BASE || value != MBX_2D_SUBMIT)
        return false;

    uint32_t packet_off, command_count;
    if (!mbx_2d_pending_get(m, &packet_off, &command_count)) return false;
    mbx_2d_pending_clear(m);

    /* Count zero is the explicit >=256/legacy-unknown marker. Recording 256
     * is a truthful lower bound; normal measured batches retain exact counts. */
    uint32_t metric_count = command_count ? command_count : 256u;
    mbx_counter_add(&mbx_2d_candidates, metric_count);
    if (telemetry)
        mbx_counter_add(&telemetry->candidates_2d, metric_count);

    const char *why = "unknown rejection";
    uint64_t committed = 0u;
    bool ok = mbx_run_2d_submit(m, bus, packet_off, command_count, true,
                                &why, &committed);
    if (!ok) {
        mbx_counter_add(&mbx_2d_rejected, metric_count);
        if (telemetry) {
            mbx_counter_add(&telemetry->rejected_2d, metric_count);
            telemetry->last_rejected_2d_ring_offset =
                packet_off - S5L_MBX_2D_RING_BASE;
            telemetry->last_rejected_2d_count = command_count;
            telemetry->last_rejected_2d_reason_hash =
                mbx_rejection_reason_hash(why);
        }
        if (mbx_trace_state == 1)
            fprintf(stderr, "MBX2D reject ring+0x%04x (%u commands): %s\n",
                    packet_off - S5L_MBX_2D_RING_BASE,
                    metric_count, why);
        return false;
    }

    /* Completion belongs to the whole atomic submit whose pixels were just
     * committed, not to the unrelated startup BACKGROUND_TAG write at 0x6d8. */
    m->status |= S5L_MBX_STATUS_2D_SYNC;
    mbx_counter_add(&mbx_2d_completed, command_count);
    mbx_counter_add(&mbx_2d_bytes, committed);
    if (telemetry) {
        mbx_counter_add(&telemetry->completed_2d, command_count);
        mbx_counter_add(&telemetry->bytes_2d, committed);
    }
    if (mbx_trace_state == 1)
        fprintf(stderr,
                "MBX2D complete ring+0x%04x (%u commands): %llu bytes\n",
                packet_off - S5L_MBX_2D_RING_BASE, command_count,
                (unsigned long long)committed);
    return true;
}

static bool mbx_gart_u32(const s5l_mbx_t *m, const arm_bus_t *bus,
                         uint32_t gpu_va, uint32_t *value,
                         const char **why) {
    uint8_t bytes[4];
    if (!mbx_gart_read(m, bus, gpu_va, bytes, sizeof bytes, why)) return false;
    *value = mbx_load_le32(bytes);
    return true;
}

static uint32_t mbx_3d_decode_address(uint32_t word) {
    return (word & MBX_3D_ADDRESS_MASK) << 7;
}

/* QuartzCore selects source ONE and destination ONE_MINUS_SRC_ALPHA for this
 * object. Its shipped software compositor at 0x3122dcf4 uses the same 8-bit
 * fixed-point equation, with 256-alpha rather than an inferred /255 rule.
 * Fixed-function blending clamps each resulting component. Premultiplied
 * sources never reach that clamp, while arbitrary BGRA8 texture bytes can. */
static uint32_t mbx_source_over_clamped(uint32_t dst, uint32_t src) {
    uint32_t inv = 256u - (src >> 24);
    uint32_t out = 0u;
    for (unsigned shift = 0; shift < 32u; shift += 8u) {
        uint32_t s = (src >> shift) & 0xffu;
        uint32_t d = (dst >> shift) & 0xffu;
        uint32_t blended = s + ((d * inv) >> 8);
        if (blended > 0xffu) blended = 0xffu;
        out |= blended << shift;
    }
    return out;
}

/* QuartzCore's software rasterizer modulates a sampled channel as
 * `vertex * (texture + 1) >> 8`. The r180 software frame and r382 MBX frame
 * independently pin that equation for these status sprites: an unmodulated
 * source channel 255 becomes 191 under the captured 0xbf vertex alpha, and
 * 183 becomes 137. Applying one alpha to every premultiplied BGRA8 channel
 * preserves the source-over invariant and the identity endpoint at 255. */
static uint32_t mbx_modulate_vertex_alpha(uint32_t src, uint32_t alpha) {
    uint32_t out = 0u;
    for (unsigned shift = 0; shift < 32u; shift += 8u) {
        uint32_t component = (src >> shift) & 0xffu;
        out |= (((component + 1u) * alpha) >> 8) << shift;
    }
    return out;
}

/* QuartzCore's shipped sw_sample_linear_BGRA8 implementation at
 * 0x3122bc08..0x3122bcb4 interpolates BGRA8 in two packed byte lanes.  Keep
 * the unsigned wrapping multiply/adds: for a descending channel they are what
 * reproduces the native floor-toward-minus-infinity result after the mask. */
static uint32_t mbx_linear_bgra8(uint32_t first, uint32_t second,
                                 uint32_t weight) {
    const uint32_t lanes = UINT32_C(0x00ff00ff);
    uint32_t first_even = first & lanes;
    uint32_t second_even = second & lanes;
    uint32_t first_odd = (first >> 8) & lanes;
    uint32_t second_odd = (second >> 8) & lanes;
    uint32_t even = first_even +
                    ((weight * (second_even - first_even)) >> 8);
    uint32_t odd = first_odd +
                   ((weight * (second_odd - first_odd)) >> 8);

    return (even & lanes) | ((odd & lanes) << 8);
}

struct mbx_bilinear_axis {
    uint32_t first;
    uint32_t second;
    uint32_t weight;
};

static bool mbx_bilinear_coordinate(float coordinate, uint32_t dimension,
                                    struct mbx_bilinear_axis *axis) {
    if (!axis || !dimension || dimension > 512u || coordinate < 0.0f)
        return false;
    float fixed_float = coordinate * 65536.0f;
    if (fixed_float > (float)INT32_MAX) return false;

    int64_t raw = (int64_t)(int32_t)fixed_float - INT64_C(32768);
    int64_t neighbour = raw + INT64_C(65536);
    uint32_t maximum = (dimension << 16) - 1u;
    uint32_t first_fixed = raw < 0 ? 0u :
        (uint64_t)raw > maximum ? maximum : (uint32_t)raw;
    uint32_t second_fixed = neighbour < 0 ? 0u :
        (uint64_t)neighbour > maximum ? maximum : (uint32_t)neighbour;

    axis->first = first_fixed >> 16;
    axis->second = second_fixed >> 16;
    axis->weight = (first_fixed & UINT32_C(0xffff)) >> 8;
    return true;
}

/* Convert one covered destination pixel centre into the same clamped 16.16
 * tap pair consumed by QuartzCore's software sampler.  The MBX interpolator's
 * undocumented sub-LSB precision cannot be hardware-oracled here; binary32
 * interpolation is the producer/software-renderer reference and is kept
 * explicit instead of pretending the old contiguous-row shortcut was exact. */
static bool mbx_bilinear_axis(float origin, float span,
                              float texel_origin, float texel_span,
                              uint32_t pixel, uint32_t dimension,
                              struct mbx_bilinear_axis *axis) {
    if (!axis || !dimension || dimension > 512u || span <= 0.0f ||
        texel_origin < 0.0f || texel_span < 0.0f)
        return false;

    float step = texel_span / span;
    float coordinate = texel_origin +
                       ((float)pixel + 0.5f - origin) * step;
    return mbx_bilinear_coordinate(coordinate, dimension, axis);
}

struct mbx_3d_word {
    uint16_t off;
    uint32_t value;
};

struct mbx_3d_background_form {
    uint32_t xclip, yclip;
    uint32_t tile_x0, tile_x1, tile_y0, tile_y1;
    uint32_t left, top, width, height;
    uint32_t source_row0;
    uint32_t boundary[8];
};

/* The first entry is r369's full 320x96 overlay. r379 captured the next redraw
 * as two literal dirty rectangles over the same quad/source: source rows 0..76
 * across the full width, then rows 77..88 inset by eight pixels. Keeping all
 * three forms exact makes those clipped redraws possible without accepting an
 * arbitrary tile stream or inferring a generic PowerVR rasterizer. */
static const struct mbx_3d_background_form mbx_3d_background_forms[] = {
    {
        .xclip = 0x01400000u, .yclip = 0x00800010u,
        .tile_x0 = 0u, .tile_x1 = 39u,
        .tile_y0 = 1u, .tile_y1 = 7u,
        .left = 0u, .top = 20u, .width = 320u, .height = 96u,
        .source_row0 = 0u,
        .boundary = {
            0x00000000u, 0x42e80000u, 0x00000000u, 0x41a00000u,
            0x43a00000u, 0x42e80000u, 0x43a00000u, 0x41a00000u,
        },
    },
    {
        .xclip = 0x01400000u, .yclip = 0x00700010u,
        .tile_x0 = 0u, .tile_x1 = 39u,
        .tile_y0 = 1u, .tile_y1 = 6u,
        .left = 0u, .top = 20u, .width = 320u, .height = 77u,
        .source_row0 = 0u,
        .boundary = {
            0x00000000u, 0x42c20000u, 0x00000000u, 0x41a00000u,
            0x43a00000u, 0x42c20000u, 0x43a00000u, 0x41a00000u,
        },
    },
    {
        .xclip = 0x01380008u, .yclip = 0x00700060u,
        .tile_x0 = 1u, .tile_x1 = 38u,
        .tile_y0 = 6u, .tile_y1 = 6u,
        .left = 8u, .top = 97u, .width = 304u, .height = 12u,
        .source_row0 = 77u,
        .boundary = {
            0x41000000u, 0x42da0000u, 0x41000000u, 0x42c20000u,
            0x439c0000u, 0x42da0000u, 0x439c0000u, 0x42c20000u,
        },
    },
};

static const struct mbx_3d_background_form *
mbx_3d_find_background_form(const s5l_mbx_t *m) {
    uint32_t xclip = m->reg[S5L_MBX_FBXCLIP / 4u];
    uint32_t yclip = m->reg[S5L_MBX_FBYCLIP / 4u];
    for (unsigned i = 0;
         i < sizeof mbx_3d_background_forms /
             sizeof mbx_3d_background_forms[0]; i++) {
        const struct mbx_3d_background_form *form =
            &mbx_3d_background_forms[i];
        if (form->xclip == xclip && form->yclip == yclip) return form;
    }
    return NULL;
}

static uint32_t mbx_3d_boundary_fixed_expected(uint32_t off) {
    static const struct mbx_3d_word nonzero[] = {
        {0x080u, 0x22206f80u}, {0x088u, 0x45800000u},
        {0x094u, 0x45800000u}, {0x098u, 0x45800000u},
        {0x09cu, 0x45800000u}, {0x0b4u, 0x22207f80u},
    };
    for (unsigned i = 0; i < sizeof nonzero / sizeof nonzero[0]; i++)
        if (nonzero[i].off == off) return nonzero[i].value;
    return 0u;
}

static uint32_t mbx_3d_background_boundary_expected(
    const struct mbx_3d_background_form *form, uint32_t off) {
    if (off >= 0x0b8u && off <= 0x0d4u)
        return form->boundary[(off - 0x0b8u) / 4u];
    return mbx_3d_boundary_fixed_expected(off);
}

static bool mbx_execute_first_tiled_over(s5l_mbx_t *m,
                                         const arm_bus_t *bus,
                                         const char **why,
                                         uint32_t *pixels_blended) {
    uint32_t region = m->reg[S5L_MBX_RGNBASE / 4u];
    uint32_t object = m->reg[S5L_MBX_OBJBASE / 4u];
    uint32_t target = m->reg[S5L_MBX_FBSTART / 4u];
    const struct mbx_3d_background_form *form =
        mbx_3d_find_background_form(m);

    if ((region & 3u) || (object & 3u) || (target & 3u) ||
        object > UINT32_MAX - 0x2a0u) {
        if (why) *why = "region, object, or framebuffer base is invalid";
        return false;
    }
    if (m->reg[S5L_MBX_3DPIXSAMP / 4u] != 0x00020007u ||
        m->reg[S5L_MBX_FBCTL / 4u] != 0x00000006u ||
        m->reg[S5L_MBX_FBLINESTRIDE / 4u] != MBX_3D_WIDTH || !form) {
        if (why) *why = "render registers are not a captured BGRA8 background form";
        return false;
    }

    /* Each captured form is an ordered rectangle of 8x16 tiles. Every tile
     * points at the same list, and only the final tile carries bit 31. */
    uint32_t list = object + 0x68u;
    uint32_t tile_count = (form->tile_x1 - form->tile_x0 + 1u) *
                          (form->tile_y1 - form->tile_y0 + 1u);
    uint32_t tile_index = 0u;
    for (uint32_t y = form->tile_y0; y <= form->tile_y1; y++) {
        for (uint32_t x = form->tile_x0; x <= form->tile_x1; x++) {
            uint32_t pair = tile_index * 8u;
            uint32_t code, pointer;
            if (!mbx_gart_u32(m, bus, region + pair, &code, why) ||
                !mbx_gart_u32(m, bus, region + pair + 4u, &pointer, why))
                return false;
            uint32_t expected = (y << 8) | x;
            if (tile_index + 1u == tile_count) expected |= 0x80000000u;
            if (code != expected || pointer != list) {
                if (why) *why = "region tile list differs from its captured background stream";
                return false;
            }
            tile_index++;
        }
    }

    static const uint32_t list_words[4] = {
        0x60200020u, 0x6020002du, 0x61a0007cu, 0xf0000000u
    };
    for (unsigned i = 0; i < 4u; i++) {
        uint32_t value;
        if (!mbx_gart_u32(m, bus, list + i * 4u, &value, why)) return false;
        if (value != list_words[i]) {
            if (why) *why = "object list is not the captured three-object list";
            return false;
        }
    }

    static const uint32_t background[26] = {
        0xe0000000u, 0xa7718000u, 0u, 0xd6887610u,
        0x22220e80u, 0u, 0u, 0x45000000u,
        0u, 0u, 0x45000000u, 0x3f800000u,
        0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
        0x3f800000u, 0u, 0u, 0u,
        0u, 0x40000000u, 0u, 0u,
        0u, 0x40000000u,
    };
    for (unsigned i = 0; i < 26u; i++) {
        uint32_t value;
        if (!mbx_gart_u32(m, bus, object + i * 4u, &value, why)) return false;
        if (i == 2u) {
            if ((value & ~MBX_3D_ADDRESS_MASK) != 0x0e500000u ||
                mbx_3d_decode_address(value) != target) {
                if (why) *why = "background texture does not resolve to FBSTART";
                return false;
            }
        } else if (value != background[i]) {
            if (why) *why = "background object differs from the captured form";
            return false;
        }
    }

    /* The first two list entries are consecutive 13-word boundary objects at
     * +0x80 and +0xb4. Bytes from +0xe8 to the pointer-selected third object
     * are not referenced and may retain an older command. */
    for (uint32_t off = 0x80u; off < 0x0e8u; off += 4u) {
        uint32_t value;
        if (!mbx_gart_u32(m, bus, object + off, &value, why)) return false;
        if (value != mbx_3d_background_boundary_expected(form, off)) {
            if (why) *why = "rectangle boundary object differs from the captured form";
            return false;
        }
    }

    static const uint32_t quad[44] = {
        0xe0000000u, 0xa0418001u, 0u, 0xa6884710u,
        0xa7718000u, 0u, 0xae504ea0u, 0x22250e80u,
        0x00000000u, 0x41a00000u, 0x43a00000u, 0x41a00000u,
        0x00000000u, 0x42e80000u, 0x43a00000u, 0x42e80000u,
        0u, 0u, 0u, 0u,
        0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
        0xff000000u, 0x00000000u, 0x00000000u, 0x00000000u,
        0x3ca00000u, 0xff000000u, 0x3d800000u, 0x00000000u,
        0x3ea00000u, 0x3ca00000u, 0xff000000u, 0x00000000u,
        0x3f400000u, 0x00000000u, 0x3de80000u, 0xff000000u,
        0x3d800000u, 0x3f400000u, 0x3ea00000u, 0x3de80000u,
    };
    uint32_t source = 0u;
    for (unsigned i = 0; i < 44u; i++) {
        uint32_t value;
        if (!mbx_gart_u32(m, bus, object + 0x1f0u + i * 4u,
                          &value, why))
            return false;
        if (i == 2u) {
            if ((value & ~MBX_3D_ADDRESS_MASK) != 0x8e000000u) {
                if (why) *why = "source texture address word has an unknown format";
                return false;
            }
            source = mbx_3d_decode_address(value);
        } else if (i == 5u) {
            if ((value & ~MBX_3D_ADDRESS_MASK) != 0x0e500000u ||
                mbx_3d_decode_address(value) != target) {
                if (why) *why = "destination texture does not resolve to FBSTART";
                return false;
            }
        } else if (value != quad[i]) {
            if (why) *why = "textured quad differs from the captured source-over form";
            return false;
        }
    }
    if (!source) {
        if (why) *why = "source texture resolves to GPU address zero";
        return false;
    }

    uint32_t row_bytes = form->width * 4u;
    uint32_t total = row_bytes * form->height;
    uint64_t source_end = (uint64_t)source +
                          (uint64_t)(form->source_row0 + form->height - 1u) *
                              MBX_3D_SOURCE_STRIDE + 4u;
    uint64_t target_end = (uint64_t)target +
                          (uint64_t)(form->top + form->height - 1u) *
                              MBX_3D_TARGET_STRIDE +
                          (uint64_t)(form->left + form->width) * 4u;
    if (source_end > UINT32_MAX || target_end > UINT32_MAX) {
        if (why) *why = "captured background rectangle overflows its surface";
        return false;
    }
    for (uint32_t row = 0; row < form->height; row++) {
        uint32_t src = source +
                       (form->source_row0 + row) * MBX_3D_SOURCE_STRIDE;
        uint32_t dst = target + (form->top + row) * MBX_3D_TARGET_STRIDE +
                       form->left * 4u;
        if (!mbx_gart_validate(m, bus, src, 4u, why) ||
            !mbx_gart_validate(m, bus, dst, row_bytes, why))
            return false;
    }

    uint8_t *pixels = malloc(total);
    if (!pixels) {
        if (why) *why = "host allocation for staged 3D pixels failed";
        return false;
    }
    bool ok = true;
    for (uint32_t row = 0; row < form->height && ok; row++) {
        uint8_t src_bytes[4];
        uint32_t src_addr = source +
                            (form->source_row0 + row) * MBX_3D_SOURCE_STRIDE;
        uint32_t dst = target + (form->top + row) * MBX_3D_TARGET_STRIDE +
                       form->left * 4u;
        ok = mbx_gart_read(m, bus, src_addr, src_bytes,
                           sizeof src_bytes, why) &&
             mbx_gart_read(m, bus, dst, pixels + row * row_bytes,
                           row_bytes, why);
        if (!ok) break;
        uint32_t src = mbx_load_le32(src_bytes);
        uint32_t alpha = src >> 24;
        if ((src & 0xffu) > alpha || ((src >> 8) & 0xffu) > alpha ||
            ((src >> 16) & 0xffu) > alpha) {
            if (why) *why = "source texture is not premultiplied BGRA8";
            ok = false;
            break;
        }
        for (uint32_t x = 0; x < form->width; x++) {
            uint8_t *pixel = pixels + row * row_bytes + x * 4u;
            uint32_t blended = mbx_source_over_clamped(
                mbx_load_le32(pixel), src);
            pixel[0] = (uint8_t)blended;
            pixel[1] = (uint8_t)(blended >> 8);
            pixel[2] = (uint8_t)(blended >> 16);
            pixel[3] = (uint8_t)(blended >> 24);
        }
    }
    for (uint32_t row = 0; row < form->height && ok; row++) {
        uint32_t dst = target + (form->top + row) * MBX_3D_TARGET_STRIDE +
                       form->left * 4u;
        ok = mbx_gart_write(m, bus, dst, pixels + row * row_bytes,
                            row_bytes, why);
    }
    free(pixels);
    if (ok && pixels_blended) *pixels_blended = form->width * form->height;
    return ok;
}

struct mbx_3d_status_form {
    uint32_t xclip, yclip;
    uint32_t target;
    bool variable_vertex_alpha;
    bool boundary_override;
    uint32_t tile_x0, tile_x1, tile_y0, tile_y1;
    uint32_t left, top, width, height;
    uint32_t source_row0;
    uint32_t source_stride;
    uint32_t source_control;
    uint32_t boundary[8];
    uint32_t quad[44];
};

/* These are literal transcriptions of the live object streams. Words 2 and 5
 * are address fields and are validated separately against each form's control
 * bits and FBSTART; every other word must match exactly. A zero target accepts
 * either surface used by the earlier status forms.
 *
 * The slider label is the one variable-alpha exception: r385/r387/r389
 * measured the same word at all four vertices while its high byte stepped b4,
 * 8a, 61, 37 and its low 24 bits stayed zero. Only those four words may vary,
 * must remain identical, and are consumed as the per-vertex alpha established
 * by the software-renderer pixel oracle. r402-r406's tutorial layers retain
 * their literal b7/05 alpha words; one capture is not treated as proof of an
 * arbitrary opacity range. Five former 0x612 forms were removed after r414
 * proved that they followed the stale +0x1f0 texture instead of the object
 * selected by the list pointer. */
static const struct mbx_3d_status_form mbx_3d_status_forms[] = {
    {
        .xclip = 0x00a80098u, .yclip = 0x00200000u,
        .tile_x0 = 0x13u, .tile_x1 = 0x14u,
        .tile_y0 = 0u, .tile_y1 = 1u,
        .left = 155u, .top = 0u, .width = 10u, .height = 20u,
        .source_stride = 0x40u, .source_control = 0x0e040000u,
        .quad = {
            0xe0000000u, 0xa1218000u, 0u, 0xcd206c40u,
            0xa7718000u, 0u, 0xae504ea0u, 0x22250e80u,
            0x431b0000u, 0x41a00000u, 0x431b0000u, 0x00000000u,
            0x43250000u, 0x41a00000u, 0x43250000u, 0x00000000u,
            0u, 0u, 0u, 0u,
            0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
            0xbf000000u, 0x00000000u, 0x3f200000u, 0x3e1b0000u,
            0x3ca00000u, 0xbf000000u, 0x00000000u, 0x00000000u,
            0x3e1b0000u, 0x00000000u, 0xbf000000u, 0x3f200000u,
            0x3f200000u, 0x3e250000u, 0x3ca00000u, 0xbf000000u,
            0x3f200000u, 0x00000000u, 0x3e250000u, 0x00000000u,
        },
    },
    {
        .xclip = 0x00500000u, .yclip = 0x00200000u,
        .tile_x0 = 0u, .tile_x1 = 9u,
        .tile_y0 = 0u, .tile_y1 = 1u,
        .left = 4u, .top = 1u, .width = 76u, .height = 16u,
        .source_stride = 0x140u, .source_control = 0x0e140000u,
        .quad = {
            0xe0000000u, 0xa4118000u, 0u, 0xcd206c40u,
            0xa7718000u, 0u, 0xae504ea0u, 0x22250e80u,
            0x40800000u, 0x41880000u, 0x40800000u, 0x3f800000u,
            0x42a00000u, 0x41880000u, 0x42a00000u, 0x3f800000u,
            0u, 0u, 0u, 0u,
            0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
            0xbf000000u, 0x00000000u, 0x3f800000u, 0x3b800000u,
            0x3c880000u, 0xbf000000u, 0x00000000u, 0x00000000u,
            0x3b800000u, 0x3a800000u, 0xbf000000u, 0x3f180000u,
            0x3f800000u, 0x3da00000u, 0x3c880000u, 0xbf000000u,
            0x3f180000u, 0x00000000u, 0x3da00000u, 0x3a800000u,
        },
    },
    {
        .xclip = 0x01400128u, .yclip = 0x00200000u,
        .tile_x0 = 0x25u, .tile_x1 = 0x27u,
        .tile_y0 = 0u, .tile_y1 = 1u,
        .left = 296u, .top = 0u, .width = 21u, .height = 20u,
        .source_stride = 0x60u, .source_control = 0x0e040000u,
        .quad = {
            0xe0000000u, 0xa2218001u, 0u, 0xcd206c40u,
            0xa7718000u, 0u, 0xae504ea0u, 0x22250e80u,
            0x43940000u, 0x41a00000u, 0x43940000u, 0x00000000u,
            0x439e8000u, 0x41a00000u, 0x439e8000u, 0x00000000u,
            0u, 0u, 0u, 0u,
            0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
            0xbf000000u, 0x00000000u, 0x3f200000u, 0x3e940000u,
            0x3ca00000u, 0xbf000000u, 0x00000000u, 0x00000000u,
            0x3e940000u, 0x00000000u, 0xbf000000u, 0x3f280000u,
            0x3f200000u, 0x3e9e8000u, 0x3ca00000u, 0xbf000000u,
            0x3f280000u, 0x00000000u, 0x3e9e8000u, 0x00000000u,
        },
    },
    {
        .xclip = 0x00a80098u, .yclip = 0x00200010u,
        .tile_x0 = 0x13u, .tile_x1 = 0x14u,
        .tile_y0 = 1u, .tile_y1 = 1u,
        .left = 155u, .top = 16u, .width = 10u, .height = 4u,
        .source_row0 = 16u,
        .source_stride = 0x40u, .source_control = 0x0e040000u,
        .quad = {
            0xe0000000u, 0xa1218000u, 0u, 0xcd206c40u,
            0xa7718000u, 0u, 0xae504ea0u, 0x22250e80u,
            0x431b0000u, 0x41a00000u, 0x431b0000u, 0x41800000u,
            0x43250000u, 0x41a00000u, 0x43250000u, 0x41800000u,
            0u, 0u, 0u, 0u,
            0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
            0xbf000000u, 0x00000000u, 0x3f200000u, 0x3e1b0000u,
            0x3ca00000u, 0xbf000000u, 0x00000000u, 0x3f000000u,
            0x3e1b0000u, 0x3c800000u, 0xbf000000u, 0x3f200000u,
            0x3f200000u, 0x3e250000u, 0x3ca00000u, 0xbf000000u,
            0x3f200000u, 0x3f000000u, 0x3e250000u, 0x3c800000u,
        },
    },
    {
        .xclip = 0x01400128u, .yclip = 0x00200010u,
        .tile_x0 = 0x25u, .tile_x1 = 0x27u,
        .tile_y0 = 1u, .tile_y1 = 1u,
        .left = 296u, .top = 16u, .width = 21u, .height = 4u,
        .source_row0 = 16u,
        .source_stride = 0x60u, .source_control = 0x0e040000u,
        .quad = {
            0xe0000000u, 0xa2218001u, 0u, 0xcd206c40u,
            0xa7718000u, 0u, 0xae504ea0u, 0x22250e80u,
            0x43940000u, 0x41a00000u, 0x43940000u, 0x41800000u,
            0x439e8000u, 0x41a00000u, 0x439e8000u, 0x41800000u,
            0u, 0u, 0u, 0u,
            0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
            0xbf000000u, 0x00000000u, 0x3f200000u, 0x3e940000u,
            0x3ca00000u, 0xbf000000u, 0x00000000u, 0x3f000000u,
            0x3e940000u, 0x3c800000u, 0xbf000000u, 0x3f280000u,
            0x3f200000u, 0x3e9e8000u, 0x3ca00000u, 0xbf000000u,
            0x3f280000u, 0x3f000000u, 0x3e9e8000u, 0x3c800000u,
        },
    },
    {
        .xclip = 0x01400000u, .yclip = 0x00200000u,
        .target = 0x00998000u,
        .tile_x0 = 0u, .tile_x1 = 0x27u,
        .tile_y0 = 0u, .tile_y1 = 1u,
        .left = 0u, .top = 0u, .width = 320u, .height = 20u,
        .source_stride = 0x500u, .source_control = 0x0e500000u,
        .quad = {
            0xe0000000u, 0xa6218000u, 0u, 0xcd206c40u,
            0xa7718000u, 0u, 0xae504ea0u, 0x22250e80u,
            0x00000000u, 0x41a00000u, 0x00000000u, 0x00000000u,
            0x43a00000u, 0x41a00000u, 0x43a00000u, 0x00000000u,
            0u, 0u, 0u, 0u,
            0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
            0xbf000000u, 0x00000000u, 0x3f200000u, 0x00000000u,
            0x3ca00000u, 0xbf000000u, 0x00000000u, 0x00000000u,
            0x00000000u, 0x00000000u, 0xbf000000u, 0x3f200000u,
            0x3f200000u, 0x3ea00000u, 0x3ca00000u, 0xbf000000u,
            0x3f200000u, 0x00000000u, 0x3ea00000u, 0x00000000u,
        },
    },
    {
        .xclip = 0x01400000u, .yclip = 0x00200000u,
        .target = 0x00897000u,
        .tile_x0 = 0u, .tile_x1 = 0x27u,
        .tile_y0 = 0u, .tile_y1 = 1u,
        .left = 0u, .top = 0u, .width = 320u, .height = 20u,
        .source_stride = 0x500u, .source_control = 0x0e500000u,
        .quad = {
            0xe0000000u, 0xa6218000u, 0u, 0xcd206c40u,
            0xa7718000u, 0u, 0xae504ea0u, 0x22250e80u,
            0x00000000u, 0x41a00000u, 0x00000000u, 0x00000000u,
            0x43a00000u, 0x41a00000u, 0x43a00000u, 0x00000000u,
            0u, 0u, 0u, 0u,
            0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
            0xbf000000u, 0x00000000u, 0x3f200000u, 0x00000000u,
            0x3ca00000u, 0xbf000000u, 0x00000000u, 0x00000000u,
            0x00000000u, 0x00000000u, 0xbf000000u, 0x3f200000u,
            0x3f200000u, 0x3ea00000u, 0x3ca00000u, 0xbf000000u,
            0x3f200000u, 0x00000000u, 0x3ea00000u, 0x00000000u,
        },
    },
    {
        .xclip = 0x01400000u, .yclip = 0x00200000u,
        .target = 0x00a41000u,
        .tile_x0 = 0u, .tile_x1 = 0x27u,
        .tile_y0 = 0u, .tile_y1 = 1u,
        .left = 0u, .top = 0u, .width = 320u, .height = 20u,
        .source_stride = 0x500u, .source_control = 0x0e500000u,
        .quad = {
            0xe0000000u, 0xa6218000u, 0u, 0xcd206c40u,
            0xa7718000u, 0u, 0xae504ea0u, 0x22250e80u,
            0x00000000u, 0x41a00000u, 0x00000000u, 0x00000000u,
            0x43a00000u, 0x41a00000u, 0x43a00000u, 0x00000000u,
            0u, 0u, 0u, 0u,
            0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
            0xbf000000u, 0x00000000u, 0x3f200000u, 0x00000000u,
            0x3ca00000u, 0xbf000000u, 0x00000000u, 0x00000000u,
            0x00000000u, 0x00000000u, 0xbf000000u, 0x3f200000u,
            0x3f200000u, 0x3ea00000u, 0x3ca00000u, 0xbf000000u,
            0x3f200000u, 0x00000000u, 0x3ea00000u, 0x00000000u,
        },
    },
    {
        .xclip = 0x01400000u, .yclip = 0x01e00010u,
        .target = 0x00a41000u,
        .tile_x0 = 0u, .tile_x1 = 0x27u,
        .tile_y0 = 1u, .tile_y1 = 0x1du,
        .left = 0u, .top = 20u, .width = 320u, .height = 460u,
        .source_row0 = 20u,
        .source_stride = 0x500u, .source_control = 0x0e500000u,
        .quad = {
            0xe0000000u, 0xa6618000u, 0u, 0xcd206c40u,
            0xa7718000u, 0u, 0xae504ea0u, 0x22250e80u,
            0x00000000u, 0x43f00000u, 0x00000000u, 0x41a00000u,
            0x43a00000u, 0x43f00000u, 0x43a00000u, 0x41a00000u,
            0u, 0u, 0u, 0u,
            0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
            0xb7000000u, 0x00000000u, 0x3f700000u, 0x00000000u,
            0x3ef00000u, 0xb7000000u, 0x00000000u, 0x3d200000u,
            0x00000000u, 0x3ca00000u, 0xb7000000u, 0x3f200000u,
            0x3f700000u, 0x3ea00000u, 0x3ef00000u, 0xb7000000u,
            0x3f200000u, 0x3d200000u, 0x3ea00000u, 0x3ca00000u,
        },
    },
    {
        .xclip = 0x01300010u, .yclip = 0x01800080u,
        .target = 0x00a41000u,
        .tile_x0 = 2u, .tile_x1 = 0x25u,
        .tile_y0 = 8u, .tile_y1 = 0x17u,
        .left = 18u, .top = 130u, .width = 284u, .height = 241u,
        .source_stride = 0x480u, .source_control = 0x0e480000u,
        .quad = {
            0xe0000000u, 0xa6518000u, 0u, 0xcd206c40u,
            0xa7718000u, 0u, 0xae504ea0u, 0x22250e80u,
            0x41900000u, 0x43b98000u, 0x41900000u, 0x43020000u,
            0x43970000u, 0x43b98000u, 0x43970000u, 0x43020000u,
            0u, 0u, 0u, 0u,
            0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
            0x05000000u, 0x00000000u, 0x3f710000u, 0x3c900000u,
            0x3eb98000u, 0x05000000u, 0x00000000u, 0x00000000u,
            0x3c900000u, 0x3e020000u, 0x05000000u, 0x3f0e0000u,
            0x3f710000u, 0x3e970000u, 0x3eb98000u, 0x05000000u,
            0x3f0e0000u, 0x00000000u, 0x3e970000u, 0x3e020000u,
        },
    },
    {
        .xclip = 0x01280018u, .yclip = 0x00b00090u,
        .target = 0x00a41000u,
        .tile_x0 = 3u, .tile_x1 = 0x24u,
        .tile_y0 = 9u, .tile_y1 = 0x0au,
        .left = 30u, .top = 145u, .width = 260u, .height = 23u,
        .source_stride = 0x420u, .source_control = 0x0e400000u,
        .quad = {
            0xe0000000u, 0xa6218001u, 0u, 0xcd206c40u,
            0xa7718000u, 0u, 0xae504ea0u, 0x22250e80u,
            0x41f00000u, 0x43280000u, 0x41f00000u, 0x43110000u,
            0x43910000u, 0x43280000u, 0x43910000u, 0x43110000u,
            0u, 0u, 0u, 0u,
            0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
            0x05000000u, 0x00000000u, 0x3f380000u, 0x3cf00000u,
            0x3e280000u, 0x05000000u, 0x00000000u, 0x00000000u,
            0x3cf00000u, 0x3e110000u, 0x05000000u, 0x3f020000u,
            0x3f380000u, 0x3e910000u, 0x3e280000u, 0x05000000u,
            0x3f020000u, 0x00000000u, 0x3e910000u, 0x3e110000u,
        },
    },
    {
        .xclip = 0x01280018u, .yclip = 0x013000a0u,
        .target = 0x00a41000u,
        .tile_x0 = 3u, .tile_x1 = 0x24u,
        .tile_y0 = 0x0au, .tile_y1 = 0x12u,
        .left = 30u, .top = 175u, .width = 260u, .height = 121u,
        .source_stride = 0x420u, .source_control = 0x0e400000u,
        .quad = {
            0xe0000000u, 0xa6418001u, 0u, 0xcd206c40u,
            0xa7718000u, 0u, 0xae504ea0u, 0x22250e80u,
            0x41f00000u, 0x43940000u, 0x41f00000u, 0x432f0000u,
            0x43910000u, 0x43940000u, 0x43910000u, 0x432f0000u,
            0u, 0u, 0u, 0u,
            0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
            0x05000000u, 0x00000000u, 0x3f720000u, 0x3cf00000u,
            0x3e940000u, 0x05000000u, 0x00000000u, 0x00000000u,
            0x3cf00000u, 0x3e2f0000u, 0x05000000u, 0x3f020000u,
            0x3f720000u, 0x3e910000u, 0x3e940000u, 0x05000000u,
            0x3f020000u, 0x00000000u, 0x3e910000u, 0x3e2f0000u,
        },
    },
    {
        .xclip = 0x01280018u, .yclip = 0x01700130u,
        .target = 0x00a41000u,
        .tile_x0 = 3u, .tile_x1 = 0x24u,
        .tile_y0 = 0x13u, .tile_y1 = 0x16u,
        .left = 29u, .top = 312u, .width = 262u, .height = 43u,
        .source_stride = 0x420u, .source_control = 0x0e400000u,
        .quad = {
            0xe0000000u, 0xa6318001u, 0u, 0xcd206c40u,
            0xa7718000u, 0u, 0xae504ea0u, 0x22250e80u,
            0x41e80000u, 0x43b18000u, 0x41e80000u, 0x439c0000u,
            0x43918000u, 0x43b18000u, 0x43918000u, 0x439c0000u,
            0u, 0u, 0u, 0u,
            0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
            0x05000000u, 0x00000000u, 0x3f2c0000u, 0x3ce80000u,
            0x3eb18000u, 0x05000000u, 0x00000000u, 0x00000000u,
            0x3ce80000u, 0x3e9c0000u, 0x05000000u, 0x3f030000u,
            0x3f2c0000u, 0x3e918000u, 0x3eb18000u, 0x05000000u,
            0x3f030000u, 0x00000000u, 0x3e918000u, 0x3e9c0000u,
        },
    },
    {
        .xclip = 0x01180070u, .yclip = 0x01c001a0u,
        .target = 0x00897000u,
        .variable_vertex_alpha = true,
        .tile_x0 = 0x0eu, .tile_x1 = 0x22u,
        .tile_y0 = 0x1au, .tile_y1 = 0x1bu,
        .left = 114u, .top = 417u, .width = 161u, .height = 30u,
        .source_stride = 0x2a0u, .source_control = 0x0e280000u,
        .quad = {
            0xe0000000u, 0xa5218001u, 0u, 0xcd206c40u,
            0xa7718000u, 0u, 0xae504ea0u, 0x22250e80u,
            0x42e40000u, 0x43df8000u, 0x42e40000u, 0x43d08000u,
            0x43898000u, 0x43df8000u, 0x43898000u, 0x43d08000u,
            0u, 0u, 0u, 0u,
            0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
            0u, 0u, 0x3f700000u, 0x3de40000u,
            0x3edf8000u, 0u, 0u, 0u,
            0x3de40000u, 0x3ed08000u, 0u, 0x3f210000u,
            0x3f700000u, 0x3e898000u, 0x3edf8000u, 0u,
            0x3f210000u, 0u, 0x3e898000u, 0x3ed08000u,
        },
    },
    {
        .xclip = 0x01180070u, .yclip = 0x01c001a0u,
        .target = 0x00a33000u,
        .variable_vertex_alpha = true,
        .tile_x0 = 0x0eu, .tile_x1 = 0x22u,
        .tile_y0 = 0x1au, .tile_y1 = 0x1bu,
        .left = 114u, .top = 417u, .width = 161u, .height = 30u,
        .source_stride = 0x2a0u, .source_control = 0x0e280000u,
        .quad = {
            0xe0000000u, 0xa5218001u, 0u, 0xcd206c40u,
            0xa7718000u, 0u, 0xae504ea0u, 0x22250e80u,
            0x42e40000u, 0x43df8000u, 0x42e40000u, 0x43d08000u,
            0x43898000u, 0x43df8000u, 0x43898000u, 0x43d08000u,
            0u, 0u, 0u, 0u,
            0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
            0u, 0u, 0x3f700000u, 0x3de40000u,
            0x3edf8000u, 0u, 0u, 0u,
            0x3de40000u, 0x3ed08000u, 0u, 0x3f210000u,
            0x3f700000u, 0x3e898000u, 0x3edf8000u, 0u,
            0x3f210000u, 0u, 0x3e898000u, 0x3ed08000u,
        },
    },
    {
        .xclip = 0x01180070u, .yclip = 0x01c001a0u,
        .target = 0x00998000u,
        .variable_vertex_alpha = true,
        .tile_x0 = 0x0eu, .tile_x1 = 0x22u,
        .tile_y0 = 0x1au, .tile_y1 = 0x1bu,
        .left = 114u, .top = 417u, .width = 161u, .height = 30u,
        .source_stride = 0x2a0u, .source_control = 0x0e280000u,
        .quad = {
            0xe0000000u, 0xa5218001u, 0u, 0xcd206c40u,
            0xa7718000u, 0u, 0xae504ea0u, 0x22250e80u,
            0x42e40000u, 0x43df8000u, 0x42e40000u, 0x43d08000u,
            0x43898000u, 0x43df8000u, 0x43898000u, 0x43d08000u,
            0u, 0u, 0u, 0u,
            0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
            0u, 0u, 0x3f700000u, 0x3de40000u,
            0x3edf8000u, 0u, 0u, 0u,
            0x3de40000u, 0x3ed08000u, 0u, 0x3f210000u,
            0x3f700000u, 0x3e898000u, 0x3edf8000u, 0u,
            0x3f210000u, 0u, 0x3e898000u, 0x3ed08000u,
        },
    },
};

static const struct mbx_3d_status_form *
mbx_3d_find_status_form(const s5l_mbx_t *m) {
    uint32_t xclip = m->reg[S5L_MBX_FBXCLIP / 4u];
    uint32_t yclip = m->reg[S5L_MBX_FBYCLIP / 4u];
    uint32_t target = m->reg[S5L_MBX_FBSTART / 4u];
    for (unsigned i = 0;
         i < sizeof mbx_3d_status_forms / sizeof mbx_3d_status_forms[0]; i++) {
        const struct mbx_3d_status_form *form = &mbx_3d_status_forms[i];
        if (form->xclip == xclip && form->yclip == yclip &&
            (!form->target || form->target == target))
            return form;
    }
    return NULL;
}

static uint32_t
mbx_3d_status_boundary_expected(const struct mbx_3d_status_form *form,
                                 uint32_t off) {
    if (off >= 0x0b8u && off <= 0x0d4u)
        return form->boundary_override
            ? form->boundary[(off - 0x0b8u) / 4u]
            : form->quad[8u + (off - 0x0b8u) / 4u];
    return mbx_3d_boundary_fixed_expected(off);
}

static bool mbx_execute_status_sprite(s5l_mbx_t *m,
                                      const arm_bus_t *bus,
                                      const char **why,
                                      uint32_t *pixels_blended) {
    uint32_t region = m->reg[S5L_MBX_RGNBASE / 4u];
    uint32_t object = m->reg[S5L_MBX_OBJBASE / 4u];
    uint32_t target = m->reg[S5L_MBX_FBSTART / 4u];

    if ((region & 3u) || (object & 3u) || (target & 3u) ||
        object > UINT32_MAX - 0x2a0u) {
        if (why) *why = "status-sprite region, object, or framebuffer base is invalid";
        return false;
    }
    const struct mbx_3d_status_form *form = mbx_3d_find_status_form(m);
    if (m->reg[S5L_MBX_3DPIXSAMP / 4u] != 0x00020007u ||
        m->reg[S5L_MBX_FBCTL / 4u] != 0x00000006u ||
        m->reg[S5L_MBX_FBLINESTRIDE / 4u] != MBX_3D_WIDTH || !form) {
        if (why) *why = "render registers are not a captured status-sprite form";
        return false;
    }

    uint32_t list = object + 0x68u;
    uint32_t tile_count = (form->tile_x1 - form->tile_x0 + 1u) *
                          (form->tile_y1 - form->tile_y0 + 1u);
    uint32_t tile_index = 0u;
    for (uint32_t y = form->tile_y0; y <= form->tile_y1; y++) {
        for (uint32_t x = form->tile_x0; x <= form->tile_x1; x++) {
            uint32_t code, pointer;
            if (!mbx_gart_u32(m, bus, region + tile_index * 8u,
                              &code, why) ||
                !mbx_gart_u32(m, bus, region + tile_index * 8u + 4u,
                              &pointer, why))
                return false;
            uint32_t expected = (y << 8) | x;
            if (tile_index + 1u == tile_count) expected |= 0x80000000u;
            if (code != expected || pointer != list) {
                if (why) *why = "status-sprite region list differs from its captured tiles";
                return false;
            }
            tile_index++;
        }
    }

    const uint32_t list_words[4] = {
        0x60200020u, 0x6020002du, 0x61a0007cu, 0xf0000000u
    };
    for (unsigned i = 0; i < 4u; i++) {
        uint32_t value;
        if (!mbx_gart_u32(m, bus, list + i * 4u, &value, why)) return false;
        if (value != list_words[i]) {
            if (why) *why = "status-sprite list is not the captured three-object list";
            return false;
        }
    }

    static const uint32_t background[26] = {
        0xe0000000u, 0xa7718000u, 0u, 0xd6887610u,
        0x22220e80u, 0u, 0u, 0x45000000u,
        0u, 0u, 0x45000000u, 0x3f800000u,
        0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
        0x3f800000u, 0u, 0u, 0u,
        0u, 0x40000000u, 0u, 0u,
        0u, 0x40000000u,
    };
    for (unsigned i = 0; i < 26u; i++) {
        uint32_t value;
        if (!mbx_gart_u32(m, bus, object + i * 4u, &value, why)) return false;
        if (i == 2u) {
            if ((value & ~MBX_3D_ADDRESS_MASK) != 0x0e500000u ||
                mbx_3d_decode_address(value) != target) {
                if (why) *why = "status background does not resolve to FBSTART";
                return false;
            }
        } else if (value != background[i]) {
            if (why) *why = "status background object differs from the captured form";
            return false;
        }
    }

    for (uint32_t off = 0x80u; off < 0x0e8u; off += 4u) {
        uint32_t value;
        if (!mbx_gart_u32(m, bus, object + off, &value, why)) return false;
        if (value != mbx_3d_status_boundary_expected(form, off)) {
            if (why) *why = "status boundary object differs from the captured form";
            return false;
        }
    }

    uint32_t source = 0u;
    uint32_t vertex_alpha_word = 0u;
    bool vertex_alpha_seen = false;
    bool quad_matches = true;
    for (unsigned i = 0; i < 44u; i++) {
        uint32_t value;
        if (!mbx_gart_u32(m, bus, object + 0x1f0u + i * 4u,
                          &value, why))
            return false;
        if (i == 2u) {
            if ((value & ~MBX_3D_ADDRESS_MASK) != form->source_control) {
                if (why) *why = "status source address word has an unknown format";
                return false;
            }
            source = mbx_3d_decode_address(value);
        } else if (i == 5u) {
            if ((value & ~MBX_3D_ADDRESS_MASK) != 0x0e500000u ||
                mbx_3d_decode_address(value) != target) {
                if (why) *why = "status blend surface differs from the captured form";
                return false;
            }
        } else if (i == 24u || i == 29u || i == 34u || i == 39u) {
            if (!vertex_alpha_seen) {
                vertex_alpha_word = value;
                vertex_alpha_seen = true;
            }
            if (form->variable_vertex_alpha) {
                if (value & 0x00ffffffu) {
                    if (why)
                        *why = "status vertex alpha has nonzero colour bits";
                    return false;
                }
                if (value != vertex_alpha_word) {
                    if (why)
                        *why = "status vertex alpha differs between vertices";
                    return false;
                }
            } else {
                quad_matches = quad_matches && value == form->quad[i];
            }
        } else {
            quad_matches = quad_matches && value == form->quad[i];
        }
    }
    if (!source) {
        if (why) *why = "status source resolves to GPU address zero";
        return false;
    }
    if (!vertex_alpha_seen) {
        if (why) *why = "status sprite has no vertex alpha";
        return false;
    }
    if (!quad_matches) {
        if (why) *why = "textured status sprite differs from its captured form";
        return false;
    }

    uint32_t row_bytes = form->width * 4u;
    uint32_t total = row_bytes * form->height;
    uint64_t source_end = (uint64_t)source +
                          (uint64_t)(form->source_row0 + form->height - 1u) *
                              form->source_stride + row_bytes;
    uint64_t target_end = (uint64_t)target +
                          (uint64_t)(form->top + form->height - 1u) *
                              MBX_3D_TARGET_STRIDE +
                           (uint64_t)(form->left + form->width) * 4u;
    if (row_bytes > form->source_stride || source_end > UINT32_MAX ||
        target_end > UINT32_MAX) {
        if (why) *why = "status source or destination rectangle overflows";
        return false;
    }
    for (uint32_t row = 0; row < form->height; row++) {
        uint32_t src = source +
                       (form->source_row0 + row) * form->source_stride;
        uint32_t dst = target + (form->top + row) * MBX_3D_TARGET_STRIDE +
                       form->left * 4u;
        uint32_t background_address = target +
                                      (form->top + row) * MBX_3D_TARGET_STRIDE +
                                      form->left * 4u;
        if (!mbx_gart_validate(m, bus, src, row_bytes, why) ||
            !mbx_gart_validate(m, bus, background_address, row_bytes, why) ||
            !mbx_gart_validate(m, bus, dst, row_bytes, why))
            return false;
    }

    uint8_t *source_pixels = malloc(total);
    uint8_t *pixels = malloc(total);
    if (!source_pixels || !pixels) {
        free(source_pixels);
        free(pixels);
        if (why) *why = "host allocation for staged status sprite failed";
        return false;
    }
    bool ok = true;
    for (uint32_t row = 0; row < form->height && ok; row++) {
        uint32_t src = source +
                       (form->source_row0 + row) * form->source_stride;
        uint32_t background_address = target +
                                      (form->top + row) * MBX_3D_TARGET_STRIDE +
                                      form->left * 4u;
        ok = mbx_gart_read(m, bus, src, source_pixels + row * row_bytes,
                           row_bytes, why) &&
             mbx_gart_read(m, bus, background_address, pixels + row * row_bytes,
                           row_bytes, why);
    }
    for (uint32_t i = 0; i < total && ok; i += 4u) {
        uint32_t src = mbx_load_le32(source_pixels + i);
        uint32_t alpha = src >> 24;
        if ((src & 0xffu) > alpha || ((src >> 8) & 0xffu) > alpha ||
            ((src >> 16) & 0xffu) > alpha) {
            if (why) *why = "status source is not premultiplied BGRA8";
            ok = false;
            break;
        }
        src = mbx_modulate_vertex_alpha(src, vertex_alpha_word >> 24);
        uint32_t blended = mbx_source_over_clamped(
            mbx_load_le32(pixels + i), src);
        pixels[i] = (uint8_t)blended;
        pixels[i + 1u] = (uint8_t)(blended >> 8);
        pixels[i + 2u] = (uint8_t)(blended >> 16);
        pixels[i + 3u] = (uint8_t)(blended >> 24);
    }
    for (uint32_t row = 0; row < form->height && ok; row++) {
        uint32_t dst = target + (form->top + row) * MBX_3D_TARGET_STRIDE +
                       form->left * 4u;
        ok = mbx_gart_write(m, bus, dst, pixels + row * row_bytes,
                            row_bytes, why);
    }
    free(source_pixels);
    free(pixels);
    if (ok && pixels_blended)
        *pixels_blended = form->width * form->height;
    return ok;
}

/* r409-r412 initially supplied four literal app-icon/label forms. Reading the
 * shipped _mbx3DCtxQuadCopyPerspective producer explains the shared packet
 * instead: it is a BGRA8 textured sprite whose independently encoded
 * destination, UV, texture-power, pitch, clip and tile bounds must agree.
 *
 * The texture header stores log2(power-of-two dimension)-3 in nibbles 6 and 5.
 * Linear pitch is split because the source address consumes bits 0..17 of its
 * word: pitch_bytes/16 bits 2..7 remain in source-control bits 18..23, while
 * bit 1 is relocated to texture-header bit 0. All measured linear textures
 * use an eight-pixel padded pitch, and that reconstruction yields every
 * captured 0x40..0x500 stride exactly. r415 adds the producer's second vertex
 * order: control 0x0e uses full width/power and height/power UV extents, while
 * control 0x8e uses the previously recovered half-texel extents. QuartzCore's
 * transform_filter_bits path proves that the latter is its filtered form, and
 * the co-shipped software sampler supplies the clamped 8-bit bilinear kernel.
 * r420 then contributes one exact uniformly minified 320x460 form and a third
 * measured state pair. Both vertex orders redundantly encode the same
 * destination and are checked in full. r434 and r438 add the filtered affine
 * subset: the direct sampler remains rigid 1:1, while the modulated sampler may
 * apply a positive uniform similarity transform. The captured samplers carry
 * one uniform alpha byte, which uses the already recovered channel-modulation
 * equation. The background object, blend object and FBSTART must all resolve to
 * the same mapped target, but its GPU address is not a rendering semantic and
 * is therefore not whitelisted. This is still not a perspective or arbitrary
 * affine rasterizer: shear, nonuniform affine scale, four-point warps and
 * coloured vertices remain rejected. r416's partly off-screen label is
 * filtered only when its normalized coordinates, integer boundary, clip and
 * tiles all independently agree. */
static bool mbx_3d_word_to_finite_float(uint32_t word, float *value) {
    if ((word & 0x7f800000u) == 0x7f800000u ||
        sizeof *value != sizeof word)
        return false;
    memcpy(value, &word, sizeof *value);
    return true;
}

static bool mbx_3d_word_to_nonnegative_float(uint32_t word, float *value) {
    return !(word & 0x80000000u) &&
           mbx_3d_word_to_finite_float(word, value);
}

static uint32_t mbx_3d_float_to_word(float value) {
    uint32_t word = 0u;
    memcpy(&word, &value, sizeof word);
    return word;
}

static int32_t mbx_3d_ceil_to_i32(float value) {
    int32_t integer = (int32_t)value;
    return integer + ((float)integer < value);
}

struct mbx_affine_transform {
    float origin_x, origin_y;
    float u_x, u_y;
    float v_x, v_y;
    float determinant;
};

static bool mbx_affine_pixel(const struct mbx_affine_transform *transform,
                             uint32_t x, uint32_t y,
                             float *u_fraction, float *v_fraction) {
    if (!transform || !u_fraction || !v_fraction ||
        transform->determinant <= 0.0f)
        return false;
    float dx = (float)x + 0.5f - transform->origin_x;
    float dy = (float)y + 0.5f - transform->origin_y;
    float u = (dx * transform->v_y - dy * transform->v_x) /
              transform->determinant;
    float v = (transform->u_x * dy - transform->u_y * dx) /
              transform->determinant;
    if (u < 0.0f || v < 0.0f || u >= 1.0f || v >= 1.0f)
        return false;
    *u_fraction = u;
    *v_fraction = v;
    return true;
}

/* r414 exposed why the object-list word is not merely another packet
 * discriminator.  Its low twenty bits are a word offset from OBJBASE:
 * 0x61a0007c references the textured object at +0x1f0, while 0x612000a8
 * references a different object at +0x2a0.  The latter is the five-record
 * form emitted by the shipped _mbx3DCtxQuadColor producer.  The apparently
 * related texture object at +0x1f0 is stale and must not be executed.
 *
 * This decoder follows that pointer, then requires the independently encoded
 * main geometry, normalized coordinates, boundary object, clip registers and
 * row-major tile list to agree.  Disassembly of the shipped _mbx3DQuadColor
 * wrapper and producer proves that its second argument is copied to the main
 * record before every normalized vertex pair.  Those four words must be one
 * uniform premultiplied A8R8G8B8 colour.  The four trailing parameter records
 * carry fixed all-one words and exact controls; they are not the quad colour.
 * Non-axis-aligned quads remain rejected. */
static bool mbx_execute_solid_quad(s5l_mbx_t *m,
                                   const arm_bus_t *bus,
                                   const char **why,
                                   uint32_t *pixels_filled) {
    uint32_t region = m->reg[S5L_MBX_RGNBASE / 4u];
    uint32_t object = m->reg[S5L_MBX_OBJBASE / 4u];
    uint32_t target = m->reg[S5L_MBX_FBSTART / 4u];
    if ((region & 3u) || (object & 3u) || (target & 3u) ||
        object > UINT32_MAX - 0x68u) {
        if (why) *why = "solid-quad region, object, or framebuffer base is invalid";
        return false;
    }
    if (m->reg[S5L_MBX_3DPIXSAMP / 4u] != 0x00020007u ||
        m->reg[S5L_MBX_FBCTL / 4u] != 0x00000006u ||
        m->reg[S5L_MBX_FBLINESTRIDE / 4u] != MBX_3D_WIDTH) {
        if (why) *why = "render registers are not the measured solid-quad family";
        return false;
    }

    uint32_t list = object + 0x68u;
    uint32_t list_words[4];
    for (unsigned i = 0; i < 4u; i++)
        if (!mbx_gart_u32(m, bus, list + i * 4u, &list_words[i], why))
            return false;
    bool solid_pointer =
        (list_words[2] & 0xfff00000u) == 0x61200000u;
    if (list_words[0] != 0x60200020u ||
        list_words[1] != 0x6020002du || !solid_pointer ||
        list_words[3] != 0xf0000000u) {
        /* Preserve the textured decoder's more relevant reason when this is
         * plainly its 0x61a pointer family. */
        if (why && solid_pointer)
            *why = "solid-quad list is not the measured pointer form";
        return false;
    }
    uint64_t solid64 = (uint64_t)object +
                       (uint64_t)(list_words[2] & 0x000fffffu) * 4u;
    if (solid64 < (uint64_t)object + 0x1f0u ||
        solid64 + 5u * 33u * 4u > (uint64_t)UINT32_MAX + 1u) {
        if (why) *why = "solid-quad object pointer is outside its safe span";
        return false;
    }
    uint32_t solid = (uint32_t)solid64;

    uint32_t main[33];
    for (unsigned i = 0; i < 33u; i++)
        if (!mbx_gart_u32(m, bus, solid + i * 4u, &main[i], why))
            return false;
    if (main[0] != 0xe0000000u || main[1] != 0xa7718000u ||
        main[3] != 0xa6104620u || main[4] != 0x22220e80u) {
        if (why) *why = "solid-quad main record has unknown controls";
        return false;
    }
    if ((main[2] & ~MBX_3D_ADDRESS_MASK) != 0x0e500000u ||
        mbx_3d_decode_address(main[2]) != target) {
        if (why) *why = "solid-quad main record does not resolve to FBSTART";
        return false;
    }
    for (unsigned i = 13u; i < 17u; i++) {
        if (main[i] != 0u) {
            if (why) *why = "solid-quad perspective terms are nonzero";
            return false;
        }
    }
    for (unsigned i = 17u; i < 21u; i++) {
        if (main[i] != 0x3f800000u) {
            if (why) *why = "solid-quad perspective divisors are not one";
            return false;
        }
    }
    if (main[5] != main[7] || main[5] == main[9] ||
        main[9] != main[11] || main[6] != main[10] ||
        main[6] == main[8] || main[8] != main[12]) {
        if (why) *why = "solid-quad destination is not an axis-aligned rectangle";
        return false;
    }

    float x0, y0, x1, y1;
    if (!mbx_3d_word_to_nonnegative_float(main[5], &x0) ||
        !mbx_3d_word_to_nonnegative_float(main[8], &y0) ||
        !mbx_3d_word_to_nonnegative_float(main[9], &x1) ||
        !mbx_3d_word_to_nonnegative_float(main[6], &y1) ||
        x0 >= x1 || y0 >= y1 || x1 > 320.0f || y1 > 480.0f) {
        if (why) *why = "solid-quad destination coordinates are invalid";
        return false;
    }

    static const unsigned x_words[4] = {5u, 7u, 9u, 11u};
    static const unsigned y_words[4] = {6u, 8u, 10u, 12u};
    uint32_t colour = main[21];
    uint32_t alpha = colour >> 24;
    if ((colour & 0xffu) > alpha || ((colour >> 8) & 0xffu) > alpha ||
        ((colour >> 16) & 0xffu) > alpha) {
        if (why) *why = "solid-quad colour is not premultiplied A8R8G8B8";
        return false;
    }
    for (unsigned vertex = 0; vertex < 4u; vertex++) {
        float x, y;
        unsigned attribute = 21u + vertex * 3u;
        if (!mbx_3d_word_to_nonnegative_float(main[x_words[vertex]], &x) ||
            !mbx_3d_word_to_nonnegative_float(main[y_words[vertex]], &y) ||
            main[attribute] != colour ||
            main[attribute + 1u] != mbx_3d_float_to_word(x / 1024.0f) ||
            main[attribute + 2u] != mbx_3d_float_to_word(y / 1024.0f)) {
            if (why) *why =
                "solid-quad colour or normalized coordinates differ between vertices";
            return false;
        }
    }

    const float lower_bias = 0.468505859375f; /* producer 0x3eefe000 */
    const float upper_bias = 0.531494140625f; /* producer 0x3f081000 */
    uint32_t left = (uint32_t)(x0 + lower_bias);
    uint32_t top = (uint32_t)(y0 + lower_bias);
    uint32_t right = (uint32_t)(x1 + upper_bias);
    uint32_t bottom = (uint32_t)(y1 + upper_bias);
    if (left >= right || top >= bottom || right > MBX_3D_WIDTH ||
        bottom > 480u) {
        if (why) *why = "solid-quad producer bounds leave the 320x480 surface";
        return false;
    }
    uint32_t clip_left = left & ~7u;
    uint32_t clip_right = (right + 7u) & ~7u;
    uint32_t clip_top = top & ~15u;
    uint32_t clip_bottom = (bottom + 15u) & ~15u;
    if (m->reg[S5L_MBX_FBXCLIP / 4u] !=
            ((clip_right << 16) | clip_left) ||
        m->reg[S5L_MBX_FBYCLIP / 4u] !=
            ((clip_bottom << 16) | clip_top)) {
        if (why) *why = "solid-quad clip registers disagree with producer geometry";
        return false;
    }

    uint32_t tile_x0 = clip_left / 8u;
    uint32_t tile_x1 = clip_right / 8u - 1u;
    uint32_t tile_y0 = clip_top / 16u;
    uint32_t tile_y1 = clip_bottom / 16u - 1u;
    uint32_t tile_count = (tile_x1 - tile_x0 + 1u) *
                          (tile_y1 - tile_y0 + 1u);
    if (!tile_count || tile_count > 40u * 30u ||
        (uint64_t)region + (uint64_t)tile_count * 8u > UINT32_MAX) {
        if (why) *why = "solid-quad tile rectangle is invalid";
        return false;
    }
    uint32_t tile_index = 0u;
    for (uint32_t y = tile_y0; y <= tile_y1; y++) {
        for (uint32_t x = tile_x0; x <= tile_x1; x++) {
            uint32_t code, pointer;
            if (!mbx_gart_u32(m, bus, region + tile_index * 8u,
                              &code, why) ||
                !mbx_gart_u32(m, bus, region + tile_index * 8u + 4u,
                              &pointer, why))
                return false;
            uint32_t expected = (y << 8) | x;
            if (tile_index + 1u == tile_count) expected |= 0x80000000u;
            if (code != expected || pointer != list) {
                if (why) *why = "solid-quad region list disagrees with its clip tiles";
                return false;
            }
            tile_index++;
        }
    }

    static const uint32_t background[26] = {
        0xe0000000u, 0xa7718000u, 0u, 0xd6887610u,
        0x22220e80u, 0u, 0u, 0x45000000u,
        0u, 0u, 0x45000000u, 0x3f800000u,
        0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
        0x3f800000u, 0u, 0u, 0u,
        0u, 0x40000000u, 0u, 0u,
        0u, 0x40000000u,
    };
    for (unsigned i = 0; i < 26u; i++) {
        uint32_t value;
        if (!mbx_gart_u32(m, bus, object + i * 4u, &value, why))
            return false;
        if (i == 2u) {
            if ((value & ~MBX_3D_ADDRESS_MASK) != 0x0e500000u ||
                mbx_3d_decode_address(value) != target) {
                if (why) *why = "solid-quad background does not resolve to FBSTART";
                return false;
            }
        } else if (value != background[i]) {
            if (why) *why = "solid-quad background object is unknown";
            return false;
        }
    }

    for (uint32_t off = 0x80u; off < 0x0e8u; off += 4u) {
        uint32_t value;
        if (!mbx_gart_u32(m, bus, object + off, &value, why)) return false;
        uint32_t expected = mbx_3d_boundary_fixed_expected(off);
        if (off >= 0x0b8u && off <= 0x0d4u)
            expected = main[5u + (off - 0x0b8u) / 4u];
        if (value != expected) {
            if (why) *why = "solid-quad boundary object disagrees with its geometry";
            return false;
        }
    }

    static const uint32_t parameter_controls[4] = {
        0x22620ea0u, 0x46622ea0u, 0x66622ea0u, 0x82622ea0u
    };
    for (unsigned record = 0; record < 4u; record++) {
        uint32_t base = solid + (record + 1u) * 33u * 4u;
        for (unsigned i = 0; i < 33u; i++) {
            uint32_t value;
            if (!mbx_gart_u32(m, bus, base + i * 4u, &value, why))
                return false;
            uint32_t expected = 0u;
            if (i == 0u) expected = 0xe0000000u;
            else if (i == 3u) expected = 0x86084610u;
            else if (i == 4u) expected = parameter_controls[record];
            else if (i >= 17u && i <= 20u) expected = 0x3f800000u;
            else if (i >= 21u && (i - 21u) % 3u == 0u)
                expected = 0xffffffffu;
            if (value != expected) {
                if (why) *why = "solid-quad parameter records are inconsistent";
                return false;
            }
        }
    }

    uint32_t width = right - left;
    uint32_t height = bottom - top;
    uint32_t row_bytes = width * 4u;
    uint32_t total = row_bytes * height;
    uint64_t target_end = (uint64_t)target +
        (uint64_t)(bottom - 1u) * MBX_3D_TARGET_STRIDE +
        (uint64_t)right * 4u;
    if (target_end > UINT32_MAX) {
        if (why) *why = "solid-quad destination rectangle overflows";
        return false;
    }
    for (uint32_t row = 0; row < height; row++) {
        uint32_t dst = target + (top + row) * MBX_3D_TARGET_STRIDE +
                       left * 4u;
        if (!mbx_gart_validate(m, bus, dst, row_bytes, why)) return false;
    }

    uint8_t *pixels = malloc(total);
    if (!pixels) {
        if (why) *why = "host allocation for staged solid quad failed";
        return false;
    }
    bool ok = true;
    for (uint32_t row = 0; row < height && ok; row++) {
        uint32_t dst = target + (top + row) * MBX_3D_TARGET_STRIDE +
                       left * 4u;
        ok = mbx_gart_read(m, bus, dst, pixels + row * row_bytes,
                           row_bytes, why);
    }
    for (uint32_t i = 0; i < total && ok; i += 4u) {
        uint32_t blended = mbx_source_over_clamped(
            mbx_load_le32(pixels + i), colour);
        pixels[i] = (uint8_t)blended;
        pixels[i + 1u] = (uint8_t)(blended >> 8);
        pixels[i + 2u] = (uint8_t)(blended >> 16);
        pixels[i + 3u] = (uint8_t)(blended >> 24);
    }
    for (uint32_t row = 0; row < height && ok; row++) {
        uint32_t dst = target + (top + row) * MBX_3D_TARGET_STRIDE +
                       left * 4u;
        ok = mbx_gart_write(m, bus, dst, pixels + row * row_bytes,
                            row_bytes, why);
    }
    free(pixels);
    if (ok && pixels_filled) *pixels_filled = width * height;
    return ok;
}

/* MBX2D has two related textured producers. _mbx3DCtxQuadCopyPerspective
 * emits the measured 44-word source-over record selected by 0x61a. The
 * shipped _mbx3DCtxBlitCopy fast path emits a compact 33-word opaque-copy
 * record selected by 0x612: it omits the blend-surface state and the four
 * redundant normalized-destination pairs, but retains independently encoded
 * geometry, UVs, texture allocation, boundary, clip and tiles. Decode both
 * through one sampler so their filtering and guard rules cannot drift. */
static bool mbx_execute_textured_sprite(s5l_mbx_t *m,
                                        const arm_bus_t *bus,
                                        const char **why,
                                        uint32_t *pixels_blended) {
    uint32_t region = m->reg[S5L_MBX_RGNBASE / 4u];
    uint32_t object = m->reg[S5L_MBX_OBJBASE / 4u];
    uint32_t target = m->reg[S5L_MBX_FBSTART / 4u];
    if ((region & 3u) || (object & 3u) || (target & 3u) ||
        object > UINT32_MAX - 0x0e8u) {
        if (why) *why = "sprite region, object, or framebuffer base is invalid";
        return false;
    }
    if (m->reg[S5L_MBX_3DPIXSAMP / 4u] != 0x00020007u ||
        m->reg[S5L_MBX_FBCTL / 4u] != 0x00000006u ||
        m->reg[S5L_MBX_FBLINESTRIDE / 4u] != MBX_3D_WIDTH) {
        if (why) *why =
            "render registers are not the measured textured-sprite family";
        return false;
    }

    uint32_t list = object + 0x68u;
    uint32_t list_words[4];
    for (unsigned i = 0; i < 4u; i++)
        if (!mbx_gart_u32(m, bus, list + i * 4u, &list_words[i], why))
            return false;
    bool perspective_copy = list_words[2] == 0x61a0007cu;
    bool compact_copy =
        (list_words[2] & 0xfff00000u) == 0x61200000u;
    if (list_words[0] != 0x60200020u ||
        list_words[1] != 0x6020002du ||
        (!perspective_copy && !compact_copy) ||
        list_words[3] != 0xf0000000u) {
        if (why) *why = "sprite list is not a supported three-object pointer form";
        return false;
    }

    if (perspective_copy) {
        static const uint32_t background[26] = {
            0xe0000000u, 0xa7718000u, 0u, 0xd6887610u,
            0x22220e80u, 0u, 0u, 0x45000000u,
            0u, 0u, 0x45000000u, 0x3f800000u,
            0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
            0x3f800000u, 0u, 0u, 0u,
            0u, 0x40000000u, 0u, 0u,
            0u, 0x40000000u,
        };
        for (unsigned i = 0; i < 26u; i++) {
            uint32_t value;
            if (!mbx_gart_u32(m, bus, object + i * 4u, &value, why))
                return false;
            if (i == 2u) {
                if ((value & ~MBX_3D_ADDRESS_MASK) != 0x0e500000u ||
                    mbx_3d_decode_address(value) != target) {
                    if (why) *why =
                        "sprite background does not resolve to FBSTART";
                    return false;
                }
            } else if (value != background[i]) {
                if (why) *why = "sprite background object is unknown";
                return false;
            }
        }
    }

    uint32_t quad[44] = {0};
    if (perspective_copy) {
        if (object > UINT32_MAX - 0x2a0u) {
            if (why) *why = "perspective sprite object span overflows";
            return false;
        }
        for (unsigned i = 0; i < 44u; i++)
            if (!mbx_gart_u32(m, bus, object + 0x1f0u + i * 4u,
                              &quad[i], why))
                return false;
    } else {
        uint64_t compact64 = (uint64_t)object +
            (uint64_t)(list_words[2] & 0x000fffffu) * 4u;
        if (compact64 < (uint64_t)object + 0x0e8u ||
            compact64 + 33u * 4u > (uint64_t)UINT32_MAX + 1u) {
            if (why) *why = "compact-copy object pointer is outside its safe span";
            return false;
        }
        uint32_t compact = (uint32_t)compact64;
        uint32_t record[33];
        for (unsigned i = 0; i < 33u; i++)
            if (!mbx_gart_u32(m, bus, compact + i * 4u,
                              &record[i], why))
                return false;
        for (unsigned i = 0; i < 4u; i++) quad[i] = record[i];
        quad[7] = record[4];
        for (unsigned i = 0; i < 16u; i++)
            quad[8u + i] = record[5u + i];
        for (unsigned vertex = 0; vertex < 4u; vertex++) {
            unsigned compact_attribute = 21u + vertex * 3u;
            unsigned quad_attribute = 24u + vertex * 5u;
            quad[quad_attribute] = record[compact_attribute];
            quad[quad_attribute + 1u] = record[compact_attribute + 1u];
            quad[quad_attribute + 2u] = record[compact_attribute + 2u];
        }
    }
    bool direct_sampler = compact_copy
        ? quad[3] == 0xa6887610u
        : quad[3] == 0xa6884710u && quad[6] == 0xae504ea0u;
    bool modulated_sampler = quad[3] == 0xcd206c40u &&
                             quad[6] == 0xae504ea0u;
    bool scaled_sampler = quad[3] == 0xd6887610u &&
                          quad[6] == 0xa3104620u;
    if (quad[0] != 0xe0000000u ||
        (!direct_sampler && !modulated_sampler && !scaled_sampler) ||
        (compact_copy
            ? quad[7] != 0x22220e80u
            : quad[4] != 0xa7718000u || quad[7] != 0x22250e80u)) {
        if (why) *why = "sprite quad setup is not a measured BGRA8 copy form";
        return false;
    }
    if (!compact_copy &&
        ((quad[5] & ~MBX_3D_ADDRESS_MASK) != 0x0e500000u ||
         mbx_3d_decode_address(quad[5]) != target)) {
        if (why) *why = "sprite blend surface differs from FBSTART";
        return false;
    }
    for (unsigned i = 16u; i < 20u; i++) {
        if (quad[i] != 0u) {
            if (why) *why = "sprite perspective terms are nonzero";
            return false;
        }
    }
    for (unsigned i = 20u; i < 24u; i++) {
        if (quad[i] != 0x3f800000u) {
            if (why) *why = "sprite perspective divisors are not one";
            return false;
        }
    }
    static const unsigned alpha_words[4] = {24u, 29u, 34u, 39u};
    uint32_t vertex_alpha_word = quad[alpha_words[0]];
    /* Opaque compact copies bypass vertex modulation in the commit loop. The
     * producer has emitted both all-zero unused colour words (Safari) and the
     * older all-opaque form; require one of those exact uniform encodings. */
    bool compact_unused_colour = compact_copy &&
        (vertex_alpha_word == 0u || vertex_alpha_word == 0xff000000u);
    if ((vertex_alpha_word & 0x00ffffffu) ||
        (direct_sampler && !compact_unused_colour &&
         vertex_alpha_word != 0xff000000u)) {
        if (mbx_trace_state == 1) {
            fprintf(stderr,
                    "MBX3D sprite vertex modulation=%08x,%08x,%08x,%08x "
                    "direct=%u compact=%u\n",
                    quad[alpha_words[0]], quad[alpha_words[1]],
                    quad[alpha_words[2]], quad[alpha_words[3]],
                    direct_sampler ? 1u : 0u, compact_copy ? 1u : 0u);
        }
        if (why) *why = "sprite vertex modulation is invalid for its sampler";
        return false;
    }
    for (unsigned i = 1u; i < 4u; i++) {
        if (quad[alpha_words[i]] != vertex_alpha_word) {
            if (why) *why = "sprite vertex modulation differs between vertices";
            return false;
        }
    }

    uint32_t source_control = quad[2] & ~MBX_3D_ADDRESS_MASK;
    bool half_texel_layout = (source_control & 0x80000000u) != 0u;
    /* Texture filtering and corner order are independent producer choices.
     * Direct packets retain row-major p00,p10,p01,p11 corners in both the
     * half-texel 0x8e form and Spotlight's measured full-extent 0x0e form.
     * The older modulated 0x0e producer uses the alternate ordering below.
     * No full-extent packet has established alternate-sampler semantics. */
    bool row_major_corners = half_texel_layout || direct_sampler;
    if (!half_texel_layout && scaled_sampler) {
        if (why) *why = "full-extent alternate sampler is unmeasured";
        return false;
    }
    const float epsilon = 0.0009765625f;
    float destination_x[4], destination_y[4];
    for (unsigned i = 0; i < 4u; i++) {
        if (!mbx_3d_word_to_finite_float(quad[8u + i * 2u],
                                         &destination_x[i]) ||
            !mbx_3d_word_to_finite_float(quad[9u + i * 2u],
                                         &destination_y[i])) {
            if (why) *why = "sprite destination coordinates are not finite";
            return false;
        }
    }
    bool axis_aligned = row_major_corners
        ? quad[8] == quad[12] && quad[10] == quad[14] &&
          quad[9] == quad[11] && quad[13] == quad[15]
        : quad[8] == quad[10] && quad[12] == quad[14] &&
          quad[9] == quad[13] && quad[11] == quad[15];
    unsigned p00 = row_major_corners ? 0u : 1u;
    unsigned p10 = row_major_corners ? 1u : 3u;
    unsigned p01 = row_major_corners ? 2u : 0u;
    unsigned p11 = row_major_corners ? 3u : 2u;
    float x0 = destination_x[p00], y0 = destination_y[p00];
    float x1 = destination_x[p11], y1 = destination_y[p11];
    struct mbx_affine_transform affine = {0};
    bool affine_sprite = !axis_aligned;
    if (compact_copy && affine_sprite) {
        if (why) *why = "compact blit-copy destination is not axis-aligned";
        return false;
    }
    if (axis_aligned) {
        if (x0 >= x1 || y0 >= y1) {
            if (why) *why = "sprite destination coordinates are invalid";
            return false;
        }
    } else {
        /* r434's first wiggle-mode render is a rigid affine instance of the
         * direct filtered producer. r438 contributes the modulated producer's
         * uniformly scaled affine form. Both have zero perspective and four
         * corners that close to a parallelogram. Keep unfiltered, alternate-
         * sampler, perspective, shear, and arbitrary four-point warps
         * rejected. */
        float closure_x = destination_x[p00] + destination_x[p11] -
                          destination_x[p10] - destination_x[p01];
        float closure_y = destination_y[p00] + destination_y[p11] -
                          destination_y[p10] - destination_y[p01];
        if (!half_texel_layout ||
            (!direct_sampler && !modulated_sampler) ||
            closure_x < -epsilon || closure_x > epsilon ||
            closure_y < -epsilon || closure_y > epsilon) {
            if (why) *why =
                "sprite destination is neither axis-aligned nor a measured affine quad";
            return false;
        }
        affine.origin_x = destination_x[p00];
        affine.origin_y = destination_y[p00];
        affine.u_x = destination_x[p10] - destination_x[p00];
        affine.u_y = destination_y[p10] - destination_y[p00];
        affine.v_x = destination_x[p01] - destination_x[p00];
        affine.v_y = destination_y[p01] - destination_y[p00];
        affine.determinant = affine.u_x * affine.v_y -
                             affine.u_y * affine.v_x;
        if (affine.determinant <= 0.0f) {
            if (why) *why = "affine sprite orientation is degenerate or reversed";
            return false;
        }
        x0 = x1 = destination_x[0];
        y0 = y1 = destination_y[0];
        for (unsigned i = 1u; i < 4u; i++) {
            if (destination_x[i] < x0) x0 = destination_x[i];
            if (destination_x[i] > x1) x1 = destination_x[i];
            if (destination_y[i] < y0) y0 = destination_y[i];
            if (destination_y[i] > y1) y1 = destination_y[i];
        }
        if (x0 >= x1 || y0 >= y1) {
            if (why) *why = "affine sprite has empty destination bounds";
            return false;
        }
    }

    if (x0 <= -(float)INT32_MAX || y0 <= -(float)INT32_MAX ||
        x1 >= (float)INT32_MAX || y1 >= (float)INT32_MAX) {
        if (why) *why = "sprite destination coordinates are invalid";
        return false;
    }

    static const unsigned destination_words[8] = {
        8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u
    };
    static const unsigned normalized_words[8] = {
        27u, 28u, 32u, 33u, 37u, 38u, 42u, 43u
    };
    for (unsigned i = 0; !compact_copy && i < 8u; i++) {
        float coordinate;
        if (!mbx_3d_word_to_finite_float(
                quad[destination_words[i]], &coordinate) ||
            quad[normalized_words[i]] !=
                mbx_3d_float_to_word(coordinate / 1024.0f)) {
            if (why) *why = "sprite normalized destination disagrees with its quad";
            return false;
        }
    }

    /* These are the exact positive float literals loaded at 0x30e1d258/25c
     * by _mbx3DCtxQuadCopyPerspective before VCVT.U32.F32 and subsequent 8x16
     * tile alignment. The producer first intersects its float extrema with the
     * context bounds. The surface intersection below avoids relying on a C
     * conversion whose negative-input behaviour differs from ARM saturation;
     * the boundary object then supplies any stricter integer context scissor. */
    const float lower_bias = 0.468505859375f; /* 0x3eefe000 */
    const float upper_bias = 0.531494140625f; /* 0x3f081000 */
    float dx = x1 - x0;
    float dy = y1 - y0;
    /* UI transitions can emit a positive subpixel extent on one axis while
     * still covering one or more pixel centres. Empty and reversed geometry
     * was rejected above; the scale family, pixel-centre coverage, boundary,
     * clip, tile, source and target checks below remain authoritative. */
    if (dx > 512.0f + epsilon || dy > 512.0f + epsilon) {
        if (why) *why = "sprite transform exceeds the bounded textured family";
        return false;
    }

    /* Source dimensions are encoded independently of destination geometry.
     * Invert the producer's normalized UV extent first, then decode the
     * power-of-two allocation and split pitch independently.  This is what
     * distinguishes r420's 320x460 texture from its 28.8x41.4 destination. */
    uint32_t header_width_field = (quad[1] >> 24) & 7u;
    uint32_t header_height_field = (quad[1] >> 20) & 7u;
    uint32_t header_texture_width = 8u << header_width_field;
    uint32_t header_texture_height = 8u << header_height_field;
    uint32_t u0_word = quad[25];
    uint32_t v0_word = row_major_corners ? quad[26] : quad[31];
    uint32_t u1_word = row_major_corners ? quad[30] : quad[35];
    uint32_t v1_word = row_major_corners ? quad[36] : quad[26];
    bool uv_rectangle = row_major_corners
        ? quad[35] == u0_word && quad[31] == v0_word &&
          quad[40] == u1_word && quad[41] == v1_word
        : quad[30] == u0_word && quad[41] == v0_word &&
          quad[40] == u1_word && quad[36] == v1_word;
    float normalized_u0, normalized_v0, normalized_u1, normalized_v1;
    if (!uv_rectangle ||
        !mbx_3d_word_to_nonnegative_float(u0_word, &normalized_u0) ||
        !mbx_3d_word_to_nonnegative_float(v0_word, &normalized_v0) ||
        !mbx_3d_word_to_nonnegative_float(u1_word, &normalized_u1) ||
        !mbx_3d_word_to_nonnegative_float(v1_word, &normalized_v1)) {
        if (why) *why =
            "sprite UVs are not an axis-aligned finite rectangle";
        return false;
    }
    float u_texel_start =
        normalized_u0 * (float)header_texture_width;
    float v_texel_start =
        normalized_v0 * (float)header_texture_height;
    float u_texel_end =
        normalized_u1 * (float)header_texture_width;
    float v_texel_end =
        normalized_v1 * (float)header_texture_height;
    /* A constant-u strip (both u edges on one texel column) is a legitimate
     * gradient stretch: SpringBoard's app-close animation samples a single
     * column of an 8x256 ramp across the full 320-pixel width. */
    if (u_texel_start > u_texel_end ||
        v_texel_start >= v_texel_end ||
        u_texel_end > 512.0f || v_texel_end > 512.0f ||
        u_texel_end > (float)header_texture_width ||
        v_texel_end > (float)header_texture_height) {
        if (why) *why = "sprite UV rectangle leaves the supported texture bounds";
        return false;
    }
    uint32_t source_left = (uint32_t)u_texel_start;
    uint32_t source_top = (uint32_t)v_texel_start;
    uint32_t source_right = (uint32_t)u_texel_end;
    uint32_t source_bottom = (uint32_t)v_texel_end;
    source_right += (float)source_right < u_texel_end;
    source_bottom += (float)source_bottom < v_texel_end;
    uint32_t source_width = source_right - source_left;
    uint32_t source_height = source_bottom - source_top;
    float u_texel_span = u_texel_end - u_texel_start;
    float v_texel_span = v_texel_end - v_texel_start;
    bool compact_full_extent_uniform_minification = false;

    /* The texture allocation, UV rectangle and linear row pitch are independent
     * producer inputs.  _mbx3DCtxQuadCopyPerspective derives the header power
     * from its surface-width argument at 0x30e1ced4..0x30e1cf18, while the row
     * bytes arrive independently through context+0x14.  Retained Safari unlock
     * packets place 8- or 16-pixel allocations inside 72- or 320-pixel rows.
     * Requiring header width to equal the smallest power covering the pitch
     * therefore rejects valid padded-row sub-rectangles.  Decode both fields
     * independently, while requiring the sampled UV footprint to fit both.
     *
     * BGRA8 rows are eight-pixel aligned, so pitch_bytes/16 is even.  Bits
     * 2..7 stay in source-control bits 18..23 and bit 1 moves to header bit 0.
     * Bit 0 is consequently zero rather than an omitted unknown. */
    uint32_t pitch_units = ((source_control >> 16) & 0xfcu) |
                           ((quad[1] & 1u) << 1);
    uint32_t source_stride = pitch_units * 16u;
    uint32_t source_pitch_pixels = source_stride / 4u;
    if (!pitch_units || source_pitch_pixels > 512u ||
        header_texture_width > 512u || header_texture_height > 512u ||
        source_right > source_pitch_pixels ||
        source_bottom > header_texture_height) {
        if (why) *why =
            "sprite UV rectangle exceeds its encoded texture allocation";
        return false;
    }
    uint32_t expected_header = 0xa0018000u |
        (header_width_field << 24) | (header_height_field << 20) |
        ((pitch_units & 2u) >> 1);
    uint32_t expected_source_control =
        (half_texel_layout ? 0x8e000000u : 0x0e000000u) |
        ((pitch_units & ~3u) << 16);
    if (quad[1] != expected_header ||
        (quad[2] & ~MBX_3D_ADDRESS_MASK) != expected_source_control) {
        if (why) *why =
            "sprite texture allocation or split pitch is inconsistent";
        return false;
    }

    if (affine_sprite) {
        float expected_u2 = (float)source_width * (float)source_width;
        float expected_v2 = (float)source_height * (float)source_height;
        float expected_det = (float)source_width * (float)source_height;
        float u2 = affine.u_x * affine.u_x + affine.u_y * affine.u_y;
        float v2 = affine.v_x * affine.v_x + affine.v_y * affine.v_y;
        float dot = affine.u_x * affine.v_x + affine.u_y * affine.v_y;
        if (dot < 0.0f) dot = -dot;
        if (source_width > MBX_3D_WIDTH || source_height > 480u) {
            if (why) *why = "affine sprite source is outside measured bounds";
            return false;
        }
        if (direct_sampler) {
            float u_error = u2 - expected_u2;
            float v_error = v2 - expected_v2;
            float det_error = affine.determinant - expected_det;
            float tolerance =
                ((float)source_width + (float)source_height) * epsilon;
            if (u_error < 0.0f) u_error = -u_error;
            if (v_error < 0.0f) v_error = -v_error;
            if (det_error < 0.0f) det_error = -det_error;
            if (u_error > tolerance || v_error > tolerance ||
                dot > tolerance || det_error > tolerance) {
                if (why) *why =
                    "direct affine sprite is not the measured rigid unity transform";
                return false;
            }
        } else {
            float scale2 = u2 / expected_u2;
            float v_error = v2 - expected_v2 * scale2;
            float det_error = affine.determinant - expected_det * scale2;
            float tolerance =
                ((float)source_width + (float)source_height) * epsilon *
                (scale2 > 1.0f ? scale2 : 1.0f);
            if (v_error < 0.0f) v_error = -v_error;
            if (det_error < 0.0f) det_error = -det_error;
            if (scale2 <= 0.0f || v_error > tolerance ||
                dot > tolerance || det_error > tolerance) {
                if (why) *why =
                    "modulated affine sprite is not a measured uniform similarity transform";
                return false;
            }
        }
    } else {
        bool unity_transform =
            dx >= (float)source_width - epsilon &&
            dx <= (float)source_width + epsilon &&
            dy >= (float)source_height - epsilon &&
            dy <= (float)source_height + epsilon;
        if (!unity_transform) {
            float scale_x = dx / (float)source_width;
            float scale_y = dy / (float)source_height;
            float scale_difference = scale_x > scale_y
                ? scale_x - scale_y : scale_y - scale_x;
            /* The device unlock capture is a direct-filtered 67x20 source at
             * 0.724135x in both axes. Rejecting every direct minification left
             * 3DIdle false and sent AppleMBX's watchdog into an endless
             * Graphics Recovery Event loop. Preserve the previously measured
             * nonuniform magnification family, but admit minification only
             * when both axes carry the captured uniform-scale invariant. */
            bool direct_magnification = direct_sampler && half_texel_layout &&
                scale_x >= 1.0f - epsilon && scale_y >= 1.0f - epsilon;
            bool direct_uniform_minification =
                direct_sampler && half_texel_layout &&
                scale_x > 0.0f && scale_y > 0.0f &&
                scale_x <= 1.0f + epsilon && scale_y <= 1.0f + epsilon &&
                scale_difference <= 0.00001f;
            /* Safari's keyboard/address transition contributes a direct-
             * filtered row operation with a wide source rectangle. It keeps
             * the conservative source height exactly 1:1 while reducing only
             * X. Treat that one-dimensional resample as its own family: this
             * does not admit vertical minification, mixed two-axis scaling,
             * or an unchecked generic textured quad. */
            bool direct_horizontal_minification =
                direct_sampler && half_texel_layout && source_width > 2u &&
                scale_x > 0.0f && scale_x <= 1.0f + epsilon &&
                scale_y >= 1.0f - epsilon && scale_y <= 1.0f + epsilon;
            /* The compact blit producer used while unlocking back into
             * Safari carries the same direct filtered sampler as the older
             * compact records, but selects the 0x0e texture-coordinate layout.
             * Its independently encoded UV rectangle starts on integer texels
             * and ends exactly one half texel inside both conservative source
             * bounds.  Two retained phases uniformly reduce the same 320x356
             * source to different subpixel rectangles.  Keep this distinct
             * from the genuinely unfiltered 0x0e perspective producer: both
             * axes must be strict, positive, uniform minification and neither
             * source axis may collapse into the narrow-strip family. */
            bool compact_half_texel_envelope =
                compact_copy && direct_sampler && !half_texel_layout &&
                source_width > 2u && source_height > 2u &&
                u_texel_start == (float)source_left &&
                v_texel_start == (float)source_top &&
                u_texel_end == (float)source_right - 0.5f &&
                v_texel_end == (float)source_bottom - 0.5f;
            compact_full_extent_uniform_minification =
                compact_half_texel_envelope &&
                scale_x > 0.0f && scale_y > 0.0f &&
                scale_x < 1.0f - epsilon &&
                scale_y < 1.0f - epsilon &&
                scale_difference <= 0.00001f;
            /* Retained direct, alternate and modulated producer packets all
             * use a one-texel-or-narrower horizontal UV strip. They magnify
             * that strip in X while retaining or reducing its rows in Y. The
             * Spotlight return packet's normalized float round-trip lands
             * 0.00000191 texels above one, so its conservative floor/ceil
             * envelope spans two columns. Classify the sampled UV span, while
             * bounding that envelope to two columns; genuinely wider strips
             * and vertical magnification still require another measured
             * family. */
            bool filtered_narrow_strip_resample =
                (direct_sampler || modulated_sampler || scaled_sampler) &&
                half_texel_layout && source_width <= 2u &&
                u_texel_span <= 1.0f + epsilon &&
                scale_x >= 1.0f - epsilon &&
                scale_y > 0.0f && scale_y <= 1.0f + epsilon;
            bool modulated_uniform_scale =
                modulated_sampler && half_texel_layout &&
                scale_x > 0.0f && scale_y > 0.0f &&
                scale_difference <= 0.00001f;
            bool alternate_uniform_minification =
                scaled_sampler && half_texel_layout &&
                scale_x > 0.0f && scale_y > 0.0f &&
                scale_x <= 1.0f + epsilon && scale_y <= 1.0f + epsilon &&
                scale_difference <= 0.00001f;
            if (source_width > MBX_3D_WIDTH || source_height > 480u ||
                (!direct_magnification && !direct_uniform_minification &&
                 !direct_horizontal_minification &&
                 !compact_full_extent_uniform_minification &&
                 !filtered_narrow_strip_resample &&
                 !modulated_uniform_scale &&
                 !alternate_uniform_minification)) {
                if (mbx_trace_state == 1) {
                    fprintf(stderr,
                            "MBX3D transform reject: sampler=%s half=%u "
                            "source=%ux%u uv-span=%.9gx%.9g "
                            "destination=%.9gx%.9g scale=%.9gx%.9g "
                            "difference=%.9g quad=%08x/%08x/%08x/%08x/%08x\n",
                            direct_sampler ? "direct" :
                            (modulated_sampler ? "modulated" : "scaled"),
                            half_texel_layout ? 1u : 0u,
                            source_width, source_height,
                            (double)u_texel_span, (double)v_texel_span,
                            (double)dx, (double)dy,
                            (double)scale_x, (double)scale_y,
                            (double)scale_difference,
                            quad[1], quad[2], quad[3], quad[6], quad[7]);
                }
                if (why) *why =
                    "filtered transform is outside its measured sampler scale family";
                return false;
            }
        } else if (scaled_sampler) {
            if (why) *why =
                "alternate filtered state lacks its measured minification";
            return false;
        }
    }
    bool filtered_sampling = half_texel_layout ||
                             compact_full_extent_uniform_minification;
    float bounded_x0 = x0 < 0.0f ? 0.0f : x0;
    float bounded_y0 = y0 < 0.0f ? 0.0f : y0;
    float bounded_x1 = x1 > (float)MBX_3D_WIDTH
        ? (float)MBX_3D_WIDTH : x1;
    float bounded_y1 = y1 > 480.0f ? 480.0f : y1;
    if (bounded_x0 >= bounded_x1 || bounded_y0 >= bounded_y1) {
        if (why) *why = "sprite does not intersect the 320x480 surface";
        return false;
    }

    uint32_t natural_left = (uint32_t)bounded_x0;
    uint32_t natural_top = (uint32_t)bounded_y0;
    uint32_t natural_right = (uint32_t)bounded_x1 +
        ((float)(uint32_t)bounded_x1 != bounded_x1);
    uint32_t natural_bottom = (uint32_t)bounded_y1 +
        ((float)(uint32_t)bounded_y1 != bounded_y1);
    uint32_t boundary[8] = {0};
    for (uint32_t off = 0x80u; off < 0x0e8u; off += 4u) {
        uint32_t value;
        if (!mbx_gart_u32(m, bus, object + off, &value, why)) return false;
        if (off >= 0x0b8u && off <= 0x0d4u) {
            float coordinate;
            if (!mbx_3d_word_to_nonnegative_float(value, &coordinate) ||
                coordinate > 480.0f ||
                coordinate != (float)(uint32_t)coordinate) {
                if (why) *why =
                    "sprite boundary object is not an integer clipped quad";
                return false;
            }
            boundary[(off - 0x0b8u) / 4u] = (uint32_t)coordinate;
        } else if (value != mbx_3d_boundary_fixed_expected(off)) {
            if (why) *why = "sprite boundary object setup is unknown";
            return false;
        }
    }
    uint32_t boundary_left = boundary[0];
    uint32_t boundary_bottom = boundary[1];
    uint32_t boundary_top = boundary[3];
    uint32_t boundary_right = boundary[4];
    if (boundary[2] != boundary_left || boundary[5] != boundary_bottom ||
        boundary[6] != boundary_right || boundary[7] != boundary_top ||
        boundary_left >= boundary_right || boundary_top >= boundary_bottom ||
        boundary_right > MBX_3D_WIDTH || boundary_bottom > 480u ||
        boundary_left < natural_left || boundary_top < natural_top ||
        boundary_right > natural_right || boundary_bottom > natural_bottom) {
        if (why) *why =
            "sprite boundary object is not a clipped subset of its quad";
        return false;
    }

    /* An inward edge is the integer context scissor measured in r418. Natural
     * floor/ceil edges retain the original float extrema so the producer's
     * asymmetric guard rounding remains independently checkable. */
    float producer_x0 = boundary_left > natural_left
        ? (float)boundary_left : bounded_x0;
    float producer_y0 = boundary_top > natural_top
        ? (float)boundary_top : bounded_y0;
    float producer_x1 = boundary_right < natural_right
        ? (float)boundary_right : bounded_x1;
    float producer_y1 = boundary_bottom < natural_bottom
        ? (float)boundary_bottom : bounded_y1;
    uint32_t guard_left = (uint32_t)(producer_x0 + lower_bias);
    uint32_t guard_top = (uint32_t)(producer_y0 + lower_bias);
    uint32_t guard_right = (uint32_t)(producer_x1 + upper_bias);
    uint32_t guard_bottom = (uint32_t)(producer_y1 + upper_bias);

    /* Standard pixel-centre coverage can be empty even when the producer's
     * conservative, tile-aligned region is non-empty.  Work it out before
     * rejecting a collapsed integer guard: the fully validated command must
     * still complete so AppleMBX can return to 3DIdle. */
    int32_t raster_left_unclipped = mbx_3d_ceil_to_i32(x0 - 0.5f);
    int32_t raster_top_unclipped = mbx_3d_ceil_to_i32(y0 - 0.5f);
    int32_t raster_right_unclipped = mbx_3d_ceil_to_i32(x1 - 0.5f);
    int32_t raster_bottom_unclipped = mbx_3d_ceil_to_i32(y1 - 0.5f);
    int32_t raster_left = raster_left_unclipped < 0
        ? 0 : raster_left_unclipped;
    int32_t raster_top = raster_top_unclipped < 0
        ? 0 : raster_top_unclipped;
    int32_t raster_right = raster_right_unclipped > (int32_t)MBX_3D_WIDTH
        ? (int32_t)MBX_3D_WIDTH : raster_right_unclipped;
    int32_t raster_bottom = raster_bottom_unclipped > 480
        ? 480 : raster_bottom_unclipped;
    if (raster_left < (int32_t)boundary_left)
        raster_left = (int32_t)boundary_left;
    if (raster_top < (int32_t)boundary_top)
        raster_top = (int32_t)boundary_top;
    if (raster_right > (int32_t)boundary_right)
        raster_right = (int32_t)boundary_right;
    if (raster_bottom > (int32_t)boundary_bottom)
        raster_bottom = (int32_t)boundary_bottom;
    bool zero_coverage =
        raster_left >= raster_right || raster_top >= raster_bottom;
    uint32_t left = (uint32_t)raster_left;
    uint32_t top = (uint32_t)raster_top;
    uint32_t right = (uint32_t)raster_right;
    uint32_t bottom = (uint32_t)raster_bottom;
    uint32_t width = zero_coverage ? 0u : right - left;
    uint32_t height = zero_coverage ? 0u : bottom - top;

    bool collapsed_guard =
        guard_left >= guard_right || guard_top >= guard_bottom;
    if ((collapsed_guard && !zero_coverage) ||
        guard_right > MBX_3D_WIDTH || guard_bottom > 480u) {
        if (why) *why = "sprite producer bounds leave the 320x480 surface";
        return false;
    }

    uint32_t source_x0 = 0u, source_y0 = 0u;
    if (!filtered_sampling && !zero_coverage) {
        /* The remaining 0x0e producer order is unfiltered.  Its integer-sized
         * unity transform must still select one strict contiguous source crop.
         * The crop does not have to begin at texture origin: r430 copies
         * source rows 20..479 to destination rows 20..479 from a 320x480
         * surface inside a 512x512 allocation.  Require integer UV edges so
         * this remains a direct texel copy rather than inventing nearest-
         * neighbour semantics for fractional unfiltered coordinates. */
        int32_t full_raster_width =
            raster_right_unclipped - raster_left_unclipped;
        int32_t full_raster_height =
            raster_bottom_unclipped - raster_top_unclipped;
        bool integer_source_rectangle =
            u_texel_start == (float)source_left &&
            v_texel_start == (float)source_top &&
            u_texel_end == (float)source_right &&
            v_texel_end == (float)source_bottom;
        if (!integer_source_rectangle ||
            full_raster_width != (int32_t)source_width ||
            full_raster_height != (int32_t)source_height ||
            raster_left < raster_left_unclipped ||
            raster_top < raster_top_unclipped ||
            raster_right > raster_right_unclipped ||
            raster_bottom > raster_bottom_unclipped) {
            if (why) *why =
                "unfiltered sprite coverage is not a contiguous 1:1 crop";
            return false;
        }
        source_x0 = source_left +
                    (uint32_t)(raster_left - raster_left_unclipped);
        source_y0 = source_top +
                    (uint32_t)(raster_top - raster_top_unclipped);
        if (source_x0 + width > source_right ||
            source_y0 + height > source_bottom) {
            if (why) *why = "unfiltered sprite crop exceeds its source";
            return false;
        }
    }

    uint32_t source = mbx_3d_decode_address(quad[2]);
    if (!source) {
        if (why) *why = "sprite source resolves to GPU address zero";
        return false;
    }

    uint32_t clip_left = guard_left & ~7u;
    uint32_t clip_right = (guard_right + 7u) & ~7u;
    uint32_t clip_top = guard_top & ~15u;
    uint32_t clip_bottom = (guard_bottom + 15u) & ~15u;
    if (clip_left >= clip_right || clip_top >= clip_bottom ||
        clip_right > MBX_3D_WIDTH || clip_bottom > 480u) {
        if (why) {
            *why = "sprite aligned clip rectangle is empty or out of bounds";
        }
        return false;
    }
    uint32_t expected_xclip = (clip_right << 16) | clip_left;
    uint32_t expected_yclip = (clip_bottom << 16) | clip_top;
    if (m->reg[S5L_MBX_FBXCLIP / 4u] != expected_xclip ||
        m->reg[S5L_MBX_FBYCLIP / 4u] != expected_yclip) {
        if (why) *why = "sprite clip registers disagree with producer geometry";
        return false;
    }

    uint32_t tile_x0 = clip_left / 8u;
    uint32_t tile_x1 = clip_right / 8u - 1u;
    uint32_t tile_y0 = clip_top / 16u;
    uint32_t tile_y1 = clip_bottom / 16u - 1u;
    uint32_t tile_count = (tile_x1 - tile_x0 + 1u) *
                          (tile_y1 - tile_y0 + 1u);
    if (!tile_count || tile_count > 40u * 30u ||
        (uint64_t)region + (uint64_t)tile_count * 8u > UINT32_MAX) {
        if (why) *why = "sprite tile rectangle is invalid";
        return false;
    }
    uint32_t tile_index = 0u;
    for (uint32_t y = tile_y0; y <= tile_y1; y++) {
        for (uint32_t x = tile_x0; x <= tile_x1; x++) {
            uint32_t code, pointer;
            if (!mbx_gart_u32(m, bus, region + tile_index * 8u,
                              &code, why) ||
                !mbx_gart_u32(m, bus, region + tile_index * 8u + 4u,
                              &pointer, why))
                return false;
            uint32_t expected = (y << 8) | x;
            if (tile_index + 1u == tile_count) expected |= 0x80000000u;
            if (code != expected || pointer != list) {
                if (why) *why = "sprite region list disagrees with its clip tiles";
                return false;
            }
            tile_index++;
        }
    }

    /* A conservative producer boundary can survive clipping even when the
     * subpixel quad covers no pixel centre.  That is a valid no-op draw, not
     * a rejected submission: the guest still waits for its completion event.
     * Keep this lifecycle rule independent of any captured address or packet,
     * but do not let it hide malformed state.  The texture allocation,
     * render-target boundary, clip registers and complete tile list must all
     * be valid before the command completes without reading or writing a
     * pixel. */
    if (zero_coverage) {
        uint64_t source_end = (uint64_t)source +
            (uint64_t)header_texture_height * source_stride;
        if (source_end > UINT32_MAX) {
            if (why) *why = "zero-coverage sprite source allocation overflows";
            return false;
        }
        for (uint32_t row = 0; row < header_texture_height; row++) {
            uint32_t src = source + row * source_stride;
            if (!mbx_gart_validate(m, bus, src, source_stride, why))
                return false;
        }

        uint32_t target_row_bytes =
            (boundary_right - boundary_left) * 4u;
        uint64_t target_end = (uint64_t)target +
            (uint64_t)(boundary_bottom - 1u) * MBX_3D_TARGET_STRIDE +
            (uint64_t)boundary_right * 4u;
        if (target_end > UINT32_MAX) {
            if (why) *why = "zero-coverage sprite target boundary overflows";
            return false;
        }
        for (uint32_t row = boundary_top; row < boundary_bottom; row++) {
            uint32_t dst = target + row * MBX_3D_TARGET_STRIDE +
                           boundary_left * 4u;
            if (!mbx_gart_validate(m, bus, dst, target_row_bytes, why))
                return false;
        }
        *pixels_blended = 0u;
        return true;
    }

    uint32_t row_bytes = width * 4u;
    uint32_t total = row_bytes * height;
    uint64_t target_end = (uint64_t)target +
        (uint64_t)(top + height - 1u) * MBX_3D_TARGET_STRIDE +
        (uint64_t)(left + width) * 4u;
    if (target_end > UINT32_MAX) {
        if (why) *why = "sprite destination rectangle overflows";
        return false;
    }
    for (uint32_t row = 0; row < height; row++) {
        uint32_t dst = target + (top + row) * MBX_3D_TARGET_STRIDE +
                       left * 4u;
        if (!mbx_gart_validate(m, bus, dst, row_bytes, why))
            return false;
    }

    struct mbx_bilinear_axis x_axis[MBX_3D_WIDTH];
    struct mbx_bilinear_axis y_axis[480u];
    uint32_t source_stage_x0 = source_x0;
    uint32_t source_stage_y0 = source_y0;
    uint32_t source_stage_width = width;
    uint32_t source_stage_height = height;
    if (filtered_sampling) {
        uint32_t minimum_x = UINT32_MAX, minimum_y = UINT32_MAX;
        uint32_t maximum_x = 0u, maximum_y = 0u;
        if (!affine_sprite) {
            for (uint32_t x = 0; x < width; x++) {
                if (!mbx_bilinear_axis(x0, dx, u_texel_start, u_texel_span,
                                       left + x, source_pitch_pixels,
                                       &x_axis[x])) {
                    if (why) *why =
                        "filtered sprite has an invalid horizontal sample";
                    return false;
                }
                if (x_axis[x].first < minimum_x) minimum_x = x_axis[x].first;
                if (x_axis[x].second > maximum_x) maximum_x = x_axis[x].second;
            }
            for (uint32_t y = 0; y < height; y++) {
                if (!mbx_bilinear_axis(y0, dy, v_texel_start, v_texel_span,
                                       top + y, header_texture_height,
                                       &y_axis[y])) {
                    if (why) *why =
                        "filtered sprite has an invalid vertical sample";
                    return false;
                }
                if (y_axis[y].first < minimum_y) minimum_y = y_axis[y].first;
                if (y_axis[y].second > maximum_y) maximum_y = y_axis[y].second;
            }
        } else {
            /* The inverse transform already proves every covered fragment has
             * u/v in [0, 1).  A bilinear tap can extend at most one texel past
             * the UV rectangle's floor/ceil envelope, so stage that bounded
             * superset directly.  The old path traversed the whole destination
             * once to discover this window and then repeated the same affine
             * divisions and coordinate conversion while rendering.  Staging
             * a conservative rectangle removes that entire discovery pass
             * without caching or changing any rendered sample calculation. */
            source_stage_x0 = source_left ? source_left - 1u : 0u;
            source_stage_y0 = source_top ? source_top - 1u : 0u;
            uint32_t source_stage_right = source_right < source_pitch_pixels
                ? source_right + 1u : source_pitch_pixels;
            uint32_t source_stage_bottom =
                source_bottom < header_texture_height
                    ? source_bottom + 1u : header_texture_height;
            source_stage_width = source_stage_right - source_stage_x0;
            source_stage_height = source_stage_bottom - source_stage_y0;
        }
        if (!affine_sprite) {
            source_stage_x0 = minimum_x;
            source_stage_y0 = minimum_y;
            source_stage_width = maximum_x - minimum_x + 1u;
            source_stage_height = maximum_y - minimum_y + 1u;
        }
    }

    uint32_t source_row_bytes = source_stage_width * 4u;
    uint32_t source_total = source_row_bytes * source_stage_height;
    uint64_t source_end = (uint64_t)source +
        (uint64_t)(source_stage_y0 + source_stage_height - 1u) * source_stride +
        (uint64_t)(source_stage_x0 + source_stage_width) * 4u;
    if (source_row_bytes > source_stride || source_end > UINT32_MAX) {
        if (why) *why = "sprite source sample window overflows";
        return false;
    }
    for (uint32_t row = 0; row < source_stage_height; row++) {
        uint32_t src = source + (source_stage_y0 + row) * source_stride +
                       source_stage_x0 * 4u;
        if (!mbx_gart_validate(m, bus, src, source_row_bytes, why)) return false;
    }

    uint8_t *source_pixels = malloc(source_total);
    uint8_t *pixels = malloc(total);
    if (!source_pixels || !pixels) {
        free(source_pixels);
        free(pixels);
        if (why) *why = "host allocation for staged sprite failed";
        return false;
    }
    bool ok = true;
    for (uint32_t row = 0; row < source_stage_height && ok; row++) {
        uint32_t src = source + (source_stage_y0 + row) * source_stride +
                       source_stage_x0 * 4u;
        ok = mbx_gart_read(m, bus, src,
                           source_pixels + row * source_row_bytes,
                           source_row_bytes, why);
    }
    /* The compact producer is an opaque copy and the validated axis-aligned
     * raster loop below writes every byte in `pixels` before the commit.  Its
     * previous destination read therefore moved an entire rectangle across
     * the GART only to overwrite it.  Blended perspective sprites still need
     * the original destination, and keeping the shared output buffer preserves
     * the all-validation-before-first-write transaction boundary. */
    for (uint32_t row = 0; !compact_copy && row < height && ok; row++) {
        uint32_t dst = target + (top + row) * MBX_3D_TARGET_STRIDE +
                       left * 4u;
        ok = mbx_gart_read(m, bus, dst, pixels + row * row_bytes,
                           row_bytes, why);
    }
    /* Axis-aligned filtered sprites reuse the same vertical interpolation for
     * every destination x that names a given source column.  The literal
     * transcription used to recompute both vertical lanes for every output
     * pixel and only then apply the horizontal filter.  Cache that exact
     * vertical result once per source column when the staged source is no
     * wider than the output.  Minification can leave large gaps between taps,
     * so it keeps the literal path rather than scanning unused source columns.
     * The order *inside each sample* remains vertical then horizontal, which
     * preserves the shipped packed-byte rounding exactly. */
    uint32_t vertical_pixels[MBX_3D_WIDTH + 2u];
    bool cache_vertical = filtered_sampling && !affine_sprite &&
                          source_stage_width <= width + 2u;
    uint32_t affine_rendered_pixels = 0u;
    for (uint32_t y = 0; y < height && ok; y++) {
        if (cache_vertical) {
            const struct mbx_bilinear_axis *sample_y = &y_axis[y];
            uint32_t y0_offset = sample_y->first - source_stage_y0;
            uint32_t y1_offset = sample_y->second - source_stage_y0;
            const uint8_t *top_row = source_pixels +
                                     y0_offset * source_row_bytes;
            const uint8_t *bottom_row = source_pixels +
                                        y1_offset * source_row_bytes;
            for (uint32_t sx = 0; sx < source_stage_width; sx++) {
                uint32_t top_pixel = mbx_load_le32(top_row + sx * 4u);
                uint32_t bottom_pixel = mbx_load_le32(bottom_row + sx * 4u);
                vertical_pixels[sx] = top_pixel == bottom_pixel
                    ? top_pixel
                    : mbx_linear_bgra8(top_pixel, bottom_pixel,
                                       sample_y->weight);
            }
        }
        for (uint32_t x = 0; x < width; x++) {
            uint32_t src;
            if (filtered_sampling) {
                struct mbx_bilinear_axis affine_x, affine_y;
                const struct mbx_bilinear_axis *sample_x = &x_axis[x];
                const struct mbx_bilinear_axis *sample_y = &y_axis[y];
                if (affine_sprite) {
                    float u_fraction, v_fraction;
                    if (!mbx_affine_pixel(&affine, left + x, top + y,
                                          &u_fraction, &v_fraction))
                        continue;
                    float u_coordinate =
                        u_texel_start + u_fraction * u_texel_span;
                    float v_coordinate =
                        v_texel_start + v_fraction * v_texel_span;
                    if (!mbx_bilinear_coordinate(
                            u_coordinate, source_pitch_pixels, &affine_x) ||
                        !mbx_bilinear_coordinate(
                            v_coordinate, header_texture_height, &affine_y)) {
                        if (why) *why =
                            "affine sprite sample changed during staging";
                        ok = false;
                        break;
                    }
                    sample_x = &affine_x;
                    sample_y = &affine_y;
                    affine_rendered_pixels++;
                }
                if (sample_x->first < source_stage_x0 ||
                    sample_x->second >=
                        source_stage_x0 + source_stage_width ||
                    sample_y->first < source_stage_y0 ||
                    sample_y->second >=
                        source_stage_y0 + source_stage_height) {
                    if (why) *why =
                        "filtered sprite sample escaped its staged source window";
                    ok = false;
                    break;
                }
                uint32_t x0_offset = sample_x->first - source_stage_x0;
                uint32_t x1_offset = sample_x->second - source_stage_x0;
                uint32_t y0_offset = sample_y->first - source_stage_y0;
                uint32_t y1_offset = sample_y->second - source_stage_y0;
                uint32_t vertical_left, vertical_right;
                if (cache_vertical) {
                    vertical_left = vertical_pixels[x0_offset];
                    vertical_right = vertical_pixels[x1_offset];
                } else {
                    uint32_t top_left = mbx_load_le32(source_pixels +
                        y0_offset * source_row_bytes + x0_offset * 4u);
                    uint32_t bottom_left = mbx_load_le32(source_pixels +
                        y1_offset * source_row_bytes + x0_offset * 4u);
                    uint32_t top_right = mbx_load_le32(source_pixels +
                        y0_offset * source_row_bytes + x1_offset * 4u);
                    uint32_t bottom_right = mbx_load_le32(source_pixels +
                        y1_offset * source_row_bytes + x1_offset * 4u);
                    vertical_left = top_left == bottom_left
                        ? top_left
                        : mbx_linear_bgra8(top_left, bottom_left,
                                           sample_y->weight);
                    vertical_right = top_right == bottom_right
                        ? top_right
                        : mbx_linear_bgra8(top_right, bottom_right,
                                           sample_y->weight);
                }
                src = vertical_left == vertical_right
                    ? vertical_left
                    : mbx_linear_bgra8(vertical_left, vertical_right,
                                       sample_x->weight);
            } else {
                src = mbx_load_le32(source_pixels + y * source_row_bytes +
                                    x * 4u);
            }
            uint32_t pixel_offset = y * row_bytes + x * 4u;
            uint32_t output = src;
            if (!compact_copy) {
                src = mbx_modulate_vertex_alpha(
                    src, vertex_alpha_word >> 24);
                output = mbx_source_over_clamped(
                    mbx_load_le32(pixels + pixel_offset), src);
            }
            pixels[pixel_offset] = (uint8_t)output;
            pixels[pixel_offset + 1u] = (uint8_t)(output >> 8);
            pixels[pixel_offset + 2u] = (uint8_t)(output >> 16);
            pixels[pixel_offset + 3u] = (uint8_t)(output >> 24);
        }
    }
    if (ok && affine_sprite && !affine_rendered_pixels) {
        if (why) *why = "affine sprite covers no destination pixel centres";
        ok = false;
    }
    for (uint32_t row = 0; row < height && ok; row++) {
        uint32_t dst = target + (top + row) * MBX_3D_TARGET_STRIDE +
                       left * 4u;
        ok = mbx_gart_write(m, bus, dst, pixels + row * row_bytes,
                            row_bytes, why);
    }
    free(source_pixels);
    free(pixels);
    if (ok && pixels_blended) {
        *pixels_blended = affine_sprite
            ? affine_rendered_pixels : width * height;
    }
    return ok;
}

/* The PIO TA stream retained from Voice Memos is not an object list: it is
 * the input from which real MBX hardware would build the region and object
 * buffers. The stream nevertheless describes ordinary BGRA8 quads using the
 * same texture header, pitch, sampler, vertex-alpha and source-over
 * conventions already proved by the object-list decoders above. Decode only
 * that measured subset. In particular, an unfamiliar state block, control
 * word, vertex layout, transform, colour modulation, or texture alias rejects
 * the entire scene before the first target write. */
#define MBX_TA_STATE_WORDS 14u

enum mbx_ta_parse_result {
    MBX_TA_NOT_DRAW = 0,
    MBX_TA_DRAW_OK,
    MBX_TA_DRAW_BAD,
};

struct mbx_ta_draw {
    uint32_t next_word;
    bool textured;
    bool filtered;
    bool affine;
    uint32_t source;
    uint32_t source_stride;
    uint32_t texture_width;
    uint32_t texture_height;
    uint32_t colour;
    float x0, y0, x1, y1;
    float u0, v0, u1, v1;
    struct mbx_affine_transform transform;
};

struct mbx_ta_global_transform {
    float origin_x;
    float origin_y;
};

static bool mbx_ta_sampler(uint32_t value) {
    return value == 0xd6087610u || value == 0x86084610u ||
           value == 0x86081e10u;
}

static bool mbx_ta_texture_state(const uint32_t *words, uint32_t start,
                                 uint32_t control,
                                 struct mbx_ta_draw *draw) {
    uint32_t matches = 0u;
    for (uint32_t i = start + 1u; i + 2u < control; i++) {
        uint32_t header = words[i];
        uint32_t source_word = words[i + 1u];
        if (!mbx_ta_sampler(words[i + 2u])) continue;

        uint32_t width_field = (header >> 24) & 7u;
        uint32_t height_field = (header >> 20) & 7u;
        uint32_t width = 8u << width_field;
        uint32_t height = 8u << height_field;
        bool filtered = (source_word & 0x80000000u) != 0u;
        uint32_t pitch_units = ((source_word >> 16) & 0xfcu) |
                               ((header & 1u) << 1);
        uint32_t expected_header = 0xa0018000u |
            (width_field << 24) | (height_field << 20) |
            ((pitch_units & 2u) >> 1);
        uint32_t expected_source =
            (filtered ? 0x8e000000u : 0x0e000000u) |
            ((pitch_units & ~3u) << 16);
        if (!pitch_units || width > 512u || height > 512u ||
            header != expected_header ||
            (source_word & ~MBX_3D_ADDRESS_MASK) != expected_source)
            continue;

        matches++;
        draw->filtered = filtered;
        draw->source = mbx_3d_decode_address(source_word);
        draw->source_stride = pitch_units * 16u;
        draw->texture_width = width;
        draw->texture_height = height;
    }
    return matches == 1u;
}

static enum mbx_ta_parse_result mbx_ta_parse_vertices(
        const uint32_t *words, uint32_t count, uint32_t control,
        uint32_t stride, float origin_x, float origin_y,
        struct mbx_ta_draw *draw, const char **why) {
    if (control > UINT32_MAX - 2u - 4u * stride ||
        control + 1u + 4u * stride >= count ||
        words[control + 1u + 4u * stride] != 0x00000003u) {
        if (why) *why = "TA draw has an unknown vertex boundary";
        return MBX_TA_DRAW_BAD;
    }

    draw->next_word = control + 2u + 4u * stride;
    draw->textured = stride == 8u;
    float x[4], y[4], u[4] = {0}, v[4] = {0};
    uint32_t colour = words[control + 1u + 5u];
    for (uint32_t vertex = 0u; vertex < 4u; vertex++) {
        uint32_t base = control + 1u + vertex * stride;
        if (words[base] != 0u || words[base + 3u] != 0u ||
            words[base + 4u] != 0x3f800000u ||
            words[base + 5u] != colour ||
            !mbx_3d_word_to_finite_float(words[base + 1u], &x[vertex]) ||
            !mbx_3d_word_to_finite_float(words[base + 2u], &y[vertex])) {
            if (why) *why = "TA draw has an unknown vertex layout";
            return MBX_TA_DRAW_BAD;
        }
        if (draw->textured &&
            (!mbx_3d_word_to_nonnegative_float(words[base + 6u],
                                                &u[vertex]) ||
             !mbx_3d_word_to_nonnegative_float(words[base + 7u],
                                                &v[vertex]))) {
            if (why) *why = "TA draw has invalid texture coordinates";
            return MBX_TA_DRAW_BAD;
        }
        x[vertex] += origin_x;
        y[vertex] += origin_y;
    }

    draw->colour = colour;
    uint32_t corner_index[4] = {UINT32_MAX, UINT32_MAX,
                                UINT32_MAX, UINT32_MAX};
    if (draw->textured) {
        draw->u0 = draw->u1 = u[0];
        draw->v0 = draw->v1 = v[0];
        for (uint32_t i = 1u; i < 4u; i++) {
            if (u[i] < draw->u0) draw->u0 = u[i];
            if (u[i] > draw->u1) draw->u1 = u[i];
            if (v[i] < draw->v0) draw->v0 = v[i];
            if (v[i] > draw->v1) draw->v1 = v[i];
        }
        if (!(draw->u0 < draw->u1) || !(draw->v0 < draw->v1)) {
            if (why) *why = "TA draw has an empty texture rectangle";
            return MBX_TA_DRAW_BAD;
        }
        uint32_t seen_corners = 0u;
        for (uint32_t i = 0u; i < 4u; i++) {
            if ((u[i] != draw->u0 && u[i] != draw->u1) ||
                (v[i] != draw->v0 && v[i] != draw->v1)) {
                if (why) *why = "TA draw UVs are not a rectangle";
                return MBX_TA_DRAW_BAD;
            }
            uint32_t corner = (u[i] == draw->u1 ? 1u : 0u) |
                              (v[i] == draw->v1 ? 2u : 0u);
            if (seen_corners & (1u << corner)) {
                if (why) *why = "TA draw repeats a texture corner";
                return MBX_TA_DRAW_BAD;
            }
            seen_corners |= 1u << corner;
            corner_index[corner] = i;
        }
        if (seen_corners != 0x0fu) {
            if (why) *why = "TA draw does not contain four texture corners";
            return MBX_TA_DRAW_BAD;
        }
        uint32_t alpha = colour >> 24;
        if (colour != alpha * 0x01010101u) {
            if (why) *why = "TA draw uses coloured vertex modulation";
            return MBX_TA_DRAW_BAD;
        }
    } else {
        draw->x0 = draw->x1 = x[0];
        draw->y0 = draw->y1 = y[0];
        for (uint32_t i = 1u; i < 4u; i++) {
            if (x[i] < draw->x0) draw->x0 = x[i];
            if (x[i] > draw->x1) draw->x1 = x[i];
            if (y[i] < draw->y0) draw->y0 = y[i];
            if (y[i] > draw->y1) draw->y1 = y[i];
        }
        uint32_t seen_corners = 0u;
        for (uint32_t i = 0u; i < 4u; i++) {
            if ((x[i] != draw->x0 && x[i] != draw->x1) ||
                (y[i] != draw->y0 && y[i] != draw->y1)) {
                if (why) *why = "TA solid draw is affine or perspective geometry";
                return MBX_TA_DRAW_BAD;
            }
            uint32_t corner = (x[i] == draw->x1 ? 1u : 0u) |
                              (y[i] == draw->y1 ? 2u : 0u);
            if (seen_corners & (1u << corner)) {
                if (why) *why = "TA solid draw repeats a rectangle corner";
                return MBX_TA_DRAW_BAD;
            }
            seen_corners |= 1u << corner;
        }
        if (!(draw->x0 < draw->x1) || !(draw->y0 < draw->y1) ||
            seen_corners != 0x0fu) {
            if (why) *why = "TA solid draw does not contain a rectangle";
            return MBX_TA_DRAW_BAD;
        }
        return MBX_TA_DRAW_OK;
    }

    const uint32_t p00 = corner_index[0u];
    const uint32_t p10 = corner_index[1u];
    const uint32_t p01 = corner_index[2u];
    const uint32_t p11 = corner_index[3u];
    bool axis_aligned = x[p00] == x[p01] && x[p10] == x[p11] &&
                        y[p00] == y[p10] && y[p01] == y[p11] &&
                        x[p00] < x[p10] && y[p00] < y[p01];
    if (axis_aligned) {
        draw->x0 = x[p00];
        draw->y0 = y[p00];
        draw->x1 = x[p11];
        draw->y1 = y[p11];
        return MBX_TA_DRAW_OK;
    }

    const float epsilon = 0.0009765625f;
    float closure_x = x[p00] + x[p11] - x[p10] - x[p01];
    float closure_y = y[p00] + y[p11] - y[p10] - y[p01];
    if (!draw->filtered || closure_x < -epsilon || closure_x > epsilon ||
        closure_y < -epsilon || closure_y > epsilon) {
        if (why) *why = "TA textured draw is not a measured affine quad";
        return MBX_TA_DRAW_BAD;
    }
    draw->affine = true;
    draw->transform.origin_x = x[p00];
    draw->transform.origin_y = y[p00];
    draw->transform.u_x = x[p10] - x[p00];
    draw->transform.u_y = y[p10] - y[p00];
    draw->transform.v_x = x[p01] - x[p00];
    draw->transform.v_y = y[p01] - y[p00];
    draw->transform.determinant =
        draw->transform.u_x * draw->transform.v_y -
        draw->transform.u_y * draw->transform.v_x;
    if (draw->transform.determinant <= 0.0f) {
        if (why) *why = "TA affine draw is degenerate or reversed";
        return MBX_TA_DRAW_BAD;
    }

    uint32_t source_left = (uint32_t)draw->u0;
    uint32_t source_top = (uint32_t)draw->v0;
    int32_t source_right_i = mbx_3d_ceil_to_i32(draw->u1);
    int32_t source_bottom_i = mbx_3d_ceil_to_i32(draw->v1);
    if (source_right_i <= (int32_t)source_left ||
        source_bottom_i <= (int32_t)source_top) {
        if (why) *why = "TA affine draw has an invalid source extent";
        return MBX_TA_DRAW_BAD;
    }
    uint32_t source_width = (uint32_t)source_right_i - source_left;
    uint32_t source_height = (uint32_t)source_bottom_i - source_top;
    float u2 = draw->transform.u_x * draw->transform.u_x +
               draw->transform.u_y * draw->transform.u_y;
    float v2 = draw->transform.v_x * draw->transform.v_x +
               draw->transform.v_y * draw->transform.v_y;
    float dot = draw->transform.u_x * draw->transform.v_x +
                draw->transform.u_y * draw->transform.v_y;
    float expected_u2 = (float)source_width * (float)source_width;
    float expected_v2 = (float)source_height * (float)source_height;
    float expected_det = (float)source_width * (float)source_height;
    float u_error = u2 - expected_u2;
    float v_error = v2 - expected_v2;
    float det_error = draw->transform.determinant - expected_det;
    if (dot < 0.0f) dot = -dot;
    if (u_error < 0.0f) u_error = -u_error;
    if (v_error < 0.0f) v_error = -v_error;
    if (det_error < 0.0f) det_error = -det_error;
    float tolerance = ((float)source_width + (float)source_height) * epsilon;
    if (source_width > MBX_3D_WIDTH || source_height > 480u ||
        u_error > tolerance || v_error > tolerance || dot > tolerance ||
        det_error > tolerance) {
        if (why) *why = "TA affine draw is not the measured rigid unity transform";
        return MBX_TA_DRAW_BAD;
    }

    draw->x0 = draw->x1 = x[0];
    draw->y0 = draw->y1 = y[0];
    for (uint32_t i = 1u; i < 4u; i++) {
        if (x[i] < draw->x0) draw->x0 = x[i];
        if (x[i] > draw->x1) draw->x1 = x[i];
        if (y[i] < draw->y0) draw->y0 = y[i];
        if (y[i] > draw->y1) draw->y1 = y[i];
    }
    if (!(draw->x0 < draw->x1) || !(draw->y0 < draw->y1)) {
        if (why) *why = "TA affine draw has empty destination bounds";
        return MBX_TA_DRAW_BAD;
    }
    return MBX_TA_DRAW_OK;
}

static enum mbx_ta_parse_result mbx_ta_parse_draw(
        const uint32_t *words, uint32_t count, uint32_t start,
        float origin_x, float origin_y,
        struct mbx_ta_draw *draw, const char **why) {
    if (start >= count ||
        (words[start] & 0xff000000u) != 0x10000000u)
        return MBX_TA_NOT_DRAW;

    uint32_t limit = start + 21u;
    if (limit > count) limit = count;
    uint32_t control = UINT32_MAX;
    uint32_t stride = 0u;
    for (uint32_t i = start + 1u; i < limit; i++) {
        uint32_t candidate_stride = words[i] == 0xf0020004u ? 6u :
                                    words[i] == 0xf0020044u ? 8u : 0u;
        if (!candidate_stride) continue;
        if (control != UINT32_MAX) {
            if (why) *why = "TA draw contains multiple raster controls";
            return MBX_TA_DRAW_BAD;
        }
        control = i;
        stride = candidate_stride;
    }
    if (control == UINT32_MAX) return MBX_TA_NOT_DRAW;
    if (control == start + 1u || words[control - 1u] != 0x48020000u) {
        if (why) *why = "TA draw has an unknown vertex boundary";
        return MBX_TA_DRAW_BAD;
    }

    memset(draw, 0, sizeof *draw);
    draw->textured = stride == 8u;
    if (draw->textured &&
        !mbx_ta_texture_state(words, start, control, draw)) {
        if (why) *why = "TA draw has no unique measured BGRA8 texture state";
        return MBX_TA_DRAW_BAD;
    }
    return mbx_ta_parse_vertices(words, count, control, stride,
                                 origin_x, origin_y, draw, why);
}

/* Voice Memos changes only vertex data for adjacent draws in two measured
 * places: a solid footer strip and a pair of textured two-pixel edge quads.
 * The continuation begins at the exact 0x48020000 vertex boundary and reuses
 * only the immediately preceding pipeline type (and texture state, when
 * textured). No search or older draw is eligible for inheritance. */
static enum mbx_ta_parse_result mbx_ta_parse_continuation(
        const uint32_t *words, uint32_t count, uint32_t start,
        float origin_x, float origin_y, const struct mbx_ta_draw *previous,
        struct mbx_ta_draw *draw, const char **why) {
    if (start >= count || words[start] != 0x48020000u)
        return MBX_TA_NOT_DRAW;
    if (!previous || start + 1u >= count) {
        if (why) *why = "TA continuation has no preceding measured draw";
        return MBX_TA_DRAW_BAD;
    }
    uint32_t stride = words[start + 1u] == 0xf0020004u ? 6u :
                      words[start + 1u] == 0xf0020044u ? 8u : 0u;
    if (!stride || previous->textured != (stride == 8u)) {
        if (why) *why = "TA continuation changes its measured pipeline type";
        return MBX_TA_DRAW_BAD;
    }
    memset(draw, 0, sizeof *draw);
    draw->textured = previous->textured;
    if (draw->textured) {
        draw->filtered = previous->filtered;
        draw->source = previous->source;
        draw->source_stride = previous->source_stride;
        draw->texture_width = previous->texture_width;
        draw->texture_height = previous->texture_height;
    }
    return mbx_ta_parse_vertices(words, count, start + 1u, stride,
                                 origin_x, origin_y, draw, why);
}

static bool mbx_ta_global_transform(
        const uint32_t *words, uint32_t first_draw,
        uint32_t target_width, uint32_t target_height,
        struct mbx_ta_global_transform *transform, const char **why) {
    memset(transform, 0, sizeof *transform);
    uint32_t matches = 0u;
    for (uint32_t i = 1u; i < first_draw; i++) {
        if (words[i] != 0xa0000003u) continue;
        if (first_draw - i <= 16u) {
            if (why) *why = "TA global transform is truncated";
            return false;
        }
        matches++;
        if (matches != 1u ||
            words[i + 1u] != mbx_3d_float_to_word(
                2.0f / (float)target_width) ||
            words[i + 2u] != 0u || words[i + 3u] != 0u ||
            words[i + 5u] != 0u ||
            words[i + 6u] != mbx_3d_float_to_word(
                2.0f / (float)target_height) ||
            words[i + 7u] != 0u ||
            words[i + 9u] != 0u || words[i + 10u] != 0u ||
            words[i + 11u] != 0u || words[i + 12u] != 0u ||
            words[i + 13u] != 0u || words[i + 14u] != 0u ||
            words[i + 15u] != 0u || words[i + 16u] != 0x3f800000u) {
            if (why) *why = "TA global transform is outside the measured orthographic form";
            return false;
        }
        float tx, ty;
        if (!mbx_3d_word_to_finite_float(words[i + 4u], &tx) ||
            !mbx_3d_word_to_finite_float(words[i + 8u], &ty)) {
            if (why) *why = "TA global transform translation is not finite";
            return false;
        }
        float origin_x = (tx + 1.0f) * (float)target_width * 0.5f;
        float origin_y = (ty + 1.0f) * (float)target_height * 0.5f;
        if (origin_x <= -4096.0f || origin_x >= 4096.0f ||
            origin_y <= -4096.0f || origin_y >= 4096.0f) {
            if (why) *why = "TA global transform translation is out of range";
            return false;
        }
        int32_t rounded_x = origin_x >= 0.0f
            ? (int32_t)(origin_x + 0.5f) : (int32_t)(origin_x - 0.5f);
        int32_t rounded_y = origin_y >= 0.0f
            ? (int32_t)(origin_y + 0.5f) : (int32_t)(origin_y - 0.5f);
        float error_x = origin_x - (float)rounded_x;
        float error_y = origin_y - (float)rounded_y;
        if (error_x < 0.0f) error_x = -error_x;
        if (error_y < 0.0f) error_y = -error_y;
        if (error_x > 0.0009765625f || error_y > 0.0009765625f) {
            if (why) *why = "TA global transform is not an integer surface translation";
            return false;
        }
        transform->origin_x = (float)rounded_x;
        transform->origin_y = (float)rounded_y;
    }
    return true;
}

static bool mbx_ta_state_block(const uint32_t *words, uint32_t count,
                               uint32_t start) {
    if (start > count || count - start < MBX_TA_STATE_WORDS) return false;
    if (words[start] != 0xa01b001cu ||
        words[start + 2u] != 0u || words[start + 3u] != 0u ||
        words[start + 4u] != 0u || words[start + 5u] != 0u ||
        words[start + 7u] != 0u || words[start + 8u] != 0u ||
        words[start + 9u] != 0xa01d001du ||
        words[start + 10u] != 0u || words[start + 11u] != 0u ||
        words[start + 12u] != 0u ||
        words[start + 13u] != 0x3f800000u)
        return false;
    float first, second;
    return mbx_3d_word_to_nonnegative_float(words[start + 1u], &first) &&
           mbx_3d_word_to_nonnegative_float(words[start + 6u], &second) &&
           first <= 1.0f && second <= 1.0f;
}

static bool mbx_ta_premultiplied(uint32_t pixel) {
    uint32_t alpha = pixel >> 24;
    return (pixel & 0xffu) <= alpha &&
           ((pixel >> 8) & 0xffu) <= alpha &&
           ((pixel >> 16) & 0xffu) <= alpha;
}

static bool mbx_ta_near_u32(float value, uint32_t maximum,
                            uint32_t *rounded) {
    if (!rounded || value < 0.0f || value > (float)maximum + 0.0009765625f)
        return false;
    uint32_t candidate = (uint32_t)(value + 0.5f);
    if (candidate > maximum) return false;
    float error = value - (float)candidate;
    if (error < 0.0f) error = -error;
    if (error > 0.0009765625f) return false;
    *rounded = candidate;
    return true;
}

static bool mbx_ta_apply_draw(
        const s5l_mbx_t *m, const arm_bus_t *bus,
        const struct mbx_ta_draw *draw, uint32_t target,
        uint32_t target_width, uint32_t target_height,
        uint32_t target_bytes,
        uint32_t clip_left, uint32_t clip_top,
        uint32_t clip_right, uint32_t clip_bottom,
        uint8_t *target_pixels,
        uint32_t *dirty_left, uint32_t *dirty_top,
        uint32_t *dirty_right, uint32_t *dirty_bottom,
        uint64_t *pixels_blended, const char **why) {
    const float coordinate_limit = (float)INT32_MAX - 1024.0f;
    if (draw->x0 <= -coordinate_limit || draw->y0 <= -coordinate_limit ||
        draw->x1 >= coordinate_limit || draw->y1 >= coordinate_limit) {
        if (why) *why = "TA draw destination coordinates overflow";
        return false;
    }
    int32_t raster_left = mbx_3d_ceil_to_i32(draw->x0 - 0.5f);
    int32_t raster_top = mbx_3d_ceil_to_i32(draw->y0 - 0.5f);
    int32_t raster_right = mbx_3d_ceil_to_i32(draw->x1 - 0.5f);
    int32_t raster_bottom = mbx_3d_ceil_to_i32(draw->y1 - 0.5f);
    if (raster_left < (int32_t)clip_left)
        raster_left = (int32_t)clip_left;
    if (raster_top < (int32_t)clip_top)
        raster_top = (int32_t)clip_top;
    if (raster_right > (int32_t)clip_right)
        raster_right = (int32_t)clip_right;
    if (raster_bottom > (int32_t)clip_bottom)
        raster_bottom = (int32_t)clip_bottom;
    if (raster_left < 0) raster_left = 0;
    if (raster_top < 0) raster_top = 0;
    if (raster_right > (int32_t)target_width)
        raster_right = (int32_t)target_width;
    if (raster_bottom > (int32_t)target_height)
        raster_bottom = (int32_t)target_height;
    if (raster_left >= raster_right || raster_top >= raster_bottom)
        return true;

    uint32_t left = (uint32_t)raster_left;
    uint32_t top = (uint32_t)raster_top;
    uint32_t right = (uint32_t)raster_right;
    uint32_t bottom = (uint32_t)raster_bottom;
    uint32_t width = right - left;
    uint32_t height = bottom - top;
    uint64_t draw_pixels = 0u;
    if (!draw->textured) {
        if (!mbx_ta_premultiplied(draw->colour)) {
            if (why) *why = "TA solid colour is not premultiplied BGRA8";
            return false;
        }
        for (uint32_t y = top; y < bottom; y++) {
            for (uint32_t x = left; x < right; x++) {
                uint8_t *pixel = target_pixels +
                    (y * target_width + x) * 4u;
                uint32_t output = mbx_source_over_clamped(
                    mbx_load_le32(pixel), draw->colour);
                pixel[0] = (uint8_t)output;
                pixel[1] = (uint8_t)(output >> 8);
                pixel[2] = (uint8_t)(output >> 16);
                pixel[3] = (uint8_t)(output >> 24);
            }
        }
        draw_pixels = (uint64_t)width * height;
    } else {
        uint32_t pitch_pixels = draw->source_stride / 4u;
        if (!pitch_pixels || draw->u1 > (float)pitch_pixels ||
            draw->u1 > (float)draw->texture_width ||
            draw->v1 > (float)draw->texture_height) {
            if (why) *why = "TA texture rectangle exceeds its allocation";
            return false;
        }
        uint64_t allocation_bytes =
            (uint64_t)draw->source_stride * draw->texture_height;
        if (!allocation_bytes || allocation_bytes > UINT32_MAX ||
            (uint64_t)draw->source + allocation_bytes >
                (uint64_t)UINT32_MAX + 1u) {
            if (why) *why = "TA texture allocation is invalid";
            return false;
        }

        struct mbx_bilinear_axis x_axis[MBX_3D_WIDTH];
        struct mbx_bilinear_axis y_axis[480u];
        uint32_t source_x0 = UINT32_MAX, source_y0 = UINT32_MAX;
        uint32_t source_x1 = 0u, source_y1 = 0u;
        if (draw->filtered) {
            if (draw->affine) {
                uint32_t source_left = (uint32_t)draw->u0;
                uint32_t source_top = (uint32_t)draw->v0;
                uint32_t source_right =
                    (uint32_t)mbx_3d_ceil_to_i32(draw->u1);
                uint32_t source_bottom =
                    (uint32_t)mbx_3d_ceil_to_i32(draw->v1);
                source_x0 = source_left ? source_left - 1u : 0u;
                source_y0 = source_top ? source_top - 1u : 0u;
                source_x1 = source_right < pitch_pixels
                    ? source_right + 1u : pitch_pixels;
                source_y1 = source_bottom < draw->texture_height
                    ? source_bottom + 1u : draw->texture_height;
            } else {
                float dx = draw->x1 - draw->x0;
                float dy = draw->y1 - draw->y0;
                float du = draw->u1 - draw->u0;
                float dv = draw->v1 - draw->v0;
                for (uint32_t x = 0u; x < width; x++) {
                    if (!mbx_bilinear_axis(draw->x0, dx, draw->u0, du,
                                           left + x, pitch_pixels,
                                           &x_axis[x])) {
                        if (why) *why =
                            "TA filtered horizontal sample is invalid";
                        return false;
                    }
                    if (x_axis[x].first < source_x0)
                        source_x0 = x_axis[x].first;
                    if (x_axis[x].second > source_x1)
                        source_x1 = x_axis[x].second;
                }
                for (uint32_t y = 0u; y < height; y++) {
                    if (!mbx_bilinear_axis(draw->y0, dy, draw->v0, dv,
                                           top + y, draw->texture_height,
                                           &y_axis[y])) {
                        if (why) *why =
                            "TA filtered vertical sample is invalid";
                        return false;
                    }
                    if (y_axis[y].first < source_y0)
                        source_y0 = y_axis[y].first;
                    if (y_axis[y].second > source_y1)
                        source_y1 = y_axis[y].second;
                }
                source_x1++;
                source_y1++;
            }
        } else {
            int32_t destination_x = (int32_t)draw->x0;
            int32_t destination_y = (int32_t)draw->y0;
            int32_t destination_right = (int32_t)draw->x1;
            int32_t destination_bottom = (int32_t)draw->y1;
            uint32_t texture_x, texture_y, texture_right, texture_bottom;
            if ((float)destination_x != draw->x0 ||
                (float)destination_y != draw->y0 ||
                (float)destination_right != draw->x1 ||
                (float)destination_bottom != draw->y1 ||
                !mbx_ta_near_u32(draw->u0, pitch_pixels, &texture_x) ||
                !mbx_ta_near_u32(draw->v0, draw->texture_height,
                                 &texture_y) ||
                !mbx_ta_near_u32(draw->u1, pitch_pixels,
                                 &texture_right) ||
                !mbx_ta_near_u32(draw->v1, draw->texture_height,
                                 &texture_bottom) ||
                destination_right - destination_x !=
                    (int32_t)(texture_right - texture_x) ||
                destination_bottom - destination_y !=
                    (int32_t)(texture_bottom - texture_y)) {
                if (why) *why = "TA unfiltered texture is not a measured 1:1 crop";
                return false;
            }
            int64_t first_x = (int64_t)texture_x +
                              (int64_t)raster_left - destination_x;
            int64_t first_y = (int64_t)texture_y +
                              (int64_t)raster_top - destination_y;
            if (first_x < 0 || first_y < 0 ||
                (uint64_t)first_x + width > pitch_pixels ||
                (uint64_t)first_x + width > draw->texture_width ||
                (uint64_t)first_y + height > draw->texture_height) {
                if (why) *why = "TA unfiltered crop leaves its texture";
                return false;
            }
            source_x0 = (uint32_t)first_x;
            source_y0 = (uint32_t)first_y;
            source_x1 = source_x0 + width;
            source_y1 = source_y0 + height;
        }

        uint32_t source_width = source_x1 - source_x0;
        uint32_t source_height = source_y1 - source_y0;
        uint32_t source_row_bytes = source_width * 4u;
        uint32_t source_total = source_row_bytes * source_height;
        uint8_t *source_pixels = malloc(source_total);
        if (!source_pixels) {
            if (why) *why = "host allocation for staged TA texture failed";
            return false;
        }
        bool ok = source_row_bytes <= draw->source_stride;
        for (uint32_t row = 0u; row < source_height && ok; row++) {
            uint64_t source64 = (uint64_t)draw->source +
                (uint64_t)(source_y0 + row) * draw->source_stride +
                (uint64_t)source_x0 * 4u;
            if (source64 > UINT32_MAX ||
                mbx_2d_ranges_overlap((uint32_t)source64,
                                      source_row_bytes,
                                      target, target_bytes)) {
                if (why) *why = source64 > UINT32_MAX
                    ? "TA texture sample address overflows"
                    : "TA sampled texture rows alias FBSTART";
                ok = false;
                break;
            }
            ok = mbx_gart_read(m, bus, (uint32_t)source64,
                               source_pixels + row * source_row_bytes,
                               source_row_bytes, why);
        }
        for (uint32_t offset = 0u; offset < source_total && ok;
             offset += 4u) {
            if (!mbx_ta_premultiplied(
                    mbx_load_le32(source_pixels + offset))) {
                if (why) *why = "TA texture contains non-premultiplied BGRA8";
                ok = false;
            }
        }
        uint32_t vertex_alpha = draw->colour >> 24;
        for (uint32_t y = 0u; y < height && ok; y++) {
            for (uint32_t x = 0u; x < width; x++) {
                uint32_t source_pixel;
                if (draw->filtered) {
                    struct mbx_bilinear_axis affine_x, affine_y;
                    const struct mbx_bilinear_axis *sample_x = &x_axis[x];
                    const struct mbx_bilinear_axis *sample_y = &y_axis[y];
                    if (draw->affine) {
                        float u_fraction, v_fraction;
                        if (!mbx_affine_pixel(&draw->transform,
                                              left + x, top + y,
                                              &u_fraction, &v_fraction))
                            continue;
                        float u_coordinate = draw->u0 +
                            u_fraction * (draw->u1 - draw->u0);
                        float v_coordinate = draw->v0 +
                            v_fraction * (draw->v1 - draw->v0);
                        if (!mbx_bilinear_coordinate(
                                u_coordinate, pitch_pixels, &affine_x) ||
                            !mbx_bilinear_coordinate(
                                v_coordinate, draw->texture_height,
                                &affine_y)) {
                            if (why) *why =
                                "TA affine sample changed during staging";
                            ok = false;
                            break;
                        }
                        sample_x = &affine_x;
                        sample_y = &affine_y;
                    }
                    if (sample_x->first < source_x0 ||
                        sample_x->second >= source_x1 ||
                        sample_y->first < source_y0 ||
                        sample_y->second >= source_y1) {
                        if (why) *why =
                            "TA filtered sample escaped its staged source window";
                        ok = false;
                        break;
                    }
                    uint32_t x0 = sample_x->first - source_x0;
                    uint32_t x1 = sample_x->second - source_x0;
                    uint32_t y0 = sample_y->first - source_y0;
                    uint32_t y1 = sample_y->second - source_y0;
                    uint32_t top_left = mbx_load_le32(source_pixels +
                        y0 * source_row_bytes + x0 * 4u);
                    uint32_t bottom_left = mbx_load_le32(source_pixels +
                        y1 * source_row_bytes + x0 * 4u);
                    uint32_t top_right = mbx_load_le32(source_pixels +
                        y0 * source_row_bytes + x1 * 4u);
                    uint32_t bottom_right = mbx_load_le32(source_pixels +
                        y1 * source_row_bytes + x1 * 4u);
                    uint32_t vertical_left = top_left == bottom_left
                        ? top_left : mbx_linear_bgra8(
                            top_left, bottom_left, sample_y->weight);
                    uint32_t vertical_right = top_right == bottom_right
                        ? top_right : mbx_linear_bgra8(
                            top_right, bottom_right, sample_y->weight);
                    source_pixel = vertical_left == vertical_right
                        ? vertical_left : mbx_linear_bgra8(
                            vertical_left, vertical_right,
                            sample_x->weight);
                } else {
                    source_pixel = mbx_load_le32(source_pixels +
                        y * source_row_bytes + x * 4u);
                }
                source_pixel = mbx_modulate_vertex_alpha(
                    source_pixel, vertex_alpha);
                uint8_t *destination = target_pixels +
                    ((top + y) * target_width + left + x) * 4u;
                uint32_t output = mbx_source_over_clamped(
                    mbx_load_le32(destination), source_pixel);
                destination[0] = (uint8_t)output;
                destination[1] = (uint8_t)(output >> 8);
                destination[2] = (uint8_t)(output >> 16);
                destination[3] = (uint8_t)(output >> 24);
                draw_pixels++;
            }
        }
        free(source_pixels);
        if (!ok) return false;
        if (draw->affine && !draw_pixels) {
            if (why) *why = "TA affine draw covers no destination pixel centres";
            return false;
        }
    }

    if (left < *dirty_left) *dirty_left = left;
    if (top < *dirty_top) *dirty_top = top;
    if (right > *dirty_right) *dirty_right = right;
    if (bottom > *dirty_bottom) *dirty_bottom = bottom;
    *pixels_blended += draw_pixels;
    return true;
}

static bool mbx_execute_ta_stream(s5l_mbx_t *m, const arm_bus_t *bus,
                                  const char **why,
                                  uint32_t *pixels_blended) {
    uint32_t capture = *mbx_ta_capture_slot(m);
    if ((capture & MBX_TA_CAPTURE_MAGIC_M) != MBX_TA_CAPTURE_MAGIC) {
        if (why) *why = "no complete staged TA stream";
        return false;
    }
    uint32_t count = capture & MBX_TA_CAPTURE_COUNT_M;
    uint32_t object = m->reg[S5L_MBX_OBJBASE / 4u];
    uint32_t target = m->reg[S5L_MBX_FBSTART / 4u];
    if (count < 3u || count > MBX_TA_CAPTURE_MAX_WORDS ||
        !object || object != m->reg[S5L_MBX_TA_OBJECT_DATABASE / 4u] ||
        m->reg[S5L_MBX_RGNBASE / 4u] !=
            m->reg[S5L_MBX_TA_REGION_BASE / 4u]) {
        if (why) *why = "staged TA stream does not belong to this render";
        return false;
    }
    if ((object & 3u) || (target & 3u) ||
        m->reg[S5L_MBX_3DPIXSAMP / 4u] != 0x00020007u ||
        m->reg[S5L_MBX_FBCTL / 4u] != 0x00000006u) {
        if (why) *why = "TA render registers are outside the measured BGRA8 form";
        return false;
    }
    uint32_t xclip = m->reg[S5L_MBX_FBXCLIP / 4u];
    uint32_t yclip = m->reg[S5L_MBX_FBYCLIP / 4u];
    uint32_t clip_left = xclip & 0xffffu;
    uint32_t clip_top = yclip & 0xffffu;
    uint32_t clip_right = (xclip >> 16) + 1u;
    uint32_t clip_bottom = (yclip >> 16) + 1u;
    uint32_t target_width = m->reg[S5L_MBX_FBLINESTRIDE / 4u];
    uint32_t target_height = clip_bottom;
    if (!target_width || target_width > MBX_3D_WIDTH ||
        clip_left >= clip_right || clip_right > target_width ||
        clip_top >= clip_bottom || target_height > 480u) {
        if (why) *why = "TA framebuffer stride or clip is invalid";
        return false;
    }
    uint32_t target_bytes = target_width * target_height * 4u;
    if (target > UINT32_MAX - target_bytes) {
        if (why) *why = "TA framebuffer target span overflows";
        return false;
    }

    uint32_t byte_count = count * 4u;
    uint8_t *raw = malloc(byte_count);
    uint32_t *words = malloc(byte_count);
    struct mbx_ta_draw *draws = calloc(count, sizeof *draws);
    if (!raw || !words || !draws) {
        free(raw);
        free(words);
        free(draws);
        if (why) *why = "host allocation for staged TA stream failed";
        return false;
    }
    bool ok = mbx_gart_read(m, bus, object, raw, byte_count, why);
    for (uint32_t i = 0u; i < count && ok; i++)
        words[i] = mbx_load_le32(raw + i * 4u);
    free(raw);
    if (!ok || words[0] != 0x10000010u ||
        words[count - 1u] != S5L_MBX_3D_SUBMIT) {
        free(words);
        free(draws);
        if (ok && why) *why = "TA stream framing is unknown or incomplete";
        return false;
    }

    uint32_t cursor = 1u;
    uint32_t first_draw = UINT32_MAX;
    uint32_t draw_count = 0u;
    for (; cursor < count - 1u; cursor++) {
        enum mbx_ta_parse_result parsed = mbx_ta_parse_draw(
            words, count, cursor, 0.0f, 0.0f, &draws[0], why);
        if (parsed == MBX_TA_DRAW_BAD) {
            free(words);
            free(draws);
            return false;
        }
        if (parsed == MBX_TA_DRAW_OK) {
            first_draw = cursor;
            break;
        }
    }
    if (first_draw == UINT32_MAX) {
        free(words);
        free(draws);
        if (why) *why = "TA stream contains no measured draw";
        return false;
    }
    struct mbx_ta_global_transform transform;
    if (!mbx_ta_global_transform(words, first_draw,
                                 target_width, target_height,
                                 &transform, why) ||
        mbx_ta_parse_draw(words, count, first_draw,
                          transform.origin_x, transform.origin_y,
                          &draws[0], why) != MBX_TA_DRAW_OK) {
        free(words);
        free(draws);
        return false;
    }
    draw_count = 1u;
    cursor = draws[0].next_word;

    while (cursor != count - 1u) {
        if (cursor >= count - 1u || draw_count >= count) {
            ok = false;
            if (why) *why = "TA draw chain leaves the stream";
            break;
        }
        enum mbx_ta_parse_result parsed = mbx_ta_parse_draw(
            words, count, cursor, transform.origin_x, transform.origin_y,
            &draws[draw_count], why);
        if (parsed == MBX_TA_NOT_DRAW &&
            mbx_ta_state_block(words, count, cursor)) {
            cursor += MBX_TA_STATE_WORDS;
            parsed = mbx_ta_parse_draw(
                words, count, cursor, transform.origin_x, transform.origin_y,
                &draws[draw_count], why);
        }
        if (parsed == MBX_TA_NOT_DRAW) {
            parsed = mbx_ta_parse_continuation(
                words, count, cursor, transform.origin_x, transform.origin_y,
                &draws[draw_count - 1u], &draws[draw_count], why);
        }
        if (parsed != MBX_TA_DRAW_OK) {
            ok = false;
            if (parsed == MBX_TA_NOT_DRAW && why) {
                *why = "TA draw chain contains unknown inter-draw state";
                if (mbx_trace_state == 1) {
                    fprintf(stderr, "MBX3D TA unknown word %u/%u", cursor,
                            count);
                    uint32_t end = cursor + 20u;
                    if (end > count) end = count;
                    for (uint32_t i = cursor; i < end; i++)
                        fprintf(stderr, ":%08x", words[i]);
                    fputc('\n', stderr);
                }
            }
            break;
        }
        cursor = draws[draw_count].next_word;
        draw_count++;
    }
    free(words);
    if (!ok) {
        free(draws);
        return false;
    }
    uint8_t *target_pixels = malloc(target_bytes);
    if (!target_pixels) {
        free(draws);
        if (why) *why = "host allocation for staged TA target failed";
        return false;
    }
    ok = mbx_gart_read(m, bus, target, target_pixels, target_bytes, why);
    uint32_t dirty_left = target_width, dirty_top = target_height;
    uint32_t dirty_right = 0u, dirty_bottom = 0u;
    uint64_t total_pixels = 0u;
    for (uint32_t i = 0u; i < draw_count && ok; i++) {
        ok = mbx_ta_apply_draw(
            m, bus, &draws[i], target, target_width, target_height,
            target_bytes,
            clip_left, clip_top, clip_right, clip_bottom,
            target_pixels, &dirty_left, &dirty_top,
            &dirty_right, &dirty_bottom, &total_pixels, why);
    }
    free(draws);
    if (ok && (!total_pixels || total_pixels > UINT32_MAX)) {
        if (why) *why = "TA scene covers no pixels or exceeds telemetry bounds";
        ok = false;
    }
    uint32_t dirty_row_bytes = dirty_right > dirty_left
        ? (dirty_right - dirty_left) * 4u : 0u;
    for (uint32_t row = dirty_top; row < dirty_bottom && ok; row++) {
        ok = mbx_gart_validate(m, bus,
            target + row * target_width * 4u + dirty_left * 4u,
            dirty_row_bytes, why);
    }
    for (uint32_t row = dirty_top; row < dirty_bottom && ok; row++) {
        uint32_t offset = row * target_width * 4u + dirty_left * 4u;
        ok = mbx_gart_write(m, bus, target + offset,
                            target_pixels + offset,
                            dirty_row_bytes, why);
    }
    free(target_pixels);
    if (!ok) return false;
    if (pixels_blended) *pixels_blended = (uint32_t)total_pixels;
    if (mbx_trace_state == 1)
        fprintf(stderr, "MBX3D TA stream accepted: %u draws, %u pixels\n",
                draw_count, (uint32_t)total_pixels);
    return true;
}

static void mbx_capture_3d_rejection(
        const s5l_mbx_t *m, const arm_bus_t *bus,
        const char *tiled_why, const char *status_why,
        const char *sprite_why, const char *solid_why,
        s5l_mbx_telemetry_t *telemetry) {
    if (!m || !bus || !telemetry || !telemetry->rejected_3d) return;

    uint64_t sequence = telemetry->rejected_3d;
    s5l_mbx_3d_rejection_witness_t *witness =
        &telemetry->rejected_3d_history[
            (sequence - 1u) % S5L_MBX_3D_REJECTION_HISTORY];
    memset(witness, 0, sizeof *witness);
    witness->sequence = sequence;
    witness->tiled_reason_hash = mbx_rejection_reason_hash(tiled_why);
    witness->status_reason_hash = mbx_rejection_reason_hash(status_why);
    witness->sprite_reason_hash = mbx_rejection_reason_hash(sprite_why);
    witness->solid_reason_hash = mbx_rejection_reason_hash(solid_why);
    witness->region = m->reg[S5L_MBX_RGNBASE / 4u];
    witness->object = m->reg[S5L_MBX_OBJBASE / 4u];
    witness->target = m->reg[S5L_MBX_FBSTART / 4u];
    witness->xclip = m->reg[S5L_MBX_FBXCLIP / 4u];
    witness->yclip = m->reg[S5L_MBX_FBYCLIP / 4u];
    witness->pixel_sample = m->reg[S5L_MBX_3DPIXSAMP / 4u];
    witness->framebuffer_control = m->reg[S5L_MBX_FBCTL / 4u];
    witness->framebuffer_stride =
        m->reg[S5L_MBX_FBLINESTRIDE / 4u];

    const char *capture_why = "rejection-witness read failed";
    uint64_t list64 = (uint64_t)witness->object + 0x68u;
    if (list64 <= UINT32_MAX - 12u) {
        for (unsigned i = 0u; i < 4u; i++) {
            uint32_t value = 0u;
            if (!mbx_gart_u32(m, bus, (uint32_t)list64 + i * 4u,
                              &value, &capture_why))
                continue;
            witness->list_valid_mask |= 1u << i;
            witness->list_words[i] = value;
        }
    }

    if ((witness->list_valid_mask & 4u) == 0u) return;
    uint32_t pointer = witness->list_words[2];
    uint64_t record64 = 0u;
    if (pointer == 0x61a0007cu)
        record64 = (uint64_t)witness->object + 0x1f0u;
    else if ((pointer & 0xfff00000u) == 0x61200000u)
        record64 = (uint64_t)witness->object +
                   (uint64_t)(pointer & 0x000fffffu) * 4u;
    if (!record64 || record64 > UINT32_MAX -
            (S5L_MBX_3D_REJECTION_RECORD_WORDS - 1u) * 4u)
        return;

    witness->record_base = (uint32_t)record64;
    for (unsigned i = 0u; i < S5L_MBX_3D_REJECTION_RECORD_WORDS; i++) {
        uint32_t value = 0u;
        if (!mbx_gart_u32(m, bus, witness->record_base + i * 4u,
                          &value, &capture_why))
            break;
        witness->record_words[i] = value;
        witness->record_valid_words = i + 1u;
    }
}

static void mbx_capture_3d_accept(
        const s5l_mbx_t *m, const arm_bus_t *bus, uint32_t kind,
        uint32_t pixels, s5l_mbx_telemetry_t *telemetry) {
    if (!m || !bus || !telemetry || !telemetry->completed_3d) return;

    uint64_t sequence = telemetry->completed_3d;
    s5l_mbx_3d_accept_witness_t *witness =
        &telemetry->accepted_3d_history[
            (sequence - 1u) % S5L_MBX_3D_ACCEPT_HISTORY];
    memset(witness, 0, sizeof *witness);
    witness->sequence = sequence;
    witness->kind = kind;
    witness->pixels = pixels;
    witness->region = m->reg[S5L_MBX_RGNBASE / 4u];
    witness->object = m->reg[S5L_MBX_OBJBASE / 4u];
    witness->target = m->reg[S5L_MBX_FBSTART / 4u];
    uint32_t target_physical = 0u, target_span = 0u;
    const char *capture_why = "accept-witness read failed";
    if (mbx_gart_span(m, bus, witness->target, &target_physical,
                      &target_span, &capture_why)) {
        witness->target_physical = target_physical;
        witness->target_mapping_span = target_span;
    }
    s5l_mbx_3d_target_ledger_t *slot = NULL;
    s5l_mbx_3d_target_ledger_t *empty = NULL;
    s5l_mbx_3d_target_ledger_t *oldest =
        &telemetry->target_3d_ledger[0];
    for (unsigned i = 0u; i < S5L_MBX_3D_TARGET_LEDGER; i++) {
        s5l_mbx_3d_target_ledger_t *entry =
            &telemetry->target_3d_ledger[i];
        if (entry->completed && entry->target == witness->target &&
            entry->target_physical == witness->target_physical) {
            slot = entry;
            break;
        }
        if (!entry->completed && !empty) empty = entry;
        if (entry->last_sequence < oldest->last_sequence) oldest = entry;
    }
    if (!slot) {
        slot = empty ? empty : oldest;
        memset(slot, 0, sizeof *slot);
    }
    slot->last_sequence = sequence;
    slot->target = witness->target;
    slot->target_physical = witness->target_physical;
    slot->target_mapping_span = witness->target_mapping_span;
    slot->last_kind = kind;
    mbx_counter_add(&slot->completed, 1u);
    mbx_counter_add(&slot->pixels, pixels);
    witness->xclip = m->reg[S5L_MBX_FBXCLIP / 4u];
    witness->yclip = m->reg[S5L_MBX_FBYCLIP / 4u];
    witness->pixel_sample = m->reg[S5L_MBX_3DPIXSAMP / 4u];
    witness->framebuffer_control = m->reg[S5L_MBX_FBCTL / 4u];
    witness->framebuffer_stride =
        m->reg[S5L_MBX_FBLINESTRIDE / 4u];

    uint64_t list64 = (uint64_t)witness->object + 0x68u;
    if (list64 <= UINT32_MAX - 12u) {
        for (unsigned i = 0u; i < 4u; i++) {
            uint32_t value = 0u;
            if (!mbx_gart_u32(m, bus, (uint32_t)list64 + i * 4u,
                              &value, &capture_why))
                continue;
            witness->list_valid_mask |= 1u << i;
            witness->list_words[i] = value;
        }
    }

    if ((witness->list_valid_mask & 4u) == 0u) return;
    uint32_t pointer = witness->list_words[2];
    uint64_t record64 = 0u;
    if (pointer == 0x61a0007cu)
        record64 = (uint64_t)witness->object + 0x1f0u;
    else if ((pointer & 0xfff00000u) == 0x61200000u)
        record64 = (uint64_t)witness->object +
                   (uint64_t)(pointer & 0x000fffffu) * 4u;
    if (!record64 || record64 > UINT32_MAX -
            (S5L_MBX_3D_REJECTION_RECORD_WORDS - 1u) * 4u)
        return;

    witness->record_base = (uint32_t)record64;
    uint64_t hash = UINT64_C(14695981039346656037);
    for (unsigned i = 0u; i < S5L_MBX_3D_REJECTION_RECORD_WORDS; i++) {
        uint32_t value = 0u;
        if (!mbx_gart_u32(m, bus, witness->record_base + i * 4u,
                          &value, &capture_why))
            break;
        if (i < S5L_MBX_3D_ACCEPT_RECORD_WORDS)
            witness->record_words[i] = value;
        witness->record_valid_words = i + 1u;
        for (unsigned byte = 0u; byte < 4u; byte++) {
            hash ^= (uint8_t)(value >> (byte * 8u));
            hash *= UINT64_C(1099511628211);
        }
    }
    if (witness->record_valid_words) witness->record_hash = hash;
}

bool s5l_mbx_process_3d(s5l_mbx_t *m, const arm_bus_t *bus,
                        uint32_t written_off, uint32_t value,
                        s5l_mbx_telemetry_t *telemetry) {
    if (!m || written_off != S5L_MBX_STARTRENDER || value != 1u) return false;

    mbx_counter_add(&mbx_3d_candidates, 1u);
    if (telemetry) mbx_counter_add(&telemetry->candidates_3d, 1u);
    const char *tiled_why = "unknown tiled rejection";
    const char *status_why = "unknown status rejection";
    const char *sprite_why = "unknown sprite rejection";
    const char *solid_why = "unknown solid rejection";
    const char *ta_why = "unknown TA-stream rejection";
    uint32_t pixels = 0u;
    uint32_t kind = S5L_MBX_3D_ACCEPT_TILED;
    bool accepted = mbx_execute_first_tiled_over(
        m, bus, &tiled_why, &pixels);
    if (!accepted) {
        kind = S5L_MBX_3D_ACCEPT_STATUS;
        accepted = mbx_execute_status_sprite(m, bus, &status_why, &pixels);
    }
    if (!accepted) {
        kind = S5L_MBX_3D_ACCEPT_SPRITE;
        accepted = mbx_execute_textured_sprite(m, bus, &sprite_why, &pixels);
    }
    if (!accepted) {
        kind = S5L_MBX_3D_ACCEPT_SOLID;
        accepted = mbx_execute_solid_quad(m, bus, &solid_why, &pixels);
    }
    if (!accepted) {
        kind = S5L_MBX_3D_ACCEPT_TA_STREAM;
        accepted = mbx_execute_ta_stream(m, bus, &ta_why, &pixels);
    }
    if (!accepted) {
        mbx_counter_add(&mbx_3d_rejected, 1u);
        if (telemetry) {
            mbx_counter_add(&telemetry->rejected_3d, 1u);
            mbx_capture_3d_rejection(
                m, bus, tiled_why, status_why, sprite_why, solid_why,
                telemetry);
        }
        if (mbx_trace_state == 1) {
            fprintf(stderr,
                    "MBX3D reject STARTRENDER: tiled=%s; status=%s; "
                    "sprite=%s; solid=%s; ta=%s\n",
                    tiled_why, status_why, sprite_why, solid_why, ta_why);
            if (telemetry && telemetry->rejected_3d) {
                uint64_t sequence = telemetry->rejected_3d;
                const s5l_mbx_3d_rejection_witness_t *witness =
                    &telemetry->rejected_3d_history[
                        (sequence - 1u) % S5L_MBX_3D_REJECTION_HISTORY];
                fprintf(stderr,
                        "MBX3D witness #%llu regs "
                        "%08x:%08x:%08x:%08x:%08x:%08x:%08x:%08x "
                        "list %x:%08x:%08x:%08x:%08x record %08x:%u",
                        (unsigned long long)sequence,
                        witness->region, witness->object,
                        witness->target, witness->xclip, witness->yclip,
                        witness->pixel_sample, witness->framebuffer_control,
                        witness->framebuffer_stride,
                        witness->list_valid_mask, witness->list_words[0],
                        witness->list_words[1], witness->list_words[2],
                        witness->list_words[3], witness->record_base,
                        witness->record_valid_words);
                uint32_t valid = witness->record_valid_words;
                if (valid > S5L_MBX_3D_REJECTION_RECORD_WORDS)
                    valid = S5L_MBX_3D_REJECTION_RECORD_WORDS;
                for (uint32_t i = 0u; i < valid; i++)
                    fprintf(stderr, ":%08x", witness->record_words[i]);
                fputc('\n', stderr);
            }
        }
        return false;
    }

    /* AppleMBX's ISR records these independently and declares 3DIdle only
     * after all three have arrived. None is raised until the pixels above have
     * crossed the GART and committed through the observer-aware bus. */
    m->status |= S5L_MBX_STATUS_ISP |
                 S5L_MBX_STATUS_RENDER_COMPLETE |
                 S5L_MBX_STATUS_EVM_DALLOC;
    mbx_counter_add(&mbx_3d_completed, 1u);
    mbx_counter_add(&mbx_3d_pixels, pixels);
    if (telemetry) {
        mbx_counter_add(&telemetry->completed_3d, 1u);
        mbx_counter_add(&telemetry->pixels_3d, pixels);
        mbx_capture_3d_accept(m, bus, kind, pixels, telemetry);
    }
    if (mbx_trace_state == 1)
        fprintf(stderr, "MBX3D complete STARTRENDER: %u pixels\n", pixels);
    return true;
}

void s5l_mbx_reset(s5l_mbx_t *m) {
    if (!m) return;
    /*
     * Unlike the pointer-free device resets, this requires an initialized
     * machine-owned block: edram is an allocation that must survive a device
     * reset. Zero is the honest power-on value for the one register whose
     * meaning is known -- reset has NOT completed until it is asked for, and
     * a device that reported completion before the request would let the
     * driver past a handshake that never happened.
     *
     * The buffer belongs to the machine and outlives a device reset; its
     * CONTENTS are cleared, which is what a powered-down edram holds.
     */
    uint8_t *edram = m->edram;
    memset(m, 0, sizeof *m);
    m->edram = edram;
    if (edram) memset(edram, 0, S5L_MBX_EDRAM_SIZE);
}

uint32_t s5l_mbx_read(s5l_mbx_t *m, uint32_t off) {
    uint32_t v;

    if (!m || off >= S5L_MBX_APERTURE) return 0;

    /* Above the register block the aperture is edram: plain storage. */
    if (off >= S5L_MBX_SIZE) {
        uint32_t o = off - S5L_MBX_SIZE;
        if (!m->edram || o + 4u > S5L_MBX_EDRAM_SIZE) return 0;
        return (uint32_t)m->edram[o] | ((uint32_t)m->edram[o + 1u] << 8) |
               ((uint32_t)m->edram[o + 2u] << 16) |
               ((uint32_t)m->edram[o + 3u] << 24);
    }

    if (off == S5L_MBX_RESET) {
        /*
         * The acknowledgement. Bit 16 reads set only once the reset has been
         * requested, so the driver's spin ends because of something it did,
         * not because of a constant this file chose.
         *
         * BIT 16 IS OURS, AND THE STORED VALUE MUST NOT BE ABLE TO SUPPLY IT.
         * This used to OR reset_done onto whatever the register file held,
         * which is wrong the moment the guest writes the bit back -- and the
         * driver does exactly that. It deasserts by READ-MODIFY-WRITE: it reads
         * this register while bit 16 is set, clears bit 0, and writes the rest
         * back, so 0x00010000 lands in the register file. The old read then
         * returned that stored bit forever, and the deassert wait at
         * AppleMBX+0xb0c0 --
         *
         *     ands r0, r0, #0x10000
         *     bne  loop                  ; spin while bit 16 is SET
         *
         * -- could never end. r242 measured the consequence: 328,111,110 reads
         * of this one offset against 46 writes to the whole block, with the
         * last write to it being 0x00010000. That is not a driver polling for
         * progress, it is a driver that will never be told the reset finished.
         *
         * Masking first makes the bit a STATUS the model owns in both
         * directions: written bit 0 is the request, and bit 16 is the answer,
         * which is the coupling the driver expects and the one power.c holds
         * itself to.
         */
        v = m->reg[off / 4u] & ~S5L_MBX_RESET_DONE;
        if (m->reset_done) v |= S5L_MBX_RESET_DONE;
    } else if (off == S5L_MBX_STATUS) {
        v = m->status;
    } else if (off == S5L_MBX_REVISION) {
        /*
         * Identity, and it is read-only: the guest never writes here, so
         * serving it out of the register file would return the zero that made
         * AppleMBXDevice::start reject this device as unrecognised silicon.
         */
        v = S5L_MBX_REVISION_ID;
    } else {
        v = m->reg[off / 4u];
    }

    mbx_trace(off, v, false);
    return v;
}

/*
 * The interrupt output. See the declaration in soc.h for why it exists and why
 * it is a level rather than a pulse.
 *
 * It reports the status word and nothing else, so it cannot assert without a
 * kick having set a bit and cannot stay asserted once the driver has
 * acknowledged. A device that raised its line on its own would be exactly the
 * fabrication the header of this file refuses.
 */
bool s5l_mbx_irq(const s5l_mbx_t *m) {
    /* status & mask, exactly as the driver's ISR computes `pending`. A
     * line that asserted on an unmasked bit would interrupt for something
     * the driver has not asked about and cannot acknowledge. */
    if (!m) return false;
    return (m->status & m->reg[S5L_MBX_INTMASK / 4u]) != 0u;
}

void s5l_mbx_write(s5l_mbx_t *m, uint32_t off, uint32_t val) {
    if (!m || off >= S5L_MBX_APERTURE) return;

    if (off >= S5L_MBX_SIZE) {
        uint32_t o = off - S5L_MBX_SIZE;
        if (!m->edram || o + 4u > S5L_MBX_EDRAM_SIZE) return;
        m->edram[o]      = (uint8_t)(val & 0xffu);
        m->edram[o + 1u] = (uint8_t)((val >> 8) & 0xffu);
        m->edram[o + 2u] = (uint8_t)((val >> 16) & 0xffu);
        m->edram[o + 3u] = (uint8_t)((val >> 24) & 0xffu);
        if (off == S5L_MBX_3D_DATA_FIFO && mbx_trace_enabled()) {
            if (mbx_fifo_trace_enabled())
                fprintf(stderr, "MBX3D FIFO[%llu] = 0x%08x\n",
                        (unsigned long long)mbx_3d_fifo_words, val);
            mbx_3d_fifo_words++;
            mbx_3d_fifo_scene_words++;
        }
        /* AppleMBX writes START before streaming the scene. Its polled fallback
         * feeds every word to this one FIFO port and ends with 0xf0000000.
         * Raise TA_COMPLETE only at that measured boundary: doing it at START
         * lets the ISR context-switch while the producer is still writing and
         * exposes an empty object list to the following render. The START
         * register is the snapshotted in-flight latch and self-clears here. */
        if (off == S5L_MBX_3D_DATA_FIFO &&
            val == S5L_MBX_3D_SUBMIT &&
            m->reg[S5L_MBX_TA_START / 4u] == 1u) {
            if (mbx_trace_state == 1) {
                fprintf(stderr,
                        "MBX3D TA end words=%llu render-id=%08x "
                        "rgn=%08x obj=%08x fb=%08x\n",
                        (unsigned long long)mbx_3d_fifo_scene_words,
                        m->reg[0x810u / 4u],
                        m->reg[S5L_MBX_RGNBASE / 4u],
                        m->reg[S5L_MBX_OBJBASE / 4u],
                        m->reg[S5L_MBX_FBSTART / 4u]);
                fflush(stderr);
            }
            m->reg[S5L_MBX_TA_START / 4u] = 0u;
            m->status |= S5L_MBX_STATUS_TA_COMPLETE;
        }
        /* The register histogram deliberately covers only the 8 KiB register
         * block. The command ring is 64 KiB inside the much larger EDRAM
         * aperture, so record its sparse live writes separately when the same
         * opt-in trace is enabled. r366 proved that a complete packet can be
         * present in a checkpoint while the execution trigger reports zero
         * candidates; exact offset/value order is the missing fact. */
        if (off >= S5L_MBX_2D_RING_BASE &&
            off < S5L_MBX_2D_RING_BASE + S5L_MBX_2D_RING_SIZE &&
            mbx_trace_enabled()) {
            fprintf(stderr, "MBX2D ring write +0x%04x = 0x%08x\n",
                    off - S5L_MBX_2D_RING_BASE, val);
            fflush(stderr);
        }
        return;
    }

    mbx_trace(off, val, true);

    /* STATUS reads come from m->status. Its otherwise unreachable reg[] slot
     * carries the snapshotted pending-2D marker described above, so a recovery
     * W1S must not overwrite that private state between packet copy and submit. */
    if (off != S5L_MBX_STATUS) m->reg[off / 4u] = val;

    /*
     * Bit 0 is the request, and it is treated as the trigger because it is
     * the bit the driver sets: it writes the literal 1. A write that does NOT
     * carry bit 0 deliberately leaves the acknowledgement alone rather than
     * clearing it -- nothing observed shows the driver taking reset back, and
     * inventing a way to un-complete a reset would be a behaviour with no
     * evidence behind it.
     */
    /*
     * BIT 16 FOLLOWS THE REQUEST; it does not latch. The first version of this
     * file set it on a request and never cleared it, with the comment that
     * "nothing observed shows the driver taking a reset back". That was true
     * of the evidence then and false of the device, and the driver said so:
     * AppleMBX+0xb0c0 writes 0 to this same register and then spins
     *
     *     ands r0, r0, #0x10000
     *     bne  loop                  ; wait while bit 16 is SET
     *
     * which is the exact mirror of the assert wait at AppleMBX+0xb440. A latch
     * that never cleared turned that into an infinite loop -- the driver
     * attached, bound the display, and then hung here, which is how it was
     * found. So the bit tracks the request in both directions.
     */
    if (off == S5L_MBX_RESET) m->reset_done = (val & 1u) != 0u;

    /* 0x12c is write-one-to-set as well as readable status. AppleMBX recovery
     * injects missing 0x08, 0x04 and 0x40 events here in consecutive writes;
     * OR preserves the accumulated word that it subsequently reads. 0x134 is
     * the separate write-one-to-clear acknowledgement: it clears exactly the
     * bit the driver waited on (0x40), or 0x7ff during initialization. */
    if (off == S5L_MBX_STATUS) m->status |= val;
    if (off == S5L_MBX_STATUS_ACK) m->status &= ~val;

    /* AppleMBX+0xe854 programs 0x824..0x83c, writes BACKGROUND_TAG, waits for
     * EVM_DALLOC and acknowledges it synchronously. r365 proved those writes
     * are startup transfers, not 2D submissions. 2D_SYNC is therefore raised
     * only by s5l_mbx_process_2d(), after a decoded packet moved its pixels. */
    if (off == S5L_MBX_BACKGROUND_TAG)
        m->status |= S5L_MBX_STATUS_EVM_DALLOC;

    /* AppleMBX's context-reset setup at 0xc077ed04 writes the literal one to
     * 0x81c, then polls 0x12c bit 8 synchronously and clears that exact bit at
     * 0x134. The register and interrupt names independently agree with that
     * code path. This event says only that the reset request completed; it
     * moves no pixels and is not a substitute for a TA or render consumer.
     * The iPhone OS 1.0 AppleMBX (1A543a, 0xc032dcc8) issues the same request
     * as `write(0x85c, 0x80); write(0x81c, 0)` and polls the same bit 8, so a
     * zero write is a completed request too. */
    if (off == S5L_MBX_TA_CONTEXT_RESET && val <= 1u)
        m->status |= S5L_MBX_STATUS_TA_CONTEXT;

    /* The context-store companion uses the same synchronous TA_CONTEXT event.
     * Without it, the first correctly completed TA submission reaches the
     * driver's bounded poll and panics with `ta store timeout`. */
    if (off == S5L_MBX_TA_CONTEXT_STORE && val == 1u)
        m->status |= S5L_MBX_STATUS_TA_CONTEXT;

    /* Context load is the second half of the same driver transaction and has
     * an independent bounded poll for the shared TA_CONTEXT completion. */
    if (off == S5L_MBX_TA_CONTEXT_LOAD && val == 1u)
        m->status |= S5L_MBX_STATUS_TA_CONTEXT;

    /* AppleMBX sets TAStatus=1 immediately before writing one to 0x800. Its
     * watchdog's recovery for that exact outstanding state manufactures bit 4
     * (TA_COMPLETE), proving which event the real device failed to deliver in
     * the old model. TA submission bins a scene; it is distinct from the later
     * STARTRENDER operation that validates and commits pixels above. */
    /* TA_START is the in-flight latch consumed by the 3DDATA FIFO terminator
     * above. It deliberately raises nothing here: STARTRENDER is later still,
     * after TA completion and the context-store/load transaction. */
    if (off == S5L_MBX_TA_START && val == 1u) {
        mbx_3d_fifo_scene_words = 0u;
        if (mbx_trace_state == 1) {
            fprintf(stderr,
                    "MBX3D TA start render-id=%08x rgn=%08x obj=%08x "
                    "fb=%08x ctx=%08x evm=%08x:%08x:%08x db=%08x "
                    "tail=%08x region=%08x global=%08x clip=%08x:%08x "
                    "cfg=%08x\n",
                    m->reg[0x810u / 4u],
                    m->reg[S5L_MBX_RGNBASE / 4u],
                    m->reg[S5L_MBX_OBJBASE / 4u],
                    m->reg[S5L_MBX_FBSTART / 4u],
                    m->reg[0x820u / 4u], m->reg[0x824u / 4u],
                    m->reg[0x828u / 4u], m->reg[0x82cu / 4u],
                    m->reg[0x83cu / 4u], m->reg[0x840u / 4u],
                    m->reg[0x844u / 4u], m->reg[0x848u / 4u],
                    m->reg[0x84cu / 4u], m->reg[0x850u / 4u],
                    m->reg[0x85cu / 4u]);
            fflush(stderr);
        }
    }
    if (off == S5L_MBX_STARTRENDER && val == 1u && mbx_trace_state == 1) {
        fprintf(stderr,
                "MBX3D render start render-id=%08x rgn=%08x obj=%08x "
                "fb=%08x\n",
                m->reg[0x810u / 4u],
                m->reg[S5L_MBX_RGNBASE / 4u],
                m->reg[S5L_MBX_OBJBASE / 4u],
                m->reg[S5L_MBX_FBSTART / 4u]);
        fflush(stderr);
    }
}
