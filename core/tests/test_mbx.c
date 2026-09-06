/*
 * S5LBox -- the first measured PowerVR MBX2D command.
 *
 * This is intentionally not a generic "copy some bytes" test. It transcribes
 * the packet produced by iPhone OS 3's _pack2DCtxBlitCopy, builds the GART the
 * way AppleMBXMMU::map does, routes every command word through the real machine
 * aperture, and checks the two facts that make completion honest: translated
 * pixels moved, and bit 10 rose only after AppleMBX's separate ring+0 submit
 * store. Unknown packet modes and incomplete GARTs must move nothing and
 * complete nothing.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#include "soc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass, g_fail;
#define CHECK(cond, ...) do { \
    if (cond) g_pass++; \
    else { \
        g_fail++; \
        printf("  FAIL %s:%d: ", __func__, __LINE__); \
        printf(__VA_ARGS__); \
        printf("\n"); \
    } \
} while (0)

/* Literals, not model macros: these are the addresses/values recovered from
 * AppleMBX and the 64-byte r365 packet. A wrong constant in soc.h therefore
 * cannot make the test agree with the implementation by construction. */
#define RAM_BASE       0x08000000u
#define RAM_SIZE       0x00200000u
#define MBX_BASE       0x3b000000u
#define REG_STATUS     0x0000012cu
#define REG_MASK       0x00000130u
#define REG_ACK        0x00000134u
#define REG_KICK       0x000006d8u
#define REG_TA_START   0x00000800u
#define REG_TA_DB      0x0000083cu
#define REG_TA_REGION  0x00000844u
#define REG_CTX_LOAD   0x00000814u
#define REG_CTX_STORE  0x00000818u
#define REG_CTX_RESET  0x0000081cu
#define REG_3D_FIFO    0x00800000u
#define REG_RGNBASE    0x00000608u
#define REG_OBJBASE    0x0000060cu
#define REG_PIXSAMP    0x0000061cu
#define REG_FBCTL      0x00000650u
#define REG_FBXCLIP    0x00000654u
#define REG_FBYCLIP    0x00000658u
#define REG_FBSTART    0x0000065cu
#define REG_FBSTRIDE   0x00000660u
#define REG_RENDER     0x00000680u
#define REG_GART0      0x00001000u
#define REG_GART2      0x00001008u
#define REG_GART3      0x0000100cu
#define RING           0x00a00000u
#define GART_TABLE     0x08001000u
#define SRC_GPU        0x00800080u
#define DST_GPU        0x00810000u

static const uint32_t PACKET[16] = {
    0xa0060500u, DST_GPU, 0x94060500u, SRC_GPU,
    0x30008003u, 0x60800200u, 0x8000ccccu, 0xffffffffu,
    0x00050006u, 0x00090008u, 0x70000000u, 0x70000000u,
    0x70000000u, 0x70000000u, 0x70000000u, 0x70000000u,
};

static void write_packet(s5l8900_t *m, uint32_t ring_off,
                         const uint32_t packet[16], unsigned words) {
    for (unsigned i = 0; i < words; i++)
        m->bus.write32(m->bus.ctx, MBX_BASE + ring_off + i * 4u, packet[i]);
}

static void test_translated_copy_and_completion_boundary(void) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, RAM_BASE, RAM_SIZE), "machine init failed");
    if (!m.ram) return;
    CHECK(s5l8900_set_direct_ram_writes(&m, false),
          "observer-path fixture could not revoke direct RAM writes");

    /* GPU VA 0x008xxxxx selects root register 2. Source rows intentionally
     * land on non-contiguous physical pages; destination rows do too. */
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_GART2, GART_TABLE);
    m.bus.write32(m.bus.ctx, GART_TABLE + 0x00u, 0x08020000u); /* GPU page 0 */
    m.bus.write32(m.bus.ctx, GART_TABLE + 0x04u, 0x08021000u); /* GPU page 1 */
    m.bus.write32(m.bus.ctx, GART_TABLE + 0x44u, 0x08040000u); /* GPU page 0x11 */
    m.bus.write32(m.bus.ctx, GART_TABLE + 0x48u, 0x08050000u); /* GPU page 0x12 */

    static const uint32_t row0[4] = {
        0x11223344u, 0x55667788u, 0x99aabbccu, 0xddeeff00u
    };
    static const uint32_t row1[4] = {
        0x01234567u, 0x89abcdefu, 0x13579bdfu, 0x2468ace0u
    };
    const uint32_t src0 = 0x08020f88u; /* SRC + y*0x500 + x*4 */
    const uint32_t src1 = 0x08021488u;
    const uint32_t dst0 = 0x08040e14u; /* DST + y*0x500 + x*4 */
    const uint32_t dst1 = 0x08050314u;
    for (unsigned i = 0; i < 4; i++) {
        m.bus.write32(m.bus.ctx, src0 + i * 4u, row0[i]);
        m.bus.write32(m.bus.ctx, src1 + i * 4u, row1[i]);
    }

    /* 0x6d8 is the synchronous startup transfer. It owns bit 6, not a fake
     * bit-10 2D completion. */
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_KICK, 0x09000000u);
    uint32_t status = m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS);
    CHECK((status & 0x40u) != 0u && (status & 0x400u) == 0u,
          "startup kick status=%08x, expect bit 6 only", status);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_ACK, 0x7ffu);

    /* Neither an incomplete packet nor its last terminator is a submission.
     * AppleMBX+0x1f58's separate fixed ring+0 store is the measured boundary. */
    write_packet(&m, RING, PACKET, 15u);
    CHECK(m.bus.read32(m.bus.ctx, dst0) == 0u &&
          m.bus.read32(m.bus.ctx, dst1) == 0u,
          "an incomplete packet changed destination RAM");
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "an incomplete packet raised status");

    m.bus.write32(m.bus.ctx, MBX_BASE + RING + 15u * 4u, PACKET[15]);
    CHECK(m.bus.read32(m.bus.ctx, dst0) == 0u &&
          m.bus.read32(m.bus.ctx, dst1) == 0u,
          "packet terminators executed before the submit store");
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "packet terminators raised completion before submit");

    m.bus.write32(m.bus.ctx, MBX_BASE + RING, 0xf0000000u);
    for (unsigned i = 0; i < 4; i++) {
        CHECK(m.bus.read32(m.bus.ctx, dst0 + i * 4u) == row0[i],
              "row 0 pixel %u did not cross the GART", i);
        CHECK(m.bus.read32(m.bus.ctx, dst1 + i * 4u) == row1[i],
              "row 1 pixel %u did not cross the GART", i);
    }
    status = m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS);
    CHECK((status & 0x400u) != 0u && (status & 0x40u) == 0u,
          "executed packet status=%08x, expect bit 10 only", status);

    m.bus.write32(m.bus.ctx, MBX_BASE + REG_MASK, 0x400u);
    CHECK(s5l_mbx_irq(&m.mbx), "completed 2D packet did not assert masked IRQ");
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_ACK, 0x400u);
    CHECK(!s5l_mbx_irq(&m.mbx), "2D acknowledge did not lower IRQ");

    /* r372 disproved the old per-packet-header-rewrite interpretation. The
     * next packet begins at +0x40, but AppleMBX still submits through ring+0. */
    for (unsigned i = 0; i < 4; i++) {
        m.bus.write32(m.bus.ctx, dst0 + i * 4u, 0u);
        m.bus.write32(m.bus.ctx, dst1 + i * 4u, 0u);
    }
    write_packet(&m, RING + 0x40u, PACKET, 16u);
    CHECK(m.bus.read32(m.bus.ctx, dst0) == 0u &&
          m.bus.read32(m.bus.ctx, dst1) == 0u,
          "second packet executed before the fixed submit store");
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "second packet raised completion before the fixed submit store");

    m.bus.write32(m.bus.ctx, MBX_BASE + RING, 0xf0000000u);
    for (unsigned i = 0; i < 4; i++) {
        CHECK(m.bus.read32(m.bus.ctx, dst0 + i * 4u) == row0[i],
              "second row 0 pixel %u did not cross the GART", i);
        CHECK(m.bus.read32(m.bus.ctx, dst1 + i * 4u) == row1[i],
              "second row 1 pixel %u did not cross the GART", i);
    }
    status = m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS);
    CHECK((status & 0x400u) != 0u,
          "fixed submit store did not raise 2D completion: %08x", status);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_ACK, 0x400u);

    s5l8900_free(&m);
}

static void test_unknown_packet_and_bad_gart_are_atomic(void) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, RAM_BASE, RAM_SIZE), "machine init failed");
    if (!m.ram) return;
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_GART2, GART_TABLE);
    m.bus.write32(m.bus.ctx, GART_TABLE + 0x00u, 0x08020000u);
    m.bus.write32(m.bus.ctx, GART_TABLE + 0x04u, 0x08021000u);
    m.bus.write32(m.bus.ctx, GART_TABLE + 0x44u, 0x08040000u);
    m.bus.write32(m.bus.ctx, GART_TABLE + 0x48u, 0x08050000u);

    uint32_t unknown[16];
    memcpy(unknown, PACKET, sizeof unknown);
    unknown[5] ^= 1u; /* an unmeasured scale: reject, do not approximate */
    m.bus.write32(m.bus.ctx, 0x08040e14u, 0xa5a5a5a5u);
    write_packet(&m, RING + 0x40u, unknown, 16u);
    m.bus.write32(m.bus.ctx, MBX_BASE + RING, 0xf0000000u);
    CHECK(m.bus.read32(m.bus.ctx, 0x08040e14u) == 0xa5a5a5a5u,
          "unknown scale changed a destination pixel");
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "unknown scale raised completion");

    /* A second destination has a valid first row and a missing second-row
     * PTE. Validation must find the late hole before touching the first row. */
    uint32_t bad[16];
    memcpy(bad, PACKET, sizeof bad);
    bad[1] = 0x00820000u;
    m.bus.write32(m.bus.ctx, GART_TABLE + 0x84u, 0x08060000u); /* page 0x21 */
    /* page 0x22 at +0x88 deliberately remains zero */
    m.bus.write32(m.bus.ctx, 0x08060e14u, 0x5a5a5a5au);
    write_packet(&m, RING + 0x80u, bad, 16u);
    m.bus.write32(m.bus.ctx, MBX_BASE + RING, 0xf0000000u);
    CHECK(m.bus.read32(m.bus.ctx, 0x08060e14u) == 0x5a5a5a5au,
          "late missing PTE left a partially written rectangle");
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "bad GART raised completion");

    s5l8900_free(&m);
}

static void test_status_write_to_set_and_ack(void) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, RAM_BASE, RAM_SIZE), "machine init failed");
    if (!m.ram) return;

    /* AppleMBX recovery injects the three missing 3D events by writing them
     * separately to status. Each write must accumulate even while masked. */
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_STATUS, 0x08u); /* ISP */
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0x08u,
          "first status write did not set ISP");
    CHECK(!s5l_mbx_irq(&m.mbx),
          "masked status write asserted the MBX interrupt");

    m.bus.write32(m.bus.ctx, MBX_BASE + REG_STATUS, 0x04u); /* render */
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_STATUS, 0x40u); /* EVM */
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0x4cu,
          "consecutive status writes did not accumulate all 3D events");

    m.bus.write32(m.bus.ctx, MBX_BASE + REG_MASK, 0x4cu);
    CHECK(s5l_mbx_irq(&m.mbx),
          "unmasking accumulated status did not assert the interrupt");

    m.bus.write32(m.bus.ctx, MBX_BASE + REG_ACK, 0x04u);
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0x48u,
          "selective acknowledge cleared more than render-complete");
    CHECK(s5l_mbx_irq(&m.mbx),
          "interrupt fell while unacknowledged status remained");

    m.bus.write32(m.bus.ctx, MBX_BASE + REG_ACK, 0x48u);
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "final acknowledge left recovery status pending");
    CHECK(!s5l_mbx_irq(&m.mbx),
          "fully acknowledged recovery status left IRQ asserted");

    s5l8900_free(&m);
}

static void test_ta_context_reset_handshake(void) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, RAM_BASE, RAM_SIZE), "machine init failed");
    if (!m.ram) return;

    /* AppleMBX at 0xc077ed04 writes exactly one to 0x81c, polls status bit
     * 0x100 at 0xc077ed18, then acknowledges exactly 0x100 at 0xc077ed30.
     * Keep the literals here so a wrong model constant cannot bless itself. */
    /* iPhone OS 1.0 AppleMBX (1A543a, 0xc032dcdc) issues the same request as
     * a zero write and polls the same bit, so zero completes as well. */
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_CTX_RESET, 0u);
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0x100u,
          "zero context-reset write (1A543a form) did not raise completion");
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_ACK, 0x100u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_CTX_RESET, 2u);
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "unmeasured context-reset value raised completion");

    m.bus.write32(m.bus.ctx, MBX_BASE + REG_CTX_RESET, 1u);
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_CTX_RESET) == 1u,
          "context-reset request did not remain readable storage");
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0x100u,
          "context-reset request did not raise TA_CONTEXT");
    CHECK(!s5l_mbx_irq(&m.mbx),
          "masked synchronous context completion asserted IRQ");

    m.bus.write32(m.bus.ctx, MBX_BASE + REG_MASK, 0x100u);
    CHECK(s5l_mbx_irq(&m.mbx),
          "unmasking TA_CONTEXT did not assert its pending IRQ");
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_ACK, 0x100u);
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "TA_CONTEXT acknowledge left status pending");
    CHECK(!s5l_mbx_irq(&m.mbx),
          "TA_CONTEXT acknowledge did not lower IRQ");

    s5l8900_free(&m);
}

static void test_ta_context_store_handshake(void) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, RAM_BASE, RAM_SIZE), "machine init failed");
    if (!m.ram) return;

    /* AppleMBX at 0xc077d570 writes exactly one to 0x818, polls status bit
     * 0x100 at 0xc077d588, and panics at 0xc077d5ac if it never arrives. */
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_CTX_STORE, 0u);
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "zero context-store write raised completion");
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_CTX_STORE, 2u);
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "unmeasured context-store value raised completion");

    m.bus.write32(m.bus.ctx, MBX_BASE + REG_CTX_STORE, 1u);
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_CTX_STORE) == 1u,
          "context-store request did not remain readable storage");
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0x100u,
          "context-store request did not raise TA_CONTEXT");
    CHECK(!s5l_mbx_irq(&m.mbx),
          "masked synchronous context-store completion asserted IRQ");

    m.bus.write32(m.bus.ctx, MBX_BASE + REG_MASK, 0x100u);
    CHECK(s5l_mbx_irq(&m.mbx),
          "unmasking context-store completion did not assert pending IRQ");
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_ACK, 0x100u);
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "context-store acknowledge left TA_CONTEXT pending");
    CHECK(!s5l_mbx_irq(&m.mbx),
          "context-store acknowledge did not lower IRQ");

    s5l8900_free(&m);
}

static void test_ta_context_load_handshake(void) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, RAM_BASE, RAM_SIZE), "machine init failed");
    if (!m.ram) return;

    /* AppleMBX at 0xc077d6f4 writes exactly one to 0x814, polls status bit
     * 0x100 at 0xc077d700, and panics at 0xc077d724 if it never arrives. The
     * literal register/value pair was captured at the write-helper call. */
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_CTX_LOAD, 0u);
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "zero context-load write raised completion");
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_CTX_LOAD, 2u);
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "unmeasured context-load value raised completion");

    m.bus.write32(m.bus.ctx, MBX_BASE + REG_CTX_LOAD, 1u);
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_CTX_LOAD) == 1u,
          "context-load request did not remain readable storage");
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0x100u,
          "context-load request did not raise TA_CONTEXT");
    CHECK(!s5l_mbx_irq(&m.mbx),
          "masked synchronous context-load completion asserted IRQ");

    m.bus.write32(m.bus.ctx, MBX_BASE + REG_MASK, 0x100u);
    CHECK(s5l_mbx_irq(&m.mbx),
          "unmasking context-load completion did not assert pending IRQ");
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_ACK, 0x100u);
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "context-load acknowledge left TA_CONTEXT pending");
    CHECK(!s5l_mbx_irq(&m.mbx),
          "context-load acknowledge did not lower IRQ");

    s5l8900_free(&m);
}

static void test_ta_submission_completion(void) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, RAM_BASE, RAM_SIZE), "machine init failed");
    if (!m.ram) return;

    /* AppleMBX at 0xc077ed54 sets TAStatus=1, writes exactly one to 0x800 at
     * 0xc077ed84, and later recovers that outstanding state by injecting status
     * bit 0x10 at 0xc077f3c4. Keep the recovered literals independent of the
     * model constants, and reject unobserved request values. */
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_TA_START, 0u);
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "zero TA-start write raised completion");
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_TA_START, 2u);
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "unmeasured TA-start value raised completion");

    m.bus.write32(m.bus.ctx, MBX_BASE + REG_TA_START, 1u);
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_TA_START) == 1u,
          "TA-start request did not arm its in-flight latch");
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "TA-start request completed before its FIFO stream");
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_3D_FIFO, 0x10000004u);
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "ordinary 3D FIFO data completed the TA submission");
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_3D_FIFO, 0xf0000000u);
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_TA_START) == 0u,
          "3D FIFO terminator did not consume the in-flight TA latch");
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0x10u,
          "3D FIFO terminator did not raise TA_COMPLETE");
    CHECK(!s5l_mbx_irq(&m.mbx),
          "masked TA completion asserted IRQ");

    m.bus.write32(m.bus.ctx, MBX_BASE + REG_MASK, 0x10u);
    CHECK(s5l_mbx_irq(&m.mbx),
          "unmasking TA_COMPLETE did not assert its pending IRQ");
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_ACK, 0x10u);
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "TA_COMPLETE acknowledge left status pending");
    CHECK(!s5l_mbx_irq(&m.mbx),
          "TA_COMPLETE acknowledge did not lower IRQ");
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_3D_FIFO, 0xf0000000u);
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "unarmed 3D FIFO terminator manufactured TA_COMPLETE");

    s5l8900_free(&m);
}

static uint32_t test_gpu_pa(s5l8900_t *m, uint32_t gpu) {
    uint32_t chunk = gpu >> 22;
    uint32_t root = m->bus.read32(m->bus.ctx,
        MBX_BASE + REG_GART0 + chunk * 4u);
    uint32_t pte = m->bus.read32(m->bus.ctx,
        root + (((gpu >> 12) & 0x3ffu) * 4u));
    return pte + (gpu & 0xfffu);
}

static void test_gpu_write32(s5l8900_t *m, uint32_t gpu, uint32_t value) {
    m->bus.write32(m->bus.ctx, test_gpu_pa(m, gpu), value);
}

static uint32_t test_gpu_read32(s5l8900_t *m, uint32_t gpu) {
    return m->bus.read32(m->bus.ctx, test_gpu_pa(m, gpu));
}

static void test_gpu_write16(s5l8900_t *m, uint32_t gpu, uint16_t value) {
    m->bus.write16(m->bus.ctx, test_gpu_pa(m, gpu), value);
}

static uint32_t test_argb1555_to_bgra8(uint16_t pixel) {
    uint32_t blue5 = pixel & 0x1fu;
    uint32_t green5 = (pixel >> 5) & 0x1fu;
    uint32_t red5 = (pixel >> 10) & 0x1fu;
    uint32_t blue8 = (blue5 << 3) | (blue5 >> 2);
    uint32_t green8 = (green5 << 3) | (green5 >> 2);
    uint32_t red8 = (red5 << 3) | (red5 >> 2);
    uint32_t alpha8 = (pixel & 0x8000u) ? 0xffu : 0u;
    return blue8 | (green8 << 8) | (red8 << 16) | (alpha8 << 24);
}

static uint32_t test_float_word(float value) {
    uint32_t word = 0u;
    memcpy(&word, &value, sizeof word);
    return word;
}

static uint32_t test_over(uint32_t dst, uint32_t src) {
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

static uint32_t test_modulate_vertex_alpha(uint32_t src, uint32_t alpha) {
    uint32_t out = 0u;
    for (unsigned shift = 0; shift < 32u; shift += 8u) {
        uint32_t component = (src >> shift) & 0xffu;
        out |= (((component + 1u) * alpha) >> 8) << shift;
    }
    return out;
}

static float test_float_value(uint32_t word) {
    float value = 0.0f;
    memcpy(&value, &word, sizeof value);
    return value;
}

static uint32_t test_sprite_source_pixel(uint32_t x, uint32_t y) {
    uint32_t alpha = 0x40u + ((x * 11u + y * 7u) & 0xbfu);
    return (alpha << 24) | ((alpha * 3u / 4u) << 16) |
           ((alpha / 2u) << 8) | (alpha / 4u);
}

struct test_bilinear_axis {
    uint32_t first, second, weight;
};

static bool test_bilinear_coordinate(float coordinate, uint32_t dimension,
                                     struct test_bilinear_axis *axis) {
    if (!axis || !dimension || coordinate < 0.0f) return false;
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
    axis->weight = (first_fixed & 0xffffu) >> 8;
    return true;
}

static bool test_bilinear_axis(float origin, float span,
                               float texel_origin, float texel_span,
                               uint32_t pixel, uint32_t dimension,
                               struct test_bilinear_axis *axis) {
    if (!axis || !dimension || span <= 0.0f) return false;
    float coordinate = texel_origin +
                       ((float)pixel + 0.5f - origin) *
                       (texel_span / span);
    return test_bilinear_coordinate(coordinate, dimension, axis);
}

struct test_affine_transform {
    float origin_x, origin_y;
    float u_x, u_y;
    float v_x, v_y;
    float determinant;
};

static bool test_affine_pixel(const struct test_affine_transform *transform,
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

/* Independent scalar reference for QuartzCore's packed interpolation.  Signed
 * floor division is written out so this test does not merely copy the packed
 * production implementation it is meant to check. */
static uint32_t test_linear_bgra8(uint32_t first, uint32_t second,
                                  uint32_t weight) {
    uint32_t out = 0u;
    for (unsigned shift = 0; shift < 32u; shift += 8u) {
        int32_t a = (int32_t)((first >> shift) & 0xffu);
        int32_t b = (int32_t)((second >> shift) & 0xffu);
        int32_t product = (int32_t)weight * (b - a);
        int32_t quotient = product >= 0
            ? product / 256 : -((-product + 255) / 256);
        out |= (uint32_t)((a + quotient) & 0xff) << shift;
    }
    return out;
}

static uint32_t test_bilinear_sprite_pixel(
        const struct test_bilinear_axis *x,
        const struct test_bilinear_axis *y) {
    uint32_t top_left = test_sprite_source_pixel(x->first, y->first);
    uint32_t bottom_left = test_sprite_source_pixel(x->first, y->second);
    uint32_t top_right = test_sprite_source_pixel(x->second, y->first);
    uint32_t bottom_right = test_sprite_source_pixel(x->second, y->second);
    uint32_t vertical_left =
        test_linear_bgra8(top_left, bottom_left, y->weight);
    uint32_t vertical_right =
        test_linear_bgra8(top_right, bottom_right, y->weight);
    return test_linear_bgra8(vertical_left, vertical_right, x->weight);
}

static void test_map_gpu_page(s5l8900_t *m, uint32_t table,
                              uint32_t gpu, uint32_t pa) {
    m->bus.write32(m->bus.ctx,
        table + (((gpu >> 12) & 0x3ffu) * 4u), pa);
}

static uint32_t test_ta_solid_draw(uint32_t *stream, uint32_t start,
                                   float x0, float y0, float x1, float y1,
                                   uint32_t colour) {
    stream[start] = 0x10000021u;
    stream[start + 1u] = 0x22600e80u;
    stream[start + 2u] = 0x00000504u;
    stream[start + 3u] = 0x48020000u;
    stream[start + 4u] = 0xf0020004u;
    static const uint8_t corners[4][2] = {
        {1u, 0u}, {1u, 1u}, {0u, 0u}, {0u, 1u},
    };
    for (uint32_t vertex = 0u; vertex < 4u; vertex++) {
        uint32_t base = start + 5u + vertex * 6u;
        stream[base] = 0u;
        stream[base + 1u] = test_float_word(
            corners[vertex][0] ? x1 : x0);
        stream[base + 2u] = test_float_word(
            corners[vertex][1] ? y1 : y0);
        stream[base + 3u] = 0u;
        stream[base + 4u] = 0x3f800000u;
        stream[base + 5u] = colour;
    }
    stream[start + 29u] = 0x00000003u;
    return start + 30u;
}

static uint32_t test_ta_solid_continuation(
        uint32_t *stream, uint32_t start,
        float x0, float y0, float x1, float y1, uint32_t colour) {
    stream[start] = 0x48020000u;
    stream[start + 1u] = 0xf0020004u;
    static const uint8_t corners[4][2] = {
        {1u, 0u}, {1u, 1u}, {0u, 0u}, {0u, 1u},
    };
    for (uint32_t vertex = 0u; vertex < 4u; vertex++) {
        uint32_t base = start + 2u + vertex * 6u;
        stream[base] = 0u;
        stream[base + 1u] = test_float_word(
            corners[vertex][0] ? x1 : x0);
        stream[base + 2u] = test_float_word(
            corners[vertex][1] ? y1 : y0);
        stream[base + 3u] = 0u;
        stream[base + 4u] = 0x3f800000u;
        stream[base + 5u] = colour;
    }
    stream[start + 26u] = 0x00000003u;
    return start + 27u;
}

static uint32_t test_ta_textured_vertices(
        uint32_t *stream, uint32_t start,
        const float x[4], const float y[4],
        const float u[4], const float v[4], uint32_t colour) {
    stream[start] = 0x48020000u;
    stream[start + 1u] = 0xf0020044u;
    for (uint32_t vertex = 0u; vertex < 4u; vertex++) {
        uint32_t base = start + 2u + vertex * 8u;
        stream[base] = 0u;
        stream[base + 1u] = test_float_word(x[vertex]);
        stream[base + 2u] = test_float_word(y[vertex]);
        stream[base + 3u] = 0u;
        stream[base + 4u] = 0x3f800000u;
        stream[base + 5u] = colour;
        stream[base + 6u] = test_float_word(u[vertex]);
        stream[base + 7u] = test_float_word(v[vertex]);
    }
    stream[start + 34u] = 0x00000003u;
    return start + 35u;
}

static uint32_t test_ta_textured_draw(
        uint32_t *stream, uint32_t start,
        uint32_t header, uint32_t source, uint32_t sampler,
        const float x[4], const float y[4],
        const float u[4], const float v[4], uint32_t colour) {
    stream[start] = 0x10000004u;
    stream[start + 1u] = header;
    stream[start + 2u] = source;
    stream[start + 3u] = sampler;
    return test_ta_textured_vertices(
        stream, start + 4u, x, y, u, v, colour);
}

static uint32_t test_ta_global_transform(
        uint32_t *stream, uint32_t start,
        uint32_t width, uint32_t height,
        float origin_x, float origin_y) {
    stream[start] = 0xa0000003u;
    stream[start + 1u] = test_float_word(2.0f / (float)width);
    stream[start + 2u] = 0u;
    stream[start + 3u] = 0u;
    stream[start + 4u] = test_float_word(
        origin_x * 2.0f / (float)width - 1.0f);
    stream[start + 5u] = 0u;
    stream[start + 6u] = test_float_word(2.0f / (float)height);
    stream[start + 7u] = 0u;
    stream[start + 8u] = test_float_word(
        origin_y * 2.0f / (float)height - 1.0f);
    stream[start + 9u] = 0u;
    stream[start + 10u] = 0u;
    stream[start + 11u] = 0u;
    stream[start + 12u] = 0u;
    stream[start + 13u] = 0u;
    stream[start + 14u] = 0u;
    stream[start + 15u] = 0u;
    stream[start + 16u] = 0x3f800000u;
    return start + 17u;
}

static void test_ta_run_stream(s5l8900_t *m,
                               const uint32_t *stream, uint32_t count) {
    m->bus.write32(m->bus.ctx, MBX_BASE + REG_TA_START, 1u);
    for (uint32_t i = 0u; i < count; i++)
        m->bus.write32(m->bus.ctx, MBX_BASE + REG_3D_FIFO, stream[i]);
    m->bus.write32(m->bus.ctx, MBX_BASE + REG_ACK, 0x10u);
    m->bus.write32(m->bus.ctx, MBX_BASE + REG_RENDER, 1u);
}

static void test_ta_stream_axis_aligned_atomic_scene(void) {
    enum {
        TARGET_STRIDE = 0x500u,
        TARGET_HEIGHT = 480u,
        TARGET_BYTES = TARGET_STRIDE * TARGET_HEIGHT,
    };
    const uint32_t table0 = 0x08003000u;
    const uint32_t table2 = 0x08004000u;
    const uint32_t object = 0x00100000u;
    const uint32_t region = 0x00040000u;
    const uint32_t target = 0x00810000u;
    const uint32_t object_pa = 0x08010000u;
    const uint32_t target_pa = 0x08080000u;
    uint32_t stream[76] = {0x10000010u};
    uint32_t next = test_ta_solid_draw(
        stream, 1u, 10.0f, 20.0f, 14.0f, 23.0f, 0xff0000ffu);
    static const uint32_t state[14] = {
        0xa01b001cu, 0u, 0u, 0u, 0u, 0u, 0u,
        0u, 0u, 0xa01d001du, 0u, 0u, 0u, 0x3f800000u,
    };
    memcpy(&stream[next], state, sizeof state);
    next += 14u;
    next = test_ta_solid_draw(
        stream, next, 20.0f, 30.0f, 22.0f, 32.0f, 0xff00ff00u);
    stream[next++] = 0xf0000000u;
    CHECK(next == sizeof stream / sizeof stream[0],
          "TA stream fixture has %u words, expected %u", next,
          (unsigned)(sizeof stream / sizeof stream[0]));

    s5l8900_t m;
    CHECK(s5l8900_init(&m, RAM_BASE, RAM_SIZE),
          "TA stream machine init failed");
    if (!m.ram) return;
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_GART0, table0);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_GART2, table2);
    test_map_gpu_page(&m, table0, object, object_pa);
    for (uint32_t page = 0u; page < TARGET_BYTES; page += 0x1000u)
        test_map_gpu_page(&m, table2, target + page, target_pa + page);

    m.bus.write32(m.bus.ctx, MBX_BASE + REG_TA_DB, object);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_TA_REGION, region);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RGNBASE, region);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_OBJBASE, object);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_PIXSAMP, 0x00020007u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBCTL, 6u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBXCLIP, 0x013f0000u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBYCLIP, 0x01df0000u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBSTART, target);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBSTRIDE, 320u);

    m.bus.write32(m.bus.ctx, MBX_BASE + REG_TA_START, 1u);
    for (uint32_t i = 0u; i < next; i++) {
        m.bus.write32(m.bus.ctx, MBX_BASE + REG_3D_FIFO, stream[i]);
        CHECK(test_gpu_read32(&m, object + i * 4u) == stream[i],
              "TA word %u was not staged through its object database", i);
    }
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0x10u,
          "complete TA stream did not raise exactly TA_COMPLETE");
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_ACK, 0x10u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);

    uint32_t mismatches = 0u;
    for (uint32_t y = 20u; y < 23u; y++)
        for (uint32_t x = 10u; x < 14u; x++)
            mismatches += test_gpu_read32(
                &m, target + y * TARGET_STRIDE + x * 4u) != 0xff0000ffu;
    for (uint32_t y = 30u; y < 32u; y++)
        for (uint32_t x = 20u; x < 22u; x++)
            mismatches += test_gpu_read32(
                &m, target + y * TARGET_STRIDE + x * 4u) != 0xff00ff00u;
    CHECK(mismatches == 0u, "%u TA solid-scene pixels mismatched", mismatches);
    CHECK(test_gpu_read32(&m, target + 20u * TARGET_STRIDE + 9u * 4u) == 0u &&
          test_gpu_read32(&m, target + 23u * TARGET_STRIDE + 10u * 4u) == 0u &&
          test_gpu_read32(&m, target + 30u * TARGET_STRIDE + 19u * 4u) == 0u,
          "TA scene changed a pixel outside its rectangles");
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0x4cu,
          "accepted TA scene did not raise all three render events");
    CHECK(m.mbx_telemetry.candidates_3d == 1u &&
          m.mbx_telemetry.completed_3d == 1u &&
          m.mbx_telemetry.rejected_3d == 0u &&
          m.mbx_telemetry.pixels_3d == 16u &&
          m.mbx_telemetry.accepted_3d_history[0].kind ==
              S5L_MBX_3D_ACCEPT_TA_STREAM,
          "accepted TA scene telemetry=%llu/%llu/%llu/%llu kind=%u",
          (unsigned long long)m.mbx_telemetry.candidates_3d,
          (unsigned long long)m.mbx_telemetry.completed_3d,
          (unsigned long long)m.mbx_telemetry.rejected_3d,
          (unsigned long long)m.mbx_telemetry.pixels_3d,
          m.mbx_telemetry.accepted_3d_history[0].kind);

    /* The same TA grammar also backs temporary full-clip surfaces. The live
     * Voice Memos transition uses 64x32 and 192x64/96 targets before blending
     * them into the 320x480 screen. Stride is pixels, and must control both
     * staging and commit rather than being mistaken for a fixed phone width. */
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_ACK, 0x4cu);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBXCLIP, 0x003f0000u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBYCLIP, 0x001f0000u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBSTRIDE, 64u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_TA_START, 1u);
    for (uint32_t i = 0u; i < next; i++)
        m.bus.write32(m.bus.ctx, MBX_BASE + REG_3D_FIFO, stream[i]);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_ACK, 0x10u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
    CHECK(test_gpu_read32(
              &m, target + (20u * 64u + 10u) * 4u) == 0xff0000ffu &&
          test_gpu_read32(
              &m, target + (30u * 64u + 20u) * 4u) == 0xff00ff00u,
          "small-stride TA scene did not use its framebuffer stride");
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0x4cu &&
          m.mbx_telemetry.candidates_3d == 2u &&
          m.mbx_telemetry.completed_3d == 2u &&
          m.mbx_telemetry.rejected_3d == 0u &&
          m.mbx_telemetry.pixels_3d == 32u &&
          m.mbx_telemetry.accepted_3d_history[1].kind ==
              S5L_MBX_3D_ACCEPT_TA_STREAM,
          "small-stride TA scene did not complete with exact telemetry");

    /* A malformed second draw must reject the complete scene without leaking
     * the earlier valid draw into guest RAM. This is the critical difference
     * between a diagnostic approximation and a transactional renderer. */
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_ACK, 0x4cu);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBXCLIP, 0x013f0000u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBYCLIP, 0x01df0000u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBSTRIDE, 320u);
    for (uint32_t y = 20u; y < 23u; y++)
        for (uint32_t x = 10u; x < 14u; x++)
            test_gpu_write32(&m,
                target + y * TARGET_STRIDE + x * 4u, 0xff102030u);
    stream[49] ^= 1u; /* second draw's f0020004 control */
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_TA_START, 1u);
    for (uint32_t i = 0u; i < next; i++)
        m.bus.write32(m.bus.ctx, MBX_BASE + REG_3D_FIFO, stream[i]);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_ACK, 0x10u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
    CHECK(test_gpu_read32(
              &m, target + 20u * TARGET_STRIDE + 10u * 4u) == 0xff102030u,
          "rejected TA scene committed its earlier valid draw");
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u &&
          m.mbx_telemetry.candidates_3d == 3u &&
          m.mbx_telemetry.completed_3d == 2u &&
          m.mbx_telemetry.rejected_3d == 1u,
          "rejected TA scene raised completion or corrupted telemetry");

    s5l8900_free(&m);
}

static void test_ta_stream_orthographic_affine_texture(void) {
    enum {
        WIDTH = 64u,
        HEIGHT = 32u,
        TARGET_BYTES = WIDTH * HEIGHT * 4u,
        SOURCE_STRIDE = 64u,
        SOURCE_HEIGHT = 128u,
    };
    const uint32_t table0 = 0x08002000u;
    const uint32_t table2 = 0x08003000u;
    const uint32_t object = 0x00100000u;
    const uint32_t region = 0x00040000u;
    const uint32_t source = 0x00820000u;
    const uint32_t target = 0x00840000u;
    uint32_t stream[64] = {0x10000010u};
    uint32_t next = test_ta_global_transform(
        stream, 1u, WIDTH, HEIGHT, -10.0f, -20.0f);
    const float x[4] = {20.0f, 16.0f, 20.0f, 16.0f};
    const float y[4] = {34.0f, 34.0f, 30.0f, 30.0f};
    const float u[4] = {4.0f, 4.0f, 0.0f, 0.0f};
    const float v[4] = {0.0f, 4.0f, 0.0f, 4.0f};
    uint32_t source_word = 0x8e040000u |
        ((source >> 7) & 0x0003ffffu);
    next = test_ta_textured_draw(
        stream, next, 0xa1418000u, source_word, 0xd6087610u,
        x, y, u, v, 0xffffffffu);
    stream[next++] = 0xf0000000u;

    s5l8900_t m;
    CHECK(s5l8900_init(&m, RAM_BASE, RAM_SIZE),
          "affine TA machine init failed");
    if (!m.ram) return;
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_GART0, table0);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_GART2, table2);
    test_map_gpu_page(&m, table0, object, 0x08010000u);
    for (uint32_t page = 0u; page < SOURCE_STRIDE * SOURCE_HEIGHT;
         page += 0x1000u)
        test_map_gpu_page(&m, table2, source + page, 0x08030000u + page);
    for (uint32_t page = 0u; page < TARGET_BYTES; page += 0x1000u)
        test_map_gpu_page(&m, table2, target + page, 0x08040000u + page);
    for (uint32_t row = 0u; row < SOURCE_HEIGHT; row++)
        for (uint32_t column = 0u; column < SOURCE_STRIDE / 4u; column++)
            test_gpu_write32(&m, source + row * SOURCE_STRIDE + column * 4u,
                             test_sprite_source_pixel(column, row));

    m.bus.write32(m.bus.ctx, MBX_BASE + REG_TA_DB, object);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_TA_REGION, region);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RGNBASE, region);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_OBJBASE, object);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_PIXSAMP, 0x00020007u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBCTL, 6u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBXCLIP, 0x003f0000u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBYCLIP, 0x001f0000u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBSTART, target);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBSTRIDE, WIDTH);
    test_ta_run_stream(&m, stream, next);

    struct test_affine_transform transform = {
        10.0f, 10.0f, 0.0f, 4.0f, -4.0f, 0.0f, 16.0f,
    };
    uint32_t mismatches = 0u;
    for (uint32_t py = 10u; py < 14u; py++) {
        for (uint32_t px = 6u; px < 10u; px++) {
            float uf, vf;
            struct test_bilinear_axis sx, sy;
            bool covered = test_affine_pixel(
                &transform, px, py, &uf, &vf);
            bool sampled = covered &&
                test_bilinear_coordinate(uf * 4.0f, 16u, &sx) &&
                test_bilinear_coordinate(vf * 4.0f, SOURCE_HEIGHT, &sy);
            uint32_t expected = sampled
                ? test_bilinear_sprite_pixel(&sx, &sy) : 0u;
            mismatches += test_gpu_read32(
                &m, target + (py * WIDTH + px) * 4u) != expected;
        }
    }
    CHECK(mismatches == 0u,
          "%u orthographic affine TA pixels mismatched", mismatches);
    CHECK(test_gpu_read32(&m, target + (10u * WIDTH + 5u) * 4u) == 0u &&
          test_gpu_read32(&m, target + (10u * WIDTH + 10u) * 4u) == 0u &&
          test_gpu_read32(&m, target + (14u * WIDTH + 6u) * 4u) == 0u,
          "orthographic affine TA draw changed an outside guard");
    CHECK(m.mbx_telemetry.candidates_3d == 1u &&
          m.mbx_telemetry.completed_3d == 1u &&
          m.mbx_telemetry.rejected_3d == 0u &&
          m.mbx_telemetry.pixels_3d == 16u &&
          m.mbx_telemetry.accepted_3d_history[0].kind ==
              S5L_MBX_3D_ACCEPT_TA_STREAM,
          "orthographic affine TA telemetry is not exact");

    /* A transform that no longer encodes 2/height is not a second affine
     * family. The earlier valid draw must remain untouched on rejection. */
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_ACK, 0x4cu);
    uint32_t marker_address = target + (10u * WIDTH + 6u) * 4u;
    test_gpu_write32(&m, marker_address, 0xff102030u);
    stream[7] ^= 1u;
    test_ta_run_stream(&m, stream, next);
    CHECK(test_gpu_read32(&m, marker_address) == 0xff102030u,
          "bad orthographic transform committed a TA scene");
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u &&
          m.mbx_telemetry.candidates_3d == 2u &&
          m.mbx_telemetry.completed_3d == 1u &&
          m.mbx_telemetry.rejected_3d == 1u,
          "bad orthographic transform completed or corrupted telemetry");
    s5l8900_free(&m);
}

static void test_ta_stream_immediate_state_reuse_and_alias_bounds(void) {
    enum {
        WIDTH = 64u,
        HEIGHT = 32u,
        TARGET_BYTES = WIDTH * HEIGHT * 4u,
        SOURCE_STRIDE = 64u,
    };
    const uint32_t table0 = 0x08002000u;
    const uint32_t table2 = 0x08003000u;
    const uint32_t object = 0x00100000u;
    const uint32_t region = 0x00040000u;
    const uint32_t target = 0x00830000u;
    /* The encoded 16x32 allocation extends 0x400 bytes into target, exactly
     * like the captured temporary-surface case. The measured rows used below
     * stop 0x40 bytes before target, so sampled bytes do not alias it. */
    const uint32_t source = target - 0x400u;
    uint32_t stream[160] = {0x10000010u};
    uint32_t next = test_ta_solid_draw(
        stream, 1u, 20.0f, 20.0f, 22.0f, 22.0f, 0xff0000ffu);
    next = test_ta_solid_continuation(
        stream, next, 22.0f, 20.0f, 24.0f, 22.0f, 0xff00ff00u);
    static const uint32_t state[14] = {
        0xa01b001cu, 0u, 0u, 0u, 0u, 0u, 0u,
        0u, 0u, 0xa01d001du, 0u, 0u, 0u, 0x3f800000u,
    };
    memcpy(&stream[next], state, sizeof state);
    next += 14u;
    const float fractional_15 = test_float_value(0x416ffffeu);
    const float x0[4] = {9.0f, 9.0f, 1.0f, 1.0f};
    const float y0[4] = {1.0f, 16.0f, 1.0f, 16.0f};
    const float u0[4] = {8.0f, 8.0f, 0.0f, 0.0f};
    const float v0[4] = {0.0f, fractional_15, 0.0f, fractional_15};
    uint32_t source_word = 0x0e040000u |
        ((source >> 7) & 0x0003ffffu);
    uint32_t textured_start = next;
    next = test_ta_textured_draw(
        stream, next, 0xa1218000u, source_word, 0xd6087610u,
        x0, y0, u0, v0, 0xffffffffu);
    const float x1[4] = {11.0f, 11.0f, 9.0f, 9.0f};
    const float y1[4] = {1.0f, 3.0f, 1.0f, 3.0f};
    const float u1[4] = {10.0f, 10.0f, 8.0f, 8.0f};
    const float v1[4] = {0.0f, 2.0f, 0.0f, 2.0f};
    uint32_t textured_continuation = next;
    next = test_ta_textured_vertices(
        stream, next, x1, y1, u1, v1, 0xffffffffu);
    stream[next++] = 0xf0000000u;

    s5l8900_t m;
    CHECK(s5l8900_init(&m, RAM_BASE, RAM_SIZE),
          "state-reuse TA machine init failed");
    if (!m.ram) return;
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_GART0, table0);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_GART2, table2);
    test_map_gpu_page(&m, table0, object, 0x08010000u);
    test_map_gpu_page(&m, table2, source & ~0xfffu, 0x08030000u);
    test_map_gpu_page(&m, table2, target, 0x08031000u);
    test_map_gpu_page(&m, table2, target + 0x1000u, 0x08032000u);
    for (uint32_t row = 0u; row < 15u; row++)
        for (uint32_t column = 0u; column < SOURCE_STRIDE / 4u; column++)
            test_gpu_write32(&m, source + row * SOURCE_STRIDE + column * 4u,
                             test_sprite_source_pixel(column, row));

    m.bus.write32(m.bus.ctx, MBX_BASE + REG_TA_DB, object);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_TA_REGION, region);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RGNBASE, region);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_OBJBASE, object);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_PIXSAMP, 0x00020007u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBCTL, 6u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBXCLIP, 0x003f0000u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBYCLIP, 0x001f0000u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBSTART, target);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBSTRIDE, WIDTH);
    test_ta_run_stream(&m, stream, next);

    CHECK(test_gpu_read32(&m, target + (20u * WIDTH + 20u) * 4u) ==
              0xff0000ffu &&
          test_gpu_read32(&m, target + (20u * WIDTH + 22u) * 4u) ==
              0xff00ff00u,
          "solid immediate TA continuation did not preserve pipeline state");
    CHECK(test_gpu_read32(&m, target + (1u * WIDTH + 1u) * 4u) ==
              test_sprite_source_pixel(0u, 0u) &&
          test_gpu_read32(&m, target + (15u * WIDTH + 8u) * 4u) ==
              test_sprite_source_pixel(7u, 14u) &&
          test_gpu_read32(&m, target + (1u * WIDTH + 9u) * 4u) ==
              test_sprite_source_pixel(8u, 0u) &&
          test_gpu_read32(&m, target + (2u * WIDTH + 10u) * 4u) ==
              test_sprite_source_pixel(9u, 1u),
          "fractional-edge texture or immediate TA continuation copied wrong pixels");
    CHECK(m.mbx_telemetry.candidates_3d == 1u &&
          m.mbx_telemetry.completed_3d == 1u &&
          m.mbx_telemetry.rejected_3d == 0u &&
          m.mbx_telemetry.pixels_3d == 132u,
          "state-reuse TA telemetry is not exact");

    /* A vertex-only continuation may reuse state, but may not switch from the
     * immediately preceding texture pipeline to a solid vertex format. */
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_ACK, 0x4cu);
    uint32_t marker_address = target + (20u * WIDTH + 20u) * 4u;
    test_gpu_write32(&m, marker_address, 0xff102030u);
    stream[textured_continuation + 1u] = 0xf0020004u;
    test_ta_run_stream(&m, stream, next);
    CHECK(test_gpu_read32(&m, marker_address) == 0xff102030u,
          "cross-pipeline TA continuation committed an earlier draw");
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u &&
          m.mbx_telemetry.candidates_3d == 2u &&
          m.mbx_telemetry.completed_3d == 1u &&
          m.mbx_telemetry.rejected_3d == 1u,
          "cross-pipeline TA continuation completed or corrupted telemetry");

    /* Unused allocation padding may overlap FBSTART, but sampled rows may not.
     * Pointing the same measured draw directly at target must remain atomic. */
    stream[textured_continuation + 1u] = 0xf0020044u;
    stream[textured_start + 2u] = 0x0e040000u |
        ((target >> 7) & 0x0003ffffu);
    test_gpu_write32(&m, marker_address, 0xff405060u);
    test_ta_run_stream(&m, stream, next);
    CHECK(test_gpu_read32(&m, marker_address) == 0xff405060u,
          "sampled TA texture alias committed an earlier draw");
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u &&
          m.mbx_telemetry.candidates_3d == 3u &&
          m.mbx_telemetry.completed_3d == 1u &&
          m.mbx_telemetry.rejected_3d == 2u,
          "sampled TA texture alias completed or corrupted telemetry");
    s5l8900_free(&m);
}

/* Wallpaper Preview's rejected r447 submit contains five simple-copy strips
 * with descriptor 0x94048280: format 0x48000 and a 0x280-byte pitch. The
 * shipped QuartzCore binary maps both '1555' and little-endian '555L' to that
 * format while mapping '565L' to 0x50000. The producer's color-packing branch
 * independently fixes the bit order as A1:R5:G5:B5. Exercise the exact
 * descriptor, a 320-pixel source, non-contiguous source pages, channel/alpha
 * expansion, destination guards, and whole-batch rejection semantics.
 */
static void test_wallpaper_argb1555_simple_copy(void) {
    enum {
        SOURCE_STRIDE = 0x280u,
        TARGET_STRIDE = 0x500u,
        WIDTH = 320u,
        HEIGHT = 8u,
        LEFT = 3u,
        TOP = 68u,
    };
    const uint32_t table2 = 0x08003000u;
    const uint32_t source = 0x00a96000u;
    const uint32_t target = 0x00998000u;
    const uint32_t source_pa0 = 0x08010000u;
    const uint32_t source_pa1 = 0x08013000u;
    const uint32_t target_pa = 0x08080000u;
    uint32_t packet[16] = {
        0xa0060500u, target, 0x94048280u, source,
        0x30000000u, 0x60800200u, 0x8000ccccu, 0xffffffffu,
        (LEFT << 16) | TOP, (WIDTH << 16) | (TOP + HEIGHT),
        0x70000000u, 0x70000000u, 0x70000000u, 0x70000000u,
        0x70000000u, 0x70000000u,
    };

    s5l8900_t m;
    CHECK(s5l8900_init(&m, RAM_BASE, RAM_SIZE),
          "ARGB1555 wallpaper machine init failed");
    if (!m.ram) return;
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_GART2, table2);
    test_map_gpu_page(&m, table2, source, source_pa0);
    test_map_gpu_page(&m, table2, source + 0x1000u, source_pa1);
    for (uint32_t page = 0; page < TARGET_STRIDE * 480u; page += 0x1000u)
        test_map_gpu_page(&m, table2, target + page, target_pa + page);

    for (uint32_t y = 0; y < HEIGHT; y++) {
        for (uint32_t x = 0; x < WIDTH; x++) {
            uint16_t pixel = (uint16_t)(
                (((x + y * 3u) & 0x1fu) << 10) |
                (((x * 5u + y) & 0x1fu) << 5) |
                ((x * 7u + y * 11u) & 0x1fu) |
                (((x ^ y) & 1u) ? 0x8000u : 0u));
            test_gpu_write16(&m, source + y * SOURCE_STRIDE + x * 2u,
                             pixel);
        }
    }
    static const uint16_t vectors[] = {
        0x0000u, 0x001fu, 0x03e0u, 0x7c00u,
        0x8000u, 0xffffu, 0x4294u,
    };
    static const uint32_t expected_vectors[] = {
        0x00000000u, 0x000000ffu, 0x0000ff00u, 0x00ff0000u,
        0xff000000u, 0xffffffffu, 0x0084a5a5u,
    };
    for (uint32_t i = 0; i < sizeof vectors / sizeof vectors[0]; i++)
        test_gpu_write16(&m, source + i * 2u, vectors[i]);

    const uint32_t before = target + (TOP - 1u) * TARGET_STRIDE + LEFT * 4u;
    const uint32_t left_guard = target + TOP * TARGET_STRIDE + (LEFT - 1u) * 4u;
    const uint32_t after = target + (TOP + HEIGHT) * TARGET_STRIDE + LEFT * 4u;
    test_gpu_write32(&m, before, 0x11223344u);
    test_gpu_write32(&m, left_guard, 0x55667788u);
    test_gpu_write32(&m, after, 0x99aabbccu);
    for (uint32_t i = 0; i < sizeof vectors / sizeof vectors[0]; i++)
        test_gpu_write32(&m, target + TOP * TARGET_STRIDE +
                         (LEFT + i) * 4u, 0xdeadbeefu);

    write_packet(&m, RING + 0xc470u, packet, 16u);
    const char *why = "unset";
    uint64_t reason_hash = UINT64_MAX;
    CHECK(s5l_mbx_probe_2d_submit(&m.mbx, &m.bus,
                                  RING + 0xc470u, 1u,
                                  &reason_hash, &why) &&
          reason_hash == 0u,
          "captured ARGB1555 descriptor rejected: %s (%016llx)",
          why, (unsigned long long)reason_hash);
    m.bus.write32(m.bus.ctx, MBX_BASE + RING, 0xf0000000u);

    uint32_t mismatches = 0u;
    for (uint32_t y = 0; y < HEIGHT; y++) {
        for (uint32_t x = LEFT; x < WIDTH; x++) {
            uint32_t raw_pa = test_gpu_pa(
                &m, source + y * SOURCE_STRIDE + (x - LEFT) * 2u);
            uint16_t raw = m.bus.read16(m.bus.ctx, raw_pa);
            uint32_t actual = test_gpu_read32(
                &m, target + (TOP + y) * TARGET_STRIDE + x * 4u);
            mismatches += actual != test_argb1555_to_bgra8(raw);
        }
    }
    CHECK(mismatches == 0u,
          "ARGB1555 wallpaper strip mismatched %u converted pixels",
          mismatches);
    for (uint32_t i = 0; i < sizeof vectors / sizeof vectors[0]; i++)
        CHECK(test_gpu_read32(&m, target + TOP * TARGET_STRIDE +
                              (LEFT + i) * 4u) == expected_vectors[i],
              "ARGB1555 vector %u decoded to %08x, expected %08x", i,
              test_gpu_read32(&m, target + TOP * TARGET_STRIDE +
                              (LEFT + i) * 4u), expected_vectors[i]);
    CHECK(test_gpu_read32(&m, before) == 0x11223344u &&
          test_gpu_read32(&m, left_guard) == 0x55667788u &&
          test_gpu_read32(&m, after) == 0x99aabbccu,
          "ARGB1555 copy changed a pixel outside its destination rectangle");
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0x400u,
          "ARGB1555 copy did not raise exactly 2D_SYNC");
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_ACK, 0x400u);

    /* A late source hole must be found before any earlier row is committed. */
    uint32_t first_destination =
        target + TOP * TARGET_STRIDE + LEFT * 4u;
    test_gpu_write32(&m, first_destination, 0x13579bdfu);
    m.bus.write32(m.bus.ctx,
        table2 + (((source + 0x1000u) >> 12 & 0x3ffu) * 4u), 0u);
    write_packet(&m, RING + 0x100u, packet, 16u);
    m.bus.write32(m.bus.ctx, MBX_BASE + RING, 0xf0000000u);
    CHECK(test_gpu_read32(&m, first_destination) == 0x13579bdfu,
          "late ARGB1555 source hole partially committed an earlier row");
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "late ARGB1555 source hole raised completion");
    test_map_gpu_page(&m, table2, source + 0x1000u, source_pa1);

    /* 0x50000 is the separately identified 565L format. It is not decoded by
     * this change and must not be accepted merely because it is 16-bit. */
    uint32_t unsupported[16];
    memcpy(unsupported, packet, sizeof unsupported);
    unsupported[2] = 0x94050280u;
    test_gpu_write32(&m, first_destination, 0x2468ace0u);
    write_packet(&m, RING + 0x140u, unsupported, 16u);
    m.bus.write32(m.bus.ctx, MBX_BASE + RING, 0xf0000000u);
    CHECK(test_gpu_read32(&m, first_destination) == 0x2468ace0u,
          "unsupported RGB565 descriptor changed the destination");
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "unsupported RGB565 descriptor raised completion");

    /* A valid 1555 command followed by that unsupported format is one atomic
     * submit. The first command may not leak through before the second fails. */
    test_gpu_write32(&m, first_destination, 0xa5a5a5a5u);
    write_packet(&m, RING + 0x200u, packet, 16u);
    write_packet(&m, RING + 0x240u, unsupported, 16u);
    m.bus.write32(m.bus.ctx, MBX_BASE + RING, 0xf0000000u);
    CHECK(test_gpu_read32(&m, first_destination) == 0xa5a5a5a5u,
          "rejected mixed-format batch committed its valid first command");
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "rejected mixed-format batch raised completion");

    s5l8900_free(&m);
}

static void test_full_lower_surface_opaque_fill(void) {
    enum { STRIDE = 0x500u, WIDTH = 320u, HEIGHT = 480u, TOP = 20u };
    const uint32_t table2 = 0x08003000u;
    const uint32_t target = 0x00897000u;
    const uint32_t target_pa = 0x08040000u;
    uint32_t packet[16] = {
        0xa0060500u, target, 0x94060500u, 0x00000000u,
        0x30000000u, 0x60800200u, 0x8000f0f0u, 0xff000000u,
        0x00000014u, 0x014001e0u, 0x70000000u, 0x70000000u,
        0x70000000u, 0x70000000u, 0x70000000u, 0x70000000u,
    };

    s5l8900_t m;
    CHECK(s5l8900_init(&m, RAM_BASE, RAM_SIZE), "solid-fill machine init failed");
    if (!m.ram) return;
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_GART2, table2);
    for (uint32_t page = 0; page < STRIDE * HEIGHT; page += 0x1000u)
        test_map_gpu_page(&m, table2, target + page, target_pa + page);

    test_gpu_write32(&m, target + (TOP - 1u) * STRIDE, 0xff112233u);
    test_gpu_write32(&m, target + (TOP - 1u) * STRIDE +
                     (WIDTH - 1u) * 4u, 0xff445566u);
    test_gpu_write32(&m, target + TOP * STRIDE, 0xffabcdefu);
    test_gpu_write32(&m, target + (HEIGHT - 1u) * STRIDE +
                     (WIDTH - 1u) * 4u, 0xff123456u);

    write_packet(&m, RING, packet, 16u);
    m.bus.write32(m.bus.ctx, MBX_BASE + RING, 0xf0000000u);

    uint32_t mismatches = 0u;
    for (uint32_t y = TOP; y < HEIGHT; y++)
        for (uint32_t x = 0; x < WIDTH; x++)
            mismatches += test_gpu_read32(&m,
                target + y * STRIDE + x * 4u) != 0xff000000u;
    CHECK(mismatches == 0u,
          "captured lower-screen fill left %u non-black pixels", mismatches);
    CHECK(test_gpu_read32(&m, target + (TOP - 1u) * STRIDE) == 0xff112233u &&
          test_gpu_read32(&m, target + (TOP - 1u) * STRIDE +
                          (WIDTH - 1u) * 4u) == 0xff445566u,
          "lower-screen fill changed the status-bar row");
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0x400u,
          "lower-screen fill did not raise exactly 2D completion");

    m.bus.write32(m.bus.ctx, MBX_BASE + REG_ACK, 0x400u);
    test_gpu_write32(&m, target + TOP * STRIDE, 0x89abcdefu);
    packet[7] = 0x7f000000u;
    write_packet(&m, RING + 0x40u, packet, 16u);
    m.bus.write32(m.bus.ctx, MBX_BASE + RING, 0xf0000000u);
    CHECK(test_gpu_read32(&m, target + TOP * STRIDE) == 0x7f000000u,
          "translucent raw fill stored the wrong BGRA8 value");
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0x400u,
          "translucent raw fill did not raise exactly 2D completion");

    m.bus.write32(m.bus.ctx, MBX_BASE + REG_ACK, 0x400u);
    test_gpu_write32(&m, target + TOP * STRIDE, 0x89abcdefu);
    packet[6] ^= 1u;
    write_packet(&m, RING + 0x80u, packet, 16u);
    m.bus.write32(m.bus.ctx, MBX_BASE + RING, 0xf0000000u);
    CHECK(test_gpu_read32(&m, target + TOP * STRIDE) == 0x89abcdefu,
          "unknown fill mode changed the destination");
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "unknown fill mode raised completion");

    s5l8900_free(&m);
}

static void test_safari_tabs_opaque_fill_batch(void) {
    enum { STRIDE = 0x500u, HEIGHT = 480u };
    const uint32_t table2 = 0x08003000u;
    const uint32_t target = 0x00998000u;
    const uint32_t target_pa = 0x08040000u;
    uint32_t white[16] = {
        0xa0060500u, target, 0x94060500u, 0x00000000u,
        0x30000000u, 0x60800200u, 0x8000f0f0u, 0xffffffffu,
        0x00000148u, 0x014001b4u, 0x70000000u, 0x70000000u,
        0x70000000u, 0x70000000u, 0x70000000u, 0x70000000u,
    };
    uint32_t gray[16];
    memcpy(gray, white, sizeof gray);
    gray[7] = 0xffe0e0e0u;
    gray[8] = 0x00000147u;
    gray[9] = 0x01400148u;

    s5l8900_t m;
    CHECK(s5l8900_init(&m, RAM_BASE, RAM_SIZE),
          "Safari-tabs fill machine init failed");
    if (!m.ram) return;
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_GART2, table2);
    for (uint32_t page = 0; page < STRIDE * HEIGHT; page += 0x1000u)
        test_map_gpu_page(&m, table2, target + page, target_pa + page);

    test_gpu_write32(&m, target + 326u * STRIDE, 0xff112233u);
    test_gpu_write32(&m, target + 327u * STRIDE, 0xff445566u);
    test_gpu_write32(&m, target + 328u * STRIDE, 0xff778899u);
    test_gpu_write32(&m, target + 435u * STRIDE + 319u * 4u,
                     0xffaabbccu);
    test_gpu_write32(&m, target + 436u * STRIDE, 0xffabcdefu);

    write_packet(&m, RING + 0x23a0u, white, 16u);
    write_packet(&m, RING + 0x23e0u, gray, 16u);
    const char *probe_why = "unset";
    uint64_t probe_hash = UINT64_MAX;
    CHECK(s5l_mbx_probe_2d_submit(&m.mbx, &m.bus,
                                  RING + 0x23a0u, 2u,
                                  &probe_hash, &probe_why) &&
          probe_hash == 0u,
          "captured Safari-tabs fills rejected: %s (%016llx)",
          probe_why, (unsigned long long)probe_hash);
    CHECK(test_gpu_read32(&m, target + 327u * STRIDE) == 0xff445566u &&
          test_gpu_read32(&m, target + 328u * STRIDE) == 0xff778899u,
          "read-only Safari-tabs probe changed destination pixels");

    m.bus.write32(m.bus.ctx, MBX_BASE + RING, 0xf0000000u);
    CHECK(test_gpu_read32(&m, target + 326u * STRIDE) == 0xff112233u &&
          test_gpu_read32(&m, target + 327u * STRIDE) == 0xffe0e0e0u &&
          test_gpu_read32(&m, target + 328u * STRIDE) == 0xffffffffu &&
          test_gpu_read32(&m, target + 435u * STRIDE + 319u * 4u) ==
              0xffffffffu &&
          test_gpu_read32(&m, target + 436u * STRIDE) == 0xffabcdefu,
          "Safari-tabs opaque fill colours or bounds are wrong");
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0x400u,
          "Safari-tabs fill batch did not raise exactly 2D completion");
    CHECK(m.mbx_telemetry.candidates_2d == 2u &&
          m.mbx_telemetry.completed_2d == 2u &&
          m.mbx_telemetry.rejected_2d == 0u &&
          m.mbx_telemetry.bytes_2d ==
              (uint64_t)(320u * 108u + 320u) * 4u,
          "Safari-tabs fill ledger=%llu/%llu/%llu/%llu",
          (unsigned long long)m.mbx_telemetry.candidates_2d,
          (unsigned long long)m.mbx_telemetry.completed_2d,
          (unsigned long long)m.mbx_telemetry.rejected_2d,
          (unsigned long long)m.mbx_telemetry.bytes_2d);

    s5l8900_free(&m);
}

static void test_safari_tabs_bounded_source_stride_copy(void) {
    enum {
        TARGET_STRIDE = 0x500u,
        SOURCE_STRIDE = 0x420u,
        WIDTH = 258u,
        HEIGHT = 43u,
    };
    const uint32_t table2 = 0x08003000u;
    const uint32_t table3 = 0x08002000u;
    const uint32_t target = 0x00998000u;
    const uint32_t target_pa = 0x08040000u;
    const uint32_t source = 0x00c01080u;
    const uint32_t source_page = 0x00c01000u;
    const uint32_t source_pa = 0x08100000u;
    uint32_t packet[16] = {
        0xa0060500u, target, 0x94060420u, source,
        0x30000000u, 0x60800200u, 0x8000ccccu, 0xffffffffu,
        0x0034011cu, 0x01360147u, 0x70000000u, 0x70000000u,
        0x70000000u, 0x70000000u, 0x70000000u, 0x70000000u,
    };

    s5l8900_t m;
    CHECK(s5l8900_init(&m, RAM_BASE, RAM_SIZE),
          "Safari-tabs copy machine init failed");
    if (!m.ram) return;
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_GART2, table2);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_GART3, table3);
    for (uint32_t page = 0; page < TARGET_STRIDE * 480u; page += 0x1000u)
        test_map_gpu_page(&m, table2, target + page, target_pa + page);
    for (uint32_t page = 0;
         page < 0x80u + SOURCE_STRIDE * HEIGHT;
         page += 0x1000u)
        test_map_gpu_page(&m, table3, source_page + page,
                          source_pa + page);

    const uint32_t top_left = 0xff102030u;
    const uint32_t top_right = 0xff405060u;
    const uint32_t second_row = 0xff708090u;
    const uint32_t bottom_right = 0xffa0b0c0u;
    test_gpu_write32(&m, source, top_left);
    test_gpu_write32(&m, source + (WIDTH - 1u) * 4u, top_right);
    test_gpu_write32(&m, source + SOURCE_STRIDE, second_row);
    test_gpu_write32(&m, source + (HEIGHT - 1u) * SOURCE_STRIDE +
                     (WIDTH - 1u) * 4u, bottom_right);
    test_gpu_write32(&m, source + TARGET_STRIDE, 0xffdead00u);

    const uint32_t dst = target + 284u * TARGET_STRIDE + 52u * 4u;
    test_gpu_write32(&m, dst - 4u, 0xff112233u);
    test_gpu_write32(&m, dst + WIDTH * 4u, 0xff445566u);
    write_packet(&m, RING + 0x2460u, packet, 16u);
    m.bus.write32(m.bus.ctx, MBX_BASE + RING, 0xf0000000u);

    CHECK(test_gpu_read32(&m, dst) == top_left &&
          test_gpu_read32(&m, dst + (WIDTH - 1u) * 4u) == top_right &&
          test_gpu_read32(&m, dst + TARGET_STRIDE) == second_row &&
          test_gpu_read32(&m, dst + (HEIGHT - 1u) * TARGET_STRIDE +
                          (WIDTH - 1u) * 4u) == bottom_right,
          "Safari-tabs copy did not use its encoded 0x420-byte source stride");
    CHECK(test_gpu_read32(&m, dst - 4u) == 0xff112233u &&
          test_gpu_read32(&m, dst + WIDTH * 4u) == 0xff445566u,
          "Safari-tabs copy changed pixels outside its destination rectangle");
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0x400u &&
          m.mbx_telemetry.candidates_2d == 1u &&
          m.mbx_telemetry.completed_2d == 1u &&
          m.mbx_telemetry.rejected_2d == 0u &&
          m.mbx_telemetry.bytes_2d == (uint64_t)WIDTH * HEIGHT * 4u,
          "Safari-tabs copy completion ledger=%llu/%llu/%llu/%llu",
          (unsigned long long)m.mbx_telemetry.candidates_2d,
          (unsigned long long)m.mbx_telemetry.completed_2d,
          (unsigned long long)m.mbx_telemetry.rejected_2d,
          (unsigned long long)m.mbx_telemetry.bytes_2d);

    m.bus.write32(m.bus.ctx, MBX_BASE + REG_ACK, 0x400u);
    test_gpu_write32(&m, dst, 0xffabcdefu);
    packet[9] = (317u << 16) | 327u;
    write_packet(&m, RING + 0x24a0u, packet, 16u);
    m.bus.write32(m.bus.ctx, MBX_BASE + RING, 0xf0000000u);
    CHECK(test_gpu_read32(&m, dst) == 0xffabcdefu &&
          m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "source-stride overflow changed pixels or raised completion");

    s5l8900_free(&m);
}

static void test_split_lower_surface_black_fill(void) {
    enum { STRIDE = 0x500u, WIDTH = 320u, HEIGHT = 480u, TOP = 20u };
    const uint32_t table2 = 0x08003000u;
    const uint32_t target = 0x00998000u;
    const uint32_t target_pa = 0x08040000u;
    uint32_t first[16] = {
        0xa0060500u, target, 0x94060500u, 0x00000000u,
        0x30000000u, 0x60800200u, 0x8000f0f0u, 0xff000000u,
        0x00000014u, 0x01400185u, 0x70000000u, 0x70000000u,
        0x70000000u, 0x70000000u, 0x70000000u, 0x70000000u,
    };
    uint32_t second[16];
    memcpy(second, first, sizeof second);
    second[8] = 0x00000185u;
    second[9] = 0x014001e0u;

    s5l8900_t m;
    CHECK(s5l8900_init(&m, RAM_BASE, RAM_SIZE),
          "split-fill machine init failed");
    if (!m.ram) return;
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_GART2, table2);
    for (uint32_t page = 0; page < STRIDE * HEIGHT; page += 0x1000u)
        test_map_gpu_page(&m, table2, target + page, target_pa + page);

    test_gpu_write32(&m, target + (TOP - 1u) * STRIDE, 0xff112233u);
    write_packet(&m, RING + 0x40u, first, 16u);
    write_packet(&m, RING + 0x80u, second, 16u);
    m.bus.write32(m.bus.ctx, MBX_BASE + RING, 0xf0000000u);

    uint32_t mismatches = 0u;
    for (uint32_t y = TOP; y < HEIGHT; y++)
        for (uint32_t x = 0; x < WIDTH; x++)
            mismatches += test_gpu_read32(
                &m, target + y * STRIDE + x * 4u) != 0xff000000u;
    CHECK(mismatches == 0u,
          "captured split fill left %u non-black pixels", mismatches);
    CHECK(test_gpu_read32(&m, target + (TOP - 1u) * STRIDE) == 0xff112233u,
          "split fill changed the status-bar row");
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0x400u,
          "split fill batch did not raise exactly 2D completion");
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_ACK, 0x400u);

    uint32_t upper = target + TOP * STRIDE;
    uint32_t lower = target + 389u * STRIDE;
    test_gpu_write32(&m, upper, 0xffabcdefu);
    test_gpu_write32(&m, lower, 0xff123456u);
    second[9] ^= 1u;
    write_packet(&m, RING + 0xc0u, first, 16u);
    write_packet(&m, RING + 0x100u, second, 16u);
    m.bus.write32(m.bus.ctx, MBX_BASE + RING, 0xf0000000u);
    CHECK(test_gpu_read32(&m, upper) == 0xffabcdefu &&
          test_gpu_read32(&m, lower) == 0xff123456u,
          "rejected split fill committed part of the batch");
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "rejected split fill raised completion");

    s5l8900_free(&m);
}

static uint32_t test_settings_transition_source1(uint32_t x, uint32_t y) {
    return ((y & 0xffu) << 16) | ((x & 0xffu) << 8) |
           ((x + y) & 0xffu);
}

static uint32_t test_settings_transition_source2(uint32_t x) {
    return 0x80000000u | ((x & 0xffu) << 16) |
           (((x * 3u) & 0xffu) << 8) | ((x * 7u) & 0xffu);
}

static void test_settings_transition_transparent_clear_batch(void) {
    enum {
        STRIDE = 0x500u,
        WIDTH = 320u,
        HEIGHT = 480u,
        CLEAR_HEIGHT = 460u,
        FIRST_COPY_HEIGHT = 356u,
        SECOND_COPY_SOURCE_Y = 59u,
    };
    const uint32_t table2 = 0x08003000u;
    const uint32_t table3 = 0x08002000u;
    const uint32_t target = 0x00c69000u;
    const uint32_t target_pa = 0x08040000u;
    const uint32_t source1 = 0x00bf9080u;
    const uint32_t source1_page = 0x00bf9000u;
    const uint32_t source1_pa = 0x08100000u;
    const uint32_t source2 = 0x00af5080u;
    const uint32_t source2_page = 0x00af5000u;
    const uint32_t source2_pa = 0x08180000u;
    const uint32_t original = 0x7f556677u;
    uint32_t clear[16] = {
        0xa0060500u, target, 0x94060500u, 0x00000000u,
        0x30000000u, 0x60800200u, 0x8000f0f0u, 0x00000000u,
        0x00000000u, 0x014001ccu, 0x70000000u, 0x70000000u,
        0x70000000u, 0x70000000u, 0x70000000u, 0x70000000u,
    };
    uint32_t first_copy[18] = {
        0xa0060500u, target, 0x94060500u, source1,
        0x30000000u, 0x20000004u, 0x0d5ff000u, 0x60800200u,
        0x8002ccccu, 0xffffffffu, 0x0000003cu, 0x014001a0u,
        0x70000000u, 0x70000000u, 0x70000000u, 0x70000000u,
        0x70000000u, 0x70000000u,
    };
    uint32_t second_copy[18] = {
        0xa0060500u, target, 0x94060500u, source2,
        0x3000003bu, 0x20000004u, 0x0d5ff000u, 0x60800200u,
        0x8002ccccu, 0xffffffffu, 0x0000003bu, 0x0140003cu,
        0x70000000u, 0x70000000u, 0x70000000u, 0x70000000u,
        0x70000000u, 0x70000000u,
    };

    s5l8900_t m;
    CHECK(s5l8900_init(&m, RAM_BASE, RAM_SIZE),
          "Settings-transition machine init failed");
    if (!m.ram) return;
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_GART2, table2);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_GART3, table3);

    for (uint32_t page = 0u; page < STRIDE * HEIGHT; page += 0x1000u)
        test_map_gpu_page(&m, table3, target + page, target_pa + page);
    for (uint32_t page = 0u;
         page < 0x80u + STRIDE * FIRST_COPY_HEIGHT;
         page += 0x1000u) {
        uint32_t gpu = source1_page + page;
        uint32_t table = gpu < 0x00c00000u ? table2 : table3;
        test_map_gpu_page(&m, table, gpu, source1_pa + page);
    }
    for (uint32_t page = 0u;
         page < 0x80u + STRIDE * (SECOND_COPY_SOURCE_Y + 1u);
         page += 0x1000u)
        test_map_gpu_page(&m, table2, source2_page + page,
                          source2_pa + page);

    for (uint32_t y = 0u; y < HEIGHT; y++) {
        for (uint32_t x = 0u; x < WIDTH; x++)
            test_gpu_write32(&m, target + y * STRIDE + x * 4u, original);
    }
    for (uint32_t y = 0u; y < FIRST_COPY_HEIGHT; y++) {
        for (uint32_t x = 0u; x < WIDTH; x++)
            test_gpu_write32(&m, source1 + y * STRIDE + x * 4u,
                             test_settings_transition_source1(x, y));
    }
    for (uint32_t x = 0u; x < WIDTH; x++)
        test_gpu_write32(&m,
            source2 + SECOND_COPY_SOURCE_Y * STRIDE + x * 4u,
            test_settings_transition_source2(x));

    /* This is the exact three-command family retained when SpringBoard opens
     * Settings: a transparent raw clear followed by two opaque XRGB copies.
     * The first source crosses from GART root 2 into root 3 immediately before
     * the target mapping, so the fixture also pins that boundary. */
    write_packet(&m, RING + 0x79c0u, clear, 16u);
    write_packet(&m, RING + 0x7a00u, first_copy, 18u);
    write_packet(&m, RING + 0x7a48u, second_copy, 18u);
    const char *probe_why = "unset";
    uint64_t probe_hash = UINT64_MAX;
    CHECK(s5l_mbx_probe_2d_submit(&m.mbx, &m.bus,
                                  RING + 0x79c0u, 3u,
                                  &probe_hash, &probe_why) &&
          probe_hash == 0u,
          "captured Settings transition rejected: %s (%016llx)",
          probe_why, (unsigned long long)probe_hash);
    CHECK(test_gpu_read32(&m, target) == original &&
          m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "read-only Settings-transition probe changed machine state");

    m.bus.write32(m.bus.ctx, MBX_BASE + RING, 0xf0000000u);
    uint32_t mismatches = 0u;
    for (uint32_t y = 0u; y < HEIGHT; y++) {
        for (uint32_t x = 0u; x < WIDTH; x++) {
            uint32_t expected = original;
            if (y < CLEAR_HEIGHT) expected = 0u;
            if (y == 59u)
                expected = test_settings_transition_source2(x) |
                           0xff000000u;
            else if (y >= 60u && y < 416u)
                expected = test_settings_transition_source1(x, y - 60u) |
                           0xff000000u;
            mismatches += test_gpu_read32(
                &m, target + y * STRIDE + x * 4u) != expected;
        }
    }
    CHECK(mismatches == 0u,
          "captured Settings transition produced %u wrong pixels", mismatches);
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0x400u,
          "Settings-transition batch did not raise exactly 2D completion");
    CHECK(m.mbx_telemetry.candidates_2d == 3u &&
          m.mbx_telemetry.completed_2d == 3u &&
          m.mbx_telemetry.rejected_2d == 0u &&
          m.mbx_telemetry.bytes_2d == UINT64_C(1045760),
          "Settings-transition ledger=%llu/%llu/%llu/%llu",
          (unsigned long long)m.mbx_telemetry.candidates_2d,
          (unsigned long long)m.mbx_telemetry.completed_2d,
          (unsigned long long)m.mbx_telemetry.rejected_2d,
          (unsigned long long)m.mbx_telemetry.bytes_2d);

    m.bus.write32(m.bus.ctx, MBX_BASE + REG_ACK, 0x400u);
    test_gpu_write32(&m, target, original);
    second_copy[6] ^= 0x00100000u;
    write_packet(&m, RING + 0x7b00u, clear, 16u);
    write_packet(&m, RING + 0x7b40u, first_copy, 18u);
    write_packet(&m, RING + 0x7b88u, second_copy, 18u);
    m.bus.write32(m.bus.ctx, MBX_BASE + RING, 0xf0000000u);
    CHECK(test_gpu_read32(&m, target) == original,
          "rejected Settings transition leaked its transparent clear");
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "rejected Settings-transition batch raised completion");

    s5l8900_free(&m);
}

static void test_springboard_settings_black_fill_batch(void) {
    enum { STRIDE = 0x500u, WIDTH = 320u, HEIGHT = 480u };
    const uint32_t table2 = 0x08003000u;
    const uint32_t target = 0x00897000u;
    const uint32_t target_pa = 0x08040000u;
    static const uint16_t rects[6][4] = {
        { 3u, 32u, 317u, 107u },
        { 79u, 107u, 165u, 108u },
        { 3u, 120u, 317u, 195u },
        { 3u, 208u, 317u, 283u },
        { 3u, 296u, 241u, 371u },
        { 136u, 375u, 178u, 385u },
    };
    static const uint32_t packet_template[16] = {
        0xa0060500u, 0x00897000u, 0x94060500u, 0x00000000u,
        0x30000000u, 0x60800200u, 0x8000f0f0u, 0xff000000u,
        0u, 0u, 0x70000000u, 0x70000000u,
        0x70000000u, 0x70000000u, 0x70000000u, 0x70000000u,
    };
    uint32_t packets[6][16];
    uint64_t expected_bytes = 0u;

    s5l8900_t m;
    CHECK(s5l8900_init(&m, RAM_BASE, RAM_SIZE),
          "Settings-fill machine init failed");
    if (!m.ram) return;
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_GART2, table2);
    for (uint32_t page = 0; page < STRIDE * HEIGHT; page += 0x1000u)
        test_map_gpu_page(&m, table2, target + page, target_pa + page);

    for (unsigned i = 0; i < 6u; i++) {
        memcpy(packets[i], packet_template, sizeof packet_template);
        packets[i][8] = ((uint32_t)rects[i][0] << 16) | rects[i][1];
        packets[i][9] = ((uint32_t)rects[i][2] << 16) | rects[i][3];
        write_packet(&m, RING + i * 0x40u, packets[i], 16u);
        expected_bytes += (uint64_t)(rects[i][2] - rects[i][0]) *
                          (uint64_t)(rects[i][3] - rects[i][1]) * 4u;
    }
    test_gpu_write32(&m, target + 31u * STRIDE + 3u * 4u, 0xff112233u);
    test_gpu_write32(&m, target + 32u * STRIDE + 2u * 4u, 0xff445566u);
    test_gpu_write32(&m, target + 32u * STRIDE + 317u * 4u, 0xff778899u);
    test_gpu_write32(&m, target + 107u * STRIDE + 3u * 4u, 0xffaabbccu);
    /* Reproduce the fixed doorbell word without invoking the live handler,
     * then prove an exact accepted-batch probe changes neither pixels nor
     * completion state. The real bus write below consumes the retained heads. */
    s5l_mbx_write(&m.mbx, RING, S5L_MBX_2D_SUBMIT);
    const char *probe_why = "unset";
    uint64_t probe_hash = UINT64_MAX;
    CHECK(s5l_mbx_probe_2d_submit(&m.mbx, &m.bus, RING, 6u,
                                  &probe_hash, &probe_why) &&
          probe_hash == 0u,
          "valid Settings batch probe rejected: %s (%016llx)",
          probe_why, (unsigned long long)probe_hash);
    CHECK(test_gpu_read32(&m, target + 31u * STRIDE + 3u * 4u) ==
              0xff112233u &&
          m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "accepted Settings probe changed pixels or completion state");
    m.bus.write32(m.bus.ctx, MBX_BASE + RING, 0xf0000000u);

    uint32_t mismatches = 0u;
    for (unsigned i = 0; i < 6u; i++) {
        for (uint32_t y = rects[i][1]; y < rects[i][3]; y++) {
            for (uint32_t x = rects[i][0]; x < rects[i][2]; x++) {
                mismatches += test_gpu_read32(
                    &m, target + y * STRIDE + x * 4u) != 0xff000000u;
            }
        }
    }
    CHECK(mismatches == 0u,
          "measured Settings fill batch left %u non-black pixels", mismatches);
    CHECK(test_gpu_read32(&m, target + 31u * STRIDE + 3u * 4u) == 0xff112233u &&
          test_gpu_read32(&m, target + 32u * STRIDE + 2u * 4u) == 0xff445566u &&
          test_gpu_read32(&m, target + 32u * STRIDE + 317u * 4u) == 0xff778899u &&
          test_gpu_read32(&m, target + 107u * STRIDE + 3u * 4u) == 0xffaabbccu,
          "bounded Settings fills changed pixels outside their rectangles");
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0x400u,
          "Settings fill batch did not raise exactly 2D completion");
    CHECK(m.mbx_telemetry.candidates_2d == 6u &&
          m.mbx_telemetry.completed_2d == 6u &&
          m.mbx_telemetry.rejected_2d == 0u &&
          m.mbx_telemetry.bytes_2d == expected_bytes,
          "Settings 2D work ledger=%llu/%llu/%llu/%llu, expected 6/6/0/%llu",
          (unsigned long long)m.mbx_telemetry.candidates_2d,
          (unsigned long long)m.mbx_telemetry.completed_2d,
          (unsigned long long)m.mbx_telemetry.rejected_2d,
          (unsigned long long)m.mbx_telemetry.bytes_2d,
          (unsigned long long)expected_bytes);

    m.bus.write32(m.bus.ctx, MBX_BASE + REG_ACK, 0x400u);
    test_gpu_write32(&m, target + 32u * STRIDE + 3u * 4u, 0xff123456u);
    test_gpu_write32(&m, target + 375u * STRIDE + 136u * 4u, 0xffabcdefu);
    packets[5][9] = (321u << 16) | 385u;
    for (unsigned i = 0; i < 6u; i++)
        write_packet(&m, RING + 0x200u + i * 0x40u, packets[i], 16u);
    m.bus.write32(m.bus.ctx, MBX_BASE + RING, 0xf0000000u);
    CHECK(test_gpu_read32(&m, target + 32u * STRIDE + 3u * 4u) == 0xff123456u &&
          test_gpu_read32(&m, target + 375u * STRIDE + 136u * 4u) == 0xffabcdefu,
          "out-of-bounds late Settings fill committed part of the batch");
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "out-of-bounds Settings fill raised completion");
    CHECK(m.mbx_telemetry.candidates_2d == 12u &&
          m.mbx_telemetry.completed_2d == 6u &&
          m.mbx_telemetry.rejected_2d == 6u &&
          m.mbx_telemetry.bytes_2d == expected_bytes &&
          m.mbx_telemetry.last_rejected_2d_ring_offset == 0x200u &&
          m.mbx_telemetry.last_rejected_2d_count == 6u &&
          m.mbx_telemetry.last_rejected_2d_reason_hash != 0u,
          "rejected Settings batch ledger=%llu/%llu/%llu/%llu "
          "last=%llu/%llu/%016llx",
          (unsigned long long)m.mbx_telemetry.candidates_2d,
          (unsigned long long)m.mbx_telemetry.completed_2d,
          (unsigned long long)m.mbx_telemetry.rejected_2d,
          (unsigned long long)m.mbx_telemetry.bytes_2d,
          (unsigned long long)m.mbx_telemetry.last_rejected_2d_ring_offset,
          (unsigned long long)m.mbx_telemetry.last_rejected_2d_count,
          (unsigned long long)m.mbx_telemetry.last_rejected_2d_reason_hash);

    probe_why = "unset";
    probe_hash = 0u;
    CHECK(!s5l_mbx_probe_2d_submit(&m.mbx, &m.bus, RING + 0x200u, 6u,
                                   &probe_hash, &probe_why) &&
          strcmp(probe_why,
                 "solid-fill rectangle is empty, reversed, or outside "
                 "320x480") == 0 &&
          probe_hash == m.mbx_telemetry.last_rejected_2d_reason_hash,
          "exact rejected-batch probe=%s/%016llx, live=%016llx",
          probe_why, (unsigned long long)probe_hash,
          (unsigned long long)m.mbx_telemetry.last_rejected_2d_reason_hash);

    s5l8900_free(&m);
}

static void test_premultiplied_2d_clock_form(void) {
    const uint32_t table2 = 0x08003000u;
    const uint32_t source = 0x00900000u;
    const uint32_t target = 0x00810000u;
    const uint32_t source_pa = 0x08070000u;
    const uint32_t target_pa = 0x08080000u;
    static const uint32_t source_pixels[4] = {
        0x80804020u, 0x00000000u, 0xffffffffu, 0x40201008u
    };
    static const uint32_t destination_pixels[4] = {
        0xff102030u, 0xff405060u, 0xff708090u, 0xffa0b0c0u
    };
    uint32_t packet[18] = {
        0xa0060500u, target, 0x94060010u, source,
        0x30004000u, 0x20000004u, 0x095ff000u, 0x60800200u,
        0x8002ccccu, 0xffffffffu, 0x00020003u, 0x00040005u,
        0x70000000u, 0x70000000u, 0x70000000u, 0x70000000u,
        0x70000000u, 0x70000000u,
    };

    s5l8900_t m;
    CHECK(s5l8900_init(&m, RAM_BASE, RAM_SIZE), "machine init failed");
    if (!m.ram) return;
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_GART2, table2);
    test_map_gpu_page(&m, table2, source, source_pa);
    test_map_gpu_page(&m, table2, target, target_pa);
    test_map_gpu_page(&m, table2, target + 0x1000u, target_pa + 0x1000u);

    for (unsigned i = 0; i < 2u; i++) {
        test_gpu_write32(&m, source + 4u + i * 4u, source_pixels[i]);
        test_gpu_write32(&m, source + 0x10u + 4u + i * 4u,
                         source_pixels[2u + i]);
        test_gpu_write32(&m, target + 3u * 0x500u + (2u + i) * 4u,
                         destination_pixels[i]);
        test_gpu_write32(&m, target + 4u * 0x500u + (2u + i) * 4u,
                         destination_pixels[2u + i]);
    }
    test_gpu_write32(&m, target + 3u * 0x500u + 4u, 0x12345678u);

    /* A status W1S/ack pair between head and body must not destroy the pending
     * submit marker; that private state is snapshotted in STATUS's unread slot. */
    m.bus.write32(m.bus.ctx, MBX_BASE + RING + 0x40u, packet[0]);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_STATUS, 0x08u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_ACK, 0x08u);
    for (unsigned i = 1u; i < 18u; i++)
        m.bus.write32(m.bus.ctx, MBX_BASE + RING + 0x40u + i * 4u,
                      packet[i]);
    CHECK(test_gpu_read32(&m, target + 3u * 0x500u + 2u * 4u) ==
              destination_pixels[0],
          "2D blend executed before the fixed submit store");
    m.bus.write32(m.bus.ctx, MBX_BASE + RING, 0xf0000000u);

    uint32_t mismatches = 0u;
    for (unsigned i = 0; i < 2u; i++) {
        mismatches += test_gpu_read32(
            &m, target + 3u * 0x500u + (2u + i) * 4u) !=
            test_over(destination_pixels[i], source_pixels[i]);
        mismatches += test_gpu_read32(
            &m, target + 4u * 0x500u + (2u + i) * 4u) !=
            test_over(destination_pixels[2u + i], source_pixels[2u + i]);
    }
    CHECK(mismatches == 0u, "%u premultiplied 2D pixels mismatched", mismatches);
    CHECK(test_gpu_read32(&m, target + 3u * 0x500u + 4u) == 0x12345678u,
          "2D blend changed a pixel outside its rectangle");
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0x400u,
          "completed 2D blend did not raise only 2D_SYNC");
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_ACK, 0x400u);

    uint32_t first = target + 3u * 0x500u + 2u * 4u;
    test_gpu_write32(&m, first, 0x89abcdefu);
    packet[6] ^= 1u;
    for (unsigned i = 0; i < 18u; i++)
        m.bus.write32(m.bus.ctx, MBX_BASE + RING + 0x88u + i * 4u,
                      packet[i]);
    m.bus.write32(m.bus.ctx, MBX_BASE + RING, 0xf0000000u);
    CHECK(test_gpu_read32(&m, first) == 0x89abcdefu,
          "unknown 2D blend equation changed the destination");
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "unknown 2D blend equation raised completion");

    s5l8900_free(&m);
}

static void test_opaque_global_alpha_2d_form(void) {
    const uint32_t table2 = 0x08003000u;
    const uint32_t source = 0x00900000u;
    const uint32_t target = 0x00810000u;
    const uint32_t source_pa = 0x08070000u;
    const uint32_t target_pa = 0x08080000u;
    static const uint32_t source_pixels[4] = {
        0xff804020u, 0xff000000u, 0xffffffffu, 0xff201008u
    };
    static const uint32_t destination_pixels[4] = {
        0xff102030u, 0x80405060u, 0xff708090u, 0x40a0b0c0u
    };
    uint32_t packet[18] = {
        0xa0060500u, target, 0x94060010u, source,
        0x30004000u, 0x20000004u, 0x0d5f8000u, 0x60800200u,
        0x8002ccccu, 0xffffffffu, 0x00020003u, 0x00040005u,
        0x70000000u, 0x70000000u, 0x70000000u, 0x70000000u,
        0x70000000u, 0x70000000u,
    };

    s5l8900_t m;
    CHECK(s5l8900_init(&m, RAM_BASE, RAM_SIZE),
          "opaque-global machine init failed");
    if (!m.ram) return;
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_GART2, table2);
    test_map_gpu_page(&m, table2, source, source_pa);
    test_map_gpu_page(&m, table2, target, target_pa);
    test_map_gpu_page(&m, table2, target + 0x1000u, target_pa + 0x1000u);

    for (unsigned i = 0; i < 2u; i++) {
        test_gpu_write32(&m, source + 4u + i * 4u, source_pixels[i]);
        test_gpu_write32(&m, source + 0x10u + 4u + i * 4u,
                         source_pixels[2u + i]);
        test_gpu_write32(&m, target + 3u * 0x500u + (2u + i) * 4u,
                         destination_pixels[i]);
        test_gpu_write32(&m, target + 4u * 0x500u + (2u + i) * 4u,
                         destination_pixels[2u + i]);
    }

    write_packet(&m, RING + 0x40u, packet, 18u);
    m.bus.write32(m.bus.ctx, MBX_BASE + RING, 0xf0000000u);
    uint32_t mismatches = 0u;
    for (unsigned i = 0; i < 2u; i++) {
        uint32_t modulated = test_modulate_vertex_alpha(source_pixels[i],
                                                         0xf8u);
        mismatches += test_gpu_read32(
            &m, target + 3u * 0x500u + (2u + i) * 4u) !=
            test_over(destination_pixels[i], modulated);
        modulated = test_modulate_vertex_alpha(source_pixels[2u + i], 0xf8u);
        mismatches += test_gpu_read32(
            &m, target + 4u * 0x500u + (2u + i) * 4u) !=
            test_over(destination_pixels[2u + i], modulated);
    }
    CHECK(mismatches == 0u,
          "%u opaque global-alpha 2D pixels mismatched", mismatches);
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0x400u,
          "opaque global-alpha blend did not raise exactly 2D completion");
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_ACK, 0x400u);

    uint32_t first = target + 3u * 0x500u + 2u * 4u;
    const uint32_t ignored_alpha_source = 0x00d43210u;
    const uint32_t ignored_alpha_destination = 0x89abcdefu;
    const uint32_t ignored_alpha_expected = test_over(
        ignored_alpha_destination,
        test_modulate_vertex_alpha(ignored_alpha_source | 0xff000000u, 0xf8u));
    test_gpu_write32(&m, first, 0x89abcdefu);
    test_gpu_write32(&m, source + 4u, ignored_alpha_source);
    write_packet(&m, RING + 0x88u, packet, 18u);
    m.bus.write32(m.bus.ctx, MBX_BASE + RING, 0xf0000000u);
    CHECK(test_gpu_read32(&m, first) == ignored_alpha_expected,
          "opaque-global BGRX source rendered the wrong pixel");
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0x400u,
          "opaque-global BGRX source did not complete");
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_ACK, 0x400u);

    test_gpu_write32(&m, first, ignored_alpha_destination);
    test_gpu_write32(&m, source + 4u,
                     ignored_alpha_source | 0x80000000u);
    write_packet(&m, RING + 0xd0u, packet, 18u);
    m.bus.write32(m.bus.ctx, MBX_BASE + RING, 0xf0000000u);
    CHECK(test_gpu_read32(&m, first) == ignored_alpha_expected,
          "opaque-global output depended on the ignored source byte");
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0x400u,
          "opaque-global ignored-byte variant did not complete");
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_ACK, 0x400u);

    test_gpu_write32(&m, first, ignored_alpha_destination);
    test_gpu_write32(&m, source + 4u, source_pixels[0]);
    packet[6] ^= 0x00100000u;
    write_packet(&m, RING + 0x118u, packet, 18u);
    m.bus.write32(m.bus.ctx, MBX_BASE + RING, 0xf0000000u);
    CHECK(test_gpu_read32(&m, first) == 0x89abcdefu,
          "unknown global-alpha factors changed the destination");
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "unknown global-alpha factors raised completion");

    s5l8900_free(&m);
}

static void test_ordered_atomic_2d_batches(void) {
    enum {
        TARGET_STRIDE = 0x500u,
        TARGET_HEIGHT = 480u,
        TARGET_BYTES = TARGET_STRIDE * TARGET_HEIGHT,
    };
    const uint32_t table2 = 0x08003000u;
    const uint32_t source = 0x00900000u;
    const uint32_t target = 0x00810000u;
    const uint32_t source_pa = 0x08130000u;
    const uint32_t target_pa = 0x08080000u;
    const uint32_t destination = target + 10u * TARGET_STRIDE + 10u * 4u;
    const uint32_t original = 0xff102030u;
    const uint32_t first_source = 0x80804020u;
    const uint32_t second_source = 0x40201008u;
    uint32_t first[18] = {
        0xa0060500u, target, 0x94060010u, source,
        0x30000000u, 0x20000004u, 0x095ff000u, 0x60800200u,
        0x8002ccccu, 0xffffffffu, 0x000a000au, 0x000b000bu,
        0x70000000u, 0x70000000u, 0x70000000u, 0x70000000u,
        0x70000000u, 0x70000000u,
    };
    uint32_t second[18];
    memcpy(second, first, sizeof second);
    second[4] = 0x30004000u; /* source x=1, same destination pixel */

    s5l8900_t m;
    CHECK(s5l8900_init(&m, RAM_BASE, RAM_SIZE), "machine init failed");
    if (!m.ram) return;
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_GART2, table2);
    test_map_gpu_page(&m, table2, source, source_pa);
    for (uint32_t page = 0; page < (TARGET_BYTES + 0xfffu) / 0x1000u;
         page++)
        test_map_gpu_page(&m, table2, target + page * 0x1000u,
                          target_pa + page * 0x1000u);

    test_gpu_write32(&m, source, first_source);
    test_gpu_write32(&m, source + 4u, second_source);
    test_gpu_write32(&m, destination, original);

    /* r376's first newly exposed submit used eight adjacent 18-word blend
     * packets. Two overlapping packets are enough to prove the critical
     * ordering property: command two must see command one's result. */
    write_packet(&m, RING + 0x2c0u, first, 18u);
    write_packet(&m, RING + 0x308u, second, 18u);
    CHECK(test_gpu_read32(&m, destination) == original,
          "batched blend executed before the fixed submit store");
    m.bus.write32(m.bus.ctx, MBX_BASE + RING, 0xf0000000u);
    CHECK(test_gpu_read32(&m, destination) ==
              test_over(test_over(original, first_source), second_source),
          "overlapping blend batch did not execute in copied order");
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0x400u,
          "completed blend batch did not raise exactly 2D_SYNC");
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_ACK, 0x400u);

    /* The sparse batch staging path must replay every earlier job type, not
     * merely another blend. Put a black fill under the same translucent source
     * and require command two to see that staged black pixel rather than the
     * still-unchanged guest-RAM value. */
    uint32_t fill_then_blend[16] = {
        0xa0060500u, target, 0x94060500u, 0u,
        0x30000000u, 0x60800200u, 0x8000f0f0u, 0xff000000u,
        0x000a000au, 0x000b000bu, 0x70000000u, 0x70000000u,
        0x70000000u, 0x70000000u, 0x70000000u, 0x70000000u,
    };
    test_gpu_write32(&m, destination, original);
    write_packet(&m, RING + 0x400u, fill_then_blend, 16u);
    write_packet(&m, RING + 0x440u, first, 18u);
    m.bus.write32(m.bus.ctx, MBX_BASE + RING, 0xf0000000u);
    CHECK(test_gpu_read32(&m, destination) ==
              test_over(0xff000000u, first_source),
          "blend did not see an intersecting prior fill's staged pixels");
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0x400u,
          "mixed fill/blend batch did not raise exactly 2D_SYNC");
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_ACK, 0x400u);

    /* A later invalid command must reject the whole submit. If the first
     * command leaks through, this pixel changes and the test catches the
     * non-atomic implementation directly. */
    test_gpu_write32(&m, destination, original);
    second[6] ^= 1u;
    write_packet(&m, RING + 0x2c0u, first, 18u);
    write_packet(&m, RING + 0x308u, second, 18u);
    m.bus.write32(m.bus.ctx, MBX_BASE + RING, 0xf0000000u);
    CHECK(test_gpu_read32(&m, destination) == original,
          "rejected blend batch committed an earlier command");
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "rejected blend batch raised completion");

    /* The second r376 batch used adjacent 16-word simple copies. Exercise its
     * different cursor length instead of relying on the live replay alone. */
    uint32_t simple_first[16] = {
        0xa0060500u, target, 0x94060500u, source,
        0x30000000u, 0x60800200u, 0x8000ccccu, 0xffffffffu,
        0x00140014u, 0x00150015u, 0x70000000u, 0x70000000u,
        0x70000000u, 0x70000000u, 0x70000000u, 0x70000000u,
    };
    uint32_t simple_second[16];
    memcpy(simple_second, simple_first, sizeof simple_second);
    simple_second[4] = 0x30004000u;
    simple_second[8] = 0x00150014u;
    simple_second[9] = 0x00160015u;
    uint32_t simple_dst0 = target + 20u * TARGET_STRIDE + 20u * 4u;
    uint32_t simple_dst1 = simple_dst0 + 4u;
    test_gpu_write32(&m, simple_dst0, 0u);
    test_gpu_write32(&m, simple_dst1, 0u);
    write_packet(&m, RING + 0x500u, simple_first, 16u);
    write_packet(&m, RING + 0x540u, simple_second, 16u);
    m.bus.write32(m.bus.ctx, MBX_BASE + RING, 0xf0000000u);
    CHECK(test_gpu_read32(&m, simple_dst0) == first_source &&
          test_gpu_read32(&m, simple_dst1) == second_source,
          "adjacent simple-copy batch did not copy both packets");
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0x400u,
          "completed simple-copy batch did not raise exactly 2D_SYNC");
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_ACK, 0x400u);

    /* r432 reaches the end of AppleMBX's 64 KiB software cursor with a
     * command at +0xfff8. The kernel helper copies that command contiguously
     * into the EDRAM bytes beyond the nominal ring, then resets the *next*
     * command head to +0. Exercise both sides of that boundary; modulo-splitting
     * the first command or skipping it makes at least one pixel mismatch. */
    test_gpu_write32(&m, simple_dst0, 0u);
    test_gpu_write32(&m, simple_dst1, 0u);
    write_packet(&m, RING + 0xfff8u, simple_first, 16u);
    write_packet(&m, RING, simple_second, 16u);
    CHECK(test_gpu_read32(&m, simple_dst0) == 0u &&
          test_gpu_read32(&m, simple_dst1) == 0u,
          "ring-crossing batch executed before the fixed submit store");
    m.bus.write32(m.bus.ctx, MBX_BASE + RING, 0xf0000000u);
    CHECK(test_gpu_read32(&m, simple_dst0) == first_source &&
          test_gpu_read32(&m, simple_dst1) == second_source,
          "ring-crossing batch did not preserve both command copies");
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0x400u,
          "ring-crossing batch did not raise exactly 2D_SYNC");

    s5l8900_free(&m);
}

static void test_first_tiled_premultiplied_over(void) {
    enum {
        WIDTH = 320u, TOP = 20u, HEIGHT = 96u,
        SOURCE_STRIDE = 0x20u, TARGET_STRIDE = 0x500u
    };
    const uint32_t table0 = 0x08003000u;
    const uint32_t table2 = 0x08004000u;
    const uint32_t region = 0x00001000u;
    const uint32_t object = 0x00014000u;
    const uint32_t source = 0x0092d000u;
    const uint32_t target = 0x00897000u;
    const uint32_t region_pa = 0x08010000u;
    const uint32_t object_pa = 0x08011000u;
    const uint32_t source_pa = 0x08012000u;
    const uint32_t target_pa = 0x08020000u;

    s5l8900_t m;
    CHECK(s5l8900_init(&m, RAM_BASE, RAM_SIZE), "machine init failed");
    if (!m.ram) return;

    m.bus.write32(m.bus.ctx, MBX_BASE + REG_GART0, table0);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_GART2, table2);
    test_map_gpu_page(&m, table0, region, region_pa);
    test_map_gpu_page(&m, table0, object, object_pa);
    test_map_gpu_page(&m, table2, source, source_pa);
    for (uint32_t page = 0; page < 40u; page++)
        test_map_gpu_page(&m, table2, target + page * 0x1000u,
                          target_pa + page * 0x1000u);

    uint32_t list = object + 0x68u;
    for (uint32_t y = 1u; y <= 7u; y++) {
        for (uint32_t x = 0u; x < 40u; x++) {
            uint32_t pair = ((y - 1u) * 40u + x) * 8u;
            uint32_t code = (y << 8) | x;
            if (y == 7u && x == 39u) code |= 0x80000000u;
            test_gpu_write32(&m, region + pair, code);
            test_gpu_write32(&m, region + pair + 4u, list);
        }
    }
    static const uint32_t list_words[4] = {
        0x60200020u, 0x6020002du, 0x61a0007cu, 0xf0000000u
    };
    for (unsigned i = 0; i < 4u; i++)
        test_gpu_write32(&m, list + i * 4u, list_words[i]);

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
        uint32_t value = i == 2u
            ? 0x0e500000u | (target >> 7) : background[i];
        test_gpu_write32(&m, object + i * 4u, value);
    }

    static const struct mbx_test_word {
        uint16_t off;
        uint32_t value;
    } boundary[] = {
        {0x080u, 0x22206f80u}, {0x088u, 0x45800000u},
        {0x094u, 0x45800000u}, {0x098u, 0x45800000u},
        {0x09cu, 0x45800000u}, {0x0b4u, 0x22207f80u},
        {0x0bcu, 0x42e80000u}, {0x0c4u, 0x41a00000u},
        {0x0c8u, 0x43a00000u}, {0x0ccu, 0x42e80000u},
        {0x0d0u, 0x43a00000u}, {0x0d4u, 0x41a00000u},
    };
    for (unsigned i = 0; i < sizeof boundary / sizeof boundary[0]; i++)
        test_gpu_write32(&m, object + boundary[i].off, boundary[i].value);
    for (uint32_t off = 0x0e8u; off < 0x1f0u; off += 4u)
        test_gpu_write32(&m, object + off, 0x3c000000u ^ off);

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
    for (unsigned i = 0; i < 44u; i++) {
        uint32_t value = quad[i];
        if (i == 2u) value = 0x8e000000u | (source >> 7);
        if (i == 5u) value = 0x0e500000u | (target >> 7);
        test_gpu_write32(&m, object + 0x1f0u + i * 4u, value);
    }

    uint32_t expected[HEIGHT];
    for (uint32_t row = 0; row < HEIGHT; row++) {
        uint32_t src = ((0x80u + (row & 0x3fu)) << 24) |
                       ((0x30u + (row & 0x0fu)) << 16) |
                       ((0x20u + (row & 0x0fu)) << 8) |
                       (0x10u + (row & 0x0fu));
        test_gpu_write32(&m, source + row * SOURCE_STRIDE, src);
        uint32_t dst = 0xff204060u + row;
        expected[row] = test_over(dst, src);
        for (uint32_t x = 0; x < WIDTH; x++)
            test_gpu_write32(&m,
                target + (TOP + row) * TARGET_STRIDE + x * 4u, dst);
    }
    test_gpu_write32(&m, target + (TOP - 1u) * TARGET_STRIDE, 0x11223344u);
    test_gpu_write32(&m, target + (TOP + HEIGHT) * TARGET_STRIDE, 0x55667788u);

    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RGNBASE, region);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_OBJBASE, object);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_PIXSAMP, 0x00020007u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBCTL, 6u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBXCLIP, 0x01400000u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBYCLIP, 0x00800010u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBSTART, target);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBSTRIDE, WIDTH);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);

    uint32_t mismatches = 0u;
    for (uint32_t row = 0; row < HEIGHT; row++) {
        for (uint32_t x = 0; x < WIDTH; x++) {
            uint32_t actual = test_gpu_read32(&m,
                target + (TOP + row) * TARGET_STRIDE + x * 4u);
            mismatches += actual != expected[row];
        }
    }
    CHECK(mismatches == 0u, "%u tiled source-over pixels mismatched", mismatches);
    CHECK(test_gpu_read32(&m, target + (TOP - 1u) * TARGET_STRIDE) ==
              0x11223344u &&
          test_gpu_read32(&m, target + (TOP + HEIGHT) * TARGET_STRIDE) ==
              0x55667788u,
          "tiled render changed a pixel outside its rectangle");
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0x4cu,
          "completed tiled render status=%08x, expect ISP/render/EVM",
          m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS));
    CHECK(m.mbx_telemetry.candidates_3d == 1u &&
          m.mbx_telemetry.completed_3d == 1u &&
          m.mbx_telemetry.rejected_3d == 0u &&
          m.mbx_telemetry.pixels_3d == WIDTH * HEIGHT,
          "first 3D work ledger=%llu/%llu/%llu/%llu",
          (unsigned long long)m.mbx_telemetry.candidates_3d,
          (unsigned long long)m.mbx_telemetry.completed_3d,
          (unsigned long long)m.mbx_telemetry.rejected_3d,
          (unsigned long long)m.mbx_telemetry.pixels_3d);
    const s5l_mbx_3d_accept_witness_t *first_accept =
        &m.mbx_telemetry.accepted_3d_history[0];
    CHECK(first_accept->sequence == 1u &&
          first_accept->kind == S5L_MBX_3D_ACCEPT_TILED &&
          first_accept->pixels == WIDTH * HEIGHT &&
          first_accept->region == region &&
          first_accept->object == object &&
          first_accept->target == target &&
          first_accept->target_physical == target_pa &&
          first_accept->target_mapping_span == 0x1000u &&
          first_accept->xclip == 0x01400000u &&
          first_accept->yclip == 0x00800010u &&
          first_accept->list_valid_mask == 0x0fu &&
          first_accept->list_words[2] == 0x61a0007cu &&
          first_accept->record_base == object + 0x1f0u &&
          first_accept->record_valid_words ==
              S5L_MBX_3D_REJECTION_RECORD_WORDS &&
          first_accept->record_hash != 0u &&
          first_accept->record_words[0] == 0xe0000000u &&
          first_accept->record_words[3] == 0xa6884710u,
          "accepted 3D witness did not retain the completed tiled render");
    const s5l_mbx_3d_target_ledger_t *first_target =
        &m.mbx_telemetry.target_3d_ledger[0];
    CHECK(first_target->last_sequence == 1u &&
          first_target->completed == 1u &&
          first_target->pixels == WIDTH * HEIGHT &&
          first_target->target == target &&
          first_target->target_physical == target_pa &&
          first_target->target_mapping_span == 0x1000u &&
          first_target->last_kind == S5L_MBX_3D_ACCEPT_TILED,
          "3D target ledger did not retain the completed tiled render");

    m.bus.write32(m.bus.ctx, MBX_BASE + REG_ACK, 0x4cu);
    test_gpu_write32(&m, target + TOP * TARGET_STRIDE, 0x12345678u);
    test_gpu_write32(&m, object + 0x1f0u + 3u * 4u, 0xa6884711u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
    CHECK(test_gpu_read32(&m, target + TOP * TARGET_STRIDE) == 0x12345678u,
          "unknown 3D blend word changed the destination");
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "unknown 3D blend word raised completion");

    test_gpu_write32(&m, object + 0x1f0u + 3u * 4u, 0xa6884710u);
    uint32_t late = target + (TOP + HEIGHT - 1u) * TARGET_STRIDE;
    uint32_t late_entry = table2 + (((late >> 12) & 0x3ffu) * 4u);
    m.bus.write32(m.bus.ctx, late_entry, 0u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
    CHECK(test_gpu_read32(&m, target + TOP * TARGET_STRIDE) == 0x12345678u,
          "late missing 3D PTE left a partially rendered rectangle");
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "late missing 3D PTE raised completion");
    CHECK(m.mbx_telemetry.candidates_3d == 3u &&
          m.mbx_telemetry.completed_3d == 1u &&
          m.mbx_telemetry.rejected_3d == 2u &&
          m.mbx_telemetry.pixels_3d == WIDTH * HEIGHT,
          "rejected 3D work ledger=%llu/%llu/%llu/%llu",
          (unsigned long long)m.mbx_telemetry.candidates_3d,
          (unsigned long long)m.mbx_telemetry.completed_3d,
          (unsigned long long)m.mbx_telemetry.rejected_3d,
          (unsigned long long)m.mbx_telemetry.pixels_3d);
    CHECK(first_target->last_sequence == 1u &&
          first_target->completed == 1u &&
          first_target->pixels == WIDTH * HEIGHT,
          "rejected 3D work changed the completed-target ledger");
    const s5l_mbx_3d_rejection_witness_t *first_reject =
        &m.mbx_telemetry.rejected_3d_history[0];
    const s5l_mbx_3d_rejection_witness_t *second_reject =
        &m.mbx_telemetry.rejected_3d_history[1];
    CHECK(first_reject->sequence == 1u &&
          second_reject->sequence == 2u &&
          first_reject->region == region &&
          first_reject->object == object &&
          first_reject->target == target &&
          first_reject->xclip == 0x01400000u &&
          first_reject->yclip == 0x00800010u &&
          first_reject->pixel_sample == 0x00020007u &&
          first_reject->framebuffer_control == 6u &&
          first_reject->framebuffer_stride == WIDTH,
          "3D rejection witnesses lost sequence or render registers");
    CHECK(first_reject->tiled_reason_hash != 0u &&
          first_reject->status_reason_hash != 0u &&
          first_reject->sprite_reason_hash != 0u &&
          first_reject->solid_reason_hash != 0u &&
          first_reject->list_valid_mask == 0x0fu &&
          first_reject->list_words[2] == 0x61a0007cu &&
          first_reject->record_base == object + 0x1f0u &&
          first_reject->record_valid_words ==
              S5L_MBX_3D_REJECTION_RECORD_WORDS &&
          first_reject->record_words[3] == 0xa6884711u,
          "first 3D rejection witness did not retain the malformed record");
    CHECK(second_reject->list_valid_mask == 0x0fu &&
          second_reject->record_base == object + 0x1f0u &&
          second_reject->record_valid_words ==
              S5L_MBX_3D_REJECTION_RECORD_WORDS &&
          second_reject->record_words[3] == 0xa6884710u,
          "second 3D rejection witness did not retain the restored record");

    for (unsigned i = 0u; i < S5L_MBX_3D_REJECTION_HISTORY; i++)
        m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
    CHECK(m.mbx_telemetry.rejected_3d == 6u &&
          m.mbx_telemetry.rejected_3d_history[0].sequence == 5u &&
          m.mbx_telemetry.rejected_3d_history[1].sequence == 6u &&
          m.mbx_telemetry.rejected_3d_history[2].sequence == 3u &&
          m.mbx_telemetry.rejected_3d_history[3].sequence == 4u,
          "3D rejection history did not retain the latest bounded window");

    /* r379 captured the overwritten render before r377's retained one. It
     * reused this exact quad/source, but narrowed the tiles to y=1..6 and the
     * boundary to y=20..97. Restore the deliberately removed PTE, then prove
     * that exact 320x77 write before testing the following inset rectangle. */
    uint32_t late_page = late & ~0xfffu;
    test_map_gpu_page(&m, table2, late_page,
                      target_pa + (late_page - target));
    for (uint32_t y = 1u; y <= 6u; y++) {
        for (uint32_t x = 0u; x < 40u; x++) {
            uint32_t pair = ((y - 1u) * 40u + x) * 8u;
            uint32_t code = (y << 8) | x;
            if (y == 6u && x == 39u) code |= 0x80000000u;
            test_gpu_write32(&m, region + pair, code);
            test_gpu_write32(&m, region + pair + 4u, list);
        }
    }
    static const uint32_t upper_boundary[8] = {
        0x00000000u, 0x42c20000u, 0x00000000u, 0x41a00000u,
        0x43a00000u, 0x42c20000u, 0x43a00000u, 0x41a00000u,
    };
    for (unsigned i = 0; i < 8u; i++)
        test_gpu_write32(&m, object + 0x0b8u + i * 4u,
                         upper_boundary[i]);

    enum { UPPER_HEIGHT = 77u };
    uint32_t upper_expected[UPPER_HEIGHT];
    for (uint32_t row = 0; row < UPPER_HEIGHT; row++) {
        uint32_t src = ((0x80u + (row & 0x3fu)) << 24) |
                       ((0x30u + (row & 0x0fu)) << 16) |
                       ((0x20u + (row & 0x0fu)) << 8) |
                       (0x10u + (row & 0x0fu));
        uint32_t dst = 0xff305070u + row;
        upper_expected[row] = test_over(dst, src);
        for (uint32_t x = 0; x < WIDTH; x++)
            test_gpu_write32(&m,
                target + (TOP + row) * TARGET_STRIDE + x * 4u, dst);
    }
    uint32_t upper_above = target + (TOP - 1u) * TARGET_STRIDE;
    uint32_t upper_after = target + (TOP + UPPER_HEIGHT) * TARGET_STRIDE;
    test_gpu_write32(&m, upper_above, 0x11223344u);
    test_gpu_write32(&m, upper_after, 0x55667788u);

    m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBXCLIP, 0x01400000u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBYCLIP, 0x00700010u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
    mismatches = 0u;
    for (uint32_t row = 0; row < UPPER_HEIGHT; row++) {
        for (uint32_t x = 0; x < WIDTH; x++) {
            uint32_t actual = test_gpu_read32(&m,
                target + (TOP + row) * TARGET_STRIDE + x * 4u);
            mismatches += actual != upper_expected[row];
        }
    }
    CHECK(mismatches == 0u,
          "%u upper clipped-background pixels mismatched", mismatches);
    CHECK(test_gpu_read32(&m, upper_above) == 0x11223344u &&
          test_gpu_read32(&m, upper_after) == 0x55667788u,
          "upper clipped background changed a pixel outside its boundary");
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0x4cu,
          "upper clipped background did not raise all three 3D events");
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_ACK, 0x4cu);

    /* The immediately following render narrows the stream again to x=1..38,
     * y=6 and the boundary to x=8..312, y=97..109. Prove its source-row
     * offset and exact dirty rectangle rather than treating either redraw as
     * another full 320x96 pass. */
    for (uint32_t x = 1u; x <= 38u; x++) {
        uint32_t pair = (x - 1u) * 8u;
        uint32_t code = (6u << 8) | x;
        if (x == 38u) code |= 0x80000000u;
        test_gpu_write32(&m, region + pair, code);
        test_gpu_write32(&m, region + pair + 4u, list);
    }
    static const uint32_t partial_boundary[8] = {
        0x41000000u, 0x42da0000u, 0x41000000u, 0x42c20000u,
        0x439c0000u, 0x42da0000u, 0x439c0000u, 0x42c20000u,
    };
    for (unsigned i = 0; i < 8u; i++)
        test_gpu_write32(&m, object + 0x0b8u + i * 4u,
                         partial_boundary[i]);

    enum {
        PARTIAL_LEFT = 8u, PARTIAL_TOP = 97u,
        PARTIAL_WIDTH = 304u, PARTIAL_HEIGHT = 12u,
        PARTIAL_SOURCE_ROW = 77u,
    };
    uint32_t partial_expected[PARTIAL_HEIGHT];
    for (uint32_t row = 0; row < PARTIAL_HEIGHT; row++) {
        uint32_t source_row = PARTIAL_SOURCE_ROW + row;
        uint32_t src = ((0x80u + (source_row & 0x3fu)) << 24) |
                       ((0x30u + (source_row & 0x0fu)) << 16) |
                       ((0x20u + (source_row & 0x0fu)) << 8) |
                       (0x10u + (source_row & 0x0fu));
        partial_expected[row] = test_over(expected[source_row], src);
    }
    uint32_t partial_before = target + PARTIAL_TOP * TARGET_STRIDE +
                              (PARTIAL_LEFT - 1u) * 4u;
    uint32_t partial_above = target + (PARTIAL_TOP - 1u) * TARGET_STRIDE +
                             PARTIAL_LEFT * 4u;
    uint32_t partial_after = target +
                             (PARTIAL_TOP + PARTIAL_HEIGHT) * TARGET_STRIDE +
                             PARTIAL_LEFT * 4u;
    test_gpu_write32(&m, partial_before, 0x11223344u);
    test_gpu_write32(&m, partial_above, 0x55667788u);
    test_gpu_write32(&m, partial_after, 0x99aabbccu);

    m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBXCLIP, 0x01380008u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBYCLIP, 0x00700060u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
    mismatches = 0u;
    for (uint32_t row = 0; row < PARTIAL_HEIGHT; row++) {
        for (uint32_t x = 0; x < PARTIAL_WIDTH; x++) {
            uint32_t actual = test_gpu_read32(&m,
                target + (PARTIAL_TOP + row) * TARGET_STRIDE +
                    (PARTIAL_LEFT + x) * 4u);
            mismatches += actual != partial_expected[row];
        }
    }
    CHECK(mismatches == 0u,
          "%u clipped background-overlay pixels mismatched", mismatches);
    CHECK(test_gpu_read32(&m, partial_before) == 0x11223344u &&
          test_gpu_read32(&m, partial_above) == 0x55667788u &&
          test_gpu_read32(&m, partial_after) == 0x99aabbccu,
          "clipped background overlay changed a pixel outside its boundary");
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0x4cu,
          "clipped background overlay did not raise all three 3D events");
    CHECK(first_target->last_sequence == 3u &&
          first_target->completed == 3u &&
          first_target->pixels == WIDTH * HEIGHT +
              WIDTH * UPPER_HEIGHT + PARTIAL_WIDTH * PARTIAL_HEIGHT &&
          first_target->last_kind == S5L_MBX_3D_ACCEPT_TILED,
          "3D target ledger did not aggregate repeated framebuffer work");

    m.bus.write32(m.bus.ctx, MBX_BASE + REG_ACK, 0x4cu);
    uint32_t partial_first = target + PARTIAL_TOP * TARGET_STRIDE +
                             PARTIAL_LEFT * 4u;
    test_gpu_write32(&m, partial_first, 0x89abcdefu);
    test_gpu_write32(&m, object + 0x0b8u, partial_boundary[0] ^ 1u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
    CHECK(test_gpu_read32(&m, partial_first) == 0x89abcdefu &&
          m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "unknown clipped boundary changed pixels or raised completion");

    s5l8900_free(&m);
}

static void test_second_tiled_status_glyph(void) {
    enum {
        LEFT = 155u, WIDTH = 10u, HEIGHT = 20u,
        SOURCE_STRIDE = 0x40u, TARGET_STRIDE = 0x500u
    };
    const uint32_t table0 = 0x08003000u;
    const uint32_t table2 = 0x08004000u;
    const uint32_t region = 0x00001000u;
    const uint32_t object = 0x00014000u;
    const uint32_t source = 0x00994000u;
    const uint32_t target = 0x00897000u;
    const uint32_t region_pa = 0x08010000u;
    const uint32_t object_pa = 0x08011000u;
    const uint32_t source_pa = 0x08012000u;
    const uint32_t target_pa = 0x08020000u;

    s5l8900_t m;
    CHECK(s5l8900_init(&m, RAM_BASE, RAM_SIZE), "machine init failed");
    if (!m.ram) return;

    m.bus.write32(m.bus.ctx, MBX_BASE + REG_GART0, table0);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_GART2, table2);
    test_map_gpu_page(&m, table0, region, region_pa);
    test_map_gpu_page(&m, table0, object, object_pa);
    test_map_gpu_page(&m, table2, source, source_pa);
    for (uint32_t page = 0; page < 7u; page++)
        test_map_gpu_page(&m, table2, target + page * 0x1000u,
                          target_pa + page * 0x1000u);

    uint32_t list = object + 0x68u;
    static const uint32_t tile_codes[4] = {
        0x00000013u, 0x00000014u, 0x00000113u, 0x80000114u
    };
    for (unsigned i = 0; i < 4u; i++) {
        test_gpu_write32(&m, region + i * 8u, tile_codes[i]);
        test_gpu_write32(&m, region + i * 8u + 4u, list);
    }
    static const uint32_t list_words[4] = {
        0x60200020u, 0x6020002du, 0x61a0007cu, 0xf0000000u
    };
    for (unsigned i = 0; i < 4u; i++)
        test_gpu_write32(&m, list + i * 4u, list_words[i]);

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
        uint32_t value = i == 2u
            ? 0x0e500000u | (target >> 7) : background[i];
        test_gpu_write32(&m, object + i * 4u, value);
    }

    static const struct mbx_test_word {
        uint16_t off;
        uint32_t value;
    } boundary[] = {
        {0x080u, 0x22206f80u}, {0x088u, 0x45800000u},
        {0x094u, 0x45800000u}, {0x098u, 0x45800000u},
        {0x09cu, 0x45800000u}, {0x0b4u, 0x22207f80u},
        {0x0b8u, 0x431b0000u}, {0x0bcu, 0x41a00000u},
        {0x0c0u, 0x431b0000u}, {0x0c8u, 0x43250000u},
        {0x0ccu, 0x41a00000u}, {0x0d0u, 0x43250000u},
    };
    for (unsigned i = 0; i < sizeof boundary / sizeof boundary[0]; i++)
        test_gpu_write32(&m, object + boundary[i].off, boundary[i].value);
    for (uint32_t off = 0x0e8u; off < 0x1f0u; off += 4u)
        test_gpu_write32(&m, object + off, 0xc3000000u ^ off);

    static const uint32_t quad[44] = {
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
    };
    for (unsigned i = 0; i < 44u; i++) {
        uint32_t value = quad[i];
        if (i == 2u) value = 0x0e040000u | (source >> 7);
        if (i == 5u) value = 0x0e500000u | (target >> 7);
        test_gpu_write32(&m, object + 0x1f0u + i * 4u, value);
    }

    uint32_t expected[WIDTH * HEIGHT];
    for (uint32_t y = 0; y < HEIGHT; y++) {
        for (uint32_t x = 0; x < WIDTH; x++) {
            uint32_t alpha = 0x80u + ((x * 11u + y * 7u) & 0x7fu);
            uint32_t src = (alpha << 24) | ((alpha * 3u / 4u) << 16) |
                           ((alpha / 2u) << 8) | (alpha / 4u);
            uint32_t dst = 0xff102030u + y * 0x00010101u + x;
            test_gpu_write32(&m, source + y * SOURCE_STRIDE + x * 4u, src);
            test_gpu_write32(&m,
                target + y * TARGET_STRIDE + (LEFT + x) * 4u, dst);
            expected[y * WIDTH + x] = test_over(
                dst, test_modulate_vertex_alpha(src, quad[24] >> 24));
        }
    }
    test_gpu_write32(&m, target + (LEFT - 1u) * 4u, 0x11223344u);
    test_gpu_write32(&m, target + LEFT * 4u + HEIGHT * TARGET_STRIDE,
                     0x55667788u);

    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RGNBASE, region);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_OBJBASE, object);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_PIXSAMP, 0x00020007u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBCTL, 6u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBXCLIP, 0x00a80098u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBYCLIP, 0x00200000u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBSTART, target);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBSTRIDE, 320u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);

    uint32_t mismatches = 0u;
    for (uint32_t y = 0; y < HEIGHT; y++) {
        for (uint32_t x = 0; x < WIDTH; x++) {
            uint32_t actual = test_gpu_read32(&m,
                target + y * TARGET_STRIDE + (LEFT + x) * 4u);
            mismatches += actual != expected[y * WIDTH + x];
        }
    }
    CHECK(mismatches == 0u, "%u status-glyph pixels mismatched", mismatches);
    CHECK(test_gpu_read32(&m, target + (LEFT - 1u) * 4u) == 0x11223344u &&
          test_gpu_read32(&m, target + LEFT * 4u + HEIGHT * TARGET_STRIDE) ==
              0x55667788u,
          "status-glyph render changed a pixel outside its rectangle");
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0x4cu,
          "completed status-glyph render did not raise all three events");

    m.bus.write32(m.bus.ctx, MBX_BASE + REG_ACK, 0x4cu);
    uint32_t first = target + LEFT * 4u;
    test_gpu_write32(&m, first, 0x89abcdefu);
    test_gpu_write32(&m, object + 0x1f0u + 3u * 4u, 0xcd206c41u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
    CHECK(test_gpu_read32(&m, first) == 0x89abcdefu,
          "unknown glyph texture control changed the destination");
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "unknown glyph texture control raised completion");

    s5l8900_free(&m);
}

static void test_write_compact_blit_copy(s5l8900_t *m,
                                         uint32_t region,
                                         uint32_t object,
                                         uint32_t source,
                                         uint32_t target,
                                         uint32_t record_offset,
                                         uint32_t left,
                                         uint32_t top,
                                         uint32_t width,
                                         uint32_t height,
                                         uint32_t source_width,
                                         uint32_t source_height,
                                         uint32_t source_stride,
                                         uint32_t vertex_colour) {
    for (uint32_t off = 0u; off < 0x500u; off += 4u)
        test_gpu_write32(m, object + off, 0u);

    /* The compact pointer selects its 33-word record directly. Safari leaves
     * the preceding, unreferenced background storage stale; poison it so this
     * regression cannot accidentally depend on the older perspective form. */
    for (unsigned i = 0; i < 26u; i++)
        test_gpu_write32(m, object + i * 4u, 0x5a000000u ^ i);

    uint32_t list = object + 0x68u;
    test_gpu_write32(m, list, 0x60200020u);
    test_gpu_write32(m, list + 4u, 0x6020002du);
    test_gpu_write32(m, list + 8u,
                     0x61200000u | (record_offset / 4u));
    test_gpu_write32(m, list + 12u, 0xf0000000u);

    static const struct {
        uint16_t off;
        uint32_t value;
    } boundary_fixed[] = {
        {0x080u, 0x22206f80u}, {0x088u, 0x45800000u},
        {0x094u, 0x45800000u}, {0x098u, 0x45800000u},
        {0x09cu, 0x45800000u}, {0x0b4u, 0x22207f80u},
    };
    for (unsigned i = 0;
         i < sizeof boundary_fixed / sizeof boundary_fixed[0]; i++)
        test_gpu_write32(m, object + boundary_fixed[i].off,
                         boundary_fixed[i].value);
    const uint32_t boundary[8] = {
        left, top + height, left, top,
        left + width, top + height, left + width, top,
    };
    for (unsigned i = 0; i < 8u; i++)
        test_gpu_write32(m, object + 0x0b8u + i * 4u,
                         test_float_word((float)boundary[i]));

    uint32_t pitch_pixels = source_stride / 4u;
    uint32_t texture_width = 8u, width_field = 0u;
    while (texture_width < pitch_pixels) {
        texture_width <<= 1;
        width_field++;
    }
    uint32_t texture_height = 8u, height_field = 0u;
    while (texture_height < source_height) {
        texture_height <<= 1;
        height_field++;
    }
    uint32_t pitch_units = source_stride / 16u;
    uint32_t header = 0xa0018000u |
        (width_field << 24) | (height_field << 20) |
        ((pitch_units & 2u) >> 1);
    uint32_t source_word = 0x8e000000u |
        ((pitch_units & ~3u) << 16) | (source >> 7);
    const float x0 = (float)left;
    const float y0 = (float)top;
    const float x1 = (float)(left + width);
    const float y1 = (float)(top + height);
    const float vertices[8] = {
        x0, y0, x1, y0, x0, y1, x1, y1,
    };
    const float u0 = 0.0f, v0 = 0.0f;
    const float u1 = ((float)source_width - 0.5f) / (float)texture_width;
    const float v1 = ((float)source_height - 0.5f) / (float)texture_height;
    const float uv[8] = {
        u0, v0, u1, v0, u0, v1, u1, v1,
    };
    uint32_t record[33] = {
        [0] = 0xe0000000u,
        [1] = header,
        [2] = source_word,
        [3] = 0xa6887610u,
        [4] = 0x22220e80u,
        [17] = 0x3f800000u,
        [18] = 0x3f800000u,
        [19] = 0x3f800000u,
        [20] = 0x3f800000u,
    };
    for (unsigned i = 0; i < 8u; i++)
        record[5u + i] = test_float_word(vertices[i]);
    for (unsigned vertex = 0; vertex < 4u; vertex++) {
        unsigned attribute = 21u + vertex * 3u;
        record[attribute] = vertex_colour;
        record[attribute + 1u] = test_float_word(uv[vertex * 2u]);
        record[attribute + 2u] = test_float_word(uv[vertex * 2u + 1u]);
    }
    for (unsigned i = 0; i < 33u; i++)
        test_gpu_write32(m, object + record_offset + i * 4u, record[i]);

    const float lower_bias = 0.468505859375f;
    const float upper_bias = 0.531494140625f;
    uint32_t guard_left = (uint32_t)(x0 + lower_bias);
    uint32_t guard_top = (uint32_t)(y0 + lower_bias);
    uint32_t guard_right = (uint32_t)(x1 + upper_bias);
    uint32_t guard_bottom = (uint32_t)(y1 + upper_bias);
    uint32_t clip_left = guard_left & ~7u;
    uint32_t clip_right = (guard_right + 7u) & ~7u;
    uint32_t clip_top = guard_top & ~15u;
    uint32_t clip_bottom = (guard_bottom + 15u) & ~15u;
    uint32_t tile_x0 = clip_left / 8u;
    uint32_t tile_x1 = clip_right / 8u - 1u;
    uint32_t tile_y0 = clip_top / 16u;
    uint32_t tile_y1 = clip_bottom / 16u - 1u;
    uint32_t tile_count = (tile_x1 - tile_x0 + 1u) *
                          (tile_y1 - tile_y0 + 1u);
    uint32_t tile_index = 0u;
    for (uint32_t y = tile_y0; y <= tile_y1; y++) {
        for (uint32_t x = tile_x0; x <= tile_x1; x++) {
            uint32_t code = (y << 8) | x;
            if (tile_index + 1u == tile_count) code |= 0x80000000u;
            test_gpu_write32(m, region + tile_index * 8u, code);
            test_gpu_write32(m, region + tile_index * 8u + 4u, list);
            tile_index++;
        }
    }

    m->bus.write32(m->bus.ctx, MBX_BASE + REG_RGNBASE, region);
    m->bus.write32(m->bus.ctx, MBX_BASE + REG_OBJBASE, object);
    m->bus.write32(m->bus.ctx, MBX_BASE + REG_PIXSAMP, 0x00020007u);
    m->bus.write32(m->bus.ctx, MBX_BASE + REG_FBCTL, 6u);
    m->bus.write32(m->bus.ctx, MBX_BASE + REG_FBXCLIP,
                   (clip_right << 16) | clip_left);
    m->bus.write32(m->bus.ctx, MBX_BASE + REG_FBYCLIP,
                   (clip_bottom << 16) | clip_top);
    m->bus.write32(m->bus.ctx, MBX_BASE + REG_FBSTART, target);
    m->bus.write32(m->bus.ctx, MBX_BASE + REG_FBSTRIDE, 320u);
}

static void test_compact_opaque_blit_copy(void) {
    enum {
        LEFT = 8u, TOP = 16u, WIDTH = 16u, HEIGHT = 8u,
        SOURCE_STRIDE = 0x40u, TARGET_STRIDE = 0x500u,
    };
    const uint32_t table0 = 0x08003000u;
    const uint32_t table2 = 0x08004000u;
    const uint32_t table3 = 0x08005000u;
    const uint32_t region = 0x00001000u;
    const uint32_t object = 0x00014000u;
    const uint32_t source = 0x00b72000u;
    const uint32_t target = 0x00c98000u;
    const uint32_t region_pa = 0x08010000u;
    const uint32_t object_pa = 0x08014000u;
    const uint32_t source_pa = 0x08020000u;
    const uint32_t target_pa = 0x080c0000u;

    s5l8900_t m;
    CHECK(s5l8900_init(&m, RAM_BASE, RAM_SIZE),
          "compact blit-copy machine init failed");
    if (!m.ram) return;
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_GART0, table0);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_GART2, table2);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_GART3, table3);
    test_map_gpu_page(&m, table0, region, region_pa);
    test_map_gpu_page(&m, table0, object, object_pa);
    test_map_gpu_page(&m, table2, source, source_pa);
    uint32_t target_last = target +
        (TOP + HEIGHT - 1u) * TARGET_STRIDE + (LEFT + WIDTH) * 4u - 1u;
    for (uint32_t page = target; page <= (target_last & ~0xfffu);
         page += 0x1000u)
        test_map_gpu_page(&m, table3, page, target_pa + (page - target));

    test_write_compact_blit_copy(&m, region, object, source, target,
                                 0x0e8u, LEFT, TOP, WIDTH, HEIGHT,
                                 WIDTH, HEIGHT, SOURCE_STRIDE, 0u);
    for (uint32_t y = 0; y < HEIGHT; y++)
        for (uint32_t x = 0; x < WIDTH; x++)
            test_gpu_write32(&m, source + y * SOURCE_STRIDE + x * 4u,
                             test_sprite_source_pixel(x, y));
    for (uint32_t y = 0; y < HEIGHT; y++)
        for (uint32_t x = 0; x < WIDTH; x++)
            test_gpu_write32(&m,
                target + (TOP + y) * TARGET_STRIDE + (LEFT + x) * 4u,
                0xff102030u);
    uint32_t before = target + TOP * TARGET_STRIDE + (LEFT - 1u) * 4u;
    uint32_t after = target + (TOP + HEIGHT) * TARGET_STRIDE + LEFT * 4u;
    test_gpu_write32(&m, before, 0x11223344u);
    test_gpu_write32(&m, after, 0x55667788u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
    uint32_t mismatches = 0u;
    for (uint32_t y = 0; y < HEIGHT; y++) {
        struct test_bilinear_axis y_axis;
        bool y_ok = test_bilinear_axis(
            (float)TOP, (float)HEIGHT, 0.0f, (float)HEIGHT - 0.5f,
            TOP + y, HEIGHT, &y_axis);
        for (uint32_t x = 0; x < WIDTH; x++) {
            struct test_bilinear_axis x_axis;
            bool x_ok = test_bilinear_axis(
                (float)LEFT, (float)WIDTH, 0.0f, (float)WIDTH - 0.5f,
                LEFT + x, WIDTH, &x_axis);
            uint32_t expected = x_ok && y_ok
                ? test_bilinear_sprite_pixel(&x_axis, &y_axis) : 0u;
            uint32_t actual = test_gpu_read32(&m,
                target + (TOP + y) * TARGET_STRIDE + (LEFT + x) * 4u);
            mismatches += !x_ok || !y_ok || actual != expected;
        }
    }
    CHECK(mismatches == 0u,
          "compact filtered blit-copy mismatched %u pixels", mismatches);
    CHECK(test_gpu_read32(&m, before) == 0x11223344u &&
          test_gpu_read32(&m, after) == 0x55667788u,
          "compact blit-copy changed a pixel outside its rectangle");
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0x4cu,
          "compact blit-copy did not raise all three completion events");
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_ACK, 0x4cu);

    /* Relocating the selected record and copying a transparent,
     * non-premultiplied word prove this is pointer/opaque-copy semantics,
     * not a literal +0xe8 packet or an accidental source-over path. */
    test_write_compact_blit_copy(&m, region, object, source, target,
                                 0x300u, LEFT, TOP, WIDTH, HEIGHT,
                                 WIDTH, HEIGHT, SOURCE_STRIDE, 0u);
    for (uint32_t y = 0; y < HEIGHT; y++)
        for (uint32_t x = 0; x < WIDTH; x++) {
            test_gpu_write32(&m, source + y * SOURCE_STRIDE + x * 4u,
                             0x00112233u);
            test_gpu_write32(&m,
                target + (TOP + y) * TARGET_STRIDE + (LEFT + x) * 4u,
                0xffaabbccu);
        }
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
    mismatches = 0u;
    for (uint32_t y = 0; y < HEIGHT; y++)
        for (uint32_t x = 0; x < WIDTH; x++)
            mismatches += test_gpu_read32(&m,
                target + (TOP + y) * TARGET_STRIDE + (LEFT + x) * 4u) !=
                    0x00112233u;
    CHECK(mismatches == 0u,
          "relocated compact opaque copy mismatched %u pixels", mismatches);
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0x4cu,
          "relocated compact blit-copy did not complete");
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_ACK, 0x4cu);

    uint32_t first = target + TOP * TARGET_STRIDE + LEFT * 4u;
    uint32_t last = target + (TOP + HEIGHT - 1u) * TARGET_STRIDE +
                    (LEFT + WIDTH - 1u) * 4u;
    uint32_t sampler = object + 0x300u + 3u * 4u;
    uint32_t sampler_value = test_gpu_read32(&m, sampler);
    test_gpu_write32(&m, first, 0x89abcdefu);
    test_gpu_write32(&m, sampler, sampler_value ^ 1u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
    CHECK(test_gpu_read32(&m, first) == 0x89abcdefu &&
          m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "unknown compact sampler was not rejected atomically");
    test_gpu_write32(&m, sampler, sampler_value);

    uint32_t second_u = object + 0x300u + 25u * 4u;
    uint32_t second_u_value = test_gpu_read32(&m, second_u);
    test_gpu_write32(&m, first, 0x89abcdefu);
    test_gpu_write32(&m, second_u, second_u_value ^ 1u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
    CHECK(test_gpu_read32(&m, first) == 0x89abcdefu &&
          m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "inconsistent compact UV was not rejected atomically");
    test_gpu_write32(&m, second_u, second_u_value);

    uint32_t boundary_setup = object + 0x84u;
    test_gpu_write32(&m, first, 0x89abcdefu);
    test_gpu_write32(&m, boundary_setup, 1u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
    CHECK(test_gpu_read32(&m, first) == 0x89abcdefu &&
          m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "unknown compact boundary setup was not rejected atomically");
    test_gpu_write32(&m, boundary_setup, 0u);

    uint32_t region_word = test_gpu_read32(&m, region);
    test_gpu_write32(&m, first, 0x89abcdefu);
    test_gpu_write32(&m, region, region_word ^ 1u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
    CHECK(test_gpu_read32(&m, first) == 0x89abcdefu &&
          m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "inconsistent compact tile was not rejected atomically");
    test_gpu_write32(&m, region, region_word);

    uint32_t source_pte_address = table2 +
        (((source >> 12) & 0x3ffu) * 4u);
    uint32_t source_pte = m.bus.read32(m.bus.ctx, source_pte_address);
    test_gpu_write32(&m, first, 0x89abcdefu);
    test_gpu_write32(&m, last, 0x76543210u);
    m.bus.write32(m.bus.ctx, source_pte_address, 0u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
    m.bus.write32(m.bus.ctx, source_pte_address, source_pte);
    CHECK(test_gpu_read32(&m, first) == 0x89abcdefu &&
          test_gpu_read32(&m, last) == 0x76543210u &&
          m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "missing compact source PTE partially committed or completed");

    uint32_t last_page = last & ~0xfffu;
    uint32_t target_pte_address = table3 +
        (((last_page >> 12) & 0x3ffu) * 4u);
    uint32_t target_pte = m.bus.read32(m.bus.ctx, target_pte_address);
    test_gpu_write32(&m, first, 0x89abcdefu);
    test_gpu_write32(&m, last, 0x76543210u);
    m.bus.write32(m.bus.ctx, target_pte_address, 0u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
    m.bus.write32(m.bus.ctx, target_pte_address, target_pte);
    CHECK(test_gpu_read32(&m, first) == 0x89abcdefu &&
          test_gpu_read32(&m, last) == 0x76543210u &&
          m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "late missing compact target PTE partially committed or completed");

    s5l8900_free(&m);
}

/* The post-unlock recovery checkpoint retained this exact compact producer
 * geometry.  It expands one texture column across the full surface while
 * independently reducing 222 source rows to 37 output rows.  Rejecting the
 * mixed-axis scale leaves 3DIdle false and makes AppleMBX retry recovery
 * forever, so exercise the rendered pixels and the transaction boundary. */
static void test_compact_column_resample(void) {
    enum {
        LEFT = 0u, TOP = 42u, WIDTH = 320u, HEIGHT = 37u,
        SOURCE_WIDTH = 1u, SOURCE_HEIGHT = 222u,
        SOURCE_STRIDE = 0x20u, TEXTURE_WIDTH = 8u,
        TEXTURE_HEIGHT = 256u, TARGET_STRIDE = 0x500u,
    };
    const uint32_t table0 = 0x08003000u;
    const uint32_t table2 = 0x08004000u;
    const uint32_t region = 0x00001000u;
    const uint32_t object = 0x00014000u;
    const uint32_t source = 0x00b74000u;
    const uint32_t target = 0x00a3d000u;
    const uint32_t region_pa = 0x08010000u;
    const uint32_t object_pa = 0x08014000u;
    const uint32_t source_pa = 0x08020000u;
    const uint32_t target_pa = 0x080c0000u;

    s5l8900_t m;
    CHECK(s5l8900_init(&m, RAM_BASE, RAM_SIZE),
          "compact column-resample machine init failed");
    if (!m.ram) return;
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_GART0, table0);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_GART2, table2);
    test_map_gpu_page(&m, table0, region, region_pa);
    test_map_gpu_page(&m, table0, object, object_pa);

    uint32_t source_page0 = source & ~0xfffu;
    uint32_t source_last = source +
        (TEXTURE_HEIGHT - 1u) * SOURCE_STRIDE + TEXTURE_WIDTH * 4u - 1u;
    for (uint32_t page = source_page0;
         page <= (source_last & ~0xfffu); page += 0x1000u)
        test_map_gpu_page(&m, table2, page,
                          source_pa + (page - source_page0));

    uint32_t before = target + (TOP - 1u) * TARGET_STRIDE;
    uint32_t after = target + (TOP + HEIGHT) * TARGET_STRIDE;
    uint32_t target_page0 = target & ~0xfffu;
    uint32_t target_last = after + 3u;
    for (uint32_t page = target_page0;
         page <= (target_last & ~0xfffu); page += 0x1000u)
        test_map_gpu_page(&m, table2, page,
                          target_pa + (page - target_page0));

    const uint32_t record_offset = 0x300u;
    test_write_compact_blit_copy(
        &m, region, object, source, target, record_offset,
        LEFT, TOP, WIDTH, HEIGHT, SOURCE_WIDTH, SOURCE_HEIGHT,
        SOURCE_STRIDE, 0xff000000u);
    for (uint32_t y = 0u; y < TEXTURE_HEIGHT; y++)
        for (uint32_t x = 0u; x < TEXTURE_WIDTH; x++)
            test_gpu_write32(&m, source + y * SOURCE_STRIDE + x * 4u,
                             test_sprite_source_pixel(x, y));
    for (uint32_t y = 0u; y < HEIGHT; y++)
        for (uint32_t x = 0u; x < WIDTH; x++)
            test_gpu_write32(&m,
                target + (TOP + y) * TARGET_STRIDE + (LEFT + x) * 4u,
                0xff102030u + y * 0x00010101u + x);
    test_gpu_write32(&m, before, 0x11223344u);
    test_gpu_write32(&m, after, 0x55667788u);

    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
    uint32_t mismatches = 0u;
    for (uint32_t y = 0u; y < HEIGHT; y++) {
        struct test_bilinear_axis y_axis;
        bool y_ok = test_bilinear_axis(
            (float)TOP, (float)HEIGHT, 0.0f,
            (float)SOURCE_HEIGHT - 0.5f,
            TOP + y, TEXTURE_HEIGHT, &y_axis);
        for (uint32_t x = 0u; x < WIDTH; x++) {
            struct test_bilinear_axis x_axis;
            bool x_ok = test_bilinear_axis(
                (float)LEFT, (float)WIDTH, 0.0f,
                (float)SOURCE_WIDTH - 0.5f,
                LEFT + x, TEXTURE_WIDTH, &x_axis);
            uint32_t expected = x_ok && y_ok
                ? test_bilinear_sprite_pixel(&x_axis, &y_axis) : 0u;
            uint32_t actual = test_gpu_read32(&m,
                target + (TOP + y) * TARGET_STRIDE + (LEFT + x) * 4u);
            mismatches += !x_ok || !y_ok || actual != expected;
        }
    }
    CHECK(mismatches == 0u,
          "compact column resample mismatched %u pixels", mismatches);
    CHECK(test_gpu_read32(&m, before) == 0x11223344u &&
          test_gpu_read32(&m, after) == 0x55667788u,
          "compact column resample changed a pixel outside its rectangle");
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0x4cu,
          "compact column resample did not raise all completion events");
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_ACK, 0x4cu);

    uint32_t first = target + TOP * TARGET_STRIDE;
    uint32_t last = target + (TOP + HEIGHT - 1u) * TARGET_STRIDE +
                    (WIDTH - 1u) * 4u;
    uint32_t first_u1 = object + record_offset + 25u * 4u;
    uint32_t second_u1 = object + record_offset + 31u * 4u;
    uint32_t captured_u1 = test_float_word(0.5f / (float)TEXTURE_WIDTH);
    uint32_t wider_u1 = test_float_word(1.5f / (float)TEXTURE_WIDTH);
    CHECK(test_gpu_read32(&m, first_u1) == captured_u1 &&
          test_gpu_read32(&m, second_u1) == captured_u1,
          "compact column fixture lost its half-texel width");
    test_gpu_write32(&m, first, 0x89abcdefu);
    test_gpu_write32(&m, last, 0x76543210u);
    test_gpu_write32(&m, first_u1, wider_u1);
    test_gpu_write32(&m, second_u1, wider_u1);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
    CHECK(test_gpu_read32(&m, first) == 0x89abcdefu &&
          test_gpu_read32(&m, last) == 0x76543210u &&
          m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "wider mixed-axis compact scale was not rejected atomically");
    test_gpu_write32(&m, first_u1, captured_u1);
    test_gpu_write32(&m, second_u1, captured_u1);

    uint32_t late_source = source +
        (SOURCE_HEIGHT - 1u) * SOURCE_STRIDE + (SOURCE_WIDTH - 1u) * 4u;
    uint32_t source_pte_address = table2 +
        (((late_source >> 12) & 0x3ffu) * 4u);
    uint32_t source_pte = m.bus.read32(m.bus.ctx, source_pte_address);
    test_gpu_write32(&m, first, 0x89abcdefu);
    test_gpu_write32(&m, last, 0x76543210u);
    m.bus.write32(m.bus.ctx, source_pte_address, 0u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
    m.bus.write32(m.bus.ctx, source_pte_address, source_pte);
    CHECK(test_gpu_read32(&m, first) == 0x89abcdefu &&
          test_gpu_read32(&m, last) == 0x76543210u &&
          m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "missing column source PTE partially committed or completed");

    uint32_t target_pte_address = table2 +
        ((((last & ~0xfffu) >> 12) & 0x3ffu) * 4u);
    uint32_t target_pte = m.bus.read32(m.bus.ctx, target_pte_address);
    test_gpu_write32(&m, first, 0x89abcdefu);
    test_gpu_write32(&m, last, 0x76543210u);
    m.bus.write32(m.bus.ctx, target_pte_address, 0u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
    m.bus.write32(m.bus.ctx, target_pte_address, target_pte);
    CHECK(test_gpu_read32(&m, first) == 0x89abcdefu &&
          test_gpu_read32(&m, last) == 0x76543210u &&
          m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "missing column target PTE partially committed or completed");

    s5l8900_free(&m);
}

/* Unlocking into Safari's retained error alert produced these two exact
 * compact-copy records.  Unlike the older compact fixtures, their texture
 * control uses the 0x0e layout while their UV endpoints remain half a texel
 * inside a 320x356 source.  Both destination rectangles are uniform
 * minifications to subpixel edges.  They are two phases of one producer
 * family, not permission to treat every full-extent compact packet as a
 * filtered transform. */
struct compact_uniform_minification_capture {
    const char *name;
    uint32_t target;
    uint32_t boundary_left, boundary_top, boundary_right, boundary_bottom;
    uint32_t raster_left, raster_top, raster_right, raster_bottom;
    uint32_t xclip, yclip;
    uint32_t record[33];
};

static const struct compact_uniform_minification_capture
compact_uniform_minification_captures[] = {
    {
        .name = "Safari alert compact minification on CLCD surface",
        .target = 0x00897000u,
        .boundary_left = 120u, .boundary_top = 200u,
        .boundary_right = 200u, .boundary_bottom = 289u,
        .raster_left = 120u, .raster_top = 200u,
        .raster_right = 200u, .raster_bottom = 289u,
        .xclip = 0x00c80078u, .yclip = 0x013000c0u,
        .record = {
            0xe0000000u, 0xa6618000u, 0x0e514a21u, 0xa6887610u,
            0x22220e80u,
            0x42f08777u, 0x434843bcu, 0x4347bc44u, 0x434843bcu,
            0x42f08777u, 0x43905684u, 0x4347bc44u, 0x43905684u,
            0u, 0u, 0u, 0u,
            0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
            0xff000000u, 0u, 0u,
            0xff000000u, 0x3f1fc000u, 0u,
            0xff000000u, 0u, 0x3f31c000u,
            0xff000000u, 0x3f1fc000u, 0x3f31c000u,
        },
    },
    {
        .name = "Safari alert compact minification on transition surface",
        .target = 0x00bd1000u,
        .boundary_left = 115u, .boundary_top = 195u,
        .boundary_right = 205u, .boundary_bottom = 294u,
        .raster_left = 116u, .raster_top = 196u,
        .raster_right = 204u, .raster_bottom = 294u,
        .xclip = 0x00d00070u, .yclip = 0x013000c0u,
        .record = {
            0xe0000000u, 0xa6618000u, 0x0e514a21u, 0xa6887610u,
            0x22220e80u,
            0x42e7fb15u, 0x4343fd8bu, 0x434c0275u, 0x4343fd8bu,
            0x42e7fb15u, 0x4392f4b5u, 0x434c0275u, 0x4392f4b5u,
            0u, 0u, 0u, 0u,
            0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
            0xff000000u, 0u, 0u,
            0xff000000u, 0x3f1fc000u, 0u,
            0xff000000u, 0u, 0x3f31c000u,
            0xff000000u, 0x3f1fc000u, 0x3f31c000u,
        },
    },
};

static uint32_t test_compact_capture_gart(uint32_t gpu_page,
                                          uint32_t table2,
                                          uint32_t table3) {
    return gpu_page < 0x00c00000u ? table2 : table3;
}

static void test_compact_full_extent_uniform_minification(void) {
    enum {
        SOURCE_WIDTH = 320u, SOURCE_HEIGHT = 356u,
        SOURCE_STRIDE = 0x500u, TEXTURE_HEIGHT = 512u,
        TARGET_STRIDE = 0x500u,
    };
    const uint32_t table0 = 0x08003000u;
    const uint32_t table2 = 0x08004000u;
    const uint32_t table3 = 0x08005000u;
    const uint32_t region = 0x00001000u;
    const uint32_t object = 0x00014000u;
    const uint32_t source = 0x00a51080u;
    const uint32_t region_pa = 0x08010000u;
    const uint32_t object_pa = 0x08014000u;
    const uint32_t source_pa = 0x08020000u;
    const uint32_t target_pa = 0x08100000u;
    const uint32_t record_offset = 0x0e8u;

    for (unsigned capture_index = 0;
         capture_index < sizeof compact_uniform_minification_captures /
                             sizeof compact_uniform_minification_captures[0];
         capture_index++) {
        const struct compact_uniform_minification_capture *capture =
            &compact_uniform_minification_captures[capture_index];
        s5l8900_t m;
        CHECK(s5l8900_init(&m, RAM_BASE, RAM_SIZE),
              "%s machine init failed", capture->name);
        if (!m.ram) continue;
        m.bus.write32(m.bus.ctx, MBX_BASE + REG_GART0, table0);
        m.bus.write32(m.bus.ctx, MBX_BASE + REG_GART2, table2);
        m.bus.write32(m.bus.ctx, MBX_BASE + REG_GART3, table3);
        test_map_gpu_page(&m, table0, region, region_pa);
        test_map_gpu_page(&m, table0, object, object_pa);

        uint32_t source_page0 = source & ~0xfffu;
        uint32_t source_last = source +
            (SOURCE_HEIGHT - 1u) * SOURCE_STRIDE + SOURCE_WIDTH * 4u - 1u;
        for (uint32_t page = source_page0;
             page <= (source_last & ~0xfffu); page += 0x1000u)
            test_map_gpu_page(&m, table2, page,
                              source_pa + (page - source_page0));

        uint32_t target_first = capture->target +
            capture->boundary_top * TARGET_STRIDE;
        uint32_t target_last = capture->target +
            (capture->boundary_bottom - 1u) * TARGET_STRIDE +
            capture->boundary_right * 4u - 1u;
        uint32_t target_page0 = target_first & ~0xfffu;
        for (uint32_t page = target_page0;
             page <= (target_last & ~0xfffu); page += 0x1000u) {
            uint32_t table = test_compact_capture_gart(page, table2, table3);
            test_map_gpu_page(&m, table, page,
                              target_pa + (page - target_page0));
        }

        test_write_compact_blit_copy(
            &m, region, object, source, capture->target, record_offset,
            capture->boundary_left, capture->boundary_top,
            capture->boundary_right - capture->boundary_left,
            capture->boundary_bottom - capture->boundary_top,
            SOURCE_WIDTH, SOURCE_HEIGHT, SOURCE_STRIDE, 0xff000000u);
        for (unsigned i = 0; i < 33u; i++)
            test_gpu_write32(&m, object + record_offset + i * 4u,
                             capture->record[i]);

        CHECK(test_gpu_read32(&m, object + 0x70u) == 0x6120003au &&
              m.bus.read32(m.bus.ctx, MBX_BASE + REG_FBXCLIP) ==
                  capture->xclip &&
              m.bus.read32(m.bus.ctx, MBX_BASE + REG_FBYCLIP) ==
                  capture->yclip,
              "%s lost its exact object pointer or clip registers",
              capture->name);

        for (uint32_t y = 0u; y < SOURCE_HEIGHT; y++)
            for (uint32_t x = 0u; x < SOURCE_WIDTH; x++)
                test_gpu_write32(&m,
                    source + y * SOURCE_STRIDE + x * 4u,
                    test_sprite_source_pixel(x, y));

        const uint32_t untouched = 0x11223344u;
        for (uint32_t y = capture->boundary_top;
             y < capture->boundary_bottom; y++)
            for (uint32_t x = capture->boundary_left;
                 x < capture->boundary_right; x++)
                test_gpu_write32(&m,
                    capture->target + y * TARGET_STRIDE + x * 4u,
                    untouched);
        uint32_t outside = capture->target +
            capture->boundary_top * TARGET_STRIDE +
            (capture->boundary_left - 1u) * 4u;
        test_gpu_write32(&m, outside, 0x55667788u);

        m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
        float x0 = test_float_value(capture->record[5]);
        float y0 = test_float_value(capture->record[6]);
        float x1 = test_float_value(capture->record[7]);
        float y1 = test_float_value(capture->record[10]);
        uint32_t mismatches = 0u;
        for (uint32_t y = capture->boundary_top;
             y < capture->boundary_bottom; y++) {
            for (uint32_t x = capture->boundary_left;
                 x < capture->boundary_right; x++) {
                uint32_t expected = untouched;
                bool covered = x >= capture->raster_left &&
                               x < capture->raster_right &&
                               y >= capture->raster_top &&
                               y < capture->raster_bottom;
                if (covered) {
                    struct test_bilinear_axis x_axis, y_axis;
                    bool axes_ok = test_bilinear_axis(
                        x0, x1 - x0, 0.0f, (float)SOURCE_WIDTH - 0.5f,
                        x, SOURCE_WIDTH, &x_axis) &&
                        test_bilinear_axis(
                            y0, y1 - y0, 0.0f,
                            (float)SOURCE_HEIGHT - 0.5f,
                            y, TEXTURE_HEIGHT, &y_axis);
                    if (!axes_ok) {
                        mismatches++;
                        continue;
                    }
                    expected = test_bilinear_sprite_pixel(&x_axis, &y_axis);
                }
                uint32_t actual = test_gpu_read32(&m,
                    capture->target + y * TARGET_STRIDE + x * 4u);
                mismatches += actual != expected;
            }
        }
        CHECK(mismatches == 0u,
              "%s mismatched %u filtered or untouched pixels",
              capture->name, mismatches);
        CHECK(test_gpu_read32(&m, outside) == 0x55667788u,
              "%s changed a pixel outside its boundary", capture->name);
        CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0x4cu,
              "%s did not raise all completion events", capture->name);
        m.bus.write32(m.bus.ctx, MBX_BASE + REG_ACK, 0x4cu);

        uint32_t first = capture->target +
            capture->raster_top * TARGET_STRIDE +
            capture->raster_left * 4u;
        uint32_t last = capture->target +
            (capture->raster_bottom - 1u) * TARGET_STRIDE +
            (capture->raster_right - 1u) * 4u;
        uint32_t y1_word0 = object + record_offset + 10u * 4u;
        uint32_t y1_word1 = object + record_offset + 12u * 4u;
        uint32_t captured_y1 = capture->record[10];
        uint32_t nonuniform_y1 =
            test_float_word(test_float_value(captured_y1) + 1.0f);
        test_gpu_write32(&m, first, 0x89abcdefu);
        test_gpu_write32(&m, last, 0x76543210u);
        test_gpu_write32(&m, y1_word0, nonuniform_y1);
        test_gpu_write32(&m, y1_word1, nonuniform_y1);
        m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
        CHECK(test_gpu_read32(&m, first) == 0x89abcdefu &&
              test_gpu_read32(&m, last) == 0x76543210u &&
              m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
              "%s nonuniform scale partially committed or completed",
              capture->name);
        m.bus.write32(m.bus.ctx, MBX_BASE + REG_ACK, 0x4cu);
        test_gpu_write32(&m, y1_word0, captured_y1);
        test_gpu_write32(&m, y1_word1, captured_y1);

        uint32_t v1_word0 = object + record_offset + 29u * 4u;
        uint32_t v1_word1 = object + record_offset + 32u * 4u;
        uint32_t captured_v1 = capture->record[29];
        uint32_t full_v1 = test_float_word(
            (float)SOURCE_HEIGHT / (float)TEXTURE_HEIGHT);
        test_gpu_write32(&m, first, 0x89abcdefu);
        test_gpu_write32(&m, last, 0x76543210u);
        test_gpu_write32(&m, v1_word0, full_v1);
        test_gpu_write32(&m, v1_word1, full_v1);
        m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
        CHECK(test_gpu_read32(&m, first) == 0x89abcdefu &&
              test_gpu_read32(&m, last) == 0x76543210u &&
              m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
              "%s non-half-texel UV endpoint partially committed or completed",
              capture->name);
        m.bus.write32(m.bus.ctx, MBX_BASE + REG_ACK, 0x4cu);
        test_gpu_write32(&m, v1_word0, captured_v1);
        test_gpu_write32(&m, v1_word1, captured_v1);

        struct test_bilinear_axis late_source_x, late_source_y;
        bool late_source_ok = test_bilinear_axis(
            x0, x1 - x0, 0.0f, (float)SOURCE_WIDTH - 0.5f,
            capture->raster_right - 1u, SOURCE_WIDTH, &late_source_x) &&
            test_bilinear_axis(
                y0, y1 - y0, 0.0f, (float)SOURCE_HEIGHT - 0.5f,
                capture->raster_bottom - 1u, TEXTURE_HEIGHT,
                &late_source_y);
        CHECK(late_source_ok, "%s has no final bilinear source sample",
              capture->name);
        uint32_t late_source = source +
            late_source_y.second * SOURCE_STRIDE + late_source_x.second * 4u;
        uint32_t source_pte_address = table2 +
            (((late_source >> 12) & 0x3ffu) * 4u);
        uint32_t source_pte = m.bus.read32(m.bus.ctx, source_pte_address);
        test_gpu_write32(&m, first, 0x89abcdefu);
        test_gpu_write32(&m, last, 0x76543210u);
        m.bus.write32(m.bus.ctx, source_pte_address, 0u);
        m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
        m.bus.write32(m.bus.ctx, source_pte_address, source_pte);
        CHECK(test_gpu_read32(&m, first) == 0x89abcdefu &&
              test_gpu_read32(&m, last) == 0x76543210u &&
              m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
              "%s missing source PTE partially committed or completed",
              capture->name);
        m.bus.write32(m.bus.ctx, MBX_BASE + REG_ACK, 0x4cu);

        uint32_t target_page = last & ~0xfffu;
        uint32_t target_table = test_compact_capture_gart(
            target_page, table2, table3);
        uint32_t target_pte_address = target_table +
            (((target_page >> 12) & 0x3ffu) * 4u);
        uint32_t target_pte = m.bus.read32(m.bus.ctx, target_pte_address);
        test_gpu_write32(&m, first, 0x89abcdefu);
        test_gpu_write32(&m, last, 0x76543210u);
        m.bus.write32(m.bus.ctx, target_pte_address, 0u);
        m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
        m.bus.write32(m.bus.ctx, target_pte_address, target_pte);
        CHECK(test_gpu_read32(&m, first) == 0x89abcdefu &&
              test_gpu_read32(&m, last) == 0x76543210u &&
              m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
              "%s missing target PTE partially committed or completed",
              capture->name);

        s5l8900_free(&m);
    }
}

/* The retained unlock transition clipped this compact 320x60 texture to the
 * single row [185, 186).  Its subpixel destination begins at y=185.770721, so
 * no pixel centre survives the scissor.  The command is still a valid draw
 * whose completion makes AppleMBX 3DIdle; it must not touch the target, and a
 * malformed source, target, or tile mapping must remain rejected. */
static void test_compact_clipped_zero_coverage(void) {
    enum {
        LEFT = 120u, TOP = 185u, RIGHT = 200u, BOTTOM = 186u,
        SOURCE_STRIDE = 0x500u, TEXTURE_HEIGHT = 64u,
        TARGET_STRIDE = 0x500u,
    };
    const uint32_t table0 = 0x08003000u;
    const uint32_t table2 = 0x08004000u;
    const uint32_t region = 0x00001000u;
    const uint32_t object = 0x00014000u;
    const uint32_t source = 0x00b56080u;
    const uint32_t target = 0x00998000u;
    const uint32_t region_pa = 0x08010000u;
    const uint32_t object_pa = 0x08014000u;
    const uint32_t source_pa = 0x08020000u;
    const uint32_t target_pa = 0x080c0000u;

    s5l8900_t m;
    CHECK(s5l8900_init(&m, RAM_BASE, RAM_SIZE),
          "clipped zero-coverage machine init failed");
    if (!m.ram) return;
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_GART0, table0);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_GART2, table2);
    test_map_gpu_page(&m, table0, region, region_pa);
    test_map_gpu_page(&m, table0, object, object_pa);

    uint32_t source_page0 = source & ~0xfffu;
    uint32_t source_last = source + TEXTURE_HEIGHT * SOURCE_STRIDE - 1u;
    for (uint32_t page = source_page0;
         page <= (source_last & ~0xfffu); page += 0x1000u)
        test_map_gpu_page(&m, table2, page,
                          source_pa + (page - source_page0));

    uint32_t first = target + TOP * TARGET_STRIDE + LEFT * 4u;
    uint32_t last = target + (BOTTOM - 1u) * TARGET_STRIDE +
                    (RIGHT - 1u) * 4u;
    uint32_t target_page0 = first & ~0xfffu;
    for (uint32_t page = target_page0;
         page <= (last & ~0xfffu); page += 0x1000u)
        test_map_gpu_page(&m, table2, page,
                          target_pa + (page - target_page0));

    for (uint32_t off = 0u; off < 0x500u; off += 4u)
        test_gpu_write32(&m, object + off, 0u);
    for (unsigned i = 0; i < 26u; i++)
        test_gpu_write32(&m, object + i * 4u, 0x5a000000u ^ i);

    uint32_t list = object + 0x68u;
    test_gpu_write32(&m, list, 0x60200020u);
    test_gpu_write32(&m, list + 4u, 0x6020002du);
    test_gpu_write32(&m, list + 8u, 0x6120003au);
    test_gpu_write32(&m, list + 12u, 0xf0000000u);

    static const struct {
        uint16_t off;
        uint32_t value;
    } boundary_fixed[] = {
        {0x080u, 0x22206f80u}, {0x088u, 0x45800000u},
        {0x094u, 0x45800000u}, {0x098u, 0x45800000u},
        {0x09cu, 0x45800000u}, {0x0b4u, 0x22207f80u},
    };
    for (unsigned i = 0;
         i < sizeof boundary_fixed / sizeof boundary_fixed[0]; i++)
        test_gpu_write32(&m, object + boundary_fixed[i].off,
                         boundary_fixed[i].value);
    const uint32_t boundary[8] = {
        LEFT, BOTTOM, LEFT, TOP, RIGHT, BOTTOM, RIGHT, TOP,
    };
    for (unsigned i = 0; i < 8u; i++)
        test_gpu_write32(&m, object + 0x0b8u + i * 4u,
                         test_float_word((float)boundary[i]));

    const uint32_t record_offset = 0x0e8u;
    const uint32_t record[33] = {
        0xe0000000u, 0xa6318000u, 0x8e516ac1u, 0xa6887610u,
        0x22220e80u,
        0x42f11efeu, 0x4339c54eu, 0x43477082u, 0x4339c54eu,
        0x42f11efeu, 0x43488f7eu, 0x43477082u, 0x43488f7eu,
        0u, 0u, 0u, 0u,
        0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
        0xff000000u, 0u, 0u,
        0xff000000u, 0x3f1fc000u, 0u,
        0xff000000u, 0u, 0x3f6e0000u,
        0xff000000u, 0x3f1fc000u, 0x3f6e0000u,
    };
    for (unsigned i = 0; i < 33u; i++)
        test_gpu_write32(&m, object + record_offset + i * 4u, record[i]);

    const uint32_t tile_x0 = 15u, tile_x1 = 24u, tile_y = 11u;
    uint32_t tile_count = tile_x1 - tile_x0 + 1u;
    for (uint32_t i = 0; i < tile_count; i++) {
        uint32_t code = (tile_y << 8) | (tile_x0 + i);
        if (i + 1u == tile_count) code |= 0x80000000u;
        test_gpu_write32(&m, region + i * 8u, code);
        test_gpu_write32(&m, region + i * 8u + 4u, list);
    }

    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RGNBASE, region);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_OBJBASE, object);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_PIXSAMP, 0x00020007u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBCTL, 6u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBXCLIP, 0x00c80078u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBYCLIP, 0x00c000b0u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBSTART, target);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBSTRIDE, 320u);

    test_gpu_write32(&m, first, 0x89abcdefu);
    test_gpu_write32(&m, last, 0x76543210u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
    CHECK(test_gpu_read32(&m, first) == 0x89abcdefu &&
          test_gpu_read32(&m, last) == 0x76543210u,
          "clipped zero-coverage draw changed its target row");
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0x4cu,
          "clipped zero-coverage draw did not raise all completion events");
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_ACK, 0x4cu);

    uint32_t late_source_page = source_last & ~0xfffu;
    uint32_t source_pte_address = table2 +
        (((late_source_page >> 12) & 0x3ffu) * 4u);
    uint32_t source_pte = m.bus.read32(m.bus.ctx, source_pte_address);
    m.bus.write32(m.bus.ctx, source_pte_address, 0u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
    m.bus.write32(m.bus.ctx, source_pte_address, source_pte);
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "missing zero-coverage source PTE raised completion");

    uint32_t target_pte_address = table2 +
        (((target_page0 >> 12) & 0x3ffu) * 4u);
    uint32_t target_pte = m.bus.read32(m.bus.ctx, target_pte_address);
    m.bus.write32(m.bus.ctx, target_pte_address, 0u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
    m.bus.write32(m.bus.ctx, target_pte_address, target_pte);
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "missing zero-coverage target PTE raised completion");

    uint32_t region_word = test_gpu_read32(&m, region);
    test_gpu_write32(&m, region, region_word ^ 1u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "malformed zero-coverage tile raised completion");
    test_gpu_write32(&m, region, region_word);

    s5l8900_free(&m);
}

static void test_write_solid_quad(s5l8900_t *m,
                                  uint32_t region,
                                  uint32_t object,
                                  uint32_t target,
                                  uint32_t solid_offset,
                                  float x0, float y0,
                                  float x1, float y1,
                                  uint32_t colour) {
    for (uint32_t off = 0u; off < 0x700u; off += 4u)
        test_gpu_write32(m, object + off, 0u);

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
        uint32_t value = i == 2u
            ? 0x0e500000u | (target >> 7) : background[i];
        test_gpu_write32(m, object + i * 4u, value);
    }

    uint32_t list = object + 0x68u;
    static const uint32_t fixed_list[2] = {0x60200020u, 0x6020002du};
    test_gpu_write32(m, list, fixed_list[0]);
    test_gpu_write32(m, list + 4u, fixed_list[1]);
    test_gpu_write32(m, list + 8u,
                     0x61200000u | (solid_offset / 4u));
    test_gpu_write32(m, list + 12u, 0xf0000000u);

    static const struct {
        uint16_t off;
        uint32_t value;
    } boundary_fixed[] = {
        {0x080u, 0x22206f80u}, {0x088u, 0x45800000u},
        {0x094u, 0x45800000u}, {0x098u, 0x45800000u},
        {0x09cu, 0x45800000u}, {0x0b4u, 0x22207f80u},
        {0x0e8u, 0xe0000000u}, {0x0f4u, 0xa6887610u},
        {0x0f8u, 0x22220e80u}, {0x12cu, 0x3f800000u},
        {0x130u, 0x3f800000u}, {0x134u, 0x3f800000u},
        {0x138u, 0x3f800000u}, {0x198u, 0xe0000000u},
        {0x19cu, 0x22200e80u}, {0x1d0u, 0x3f800000u},
        {0x1d4u, 0x3f800000u}, {0x1d8u, 0x3f800000u},
        {0x1dcu, 0x3f800000u},
    };
    for (unsigned i = 0;
         i < sizeof boundary_fixed / sizeof boundary_fixed[0]; i++)
        test_gpu_write32(m, object + boundary_fixed[i].off,
                         boundary_fixed[i].value);
    const float vertices[8] = {
        x0, y1, x0, y0, x1, y1, x1, y0,
    };
    for (unsigned i = 0; i < 8u; i++)
        test_gpu_write32(m, object + 0x0b8u + i * 4u,
                         test_float_word(vertices[i]));

    uint32_t main[33] = {
        [0] = 0xe0000000u,
        [1] = 0xa7718000u,
        [2] = 0x0e500000u | (target >> 7),
        [3] = 0xa6104620u,
        [4] = 0x22220e80u,
        [17] = 0x3f800000u,
        [18] = 0x3f800000u,
        [19] = 0x3f800000u,
        [20] = 0x3f800000u,
    };
    for (unsigned i = 0; i < 8u; i++) main[5u + i] =
        test_float_word(vertices[i]);
    for (unsigned vertex = 0; vertex < 4u; vertex++) {
        unsigned attribute = 21u + vertex * 3u;
        main[attribute] = colour;
        main[attribute + 1u] = test_float_word(
            vertices[vertex * 2u] / 1024.0f);
        main[attribute + 2u] = test_float_word(
            vertices[vertex * 2u + 1u] / 1024.0f);
    }
    for (unsigned i = 0; i < 33u; i++)
        test_gpu_write32(m, object + solid_offset + i * 4u, main[i]);

    static const uint32_t parameter_controls[4] = {
        0x22620ea0u, 0x46622ea0u, 0x66622ea0u, 0x82622ea0u
    };
    for (unsigned record = 0; record < 4u; record++) {
        uint32_t base = object + solid_offset +
                        (record + 1u) * 33u * 4u;
        for (unsigned i = 0; i < 33u; i++) {
            uint32_t value = 0u;
            if (i == 0u) value = 0xe0000000u;
            else if (i == 3u) value = 0x86084610u;
            else if (i == 4u) value = parameter_controls[record];
            else if (i >= 17u && i <= 20u) value = 0x3f800000u;
            else if (i >= 21u && (i - 21u) % 3u == 0u)
                value = 0xffffffffu;
            test_gpu_write32(m, base + i * 4u, value);
        }
    }

    const float lower_bias = 0.468505859375f;
    const float upper_bias = 0.531494140625f;
    uint32_t left = (uint32_t)(x0 + lower_bias);
    uint32_t top = (uint32_t)(y0 + lower_bias);
    uint32_t right = (uint32_t)(x1 + upper_bias);
    uint32_t bottom = (uint32_t)(y1 + upper_bias);
    uint32_t clip_left = left & ~7u;
    uint32_t clip_right = (right + 7u) & ~7u;
    uint32_t clip_top = top & ~15u;
    uint32_t clip_bottom = (bottom + 15u) & ~15u;
    uint32_t tile_x0 = clip_left / 8u;
    uint32_t tile_x1 = clip_right / 8u - 1u;
    uint32_t tile_y0 = clip_top / 16u;
    uint32_t tile_y1 = clip_bottom / 16u - 1u;
    uint32_t tile_count = (tile_x1 - tile_x0 + 1u) *
                          (tile_y1 - tile_y0 + 1u);
    uint32_t tile_index = 0u;
    for (uint32_t y = tile_y0; y <= tile_y1; y++) {
        for (uint32_t x = tile_x0; x <= tile_x1; x++) {
            uint32_t code = (y << 8) | x;
            if (tile_index + 1u == tile_count) code |= 0x80000000u;
            test_gpu_write32(m, region + tile_index * 8u, code);
            test_gpu_write32(m, region + tile_index * 8u + 4u, list);
            tile_index++;
        }
    }

    m->bus.write32(m->bus.ctx, MBX_BASE + REG_RGNBASE, region);
    m->bus.write32(m->bus.ctx, MBX_BASE + REG_OBJBASE, object);
    m->bus.write32(m->bus.ctx, MBX_BASE + REG_PIXSAMP, 0x00020007u);
    m->bus.write32(m->bus.ctx, MBX_BASE + REG_FBCTL, 6u);
    m->bus.write32(m->bus.ctx, MBX_BASE + REG_FBXCLIP,
                   (clip_right << 16) | clip_left);
    m->bus.write32(m->bus.ctx, MBX_BASE + REG_FBYCLIP,
                   (clip_bottom << 16) | clip_top);
    m->bus.write32(m->bus.ctx, MBX_BASE + REG_FBSTART, target);
    m->bus.write32(m->bus.ctx, MBX_BASE + REG_FBSTRIDE, 320u);
}

static void test_pointer_selected_solid_quad(void) {
    enum { TARGET_STRIDE = 0x500u };
    const uint32_t table0 = 0x08003000u;
    const uint32_t table2 = 0x08004000u;
    const uint32_t region = 0x00001000u;
    const uint32_t object = 0x00014000u;
    const uint32_t target = 0x00a41000u;
    const uint32_t region_pa = 0x08010000u;
    const uint32_t object_pa = 0x08014000u;
    const uint32_t target_pa = 0x08040000u;
    const uint32_t target_page0 = target & ~0xfffu;

    s5l8900_t m;
    CHECK(s5l8900_init(&m, RAM_BASE, RAM_SIZE), "solid-quad machine init failed");
    if (!m.ram) return;
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_GART0, table0);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_GART2, table2);
    test_map_gpu_page(&m, table0, region, region_pa);
    test_map_gpu_page(&m, table0, object, object_pa);
    for (uint32_t page = target_page0;
         page < target_page0 + 320u * 480u * 4u; page += 0x1000u)
        test_map_gpu_page(&m, table2, page,
                          target_pa + (page - target_page0));

    /* Exact r414 form.  The poisoned +0x1f0 texture is stale by construction;
     * only the 0x612... pointer-selected transparent colour object at +0x2a0
     * may execute and complete without changing its destination. */
    test_write_solid_quad(&m, region, object, target, 0x2a0u,
                          159.0f, 239.0f, 161.0f, 241.0f,
                          0x00000000u);
    for (uint32_t off = 0x0e8u; off < 0x1f0u; off += 4u)
        test_gpu_write32(&m, object + off, 0xa5000000u ^ off);
    for (unsigned i = 0; i < 44u; i++)
        test_gpu_write32(&m, object + 0x1f0u + i * 4u,
                         0xdead0000u + i);
    for (uint32_t y = 239u; y < 241u; y++)
        for (uint32_t x = 159u; x < 161u; x++)
            test_gpu_write32(&m, target + y * TARGET_STRIDE + x * 4u,
                             0xff102030u);
    uint32_t outside = target + 239u * TARGET_STRIDE + 158u * 4u;
    test_gpu_write32(&m, outside, 0xff123456u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
    uint32_t mismatches = 0u;
    for (uint32_t y = 239u; y < 241u; y++)
        for (uint32_t x = 159u; x < 161u; x++)
            mismatches += test_gpu_read32(
                &m, target + y * TARGET_STRIDE + x * 4u) != 0xff102030u;
    CHECK(mismatches == 0u,
          "r414 transparent solid quad changed %u pixels", mismatches);
    CHECK(test_gpu_read32(&m, outside) == 0xff123456u,
          "r414 solid quad changed a pixel outside its rectangle");
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0x4cu,
          "r414 solid quad did not raise all three 3D events");
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_ACK, 0x4cu);

    /* A deliberately synthetic second address, size, position and colour
     * proves that neither 0xa8 nor the centre pixel rectangle is a whitelist. */
    test_write_solid_quad(&m, region, object, target, 0x300u,
                          23.0f, 33.0f, 47.0f, 41.0f,
                          0xff336699u);
    for (uint32_t y = 33u; y < 41u; y++)
        for (uint32_t x = 23u; x < 47u; x++)
            test_gpu_write32(&m, target + y * TARGET_STRIDE + x * 4u,
                             0xff102030u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
    mismatches = 0u;
    for (uint32_t y = 33u; y < 41u; y++)
        for (uint32_t x = 23u; x < 47u; x++)
            mismatches += test_gpu_read32(
                &m, target + y * TARGET_STRIDE + x * 4u) != 0xff336699u;
    CHECK(mismatches == 0u,
          "synthetic pointer-selected solid quad mismatched %u pixels",
          mismatches);
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0x4cu,
          "synthetic solid quad did not complete");
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_ACK, 0x4cu);

    uint32_t first = target + 33u * TARGET_STRIDE + 23u * 4u;
    uint32_t second_colour = object + 0x300u + 24u * 4u;
    uint32_t second_colour_value = test_gpu_read32(&m, second_colour);
    test_gpu_write32(&m, first, 0xffabcdefu);
    test_gpu_write32(&m, second_colour,
                     second_colour_value ^ 0x01000000u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
    CHECK(test_gpu_read32(&m, first) == 0xffabcdefu &&
          m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "per-vertex solid-colour mismatch was not rejected atomically");
    test_gpu_write32(&m, second_colour, second_colour_value);

    test_gpu_write32(&m, first, 0xffabcdefu);
    uint32_t normalized = object + 0x300u + 22u * 4u;
    uint32_t normalized_value = test_gpu_read32(&m, normalized);
    test_gpu_write32(&m, normalized, normalized_value ^ 1u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
    CHECK(test_gpu_read32(&m, first) == 0xffabcdefu &&
          m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "normalized-coordinate mutation was not rejected atomically");
    test_gpu_write32(&m, normalized, normalized_value);

    uint32_t late_parameter =
        object + 0x300u + 4u * 33u * 4u + 30u * 4u;
    test_gpu_write32(&m, late_parameter, 0xfffffffeu);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
    CHECK(test_gpu_read32(&m, first) == 0xffabcdefu &&
          m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "late solid-parameter mutation was not rejected atomically");
    test_gpu_write32(&m, late_parameter, 0xffffffffu);

    test_write_solid_quad(&m, region, object, target, 0x300u,
                          23.0f, 33.0f, 47.0f, 41.0f,
                          0x80332211u);
    test_gpu_write32(&m, first, 0xffabcdefu);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
    CHECK(test_gpu_read32(&m, first) ==
              test_over(0xffabcdefu, 0x80332211u) &&
          m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0x4cu,
          "premultiplied translucent solid did not blend and complete");
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_ACK, 0x4cu);

    /* Exact r419 geometry and exported _mbx3DQuadColor argument. */
    test_write_solid_quad(&m, region, object, target, 0x2a0u,
                          145.0f, 218.0f, 175.0f, 262.0f,
                          0x17000000u);
    uint32_t r419_first = target + 218u * TARGET_STRIDE + 145u * 4u;
    test_gpu_write32(&m, r419_first, 0xff8090a0u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
    CHECK(test_gpu_read32(&m, r419_first) ==
              test_over(0xff8090a0u, 0x17000000u) &&
          m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0x4cu,
          "r419 translucent-black solid did not blend and complete");
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_ACK, 0x4cu);

    test_write_solid_quad(&m, region, object, target, 0x300u,
                          23.0f, 33.0f, 47.0f, 41.0f,
                          0x80112299u);
    test_gpu_write32(&m, first, 0xffabcdefu);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
    CHECK(test_gpu_read32(&m, first) == 0xffabcdefu &&
          m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "non-premultiplied solid colour was not rejected atomically");

    test_write_solid_quad(&m, region, object, target, 0x300u,
                          23.0f, 33.0f, 47.0f, 41.0f,
                          0xff336699u);
    test_gpu_write32(&m, first, 0xffabcdefu);
    uint32_t last = target + 40u * TARGET_STRIDE + 46u * 4u;
    uint32_t late_page = last & ~0xfffu;
    uint32_t late_entry = table2 + (((late_page >> 12) & 0x3ffu) * 4u);
    m.bus.write32(m.bus.ctx, late_entry, 0u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
    CHECK(test_gpu_read32(&m, first) == 0xffabcdefu &&
          m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "late missing solid-quad PTE left a partial fill or completion");
    test_map_gpu_page(&m, table2, late_page,
                      target_pa + (late_page - target_page0));

    s5l8900_free(&m);
}

/* Genuine OS1 Safari object captured at the first rejected solid submission.
 * Main pointer +0x2a0, world y410.074..826.074, integer clip y410..411.
 * Only two framebuffer-address fields are relocated into this test GART. */
static const uint32_t os1_safari_solid_capture[384] = {
    0xe0000000u, 0xa7718000u, 0x0e5303a0u, 0xd6887610u, 0x22220e80u, 0x00000000u, 0x00000000u, 0x45000000u,
    0x00000000u, 0x00000000u, 0x45000000u, 0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
    0x3f800000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x40000000u, 0x00000000u, 0x00000000u,
    0x00000000u, 0x40000000u, 0x60200020u, 0x6020002du, 0x612000a8u, 0xf0000000u, 0x00000000u, 0x00000000u,
    0x22206f80u, 0x00000000u, 0x45800000u, 0x00000000u, 0x00000000u, 0x45800000u, 0x45800000u, 0x45800000u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x22207f80u, 0x00000000u, 0x43cd8000u,
    0x00000000u, 0x43cd0000u, 0x43a00000u, 0x43cd8000u, 0x43a00000u, 0x43cd0000u, 0x00000000u, 0x00000000u,
    0x00000000u, 0x00000000u, 0xe0000000u, 0xa6318000u, 0x8e535b61u, 0xa6887610u, 0x22220e80u, 0x00000000u,
    0x43b7097au, 0x43a00000u, 0x43b7097au, 0x00000000u, 0x43cd097au, 0x43a00000u, 0x43cd097au, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u, 0xff000000u,
    0x00000000u, 0x3c000000u, 0xff000000u, 0x3f200000u, 0x3c000000u, 0xff000000u, 0x00000000u, 0x3f2e0000u,
    0xff000000u, 0x3f200000u, 0x3f2e0000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0xe0000000u, 0x22200e80u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0xe0000000u, 0xa3218001u, 0x8e0f5d81u, 0xcd206c40u,
    0xa7718000u, 0x0e5303a0u, 0xae504ea0u, 0x22250e80u, 0x43848000u, 0x43ba897au, 0x439d8000u, 0x43ba897au,
    0x43848000u, 0x43c9897au, 0x439d8000u, 0x43c9897au, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
    0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u, 0xff000000u, 0x00000000u, 0x3c800000u, 0x3e848000u,
    0x3eba897au, 0xff000000u, 0x3f480000u, 0x3c800000u, 0x3e9d8000u, 0x3eba897au, 0xff000000u, 0x00000000u,
    0x3f6c0000u, 0x3e848000u, 0x3ec9897au, 0xff000000u, 0x3f480000u, 0x3f6c0000u, 0x3e9d8000u, 0x3ec9897au,
    0xe0000000u, 0xa7718000u, 0x0e5303a0u, 0xa6106673u, 0x22220e80u, 0x00000000u, 0x43cd097au, 0x43a00000u,
    0x43cd097au, 0x00000000u, 0x444e84bdu, 0x43a00000u, 0x444e84bdu, 0x00000000u, 0x00000000u, 0x00000000u,
    0x00000000u, 0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u, 0xffffffffu, 0x00000000u, 0x3ecd097au,
    0xffffffffu, 0x3ea00000u, 0x3ecd097au, 0xffffffffu, 0x00000000u, 0x3f4e84bdu, 0xffffffffu, 0x3ea00000u,
    0x3f4e84bdu, 0xe0000000u, 0x00000000u, 0x00000000u, 0x86084610u, 0x22620ea0u, 0x00000000u, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
    0x00000000u, 0x00000000u, 0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u, 0xffffffffu, 0x00000000u,
    0x00000000u, 0xffffffffu, 0x00000000u, 0x00000000u, 0xffffffffu, 0x00000000u, 0x00000000u, 0xffffffffu,
    0x00000000u, 0x00000000u, 0xe0000000u, 0x00000000u, 0x00000000u, 0x86084610u, 0x46622ea0u, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u, 0xffffffffu,
    0x00000000u, 0x00000000u, 0xffffffffu, 0x00000000u, 0x00000000u, 0xffffffffu, 0x00000000u, 0x00000000u,
    0xffffffffu, 0x00000000u, 0x00000000u, 0xe0000000u, 0x00000000u, 0x00000000u, 0x86084610u, 0x66622ea0u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
    0xffffffffu, 0x00000000u, 0x00000000u, 0xffffffffu, 0x00000000u, 0x00000000u, 0xffffffffu, 0x00000000u,
    0x00000000u, 0xffffffffu, 0x00000000u, 0x00000000u, 0xe0000000u, 0x00000000u, 0x00000000u, 0x86084610u,
    0x82622ea0u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
    0x3f800000u, 0xffffffffu, 0x00000000u, 0x00000000u, 0xffffffffu, 0x00000000u, 0x00000000u, 0xffffffffu,
    0x00000000u, 0x00000000u, 0xffffffffu, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
};
#define SAFARI_REGION 0x1000u
#define SAFARI_OBJECT 0x14000u
#define SAFARI_TARGET 0xa41000u
#define SAFARI_TARGET_PA 0x8040000u
#define SAFARI_TABLE2 0x8004000u
static void safari_solid_setup(s5l8900_t *m) {
    test_write_solid_quad(m, SAFARI_REGION, SAFARI_OBJECT, SAFARI_TARGET,
                          0x2a0u, 0, 410, 320, 411, 0xffffffffu);
    for (unsigned i = 0; i < 384; i++)
        test_gpu_write32(m, SAFARI_OBJECT + i * 4u,
                         i == 2u || i == (0x2a0u / 4u + 2u)
                         ? 0x0e500000u | (SAFARI_TARGET >> 7)
                         : os1_safari_solid_capture[i]);
    uint32_t *pixels = (uint32_t *)(m->ram + SAFARI_TARGET_PA - RAM_BASE);
    for (unsigned i = 0; i < 320u * 480u; i++) pixels[i] = 0xff123456u;
    m->bus.write32(m->bus.ctx, MBX_BASE + REG_ACK, 0x4cu);
}
static bool safari_solid_pixels(s5l8900_t *m, unsigned left, unsigned top,
                                unsigned right, unsigned bottom, uint32_t colour) {
    uint32_t *pixels = (uint32_t *)(m->ram + SAFARI_TARGET_PA - RAM_BASE);
    for (unsigned y = 0; y < 480u; y++)
        for (unsigned x = 0; x < 320u; x++)
            if (pixels[y * 320u + x] !=
                (x >= left && x < right && y >= top && y < bottom
                 ? colour : 0xff123456u)) return false;
    return true;
}
static void safari_solid_world(s5l8900_t *m, float x0, float y0, float x1, float y1) {
    const float vertices[8] = {x0,y0,x1,y0,x0,y1,x1,y1};
    for (unsigned i = 0; i < 8; i++)
        test_gpu_write32(m, SAFARI_OBJECT + 0x2a0u + (5u + i) * 4u,
                         test_float_word(vertices[i]));
    for (unsigned v = 0; v < 4; v++) {
        test_gpu_write32(m, SAFARI_OBJECT + 0x2a0u + (22u + v * 3u) * 4u,
                         test_float_word(vertices[v * 2u] / 1024.0f));
        test_gpu_write32(m, SAFARI_OBJECT + 0x2a0u + (23u + v * 3u) * 4u,
                         test_float_word(vertices[v * 2u + 1u] / 1024.0f));
    }
}
static void safari_solid_reject(s5l8900_t *m, const char *name) {
    m->bus.write32(m->bus.ctx, MBX_BASE + REG_RENDER, 1u);
    CHECK(m->bus.read32(m->bus.ctx, MBX_BASE + REG_STATUS) == 0u &&
          safari_solid_pixels(m, 0, 0, 0, 0, 0),
          "OS1 opaque solid %s was not rejected atomically", name);
}
static void test_os1_clipped_solid(void) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, RAM_BASE, RAM_SIZE), "OS1 solid init failed");
    if (!m.ram) return;
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_GART0, 0x08003000u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_GART2, SAFARI_TABLE2);
    test_map_gpu_page(&m, 0x08003000u, SAFARI_REGION, 0x08010000u);
    test_map_gpu_page(&m, 0x08003000u, SAFARI_OBJECT, 0x08014000u);
    for (unsigned off = 0; off < 320u * 480u * 4u; off += 4096u)
        test_map_gpu_page(&m, SAFARI_TABLE2, SAFARI_TARGET + off,
                          SAFARI_TARGET_PA + off);
    safari_solid_setup(&m);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0x4cu &&
          safari_solid_pixels(&m, 0, 410, 320, 411, 0xffffffffu),
          "captured Safari solid must fill only row410, preserving all other pixels");

    /* Opaque channel order is independent of the observed white colour. */
    safari_solid_setup(&m);
    for (unsigned v = 0; v < 4; v++)
        test_gpu_write32(&m, SAFARI_OBJECT + 0x2a0u + (21u + v * 3u) * 4u,
                         0xff336699u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0x4cu &&
          safari_solid_pixels(&m, 0, 410, 320, 411, 0xff336699u),
          "OS1 solid opaque channel order is incorrect");

    safari_solid_setup(&m);
    safari_solid_world(&m, -20, 410.074f, 340, 826.074f);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0x4cu &&
          safari_solid_pixels(&m, 0, 410, 320, 411, 0xffffffffu),
          "negative/offscreen world escaped its validated integer clip");
    safari_solid_setup(&m);
    safari_solid_world(&m, 400, 410, 420, 826);
    safari_solid_reject(&m, "fully offscreen world intersection");
    safari_solid_setup(&m);
    safari_solid_world(&m, 0, test_float_value(0x7fc00000u), 320, 826);
    safari_solid_reject(&m, "coherent NaN world with matching corner words");
    safari_solid_setup(&m);
    safari_solid_world(&m, 0, 410, 320, test_float_value(0x7f800000u));
    safari_solid_reject(&m, "coherent infinite world with matching corner words");
    safari_solid_setup(&m);
    safari_solid_world(&m, -4097, 410, 320, 826);
    safari_solid_reject(&m, "world lower bound");

    safari_solid_setup(&m);
    safari_solid_world(&m, 300, 479.1f, 340, 500);
    const float screen_edges[8] = {300,500,300,479,340,500,340,479};
    for (unsigned i = 0; i < 8; i++)
        test_gpu_write32(&m, SAFARI_OBJECT + 0xb8u + i * 4u,
                         test_float_word(screen_edges[i]));
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBXCLIP, 0x01400128u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBYCLIP, 0x01e001d0u);
    for (unsigned x = 37; x < 40; x++) {
        test_gpu_write32(&m, SAFARI_REGION + (x - 37) * 8u,
                         0x1d00u | x | (x == 39 ? 0x80000000u : 0));
        test_gpu_write32(&m, SAFARI_REGION + (x - 37) * 8u + 4u,
                         SAFARI_OBJECT + 0x68u);
    }
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0x4cu &&
          safari_solid_pixels(&m, 300, 479, 320, 480, 0xffffffffu),
          "world/boundary intersection escaped the screen dimensions");

    /* Fractional world coverage can round to an empty pixel interval while
     * the genuine producer still submits a nonempty coarse tile list. */
    safari_solid_setup(&m);
    safari_solid_world(&m, 0, 410.75f, 320, 826.75f);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0x4cu &&
          safari_solid_pixels(&m, 0, 0, 0, 0, 0),
          "zero-coverage OS1 solid must complete without any framebuffer write");
    safari_solid_setup(&m);
    safari_solid_world(&m, 0, 410.75f, 320, 826.75f);
    for (unsigned v = 0; v < 4; v++)
        test_gpu_write32(&m, SAFARI_OBJECT + 0x2a0u + (21u + v * 3u) * 4u,
                         0xcc141414u);
    test_gpu_write32(&m, SAFARI_OBJECT + 0x52cu, 0x12345678u);
    safari_solid_reject(&m, "fractional zero-coverage late parameter");
    safari_solid_setup(&m);
    safari_solid_world(&m, 0, 410.75f, 320, 826.75f);
    test_gpu_write32(&m, SAFARI_REGION + 39u * 8u, 0x00001927u);
    safari_solid_reject(&m, "zero-coverage missing tile terminator");
    safari_solid_setup(&m);
    safari_solid_world(&m, 0, 410.75f, 320, 826.75f);
    uint32_t empty_page = SAFARI_TABLE2 +
        ((((SAFARI_TARGET + 410u * 1280u) >> 12) & 1023u) * 4u);
    uint32_t empty_saved = m.bus.read32(m.bus.ctx, empty_page);
    m.bus.write32(m.bus.ctx, empty_page, 0u);
    safari_solid_reject(&m, "zero-coverage unmapped boundary");
    m.bus.write32(m.bus.ctx, empty_page, empty_saved);
    /* OS1 software-reference source-over. Independently use division, not the
     * packed ARM shift identity in the renderer. Every alpha and destination
     * byte is covered; untouched pixels and alpha endpoints are exact. */
    for (unsigned a = 0; a < 256; a++) {
        safari_solid_setup(&m);
        uint32_t colour = (a << 24) | ((a / 2u) << 16) | ((a / 3u) << 8) | a;
        if (a == 204u) colour = 0xcc141414u; /* actual Photos animation colour */
        for (unsigned v = 0; v < 4; v++)
            test_gpu_write32(&m, SAFARI_OBJECT + 0x2a0u + (21u + v * 3u) * 4u,
                             colour);
        uint32_t *frame = (uint32_t *)(m.ram + SAFARI_TARGET_PA - RAM_BASE);
        uint32_t expected[320];
        for (unsigned x = 0; x < 320; x++) {
            unsigned d = x & 255u;
            uint32_t dst = (d << 24) | (d << 16) | ((255u-d) << 8) |
                           ((d * 37u) & 255u);
            frame[410u * 320u + x] = dst;
            expected[x] = 0;
            for (unsigned sh = 0; sh < 32; sh += 8) {
                unsigned value = ((colour >> sh) & 255u) +
                    (((dst >> sh) & 255u) * (255u-a)) / 255u;
                expected[x] |= (value > 255u ? 255u : value) << sh;
            }
        }
        m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
        bool exact = true;
        for (unsigned y = 0; y < 480; y++)
            for (unsigned x = 0; x < 320; x++)
                if (frame[y * 320u + x] != (y == 410 ? expected[x] : 0xff123456u))
                    exact = false;
        CHECK(exact && m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0x4cu,
              "OS1 solid source-over/clip mismatch at alpha %u", a);
    }

    /* Producer comparisons reject disjoint bounds with BGT, preserving the
     * exact touching-edge no-op with an empty aligned tile span. */
    safari_solid_setup(&m);
    safari_solid_world(&m, 320, 410, 340, 826);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBXCLIP, 0x01400140u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0x4cu &&
          safari_solid_pixels(&m, 0, 0, 0, 0, 0),
          "touching-edge solid did not complete as a validated no-op");

    /* A smaller independent clip bounds both x and y inside the same tile. */
    safari_solid_setup(&m);
    for (unsigned i = 0; i < 8; i++) {
        const float edges[8] = {8,414,8,409,24,414,24,409};
        test_gpu_write32(&m, SAFARI_OBJECT + 0xb8u + i * 4u,
                         test_float_word(edges[i]));
    }
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBXCLIP, 0x00180008u);
    test_gpu_write32(&m, SAFARI_REGION, 0x1901u);
    test_gpu_write32(&m, SAFARI_REGION + 4u, SAFARI_OBJECT + 0x68u);
    test_gpu_write32(&m, SAFARI_REGION + 8u, 0x80001902u);
    test_gpu_write32(&m, SAFARI_REGION + 12u, SAFARI_OBJECT + 0x68u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0x4cu &&
          safari_solid_pixels(&m, 8, 410, 24, 414, 0xffffffffu),
          "OS1 solid independent clip ignored world or boundary intersection");

    /* The clipped multi-row destination crosses a page. A missing final page
     * must preserve even earlier rows whose translations are already valid. */
    uint32_t *pixels = (uint32_t *)(m.ram + SAFARI_TARGET_PA - RAM_BASE);
    for (unsigned i = 0; i < 320u * 480u; i++) pixels[i] = 0xff123456u;
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_ACK, 0x4cu);
    uint32_t clipped_first = SAFARI_TARGET + 410u * 1280u + 8u * 4u;
    uint32_t clipped_last = SAFARI_TARGET + 413u * 1280u + 23u * 4u;
    CHECK((clipped_first >> 12) != (clipped_last >> 12),
          "late-PTE fixture must span different destination pages");
    uint32_t clipped_entry = SAFARI_TABLE2 + (((clipped_last >> 12) & 1023u) * 4u);
    uint32_t clipped_saved = m.bus.read32(m.bus.ctx, clipped_entry);
    m.bus.write32(m.bus.ctx, clipped_entry, 0u);
    for (unsigned v = 0; v < 4; v++)
        test_gpu_write32(&m, SAFARI_OBJECT + 0x2a0u + (21u + v * 3u) * 4u,
                         0xcc141414u);
    safari_solid_reject(&m, "fractional late multi-row destination PTE");
    m.bus.write32(m.bus.ctx, clipped_entry, clipped_saved);

    const struct {unsigned off; uint32_t value; const char *name;} bad[] = {
        {0x2acu, 0xa6106672u, "unknown blend control"},
        {0x2b8u, 0x7fc00000u, "NaN geometry"},
        {0x2b8u, 0x7f800000u, "infinite geometry"},
        {0x2c8u, 0x45800800u, "unbounded world"},
        {0x2bcu, 0x00000000u, "repeated main corner"},
        {0x2bcu, 0x3f800000u, "nonrectangular main"},
        {0x2f4u, 0xfeffffffu, "nonpremultiplied alpha"},
        {0x300u, 0xff123456u, "nonuniform colour"},
        {0x308u, 0x00000000u, "wrong normalized y"},
        {0x2d4u, 0x3f800000u, "perspective coefficient"},
        {0x2e4u, 0x40000000u, "perspective divisor"},
        {0x0b8u, 0x7f800000u, "infinite boundary"},
        {0x0b8u, 0x7fc00000u, "NaN boundary"},
        {0x0b8u, 0x3f000000u, "fractional boundary"},
        {0x0b8u, 0xbf800000u, "negative boundary"},
        {0x0bcu, 0x45800800u, "unbounded boundary"},
        {0x0c4u, 0x43ce0000u, "reversed boundary"},
        {0x0d4u, 0x43cd8000u, "wrong boundary corner order"},
        {0x0b4u, 0x22207f81u, "boundary control"},
        {0x080u, 0x22206f81u, "world boundary control"},
        {0x074u, 0u, "missing list terminator"},
        {0x008u, 0x0e500000u, "background target"},
        {0x52cu, 0xfffffffeu, "late parameter"},
    };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        safari_solid_setup(&m);
        test_gpu_write32(&m, SAFARI_OBJECT + bad[i].off, bad[i].value);
        safari_solid_reject(&m, bad[i].name);
    }
    safari_solid_setup(&m);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBYCLIP, 0x01b00190u);
    safari_solid_reject(&m, "wrong coarse FB tile");
    safari_solid_setup(&m);
    test_gpu_write32(&m, SAFARI_REGION + 39u * 8u + 4u, SAFARI_OBJECT);
    safari_solid_reject(&m, "late tile pointer");
    safari_solid_setup(&m);
    uint32_t last = SAFARI_TARGET + 410u * 1280u + 319u * 4u;
    uint32_t pte = SAFARI_TABLE2 + (((last >> 12) & 1023u) * 4u);
    uint32_t saved = m.bus.read32(m.bus.ctx, pte);
    m.bus.write32(m.bus.ctx, pte, 0u);
    safari_solid_reject(&m, "destination PTE");
    m.bus.write32(m.bus.ctx, pte, saved);
    s5l8900_free(&m);
}

struct mbx_test_status_form {
    const char *name;
    uint32_t xclip, yclip;
    uint32_t target;
    bool semantic_sprite;
    bool affine_sprite;
    bool scaled_sprite;
    bool column_resample;
    bool constant_v_row;
    bool safari_address_scale;
    bool clock_column;
    bool text_wide_stretch;
    bool row_resample;
    bool variable_vertex_alpha;
    bool boundary_override;
    bool zero_coverage;
    bool arbitrary_bgra_probe;
    uint32_t tile_x0, tile_x1, tile_y0, tile_y1;
    uint32_t left, top, width, height;
    uint32_t source, source_x0, source_row0, source_stride, source_control;
    uint32_t source_width, source_height;
    uint32_t expected_covered_pixels;
    uint32_t boundary[8];
    uint32_t quad[44];
};

static void test_captured_status_form(const struct mbx_test_status_form *form) {
    enum { TARGET_STRIDE = 0x500u, MAX_PIXELS = 320u * 480u };
    const uint32_t table0 = 0x08003000u;
    const uint32_t table2 = 0x08004000u;
    const uint32_t table3 = 0x08005000u;
    const uint32_t region = 0x00001000u;
    const uint32_t object = 0x00014000u;
    const uint32_t target = form->target ? form->target : 0x00897000u;
    const uint32_t region_pa = 0x08010000u;
    const uint32_t object_pa = 0x08014000u;
    const bool tall_source = ((form->quad[1] >> 20) & 15u) >= 7u;
    const uint32_t source_pa = tall_source ? 0x08200000u : 0x08020000u;
    const uint32_t target_pa = 0x080c0000u;
    const uint32_t tile_count =
        (form->tile_x1 - form->tile_x0 + 1u) *
        (form->tile_y1 - form->tile_y0 + 1u);
    const bool filtered_sprite = form->semantic_sprite &&
        (form->source_control & 0x80000000u) != 0u;
    const uint32_t filtered_texture_height =
        8u << ((form->quad[1] >> 20) & 15u);
    const uint32_t filtered_pitch_pixels = form->source_stride / 4u;
    const uint32_t mapped_source_x0 =
        filtered_sprite ? 0u : form->source_x0;
    const uint32_t mapped_source_y0 =
        filtered_sprite ? 0u : form->source_row0;
    const uint32_t mapped_source_width =
        filtered_sprite ? filtered_pitch_pixels : form->width;
    const uint32_t mapped_source_height =
        filtered_sprite ? filtered_texture_height : form->height;

    s5l8900_t m;
    CHECK(s5l8900_init(&m, RAM_BASE, tall_source ? 0x00400000u : RAM_SIZE), "%s machine init failed",
          form->name);
    if (!m.ram) return;

    m.bus.write32(m.bus.ctx, MBX_BASE + REG_GART0, table0);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_GART2, table2);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_GART3, table3);
    uint32_t region_page0 = region & ~0xfffu;
    uint32_t region_last = region + tile_count * 8u - 1u;
    for (uint32_t page = region_page0; page <= (region_last & ~0xfffu);
         page += 0x1000u)
        test_map_gpu_page(&m, table0, page,
                          region_pa + (page - region_page0));
    test_map_gpu_page(&m, table0, object, object_pa);
    CHECK(mapped_source_width != 0u && mapped_source_height != 0u,
          "%s has no source dimensions", form->name);
    if (!mapped_source_width || !mapped_source_height) {
        s5l8900_free(&m);
        return;
    }
    uint32_t source_page0 = form->source & ~0xfffu;
    uint32_t source_last = form->source +
                           (mapped_source_y0 + mapped_source_height - 1u) *
                               form->source_stride +
                           (mapped_source_x0 + mapped_source_width) * 4u - 1u;
    for (uint32_t page = source_page0; page <= (source_last & ~0xfffu);
         page += 0x1000u) {
        uint32_t table = (page >> 22) == 2u ? table2 : table3;
        test_map_gpu_page(&m, table, page,
                          source_pa + (page - source_page0));
    }
    uint32_t target_page0 = target & ~0xfffu;
    uint32_t target_last = target +
                           (form->top + form->height) * TARGET_STRIDE +
                           (form->left + form->width) * 4u - 1u;
    for (uint32_t page = target_page0; page <= (target_last & ~0xfffu);
         page += 0x1000u) {
        uint32_t table = (page >> 22) == 2u ? table2 : table3;
        test_map_gpu_page(&m, table, page,
                          target_pa + (page - target_page0));
    }
    uint32_t list = object + 0x68u;
    uint32_t tile_index = 0u;
    for (uint32_t y = form->tile_y0; y <= form->tile_y1; y++) {
        for (uint32_t x = form->tile_x0; x <= form->tile_x1; x++) {
            uint32_t code = (y << 8) | x;
            if (tile_index + 1u == tile_count) code |= 0x80000000u;
            test_gpu_write32(&m, region + tile_index * 8u, code);
            test_gpu_write32(&m, region + tile_index * 8u + 4u, list);
            tile_index++;
        }
    }
    uint32_t expected_first_tile =
        (form->tile_y0 << 8) | form->tile_x0;
    if (tile_count == 1u) expected_first_tile |= 0x80000000u;
    CHECK(test_gpu_read32(&m, region) == expected_first_tile &&
          test_gpu_read32(&m, region + (tile_count - 1u) * 8u) ==
              (0x80000000u | (form->tile_y1 << 8) | form->tile_x1),
          "%s region fixture lost its first or final tile", form->name);
    uint32_t region_mismatches = 0u;
    for (uint32_t i = 0; i < tile_count; i++) {
        uint32_t x = form->tile_x0 +
                     (i % (form->tile_x1 - form->tile_x0 + 1u));
        uint32_t y = form->tile_y0 +
                     (i / (form->tile_x1 - form->tile_x0 + 1u));
        uint32_t code = (y << 8) | x;
        if (i + 1u == tile_count) code |= 0x80000000u;
        region_mismatches += test_gpu_read32(&m, region + i * 8u) != code;
        region_mismatches += test_gpu_read32(&m, region + i * 8u + 4u) != list;
    }
    CHECK(region_mismatches == 0u,
          "%s region fixture has %u mismatched words",
          form->name, region_mismatches);
    const uint32_t list_words[4] = {
        0x60200020u, 0x6020002du, 0x61a0007cu, 0xf0000000u
    };
    for (unsigned i = 0; i < 4u; i++)
        test_gpu_write32(&m, list + i * 4u, list_words[i]);

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
        uint32_t value = i == 2u
            ? 0x0e500000u | (target >> 7) : background[i];
        test_gpu_write32(&m, object + i * 4u, value);
    }

    static const struct mbx_test_word {
        uint16_t off;
        uint32_t value;
    } boundary[] = {
        {0x080u, 0x22206f80u}, {0x088u, 0x45800000u},
        {0x094u, 0x45800000u}, {0x098u, 0x45800000u},
        {0x09cu, 0x45800000u}, {0x0b4u, 0x22207f80u},
        {0x0e8u, 0xe0000000u}, {0x0f4u, 0xa6887610u},
        {0x0f8u, 0x22220e80u}, {0x12cu, 0x3f800000u},
        {0x130u, 0x3f800000u}, {0x134u, 0x3f800000u},
        {0x138u, 0x3f800000u}, {0x198u, 0xe0000000u},
        {0x19cu, 0x22200e80u}, {0x1d0u, 0x3f800000u},
        {0x1d4u, 0x3f800000u}, {0x1d8u, 0x3f800000u},
        {0x1dcu, 0x3f800000u},
    };
    for (unsigned i = 0; i < sizeof boundary / sizeof boundary[0]; i++)
        test_gpu_write32(&m, object + boundary[i].off, boundary[i].value);
    for (unsigned i = 0; i < 8u; i++)
        test_gpu_write32(&m, object + 0x0b8u + i * 4u,
            form->boundary_override ? form->boundary[i] : form->quad[8u + i]);

    for (unsigned i = 0; i < 44u; i++) {
        uint32_t value = form->quad[i];
        if (i == 2u) value = form->source_control | (form->source >> 7);
        if (i == 5u) value = 0x0e500000u | (target >> 7);
        test_gpu_write32(&m, object + 0x1f0u + i * 4u, value);
    }
    /* Only the +0x80, +0xb4 and pointer-selected +0x1f0 records are in the
     * list. Poison the intervening stale storage so a decoder cannot quietly
     * turn a historical allocation pattern back into a packet requirement. */
    for (uint32_t off = 0x0e8u; off < 0x1f0u; off += 4u)
        test_gpu_write32(&m, object + off, 0x5a000000u ^ off);

    uint64_t pixel_count = (uint64_t)form->width * form->height;
    CHECK(pixel_count <= MAX_PIXELS, "%s fixture rectangle is too large",
          form->name);
    if (pixel_count > MAX_PIXELS) {
        s5l8900_free(&m);
        return;
    }
    uint32_t *expected = malloc((size_t)pixel_count * sizeof *expected);
    CHECK(expected != NULL, "%s expected-pixel allocation failed", form->name);
    if (!expected) {
        s5l8900_free(&m);
        return;
    }
    float filter_x0 = 0.0f, filter_y0 = 0.0f;
    float filter_dx = 0.0f, filter_dy = 0.0f;
    float filter_u_start = 0.0f, filter_v_start = 0.0f;
    float filter_u_span = 0.0f, filter_v_span = 0.0f;
    struct test_affine_transform filter_affine = {0};
    if (filtered_sprite) {
        filter_x0 = test_float_value(form->quad[8]);
        filter_y0 = test_float_value(form->quad[9]);
        filter_dx = test_float_value(form->quad[10]) - filter_x0;
        filter_dy = test_float_value(form->quad[13]) - filter_y0;
        if (form->affine_sprite) {
            filter_affine.origin_x = filter_x0;
            filter_affine.origin_y = filter_y0;
            filter_affine.u_x = filter_dx;
            filter_affine.u_y =
                test_float_value(form->quad[11]) - filter_y0;
            filter_affine.v_x =
                test_float_value(form->quad[12]) - filter_x0;
            filter_affine.v_y = filter_dy;
            filter_affine.determinant =
                filter_affine.u_x * filter_affine.v_y -
                filter_affine.u_y * filter_affine.v_x;
        }
        uint32_t texture_width = 8u << ((form->quad[1] >> 24) & 7u);
        uint32_t texture_height = 8u << ((form->quad[1] >> 20) & 15u);
        filter_u_start = test_float_value(form->quad[25]) * texture_width;
        filter_v_start = test_float_value(form->quad[26]) * texture_height;
        float filter_u_end =
            test_float_value(form->quad[30]) * texture_width;
        float filter_v_end =
            test_float_value(form->quad[36]) * texture_height;
        filter_u_span = filter_u_end - filter_u_start;
        filter_v_span = filter_v_end - filter_v_start;
        uint32_t inferred_left = (uint32_t)filter_u_start;
        uint32_t inferred_top = (uint32_t)filter_v_start;
        uint32_t inferred_right = (uint32_t)filter_u_end;
        uint32_t inferred_bottom = (uint32_t)filter_v_end;
        inferred_right += (float)inferred_right < filter_u_end;
        inferred_bottom += (float)inferred_bottom < filter_v_end;
        uint32_t inferred_width = inferred_right - inferred_left;
        uint32_t inferred_height = inferred_bottom - inferred_top;
        CHECK(inferred_width == form->source_width &&
              inferred_height == form->source_height,
              "%s explicit source dimensions disagree with its UV rectangle",
              form->name);
        CHECK(filter_u_start >= 0.0f && filter_v_start >= 0.0f &&
              filter_u_end <= (float)filtered_pitch_pixels &&
              filter_v_end <= (float)filtered_texture_height,
              "%s filtered UV rectangle exceeds its allocation", form->name);
        for (uint32_t y = 0; y < filtered_texture_height; y++) {
            for (uint32_t x = 0; x < filtered_pitch_pixels; x++) {
                test_gpu_write32(&m,
                    form->source + y * form->source_stride + x * 4u,
                    test_sprite_source_pixel(x, y));
            }
        }
    }
    uint32_t covered_pixels = 0u;
    uint32_t first_covered_offset = UINT32_MAX;
    uint32_t last_covered_offset = 0u;
    uint32_t maximum_sampled_x = 0u, maximum_sampled_y = 0u;
    bool have_filtered_sample = false;
    for (uint32_t y = 0; y < form->height; y++) {
        for (uint32_t x = 0; x < form->width; x++) {
            uint32_t src = 0u;
            bool covered = !form->zero_coverage;
            if (filtered_sprite) {
                struct test_bilinear_axis x_axis, y_axis;
                bool axes_ok = false;
                if (form->affine_sprite) {
                    float u_fraction, v_fraction;
                    covered = test_affine_pixel(
                        &filter_affine, form->left + x, form->top + y,
                        &u_fraction, &v_fraction);
                    if (covered) {
                        axes_ok = test_bilinear_coordinate(
                            filter_u_start + u_fraction * filter_u_span,
                            filtered_pitch_pixels, &x_axis) &&
                            test_bilinear_coordinate(
                                filter_v_start + v_fraction * filter_v_span,
                                filtered_texture_height, &y_axis);
                    }
                } else {
                    axes_ok = test_bilinear_axis(
                        filter_x0, filter_dx, filter_u_start, filter_u_span,
                        form->left + x, filtered_pitch_pixels, &x_axis) &&
                        test_bilinear_axis(
                            filter_y0, filter_dy, filter_v_start, filter_v_span,
                            form->top + y, filtered_texture_height, &y_axis);
                }
                if (covered && !axes_ok) {
                    CHECK(false, "%s expected filter axis is invalid", form->name);
                    covered = false;
                }
                if (covered) {
                    src = test_bilinear_sprite_pixel(&x_axis, &y_axis);
                    if (!have_filtered_sample ||
                        x_axis.second > maximum_sampled_x)
                        maximum_sampled_x = x_axis.second;
                    if (!have_filtered_sample ||
                        y_axis.second > maximum_sampled_y)
                        maximum_sampled_y = y_axis.second;
                    have_filtered_sample = true;
                }
            } else {
                src = test_sprite_source_pixel(x, y);
                test_gpu_write32(&m,
                    form->source + (form->source_row0 + y) *
                        form->source_stride + (form->source_x0 + x) * 4u, src);
            }
            uint32_t dst = 0xff102030u + y * 0x00010101u + x;
            test_gpu_write32(&m,
                target + (form->top + y) * TARGET_STRIDE +
                    (form->left + x) * 4u, dst);
            uint32_t offset = y * form->width + x;
            if (covered) {
                uint32_t modulated = test_modulate_vertex_alpha(
                    src, form->quad[24] >> 24);
                expected[offset] = test_over(dst, modulated);
                if (first_covered_offset == UINT32_MAX)
                    first_covered_offset = offset;
                last_covered_offset = offset;
                covered_pixels++;
            } else {
                expected[offset] = dst;
            }
        }
    }
    CHECK(form->zero_coverage
              ? first_covered_offset == UINT32_MAX && covered_pixels == 0u
              : first_covered_offset != UINT32_MAX,
          "%s reference coverage disagrees with its lifecycle class",
          form->name);
    if (form->expected_covered_pixels) {
        CHECK(covered_pixels == form->expected_covered_pixels,
              "%s expected %u covered pixels but reference found %u",
              form->name, form->expected_covered_pixels, covered_pixels);
    }
    bool before_below = form->left == 0u;
    uint32_t before = target + (form->top + form->height) * TARGET_STRIDE +
                      form->left * 4u;
    if (!before_below)
        before = target + form->top * TARGET_STRIDE +
                 (form->left - 1u) * 4u;
    uint32_t after = target + (form->top + form->height) * TARGET_STRIDE +
                     (form->left + (before_below ? 1u : 0u)) * 4u;
    test_gpu_write32(&m, before, 0x11223344u);
    test_gpu_write32(&m, after, 0x55667788u);

    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RGNBASE, region);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_OBJBASE, object);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_PIXSAMP, 0x00020007u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBCTL, 6u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBXCLIP, form->xclip);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBYCLIP, form->yclip);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBSTART, target);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBSTRIDE, 320u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);

    uint32_t mismatches = 0u;
    for (uint32_t y = 0; y < form->height; y++) {
        for (uint32_t x = 0; x < form->width; x++) {
            uint32_t actual = test_gpu_read32(&m,
                target + (form->top + y) * TARGET_STRIDE +
                    (form->left + x) * 4u);
            mismatches += actual != expected[y * form->width + x];
        }
    }
    CHECK(mismatches == 0u, "%s: %u pixels mismatched",
          form->name, mismatches);
    CHECK(test_gpu_read32(&m, before) == 0x11223344u &&
          test_gpu_read32(&m, after) == 0x55667788u,
          "%s changed a pixel outside its rectangle", form->name);
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0x4cu,
          "%s did not raise all three completion events", form->name);

    m.bus.write32(m.bus.ctx, MBX_BASE + REG_ACK, 0x4cu);
    if (form->arbitrary_bgra_probe) {
        const uint32_t arbitrary = 0xbad43210u;
        CHECK(filtered_sprite && covered_pixels == (uint32_t)pixel_count,
              "%s arbitrary-BGRA probe requires full filtered coverage",
              form->name);
        for (uint32_t y = 0; y < filtered_texture_height; y++)
            for (uint32_t x = 0; x < filtered_pitch_pixels; x++)
                test_gpu_write32(&m,
                    form->source + y * form->source_stride + x * 4u,
                    arbitrary);
        for (uint32_t y = 0; y < form->height; y++)
            for (uint32_t x = 0; x < form->width; x++)
                test_gpu_write32(&m,
                    target + (form->top + y) * TARGET_STRIDE +
                        (form->left + x) * 4u,
                    0xff102030u + y * 0x00010101u + x);

        m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
        uint32_t arbitrary_mismatches = 0u;
        uint32_t modulated = test_modulate_vertex_alpha(
            arbitrary, form->quad[24] >> 24);
        for (uint32_t y = 0; y < form->height; y++) {
            for (uint32_t x = 0; x < form->width; x++) {
                uint32_t dst = 0xff102030u + y * 0x00010101u + x;
                uint32_t actual = test_gpu_read32(&m,
                    target + (form->top + y) * TARGET_STRIDE +
                        (form->left + x) * 4u);
                arbitrary_mismatches +=
                    actual != test_over(dst, modulated);
            }
        }
        CHECK(arbitrary_mismatches == 0u,
              "%s arbitrary BGRA8 source mismatched %u saturated pixels",
              form->name, arbitrary_mismatches);
        CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0x4cu,
              "%s arbitrary BGRA8 source did not complete", form->name);
        m.bus.write32(m.bus.ctx, MBX_BASE + REG_ACK, 0x4cu);
    }
    uint32_t first_offset = first_covered_offset == UINT32_MAX
        ? 0u : first_covered_offset;
    uint32_t first = target +
        (form->top + first_offset / form->width) * TARGET_STRIDE +
        (form->left + first_offset % form->width) * 4u;
    uint32_t last_destination = target +
        (form->top + last_covered_offset / form->width) * TARGET_STRIDE +
        (form->left + last_covered_offset % form->width) * 4u;
    if (form->clock_column) {
        const unsigned us[4] = {25u,30u,35u,40u};
        const float bad_u[] = {0.0f, 1.0f, 1.5f, 8.0f};
        for (unsigned k = 0; k < 4u; k++) {
            m.bus.write32(m.bus.ctx, MBX_BASE + REG_ACK, 0x4cu);
            test_gpu_write32(&m, first, 0x89abcdefu);
            test_gpu_write32(&m, last_destination, 0x76543210u);
            for (unsigned i = 0; i < 4u; i++)
                test_gpu_write32(&m, object + 0x1f0u + us[i] * 4u,
                                 test_float_word(bad_u[k] / 8.0f));
            m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
            CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u &&
                  test_gpu_read32(&m, first) == 0x89abcdefu &&
                  test_gpu_read32(&m, last_destination) == 0x76543210u,
                  "%s unmeasured column U=%g committed", form->name,
                  (double)bad_u[k]);
        }
        for (unsigned i = 0; i < 4u; i++)
            test_gpu_write32(&m, object + 0x1f0u + us[i] * 4u, form->quad[us[i]]);
        const unsigned changed[4][4] = {
            {10u,14u,32u,42u}, {13u,15u,38u,43u},
            {12u,14u,37u,42u}, {9u,11u,28u,33u},
        };
        /* Coherent width/height, shear and reversed-axis-height edits retain
         * the parallelogram and normalized duplicates but leave this family. */
        for (unsigned k = 0; k < 4u; k++) {
            m.bus.write32(m.bus.ctx, MBX_BASE + REG_ACK, 0x4cu);
            test_gpu_write32(&m, first, 0x89abcdefu);
            test_gpu_write32(&m, last_destination, 0x76543210u);
            for (unsigned i = 0; i < 2u; i++) {
                float value = test_float_value(form->quad[changed[k][i]]) + 1.0f;
                test_gpu_write32(&m, object + 0x1f0u + changed[k][i] * 4u,
                                 test_float_word(value));
                test_gpu_write32(&m, object + 0x1f0u + changed[k][i+2u] * 4u,
                                 test_float_word(value / 1024.0f));
            }
            m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
            CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u &&
                  test_gpu_read32(&m, first) == 0x89abcdefu &&
                  test_gpu_read32(&m, last_destination) == 0x76543210u,
                  "%s unsupported coherent affine edit %u committed", form->name, k);
            for (unsigned i = 0; i < 4u; i++)
                test_gpu_write32(&m, object + 0x1f0u + changed[k][i] * 4u,
                                 form->quad[changed[k][i]]);
        }
        m.bus.write32(m.bus.ctx, MBX_BASE + REG_ACK, 0x4cu);
    }
    if (form->safari_address_scale || form->text_wide_stretch) {
        /* Coherent UV edits preserve rectangularity/allocation yet leave the
         * newly measured scale families. They must not touch either sentinel. */
        const unsigned uv_words[4] = {25u,35u,26u,31u};
        for (unsigned axis = 0; axis < 2; axis++) {
            test_gpu_write32(&m, first, 0x89abcdefu);
            test_gpu_write32(&m, last_destination, 0x76543210u);
            unsigned first_word = uv_words[axis * 2u];
            unsigned second_word = uv_words[axis * 2u + 1u];
            float extent = (float)(8u << ((form->quad[1] >> (axis ? 20u : 24u)) & (axis ? 15u : 7u)));
            float value = test_float_value(form->quad[first_word]) + 0.5f / extent;
            test_gpu_write32(&m, object + 0x1f0u + first_word * 4u, test_float_word(value));
            test_gpu_write32(&m, object + 0x1f0u + second_word * 4u, test_float_word(value));
            m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
            CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u &&
                  test_gpu_read32(&m, first) == 0x89abcdefu &&
                  test_gpu_read32(&m, last_destination) == 0x76543210u,
                  "%s coherent cropped UV axis %u committed", form->name, axis);
            test_gpu_write32(&m, object + 0x1f0u + first_word * 4u, form->quad[first_word]);
            test_gpu_write32(&m, object + 0x1f0u + second_word * 4u, form->quad[second_word]);
        }
        if (form->quad[3] == 0xcd206c40u) {
            float altered_bottom = test_float_value(form->quad[13]) - 1.0f;
            test_gpu_write32(&m, object + 0x1f0u + 13u * 4u, test_float_word(altered_bottom));
            test_gpu_write32(&m, object + 0x1f0u + 15u * 4u, test_float_word(altered_bottom));
            test_gpu_write32(&m, object + 0x1f0u + 38u * 4u, test_float_word(altered_bottom / 1024.0f));
            test_gpu_write32(&m, object + 0x1f0u + 43u * 4u, test_float_word(altered_bottom / 1024.0f));
            m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
            CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u &&
                  test_gpu_read32(&m, first) == 0x89abcdefu &&
                  test_gpu_read32(&m, last_destination) == 0x76543210u,
                  "%s changed vertical scale committed", form->name);
            const unsigned restore_words[4] = {13u,15u,38u,43u};
            for(unsigned i=0;i<4;i++)test_gpu_write32(&m,
                object+0x1f0u+restore_words[i]*4u,form->quad[restore_words[i]]);
        }
    }
    if (form->text_wide_stretch) {
        const unsigned x_words[4] = {10u,14u,32u,42u};
        const float outside = 4097.0f;
        for(unsigned i=0;i<4;i++) test_gpu_write32(&m,
            object+0x1f0u+x_words[i]*4u,
            test_float_word(i<2u?outside:outside/1024.0f));
        test_gpu_write32(&m, first, 0x89abcdefu);
        test_gpu_write32(&m, last_destination, 0x76543210u);
        m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
        CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u &&
              test_gpu_read32(&m, first) == 0x89abcdefu &&
              test_gpu_read32(&m, last_destination) == 0x76543210u,
              "%s exceeded coordinate envelope", form->name);
        for(unsigned i=0;i<4;i++)test_gpu_write32(&m,
            object+0x1f0u+x_words[i]*4u,form->quad[x_words[i]]);
    }
    if (form->constant_v_row) {
        const unsigned vs[4] = {26u,31u,36u,41u};
        const float invalid_centres[] = {0.0f, 1.0f, 1.5f, 8.0f};
        for (unsigned k=0;k<4u;k++) {
            test_gpu_write32(&m, first, 0x89abcdefu);
            test_gpu_write32(&m, last_destination, 0x76543210u);
            for (unsigned v=0;v<4u;v++)
                test_gpu_write32(&m, object+0x1f0u+vs[v]*4u,
                    test_float_word(invalid_centres[k]/8.0f));
            m.bus.write32(m.bus.ctx, MBX_BASE+REG_RENDER, 1u);
            CHECK(m.bus.read32(m.bus.ctx, MBX_BASE+REG_STATUS)==0u &&
                  test_gpu_read32(&m, first)==0x89abcdefu &&
                  test_gpu_read32(&m, last_destination)==0x76543210u,
                  "%s unmeasured constant V=%g committed", form->name,
                  (double)invalid_centres[k]);
        }
        for (unsigned v=0;v<4u;v++)
            test_gpu_write32(&m, object+0x1f0u+vs[v]*4u, form->quad[vs[v]]);
        /* A coherent cropped U rectangle remains unsupported even though
         * both sides still fit the allocation and all redundant UVs agree. */
        const unsigned us[2] = {25u,35u};
        for(unsigned v=0;v<2u;v++)
            test_gpu_write32(&m,object+0x1f0u+us[v]*4u,test_float_word(0.5f/512.0f));
        m.bus.write32(m.bus.ctx, MBX_BASE+REG_RENDER, 1u);
        CHECK(m.bus.read32(m.bus.ctx, MBX_BASE+REG_STATUS)==0u &&
              test_gpu_read32(&m, first)==0x89abcdefu &&
              test_gpu_read32(&m, last_destination)==0x76543210u,
              "%s cropped constant-V row committed", form->name);
        for(unsigned v=0;v<2u;v++)
            test_gpu_write32(&m,object+0x1f0u+us[v]*4u,form->quad[us[v]]);
    }
    if (form->variable_vertex_alpha) {
        test_gpu_write32(&m, first, 0x89abcdefu);
        test_gpu_write32(&m, object + 0x264u, form->quad[29] ^ 0x01000000u);
        m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
        CHECK(test_gpu_read32(&m, first) == 0x89abcdefu,
              "%s mismatched vertex alpha changed the destination", form->name);
        CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
              "%s mismatched vertex alpha raised completion", form->name);
        test_gpu_write32(&m, object + 0x264u, form->quad[29]);

        test_gpu_write32(&m, object + 0x250u, form->quad[24] | 1u);
        m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
        CHECK(test_gpu_read32(&m, first) == 0x89abcdefu,
              "%s vertex colour bits changed the destination", form->name);
        CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
              "%s vertex colour bits raised completion", form->name);
        test_gpu_write32(&m, object + 0x250u, form->quad[24]);
    }
    if (form->semantic_sprite) {
        uint32_t word_address = object + 0x1f0u + 27u * 4u;
        uint32_t saved_word = test_gpu_read32(&m, word_address);
        test_gpu_write32(&m, first, 0x89abcdefu);
        test_gpu_write32(&m, word_address, saved_word ^ 1u);
        m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
        CHECK(test_gpu_read32(&m, first) == 0x89abcdefu,
              "%s inconsistent normalized position changed the destination",
              form->name);
        CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
              "%s inconsistent normalized position raised completion",
              form->name);
        test_gpu_write32(&m, word_address, saved_word);

        word_address = object + 0x1f0u + 29u * 4u;
        saved_word = test_gpu_read32(&m, word_address);
        test_gpu_write32(&m, first, 0x89abcdefu);
        test_gpu_write32(&m, word_address, saved_word ^ 0x01000000u);
        m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
        CHECK(test_gpu_read32(&m, first) == 0x89abcdefu,
              "%s mismatched app vertex alpha changed the destination",
              form->name);
        CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
              "%s mismatched app vertex alpha raised completion", form->name);
        test_gpu_write32(&m, word_address, saved_word);

        word_address = object + 0x1f0u + 30u * 4u;
        saved_word = test_gpu_read32(&m, word_address);
        test_gpu_write32(&m, first, 0x89abcdefu);
        test_gpu_write32(&m, word_address, saved_word ^ 1u);
        m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
        CHECK(test_gpu_read32(&m, first) == 0x89abcdefu,
              "%s inconsistent UV extent changed the destination", form->name);
        CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
              "%s inconsistent UV extent raised completion", form->name);
        test_gpu_write32(&m, word_address, saved_word);

        if (!filtered_sprite) {
            /* A half-texel UV origin has the same floor/ceil source bounds as
             * the captured integer crop.  Keep both redundant corners in
             * agreement so rejection proves the unfiltered path did not
             * silently invent nearest-neighbour rounding. */
            uint32_t first_v_word = object + 0x1f0u + 31u * 4u;
            uint32_t second_v_word = object + 0x1f0u + 41u * 4u;
            uint32_t first_v = test_gpu_read32(&m, first_v_word);
            uint32_t second_v = test_gpu_read32(&m, second_v_word);
            uint32_t texture_height =
                8u << ((form->quad[1] >> 20) & 15u);
            uint32_t fractional_v = test_float_word(
                test_float_value(first_v) + 0.5f / (float)texture_height);
            test_gpu_write32(&m, first, 0x89abcdefu);
            test_gpu_write32(&m, first_v_word, fractional_v);
            test_gpu_write32(&m, second_v_word, fractional_v);
            m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
            CHECK(test_gpu_read32(&m, first) == 0x89abcdefu,
                  "%s fractional unfiltered origin changed the destination",
                  form->name);
            CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
                  "%s fractional unfiltered origin raised completion",
                  form->name);
            test_gpu_write32(&m, first_v_word, first_v);
            test_gpu_write32(&m, second_v_word, second_v);

            if (form->quad[3] == 0xa6884710u) {
                /* Corner order does not establish sampling semantics. Keep
                 * the direct packet's coherent geometry and allocation but
                 * switch to the alternate sampler pair; no full-extent form
                 * has measured that operation, so it must remain atomic. */
                uint32_t sampler_word = object + 0x1f0u + 3u * 4u;
                uint32_t companion_word = object + 0x1f0u + 6u * 4u;
                test_gpu_write32(&m, first, 0x89abcdefu);
                test_gpu_write32(&m, sampler_word, 0xd6887610u);
                test_gpu_write32(&m, companion_word, 0xa3104620u);
                m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
                CHECK(test_gpu_read32(&m, first) == 0x89abcdefu,
                      "%s unmeasured full-extent sampler changed the destination",
                      form->name);
                CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
                      "%s unmeasured full-extent sampler raised completion",
                      form->name);
                test_gpu_write32(&m, sampler_word, form->quad[3]);
                test_gpu_write32(&m, companion_word, form->quad[6]);
            }
        }

        if (form->row_resample && !form->safari_address_scale && !form->text_wide_stretch &&
            form->quad[3] == 0xcd206c40u) {
            /* The new gradient form covers the complete texture width.
             * A coherent cropped horizontal origin remains unsupported. */
            test_gpu_write32(&m, first, 0x89abcdefu);
            test_gpu_write32(&m, last_destination, 0x76543210u);
            const uint32_t crop = test_float_word(0.5f /
                (float)(8u << ((form->quad[1] >> 24) & 7u)));
            test_gpu_write32(&m, object + 0x1f0u + 25u*4u, crop);
            test_gpu_write32(&m, object + 0x1f0u + 35u*4u, crop);
            m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
            CHECK(test_gpu_read32(&m, first) == 0x89abcdefu &&
                  test_gpu_read32(&m, last_destination) == 0x76543210u &&
                  m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
                  "%s cropped gradient silently entered full-width family",
                  form->name);
            test_gpu_write32(&m, object + 0x1f0u + 25u*4u, form->quad[25]);
            test_gpu_write32(&m, object + 0x1f0u + 35u*4u, form->quad[35]);
        }

        if (tall_source) {
            uint32_t header_address = object + 0x1f4u;
            uint32_t saved_header = test_gpu_read32(&m, header_address);
            test_gpu_write32(&m, first, 0x89abcdefu);
            test_gpu_write32(&m, last_destination, 0x76543210u);
            test_gpu_write32(&m, header_address,
                (saved_header & ~0x00f00000u) | 0x00900000u);
            m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
            CHECK(test_gpu_read32(&m, first) == 0x89abcdefu &&
                  test_gpu_read32(&m, last_destination) == 0x76543210u &&
                  m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
                  "%s unmeasured 4096-row allocation committed", form->name);
            test_gpu_write32(&m, header_address, saved_header);
            /* Shift the complete integer UV crop beyond the allocation,
             * keeping redundant corners and the original extent coherent. */
            const unsigned vs[4] = {26u,31u,36u,41u};
            for (unsigned k=0;k<4u;k++)
                test_gpu_write32(&m, object + 0x1f0u + vs[k]*4u,
                    test_float_word(test_float_value(form->quad[vs[k]])+1.0f));
            m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
            CHECK(test_gpu_read32(&m, first) == 0x89abcdefu &&
                  test_gpu_read32(&m, last_destination) == 0x76543210u &&
                  m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
                  "%s out-of-allocation tall crop committed", form->name);
            for (unsigned k=0;k<4u;k++)
                test_gpu_write32(&m, object + 0x1f0u + vs[k]*4u,
                    form->quad[vs[k]]);
        }

        /* Allocation width and linear row pitch are independent, but the
         * bounded sprite family does not admit a 1024-pixel allocation. */
        word_address = object + 0x1f0u + 1u * 4u;
        saved_word = test_gpu_read32(&m, word_address);
        uint32_t too_wide_header =
            (saved_word & ~0x07000000u) | 0x07000000u;
        CHECK(too_wide_header != saved_word,
              "%s fixture already uses an unsupported 1024-pixel allocation",
              form->name);
        test_gpu_write32(&m, first, 0x89abcdefu);
        test_gpu_write32(&m, word_address, too_wide_header);
        m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
        CHECK(test_gpu_read32(&m, first) == 0x89abcdefu,
              "%s unsupported texture allocation changed the destination",
              form->name);
        CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
              "%s unsupported texture allocation raised completion",
              form->name);
        test_gpu_write32(&m, word_address, saved_word);

        test_gpu_write32(&m, first, 0x89abcdefu);
        m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBXCLIP,
                      form->xclip ^ 8u);
        m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
        CHECK(test_gpu_read32(&m, first) == 0x89abcdefu,
              "%s inconsistent clip changed the destination", form->name);
        CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
              "%s inconsistent clip raised completion", form->name);
        m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBXCLIP, form->xclip);

        uint32_t first_region_word = test_gpu_read32(&m, region);
        test_gpu_write32(&m, first, 0x89abcdefu);
        test_gpu_write32(&m, region, first_region_word ^ 1u);
        m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
        CHECK(test_gpu_read32(&m, first) == 0x89abcdefu,
              "%s inconsistent tile region changed the destination", form->name);
        CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
              "%s inconsistent tile region raised completion", form->name);
        test_gpu_write32(&m, region, first_region_word);

        uint32_t last_source = 0u;
        if (!form->zero_coverage) {
            uint32_t sampled_x = form->source_x0 + form->width - 1u;
            uint32_t sampled_y = form->source_row0 + form->height - 1u;
            if (filtered_sprite) {
                CHECK(have_filtered_sample,
                      "%s has no filtered source sample", form->name);
                sampled_x = maximum_sampled_x;
                sampled_y = maximum_sampled_y;
            }
            last_source = form->source + sampled_y * form->source_stride +
                          sampled_x * 4u;
        } else {
            uint32_t source_table = m.bus.read32(m.bus.ctx,
                MBX_BASE + REG_GART0 + (form->source >> 22) * 4u);
            uint32_t source_pte_address = source_table +
                (((form->source >> 12) & 0x3ffu) * 4u);
            uint32_t source_pte =
                m.bus.read32(m.bus.ctx, source_pte_address);
            test_gpu_write32(&m, first, 0x89abcdefu);
            m.bus.write32(m.bus.ctx, source_pte_address, 0u);
            m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
            CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
                  "%s missing source allocation raised completion",
                  form->name);
            m.bus.write32(m.bus.ctx, source_pte_address, source_pte);
            CHECK(test_gpu_read32(&m, first) == 0x89abcdefu,
                  "%s missing source allocation changed the destination",
                  form->name);

            uint32_t target_table = m.bus.read32(m.bus.ctx,
                MBX_BASE + REG_GART0 + (target >> 22) * 4u);
            uint32_t target_pte_address = target_table +
                (((first >> 12) & 0x3ffu) * 4u);
            uint32_t target_pte =
                m.bus.read32(m.bus.ctx, target_pte_address);
            test_gpu_write32(&m, first, 0x76543210u);
            m.bus.write32(m.bus.ctx, target_pte_address, 0u);
            m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
            CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
                  "%s missing target boundary raised completion",
                  form->name);
            m.bus.write32(m.bus.ctx, target_pte_address, target_pte);
            CHECK(test_gpu_read32(&m, first) == 0x76543210u,
                  "%s missing target boundary changed the destination",
                  form->name);
        }

        if (form->affine_sprite) {
            /* Preserve the normalized duplicate while moving only p11.  The
             * resulting four-point warp still fits the same conservative
             * boundary, but it is no longer the measured parallelogram. */
            uint32_t changed_x11 = test_float_word(
                test_float_value(form->quad[14]) + 0.25f);
            uint32_t changed_normalized = test_float_word(
                test_float_value(changed_x11) / 1024.0f);
            test_gpu_write32(&m, first, 0x89abcdefu);
            test_gpu_write32(&m, last_destination, 0x76543210u);
            test_gpu_write32(&m, object + 0x1f0u + 14u * 4u,
                             changed_x11);
            test_gpu_write32(&m, object + 0x1f0u + 42u * 4u,
                             changed_normalized);
            m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
            CHECK(test_gpu_read32(&m, first) == 0x89abcdefu &&
                  test_gpu_read32(&m, last_destination) == 0x76543210u,
                  "%s non-parallelogram warp partially committed",
                  form->name);
            CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
                  "%s non-parallelogram warp raised completion", form->name);
            test_gpu_write32(&m, object + 0x1f0u + 14u * 4u,
                             form->quad[14]);
            test_gpu_write32(&m, object + 0x1f0u + 42u * 4u,
                             form->quad[42]);
        }

        if (form->scaled_sprite || tall_source) {
            if (form->column_resample) {
                /* The measured mixed-axis exception has a one-texel-or-less
                 * horizontal UV span. Preserve both redundant UV corners
                 * while widening that span by one full texel; the resulting
                 * two-dimensional transform must remain an atomic rejection. */
                uint32_t texture_width =
                    8u << ((form->quad[1] >> 24) & 7u);
                uint32_t changed_u1 = test_float_word(
                    test_float_value(form->quad[30]) +
                    1.0f / (float)texture_width);
                test_gpu_write32(&m, first, 0x89abcdefu);
                test_gpu_write32(&m, last_destination, 0x76543210u);
                test_gpu_write32(&m, object + 0x1f0u + 30u * 4u,
                                 changed_u1);
                test_gpu_write32(&m, object + 0x1f0u + 40u * 4u,
                                 changed_u1);
                m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
                CHECK(test_gpu_read32(&m, first) == 0x89abcdefu &&
                      test_gpu_read32(&m, last_destination) == 0x76543210u,
                      "%s wider mixed-axis source partially committed",
                      form->name);
                CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
                      "%s wider mixed-axis source raised completion",
                      form->name);
                test_gpu_write32(&m, object + 0x1f0u + 30u * 4u,
                                 form->quad[30]);
                test_gpu_write32(&m, object + 0x1f0u + 40u * 4u,
                                 form->quad[40]);
            } else if (form->row_resample) {
                /* The measured row-resample family preserves the vertical
                 * source extent at 1:1. Preserve both redundant UV corners
                 * while extending that extent by one texel; a generic
                 * two-axis minifier must remain an atomic rejection. */
                uint32_t texture_height =
                    8u << ((form->quad[1] >> 20) & 15u);
                uint32_t changed_v1 = test_float_word(
                    test_float_value(form->quad[36]) +
                    1.0f / (float)texture_height);
                test_gpu_write32(&m, first, 0x89abcdefu);
                test_gpu_write32(&m, last_destination, 0x76543210u);
                test_gpu_write32(&m, object + 0x1f0u + 36u * 4u,
                                 changed_v1);
                test_gpu_write32(&m, object + 0x1f0u + 41u * 4u,
                                 changed_v1);
                m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
                CHECK(test_gpu_read32(&m, first) == 0x89abcdefu &&
                      test_gpu_read32(&m, last_destination) == 0x76543210u,
                      "%s taller mixed-axis source partially committed",
                      form->name);
                CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
                      "%s taller mixed-axis source raised completion",
                      form->name);
                test_gpu_write32(&m, object + 0x1f0u + 36u * 4u,
                                 form->quad[36]);
                test_gpu_write32(&m, object + 0x1f0u + 41u * 4u,
                                 form->quad[41]);
            } else {
                /* Keep geometry and normalized records mutually consistent
                 * while making only X scale differ. A literal-packet
                 * whitelist or unchecked generic scaler would both miss this
                 * rejection. */
                uint32_t changed_x1 = test_float_word(
                    test_float_value(form->quad[10]) + 0.25f);
                uint32_t changed_normalized = test_float_word(
                    test_float_value(changed_x1) / 1024.0f);
                test_gpu_write32(&m, first, 0x89abcdefu);
                test_gpu_write32(&m, object + 0x1f0u + 10u * 4u,
                                 changed_x1);
                test_gpu_write32(&m, object + 0x1f0u + 14u * 4u,
                                 changed_x1);
                test_gpu_write32(&m, object + 0x1f0u + 32u * 4u,
                                 changed_normalized);
                test_gpu_write32(&m, object + 0x1f0u + 42u * 4u,
                                 changed_normalized);
                m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
                CHECK(test_gpu_read32(&m, first) == 0x89abcdefu,
                      "%s nonuniform filtered scale changed the destination",
                      form->name);
                CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
                      "%s nonuniform filtered scale raised completion",
                      form->name);
                test_gpu_write32(&m, object + 0x1f0u + 10u * 4u,
                                 form->quad[10]);
                test_gpu_write32(&m, object + 0x1f0u + 14u * 4u,
                                 form->quad[14]);
                test_gpu_write32(&m, object + 0x1f0u + 32u * 4u,
                                 form->quad[32]);
                test_gpu_write32(&m, object + 0x1f0u + 42u * 4u,
                                 form->quad[42]);
            }

            uint32_t source_table = m.bus.read32(m.bus.ctx,
                MBX_BASE + REG_GART0 + (last_source >> 22) * 4u);
            uint32_t source_pte_address = source_table +
                (((last_source >> 12) & 0x3ffu) * 4u);
            uint32_t source_pte =
                m.bus.read32(m.bus.ctx, source_pte_address);
            test_gpu_write32(&m, first, 0x89abcdefu);
            test_gpu_write32(&m, last_destination, 0x76543210u);
            m.bus.write32(m.bus.ctx, source_pte_address, 0u);
            m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
            CHECK(test_gpu_read32(&m, first) == 0x89abcdefu &&
                  test_gpu_read32(&m, last_destination) == 0x76543210u,
                  "%s late missing filtered source PTE partially committed",
                  form->name);
            CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
                  "%s late missing filtered source PTE raised completion",
                  form->name);
            m.bus.write32(m.bus.ctx, source_pte_address, source_pte);

            if (form->column_resample || form->row_resample || tall_source) {
                uint32_t target_table = m.bus.read32(m.bus.ctx,
                    MBX_BASE + REG_GART0 + (last_destination >> 22) * 4u);
                uint32_t target_pte_address = target_table +
                    (((last_destination >> 12) & 0x3ffu) * 4u);
                uint32_t target_pte =
                    m.bus.read32(m.bus.ctx, target_pte_address);
                test_gpu_write32(&m, first, 0x89abcdefu);
                test_gpu_write32(&m, last_destination, 0x76543210u);
                m.bus.write32(m.bus.ctx, target_pte_address, 0u);
                m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
                CHECK(test_gpu_read32(&m, first) == 0x89abcdefu &&
                      m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
                      "%s missing target PTE partially committed or completed",
                      form->name);
                m.bus.write32(m.bus.ctx, target_pte_address, target_pte);
                CHECK(test_gpu_read32(&m, last_destination) == 0x76543210u,
                      "%s missing target PTE changed its late destination",
                      form->name);
            }
        }
    }

    test_gpu_write32(&m, first, 0x89abcdefu);
    test_gpu_write32(&m, object + 0x1f4u,
                     form->quad[1] ^ 0x00008000u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
    CHECK(test_gpu_read32(&m, first) == 0x89abcdefu,
          "%s unknown control changed the destination", form->name);
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "%s unknown control raised completion", form->name);

    free(expected);
    s5l8900_free(&m);
}

/* Safari's tabs transition retained this exact alternate-sampler packet. It
 * stretches a half-texel-wide source column over the 320-pixel surface while
 * reducing 222 source rows to 37 destination rows. The sampler mode comes
 * from the producer's texture state rather than the transform geometry, and
 * rejecting this coherent draw leaves AppleMBX waiting for 3DIdle. */
static const struct mbx_test_status_form safari_tabs_column_resample_form = {
    .name = "Safari tabs alternate filtered column resample",
    .xclip = 0x01400000u, .yclip = 0x00500020u,
    .target = 0x00a3d000u,
    .semantic_sprite = true,
    .scaled_sprite = true,
    .column_resample = true,
    .boundary_override = true,
    .arbitrary_bgra_probe = true,
    .tile_x0 = 0u, .tile_x1 = 0x27u,
    .tile_y0 = 2u, .tile_y1 = 4u,
    .left = 0u, .top = 42u, .width = 320u, .height = 37u,
    .source = 0x00983000u,
    .source_stride = 0x20u, .source_control = 0x8e000000u,
    .source_width = 1u, .source_height = 222u,
    .expected_covered_pixels = 11840u,
    .boundary = {
        0x00000000u, 0x429e0000u, 0x00000000u, 0x42280000u,
        0x43a00000u, 0x429e0000u, 0x43a00000u, 0x42280000u,
    },
    .quad = {
        0xe0000000u, 0xa0518001u, 0x8e013060u, 0xd6887610u,
        0xa7718000u, 0x0e5147a0u, 0xa3104620u, 0x22250e80u,
        0x00000000u, 0x42280000u, 0x43a00000u, 0x42280000u,
        0x00000000u, 0x429e0000u, 0x43a00000u, 0x429e0000u,
        0u, 0u, 0u, 0u,
        0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
        0xf9000000u, 0x00000000u, 0x00000000u, 0x00000000u,
        0x3d280000u, 0xf9000000u, 0x3d800000u, 0x00000000u,
        0x3ea00000u, 0x3d280000u, 0xf9000000u, 0x00000000u,
        0x3f5d8000u, 0x00000000u, 0x3d9e0000u, 0xf9000000u,
        0x3d800000u, 0x3f5d8000u, 0x3ea00000u, 0x3d9e0000u,
    },
};

/* The reverse Safari transition retained this modulated-sampler companion to
 * the tabs packet. It expands one half texel to 69 output pixels while
 * retaining the 31-row source height exactly. Together the direct, alternate
 * and modulated captures establish that this one-dimensional operation is
 * independent of the filtered sampler state. */
static const struct mbx_test_status_form safari_done_column_resample_form = {
    .name = "Safari Done modulated filtered column resample",
    .xclip = 0x013000e0u, .yclip = 0x00500020u,
    .target = 0x00a3d000u,
    .semantic_sprite = true,
    .scaled_sprite = true,
    .column_resample = true,
    .boundary_override = true,
    .arbitrary_bgra_probe = true,
    .tile_x0 = 0x1cu, .tile_x1 = 0x25u,
    .tile_y0 = 2u, .tile_y1 = 4u,
    .left = 228u, .top = 41u, .width = 69u, .height = 31u,
    .source = 0x00996000u,
    .source_stride = 0x20u, .source_control = 0x8e000000u,
    .source_width = 1u, .source_height = 31u,
    .expected_covered_pixels = 2139u,
    .boundary = {
        0x43640000u, 0x42900000u, 0x43640000u, 0x42240000u,
        0x43948000u, 0x42900000u, 0x43948000u, 0x42240000u,
    },
    .quad = {
        0xe0000000u, 0xa0218001u, 0x8e0132c0u, 0xcd206c40u,
        0xa7718000u, 0x0e5147a0u, 0xae504ea0u, 0x22250e80u,
        0x43640000u, 0x42240000u, 0x43948000u, 0x42240000u,
        0x43640000u, 0x42900000u, 0x43948000u, 0x42900000u,
        0u, 0u, 0u, 0u,
        0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
        0x08000000u, 0x00000000u, 0x00000000u, 0x3e640000u,
        0x3d240000u, 0x08000000u, 0x3d800000u, 0x00000000u,
        0x3e948000u, 0x3d240000u, 0x08000000u, 0x00000000u,
        0x3f780000u, 0x3e640000u, 0x3d900000u, 0x08000000u,
        0x3d800000u, 0x3f780000u, 0x3e948000u, 0x3d900000u,
    },
};

/* SpringBoard's return from Spotlight retained this modulated-sampler strip.
 * The producer's normalized float round-trip makes its one-texel horizontal
 * UV span 1.00000191 texels, so the conservative floor/ceil envelope covers
 * two source columns even though the sampled operation is still the same
 * narrow horizontal strip. It expands that strip over 272 pixels while
 * retaining all 31 source rows. */
static const struct mbx_test_status_form
spotlight_home_narrow_strip_resample_form = {
    .name = "Spotlight home modulated narrow-strip resample",
    .xclip = 0x01180000u, .yclip = 0x00400010u,
    .target = 0x00897000u,
    .semantic_sprite = true,
    .scaled_sprite = true,
    .column_resample = true,
    .boundary_override = true,
    .arbitrary_bgra_probe = true,
    .tile_x0 = 0u, .tile_x1 = 0x22u,
    .tile_y0 = 1u, .tile_y1 = 3u,
    .left = 2u, .top = 26u, .width = 272u, .height = 31u,
    .source = 0x00981000u,
    .source_stride = 0x0a0u, .source_control = 0x8e080000u,
    .source_width = 2u, .source_height = 31u,
    .expected_covered_pixels = 8432u,
    .boundary = {
        0x3f800000u, 0x42640000u, 0x3f800000u, 0x41d00000u,
        0x43890000u, 0x42640000u, 0x43890000u, 0x41d00000u,
    },
    .quad = {
        0xe0000000u, 0xa3218001u, 0x8e093020u, 0xcd206c40u,
        0xa7718000u, 0x0e5112e0u, 0xae504ea0u, 0x22250e80u,
        0x3ffffffbu, 0x41d00000u, 0x43890000u, 0x41d00000u,
        0x3ffffffbu, 0x42640000u, 0x43890000u, 0x42640000u,
        0u, 0u, 0u, 0u,
        0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
        0xed000000u, 0x3e880000u, 0x00000000u, 0x3afffffbu,
        0x3cd00000u, 0xed000000u, 0x3e900001u, 0x00000000u,
        0x3e890000u, 0x3cd00000u, 0xed000000u, 0x3e880000u,
        0x3f780000u, 0x3afffffbu, 0x3d640000u, 0xed000000u,
        0x3e900001u, 0x3f780000u, 0x3e890000u, 0x3d640000u,
    },
};

/* SpringBoard's Spotlight entry retained a direct-sampler, full-extent
 * texture packet. The direct sampler keeps the filtered producer's row-major
 * corner order even though its 0x0e texture state is unfiltered. Its 86x13
 * quad begins four pixels left of the surface, and the boundary clips the
 * resulting strict 1:1 crop to source columns 4..85. */
static const struct mbx_test_status_form
spotlight_entry_unfiltered_direct_crop_form = {
    .name = "Spotlight entry unfiltered direct crop",
    .xclip = 0x00580000u, .yclip = 0x00d000b0u,
    .target = 0x00998000u,
    .semantic_sprite = true,
    .boundary_override = true,
    .tile_x0 = 0u, .tile_x1 = 0x0au,
    .tile_y0 = 0x0bu, .tile_y1 = 0x0cu,
    .left = 0u, .top = 182u, .width = 82u, .height = 13u,
    .source = 0x00965080u, .source_x0 = 4u,
    .source_stride = 0x160u, .source_control = 0x0e140000u,
    .source_width = 86u, .source_height = 13u,
    .expected_covered_pixels = 1066u,
    .boundary = {
        0x00000000u, 0x43430000u, 0x00000000u, 0x43360000u,
        0x42a40000u, 0x43430000u, 0x42a40000u, 0x43360000u,
    },
    .quad = {
        0xe0000000u, 0xa4118001u, 0x0e152ca1u, 0xa6884710u,
        0xa7718000u, 0x0e513300u, 0xae504ea0u, 0x22250e80u,
        0xc0800000u, 0x43360000u, 0x42a40000u, 0x43360000u,
        0xc0800000u, 0x43430000u, 0x42a40000u, 0x43430000u,
        0u, 0u, 0u, 0u,
        0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
        0xff000000u, 0x00000000u, 0x00000000u, 0xbb800000u,
        0x3e360000u, 0xff000000u, 0x3f2c0000u, 0x00000000u,
        0x3da40000u, 0x3e360000u, 0xff000000u, 0x00000000u,
        0x3f500000u, 0xbb800000u, 0x3e430000u, 0xff000000u,
        0x3f2c0000u, 0x3f500000u, 0x3da40000u, 0x3e430000u,
    },
};

/* Entering Safari's address field retained these direct-sampler records. The
 * exact object words describe a 308x31 sampled envelope in a 312-pixel-pitch
 * texture. The producer keeps Y at 1:1 while reducing X to 203.666 or 209.101
 * pixels, so this is a bounded horizontal row resample rather than uniform
 * minification. The witness did not include the separate boundary object;
 * these integer boundary words are reconstructed from the established
 * producer floor/ceil envelope and its captured clip registers. */
static const struct mbx_test_status_form
safari_keyboard_address_row_resample_form = {
    .name = "Safari keyboard address-surface row resample",
    .xclip = 0x00d80000u, .yclip = 0x00500020u,
    .target = 0x00bd1000u,
    .semantic_sprite = true,
    .scaled_sprite = true,
    .row_resample = true,
    .boundary_override = true,
    .arbitrary_bgra_probe = true,
    .tile_x0 = 0u, .tile_x1 = 0x1au,
    .tile_y0 = 2u, .tile_y1 = 4u,
    .left = 6u, .top = 42u, .width = 204u, .height = 31u,
    .source = 0x00ac4080u,
    .source_stride = 0x4e0u, .source_control = 0x8e4c0000u,
    .source_width = 308u, .source_height = 31u,
    .expected_covered_pixels = 6324u,
    .boundary = {
        0x40c00000u, 0x42920000u, 0x40c00000u, 0x42240000u,
        0x43520000u, 0x42920000u, 0x43520000u, 0x42240000u,
    },
    .quad = {
        0xe0000000u, 0xa6218001u, 0x8e4d5881u, 0xa6884710u,
        0xa7718000u, 0x0e517a20u, 0xae504ea0u, 0x22250e80u,
        0x40c00000u, 0x4227f049u, 0x4351aa9au, 0x4227f049u,
        0x40c00000u, 0x4291f824u, 0x4351aa9au, 0x4291f824u,
        0u, 0u, 0u, 0u,
        0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
        0xff000000u, 0x00000000u, 0x00000000u, 0x3bc00000u,
        0x3d27f049u, 0xff000000u, 0x3f19c000u, 0x00000000u,
        0x3e51aa9au, 0x3d27f049u, 0xff000000u, 0x00000000u,
        0x3f740000u, 0x3bc00000u, 0x3d91f824u, 0xff000000u,
        0x3f19c000u, 0x3f740000u, 0x3e51aa9au, 0x3d91f824u,
    },
};

static const struct mbx_test_status_form
safari_keyboard_surface_row_resample_form = {
    .name = "Safari keyboard surface row resample",
    .xclip = 0x00d80000u, .yclip = 0x00500020u,
    .target = 0x00998000u,
    .semantic_sprite = true,
    .scaled_sprite = true,
    .row_resample = true,
    .boundary_override = true,
    .arbitrary_bgra_probe = true,
    .tile_x0 = 0u, .tile_x1 = 0x1au,
    .tile_y0 = 2u, .tile_y1 = 4u,
    .left = 6u, .top = 43u, .width = 209u, .height = 31u,
    .source = 0x00acf080u,
    .source_stride = 0x4e0u, .source_control = 0x8e4c0000u,
    .source_width = 308u, .source_height = 31u,
    .expected_covered_pixels = 6479u,
    .boundary = {
        0x40c00000u, 0x42960000u, 0x40c00000u, 0x422c0000u,
        0x43580000u, 0x42960000u, 0x43580000u, 0x422c0000u,
    },
    .quad = {
        0xe0000000u, 0xa6218001u, 0x8e4d59e1u, 0xa6884710u,
        0xa7718000u, 0x0e513300u, 0xae504ea0u, 0x22250e80u,
        0x40c00000u, 0x422c8699u, 0x435719e7u, 0x422c8699u,
        0x40c00000u, 0x4294434cu, 0x435719e7u, 0x4294434cu,
        0u, 0u, 0u, 0u,
        0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
        0xff000000u, 0x00000000u, 0x00000000u, 0x3bc00000u,
        0x3d2c8699u, 0xff000000u, 0x3f19c000u, 0x00000000u,
        0x3e5719e7u, 0x3d2c8699u, 0xff000000u, 0x00000000u,
        0x3f740000u, 0x3bc00000u, 0x3d94434cu, 0xff000000u,
        0x3f19c000u, 0x3f740000u, 0x3e5719e7u, 0x3d94434cu,
    },
};

/* The synchronized unlock-to-Safari transition retained these four exact
 * perspective records after the earlier compact minification packets had
 * completed.  Their 8- or 16-pixel texture allocations occupy sub-rectangles
 * of independently encoded 72- or 320-pixel linear rows.  The separate
 * boundary and tile records were not retained, so those words are reconstructed
 * from each exact integer quad and its captured clip registers. */
static const struct mbx_test_status_form safari_unlock_padded_row_forms[] = {
    {
        .name = "Safari unlock 16-wide allocation in 320-pixel row",
        .xclip = 0x00100000u, .yclip = 0x00200010u,
        .target = 0x00998000u,
        .semantic_sprite = true,
        .boundary_override = true,
        .tile_x0 = 0u, .tile_x1 = 1u,
        .tile_y0 = 1u, .tile_y1 = 1u,
        .left = 0u, .top = 19u, .width = 13u, .height = 1u,
        .source = 0x00a33080u, .source_row0 = 19u,
        .source_stride = 0x500u, .source_control = 0x0e500000u,
        .source_width = 13u, .source_height = 1u,
        .expected_covered_pixels = 13u,
        .boundary = {
            0x00000000u, 0x41a00000u, 0x00000000u, 0x41980000u,
            0x41500000u, 0x41a00000u, 0x41500000u, 0x41980000u,
        },
        .quad = {
            0xe0000000u, 0xa1218000u, 0x0e514661u, 0xcd206c40u,
            0xa7718000u, 0x0e513300u, 0xae504ea0u, 0x22250e80u,
            0x00000000u, 0x41a00000u, 0x00000000u, 0x41980000u,
            0x41500000u, 0x41a00000u, 0x41500000u, 0x41980000u,
            0u, 0u, 0u, 0u,
            0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
            0x14000000u, 0x00000000u, 0x3f200000u, 0x00000000u,
            0x3ca00000u, 0x14000000u, 0x00000000u, 0x3f180000u,
            0x00000000u, 0x3c980000u, 0x14000000u, 0x3f500000u,
            0x3f200000u, 0x3c500000u, 0x3ca00000u, 0x14000000u,
            0x3f500000u, 0x3f180000u, 0x3c500000u, 0x3c980000u,
        },
    },
    {
        .name = "Safari unlock 8-wide allocation in 320-pixel row A",
        .xclip = 0x00080000u, .yclip = 0x00200000u,
        .target = 0x00897000u,
        .semantic_sprite = true,
        .boundary_override = true,
        .tile_x0 = 0u, .tile_x1 = 0u,
        .tile_y0 = 0u, .tile_y1 = 1u,
        .left = 0u, .top = 10u, .width = 7u, .height = 10u,
        .source = 0x0098d080u, .source_row0 = 10u,
        .source_stride = 0x500u, .source_control = 0x0e500000u,
        .source_width = 7u, .source_height = 10u,
        .expected_covered_pixels = 70u,
        .boundary = {
            0x00000000u, 0x41a00000u, 0x00000000u, 0x41200000u,
            0x40e00000u, 0x41a00000u, 0x40e00000u, 0x41200000u,
        },
        .quad = {
            0xe0000000u, 0xa0218000u, 0x0e5131a1u, 0xcd206c40u,
            0xa7718000u, 0x0e5112e0u, 0xae504ea0u, 0x22250e80u,
            0x00000000u, 0x41a00000u, 0x00000000u, 0x41200000u,
            0x40e00000u, 0x41a00000u, 0x40e00000u, 0x41200000u,
            0u, 0u, 0u, 0u,
            0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
            0x0d000000u, 0x00000000u, 0x3f200000u, 0x00000000u,
            0x3ca00000u, 0x0d000000u, 0x00000000u, 0x3ea00000u,
            0x00000000u, 0x3c200000u, 0x0d000000u, 0x3f600000u,
            0x3f200000u, 0x3be00000u, 0x3ca00000u, 0x0d000000u,
            0x3f600000u, 0x3ea00000u, 0x3be00000u, 0x3c200000u,
        },
    },
    {
        .name = "Safari unlock 8-wide allocation in 320-pixel row B",
        .xclip = 0x00080000u, .yclip = 0x00200000u,
        .target = 0x00897000u,
        .semantic_sprite = true,
        .boundary_override = true,
        .tile_x0 = 0u, .tile_x1 = 0u,
        .tile_y0 = 0u, .tile_y1 = 1u,
        .left = 0u, .top = 10u, .width = 7u, .height = 10u,
        .source = 0x00a33080u, .source_row0 = 10u,
        .source_stride = 0x500u, .source_control = 0x0e500000u,
        .source_width = 7u, .source_height = 10u,
        .expected_covered_pixels = 70u,
        .boundary = {
            0x00000000u, 0x41a00000u, 0x00000000u, 0x41200000u,
            0x40e00000u, 0x41a00000u, 0x40e00000u, 0x41200000u,
        },
        .quad = {
            0xe0000000u, 0xa0218000u, 0x0e514661u, 0xcd206c40u,
            0xa7718000u, 0x0e5112e0u, 0xae504ea0u, 0x22250e80u,
            0x00000000u, 0x41a00000u, 0x00000000u, 0x41200000u,
            0x40e00000u, 0x41a00000u, 0x40e00000u, 0x41200000u,
            0u, 0u, 0u, 0u,
            0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
            0x09000000u, 0x00000000u, 0x3f200000u, 0x00000000u,
            0x3ca00000u, 0x09000000u, 0x00000000u, 0x3ea00000u,
            0x00000000u, 0x3c200000u, 0x09000000u, 0x3f600000u,
            0x3f200000u, 0x3be00000u, 0x3ca00000u, 0x09000000u,
            0x3f600000u, 0x3ea00000u, 0x3be00000u, 0x3c200000u,
        },
    },
    {
        .name = "Safari unlock 8-wide allocation in split 72-pixel row",
        .xclip = 0x00080000u, .yclip = 0x00200000u,
        .target = 0x00897000u,
        .semantic_sprite = true,
        .boundary_override = true,
        .tile_x0 = 0u, .tile_x1 = 0u,
        .tile_y0 = 0u, .tile_y1 = 1u,
        .left = 4u, .top = 10u, .width = 3u, .height = 7u,
        .source = 0x00995080u, .source_row0 = 9u,
        .source_stride = 0x120u, .source_control = 0x0e100000u,
        .source_width = 3u, .source_height = 7u,
        .expected_covered_pixels = 21u,
        .boundary = {
            0x40800000u, 0x41880000u, 0x40800000u, 0x41200000u,
            0x40e00000u, 0x41880000u, 0x40e00000u, 0x41200000u,
        },
        .quad = {
            0xe0000000u, 0xa0118001u, 0x0e1132a1u, 0xcd206c40u,
            0xa7718000u, 0x0e5112e0u, 0xae504ea0u, 0x22250e80u,
            0x40800000u, 0x41880000u, 0x40800000u, 0x41200000u,
            0x40e00000u, 0x41880000u, 0x40e00000u, 0x41200000u,
            0u, 0u, 0u, 0u,
            0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
            0x09000000u, 0x00000000u, 0x3f800000u, 0x3b800000u,
            0x3c880000u, 0x09000000u, 0x00000000u, 0x3f100000u,
            0x3b800000u, 0x3c200000u, 0x09000000u, 0x3ec00000u,
            0x3f800000u, 0x3be00000u, 0x3c880000u, 0x09000000u,
            0x3ec00000u, 0x3f100000u, 0x3be00000u, 0x3c200000u,
        },
    },
};

/* Opening Settings retained these four sprite records after the bounded
 * rejection witness was added.  Their object words are captured verbatim.
 * The separate boundary record was not part of that witness, so these
 * boundary words are reconstructed from the producer's established integer
 * floor/ceil envelope and reproduce the captured clip and tile registers.
 * Each sprite is subpixel-sized on one axis but still covers valid pixel
 * centres; rejecting it before the full scale and bounds validation leaves
 * AppleMBX waiting for completion during an otherwise valid transition. */
static const struct mbx_test_status_form
settings_transition_subpixel_forms[] = {
    {
        .name = "Settings transition direct subpixel column A",
        .xclip = 0x00b000a8u, .yclip = 0x010000f0u,
        .target = 0x00998000u,
        .semantic_sprite = true,
        .scaled_sprite = true,
        .boundary_override = true,
        .tile_x0 = 0x15u, .tile_x1 = 0x15u,
        .tile_y0 = 0x0fu, .tile_y1 = 0x0fu,
        .left = 169u, .top = 248u, .width = 1u, .height = 5u,
        .source = 0x00996000u,
        .source_stride = 0x40u, .source_control = 0x8e040000u,
        .source_width = 5u, .source_height = 27u,
        .expected_covered_pixels = 5u,
        .boundary = {
            0x43290000u, 0x437d0000u, 0x43290000u, 0x43770000u,
            0x432b0000u, 0x437d0000u, 0x432b0000u, 0x43770000u,
        },
        .quad = {
            0xe0000000u, 0xa1218000u, 0x8e0532c0u, 0xa6884710u,
            0xa7718000u, 0x0e513300u, 0xae504ea0u, 0x22250e80u,
            0x43294e72u, 0x4377c15fu, 0x432a469eu, 0x4377c15fu,
            0x43294e72u, 0x437cfd7fu, 0x432a469eu, 0x437cfd7fu,
            0u, 0u, 0u, 0u,
            0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
            0xff000000u, 0x00000000u, 0x00000000u, 0x3e294e72u,
            0x3e77c15fu, 0xff000000u, 0x3ea00000u, 0x00000000u,
            0x3e2a469eu, 0x3e77c15fu, 0xff000000u, 0x00000000u,
            0x3f540000u, 0x3e294e72u, 0x3e7cfd7fu, 0xff000000u,
            0x3ea00000u, 0x3f540000u, 0x3e2a469eu, 0x3e7cfd7fu,
        },
    },
    {
        .name = "Settings transition direct subpixel column B",
        .xclip = 0x00b800b0u, .yclip = 0x010000f0u,
        .target = 0x00998000u,
        .semantic_sprite = true,
        .scaled_sprite = true,
        .boundary_override = true,
        .tile_x0 = 0x16u, .tile_x1 = 0x16u,
        .tile_y0 = 0x0fu, .tile_y1 = 0x0fu,
        .left = 183u, .top = 248u, .width = 1u, .height = 5u,
        .source = 0x00996000u,
        .source_stride = 0x40u, .source_control = 0x8e040000u,
        .source_width = 5u, .source_height = 27u,
        .expected_covered_pixels = 5u,
        .boundary = {
            0x43360000u, 0x437d0000u, 0x43360000u, 0x43770000u,
            0x43380000u, 0x437d0000u, 0x43380000u, 0x43770000u,
        },
        .quad = {
            0xe0000000u, 0xa1218000u, 0x8e0532c0u, 0xa6884710u,
            0xa7718000u, 0x0e513300u, 0xae504ea0u, 0x22250e80u,
            0x4336af36u, 0x4377c15fu, 0x4337a762u, 0x4377c15fu,
            0x4336af36u, 0x437cfd7fu, 0x4337a762u, 0x437cfd7fu,
            0u, 0u, 0u, 0u,
            0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
            0xff000000u, 0x3ec00000u, 0x00000000u, 0x3e36af36u,
            0x3e77c15fu, 0xff000000u, 0x3f280000u, 0x00000000u,
            0x3e37a762u, 0x3e77c15fu, 0xff000000u, 0x3ec00000u,
            0x3f540000u, 0x3e36af36u, 0x3e7cfd7fu, 0xff000000u,
            0x3f280000u, 0x3f540000u, 0x3e37a762u, 0x3e7cfd7fu,
        },
    },
    {
        .name = "Settings transition alternate subpixel row",
        .xclip = 0x00a80098u, .yclip = 0x00f000e0u,
        .target = 0x00897000u,
        .semantic_sprite = true,
        .scaled_sprite = true,
        .boundary_override = true,
        .tile_x0 = 0x13u, .tile_x1 = 0x14u,
        .tile_y0 = 0x0eu, .tile_y1 = 0x0eu,
        .left = 155u, .top = 233u, .width = 10u, .height = 1u,
        .source = 0x00ada080u,
        .source_stride = 0x500u, .source_control = 0x8e500000u,
        .source_width = 320u, .source_height = 20u,
        .expected_covered_pixels = 10u,
        .boundary = {
            0x431b0000u, 0x436a0000u, 0x431b0000u, 0x43690000u,
            0x43250000u, 0x436a0000u, 0x43250000u, 0x43690000u,
        },
        .quad = {
            0xe0000000u, 0xa6218000u, 0x8e515b41u, 0xd6887610u,
            0xa7718000u, 0x0e5112e0u, 0xa3104620u, 0x22250e80u,
            0x431b5b3au, 0x43694211u, 0x4324a4c7u, 0x43694211u,
            0x431b5b3au, 0x4369d6aau, 0x4324a4c7u, 0x4369d6aau,
            0u, 0u, 0u, 0u,
            0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
            0x07000000u, 0x00000000u, 0x00000000u, 0x3e1b5b3au,
            0x3e694211u, 0x07000000u, 0x3f1fc000u, 0x00000000u,
            0x3e24a4c7u, 0x3e694211u, 0x07000000u, 0x00000000u,
            0x3f1c0000u, 0x3e1b5b3au, 0x3e69d6aau, 0x07000000u,
            0x3f1fc000u, 0x3f1c0000u, 0x3e24a4c7u, 0x3e69d6aau,
        },
    },
    {
        .name = "Settings transition modulated subpixel row",
        .xclip = 0x00a80098u, .yclip = 0x00f000e0u,
        .target = 0x00897000u,
        .semantic_sprite = true,
        .scaled_sprite = true,
        .boundary_override = true,
        .tile_x0 = 0x13u, .tile_x1 = 0x14u,
        .tile_y0 = 0x0eu, .tile_y1 = 0x0eu,
        .left = 155u, .top = 233u, .width = 10u, .height = 1u,
        .source = 0x00932080u,
        .source_stride = 0x500u, .source_control = 0x8e500000u,
        .source_width = 320u, .source_height = 20u,
        .expected_covered_pixels = 10u,
        .boundary = {
            0x431b0000u, 0x436a0000u, 0x431b0000u, 0x43690000u,
            0x43250000u, 0x436a0000u, 0x43250000u, 0x43690000u,
        },
        .quad = {
            0xe0000000u, 0xa6218000u, 0x8e512641u, 0xcd206c40u,
            0xa7718000u, 0x0e5112e0u, 0xae504ea0u, 0x22250e80u,
            0x431b5b3au, 0x43694211u, 0x4324a4c7u, 0x43694211u,
            0x431b5b3au, 0x4369d6aau, 0x4324a4c7u, 0x4369d6aau,
            0u, 0u, 0u, 0u,
            0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
            0x07000000u, 0x00000000u, 0x00000000u, 0x3e1b5b3au,
            0x3e694211u, 0x07000000u, 0x3f1fc000u, 0x00000000u,
            0x3e24a4c7u, 0x3e694211u, 0x07000000u, 0x00000000u,
            0x3f1c0000u, 0x3e1b5b3au, 0x3e69d6aau, 0x07000000u,
            0x3f1fc000u, 0x3f1c0000u, 0x3e24a4c7u, 0x3e69d6aau,
        },
    },
};

/* A right-edge conservative tile can be non-empty while the subpixel quad
 * contains no sample centre after the 320-pixel surface clip.  Completion is
 * a lifecycle property of this geometry, not of the captured addresses. */
static const struct mbx_test_status_form zero_coverage_status_form = {
    .name = "right-edge zero-coverage filtered sprite",
    .xclip = 0x01400138u, .yclip = 0x014000f0u,
    .target = 0x00998000u,
    .semantic_sprite = true,
    .boundary_override = true,
    .zero_coverage = true,
    .tile_x0 = 0x27u, .tile_x1 = 0x27u,
    .tile_y0 = 0x0fu, .tile_y1 = 0x13u,
    .left = 319u, .top = 242u, .width = 1u, .height = 63u,
    .source = 0x00972000u,
    .source_stride = 0x100u, .source_control = 0x8e100000u,
    .source_width = 59u, .source_height = 62u,
    .boundary = {
        0x439f8000u, 0x43988000u, 0x439f8000u, 0x43720000u,
        0x43a00000u, 0x43988000u, 0x43a00000u, 0x43720000u,
    },
    .quad = {
        0xe0000000u, 0xa3318000u, 0u, 0xa6884710u,
        0xa7718000u, 0x0e513300u, 0xae504ea0u, 0x22250e80u,
        0x439fc326u, 0x43729a02u, 0x43bd4326u, 0x43729a02u,
        0x439fc326u, 0x43984d01u, 0x43bd4326u, 0x43984d01u,
        0u, 0u, 0u, 0u,
        0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
        0xff000000u, 0u, 0u, 0x3e9fc326u,
        0x3e729a02u, 0xff000000u, 0x3f6a0000u, 0u,
        0x3ebd4326u, 0x3e729a02u, 0xff000000u, 0u,
        0x3f760000u, 0x3e9fc326u, 0x3e984d01u, 0xff000000u,
        0x3f6a0000u, 0x3f760000u, 0x3ebd4326u, 0x3e984d01u,
    },
};

static void test_later_tiled_status_sprites(void) {
    static const struct mbx_test_status_form forms[] = {
        {
            .name = "Searching status sprite",
            .xclip = 0x00500000u, .yclip = 0x00200000u,
            .tile_x0 = 0u, .tile_x1 = 9u, .tile_y0 = 0u, .tile_y1 = 1u,
            .left = 4u, .top = 1u, .width = 76u, .height = 16u,
            .source = 0x00995080u, .source_stride = 0x140u,
            .source_control = 0x0e140000u,
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
            .name = "battery status sprite",
            .xclip = 0x01400128u, .yclip = 0x00200000u,
            .tile_x0 = 0x25u, .tile_x1 = 0x27u,
            .tile_y0 = 0u, .tile_y1 = 1u,
            .left = 296u, .top = 0u, .width = 21u, .height = 20u,
            .source = 0x00997000u, .source_stride = 0x60u,
            .source_control = 0x0e040000u,
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
            .name = "clipped padlock tail",
            .xclip = 0x00a80098u, .yclip = 0x00200010u,
            .tile_x0 = 0x13u, .tile_x1 = 0x14u,
            .tile_y0 = 1u, .tile_y1 = 1u,
            .left = 155u, .top = 16u, .width = 10u, .height = 4u,
            .source = 0x00994000u, .source_row0 = 16u,
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
            .name = "clipped battery tail",
            .xclip = 0x01400128u, .yclip = 0x00200010u,
            .tile_x0 = 0x25u, .tile_x1 = 0x27u,
            .tile_y0 = 1u, .tile_y1 = 1u,
            .left = 296u, .top = 16u, .width = 21u, .height = 4u,
            .source = 0x00997000u, .source_row0 = 16u,
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
            .name = "full-width status-bar time sprite",
            .xclip = 0x01400000u, .yclip = 0x00200000u,
            .target = 0x00998000u,
            .tile_x0 = 0u, .tile_x1 = 0x27u,
            .tile_y0 = 0u, .tile_y1 = 1u,
            .left = 0u, .top = 0u, .width = 320u, .height = 20u,
            .source = 0x00a3a080u, .source_stride = 0x500u,
            .source_control = 0x0e500000u,
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
            .name = "full-width status-bar time sprite on CLCD surface",
            .xclip = 0x01400000u, .yclip = 0x00200000u,
            .target = 0x00897000u,
            .tile_x0 = 0u, .tile_x1 = 0x27u,
            .tile_y0 = 0u, .tile_y1 = 1u,
            .left = 0u, .top = 0u, .width = 320u, .height = 20u,
            .source = 0x00a3a080u, .source_stride = 0x500u,
            .source_control = 0x0e500000u,
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
            .name = "full-width status-bar time sprite on transition surface",
            .xclip = 0x01400000u, .yclip = 0x00200000u,
            .target = 0x00a41000u,
            .tile_x0 = 0u, .tile_x1 = 0x27u,
            .tile_y0 = 0u, .tile_y1 = 1u,
            .left = 0u, .top = 0u, .width = 320u, .height = 20u,
            .source = 0x00a3a080u, .source_stride = 0x500u,
            .source_control = 0x0e500000u,
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
            .name = "lower-screen opacity composite on transition surface",
            .xclip = 0x01400000u, .yclip = 0x01e00010u,
            .target = 0x00a41000u,
            .tile_x0 = 0u, .tile_x1 = 0x27u,
            .tile_y0 = 1u, .tile_y1 = 0x1du,
            .left = 0u, .top = 20u, .width = 320u, .height = 460u,
            .source = 0x00b12080u, .source_row0 = 20u,
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
            .name = "r430 unfiltered lower-screen crop on middle surface",
            .xclip = 0x01400000u, .yclip = 0x01e00010u,
            .target = 0x00998000u,
            .semantic_sprite = true,
            .tile_x0 = 0u, .tile_x1 = 0x27u,
            .tile_y0 = 1u, .tile_y1 = 0x1du,
            .left = 0u, .top = 20u, .width = 320u, .height = 460u,
            .source = 0x00a41080u, .source_row0 = 20u,
            .source_stride = 0x500u, .source_control = 0x0e500000u,
            .source_width = 320u, .source_height = 460u,
            .quad = {
                0xe0000000u, 0xa6618000u, 0u, 0xcd206c40u,
                0xa7718000u, 0u, 0xae504ea0u, 0x22250e80u,
                0x00000000u, 0x43f00000u, 0x00000000u, 0x41a00000u,
                0x43a00000u, 0x43f00000u, 0x43a00000u, 0x41a00000u,
                0u, 0u, 0u, 0u,
                0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
                0x01000000u, 0x00000000u, 0x3f700000u, 0x00000000u,
                0x3ef00000u, 0x01000000u, 0x00000000u, 0x3d200000u,
                0x00000000u, 0x3ca00000u, 0x01000000u, 0x3f200000u,
                0x3f700000u, 0x3ea00000u, 0x3ef00000u, 0x01000000u,
                0x3f200000u, 0x3d200000u, 0x3ea00000u, 0x3ca00000u,
            },
        },
        {
            .name = "tutorial popup on transition surface",
            .xclip = 0x01300010u, .yclip = 0x01800080u,
            .target = 0x00a41000u,
            .tile_x0 = 2u, .tile_x1 = 0x25u,
            .tile_y0 = 8u, .tile_y1 = 0x17u,
            .left = 18u, .top = 130u, .width = 284u, .height = 241u,
            .source = 0x00ba9080u,
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
            .name = "tutorial title on transition surface",
            .xclip = 0x01280018u, .yclip = 0x00b00090u,
            .target = 0x00a41000u,
            .tile_x0 = 3u, .tile_x1 = 0x24u,
            .tile_y0 = 9u, .tile_y1 = 0x0au,
            .left = 30u, .top = 145u, .width = 260u, .height = 23u,
            .source = 0x00bed080u,
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
            .name = "tutorial body on transition surface",
            .xclip = 0x01280018u, .yclip = 0x013000a0u,
            .target = 0x00a41000u,
            .tile_x0 = 3u, .tile_x1 = 0x24u,
            .tile_y0 = 0x0au, .tile_y1 = 0x12u,
            .left = 30u, .top = 175u, .width = 260u, .height = 121u,
            .source = 0x00bf3080u,
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
            .name = "tutorial dismiss button on transition surface",
            .xclip = 0x01280018u, .yclip = 0x01700130u,
            .target = 0x00a41000u,
            .tile_x0 = 3u, .tile_x1 = 0x24u,
            .tile_y0 = 0x13u, .tile_y1 = 0x16u,
            .left = 29u, .top = 312u, .width = 262u, .height = 43u,
            .source = 0x00c2b080u,
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
            .name = "Messages label on transition surface",
            .xclip = 0x00600000u, .yclip = 0x00700050u,
            .target = 0x00a41000u,
            .semantic_sprite = true,
            .boundary_override = true,
            .tile_x0 = 0u, .tile_x1 = 0x0bu,
            .tile_y0 = 5u, .tile_y1 = 6u,
            .left = 3u, .top = 94u, .width = 86u, .height = 13u,
            .source = 0x00931080u,
            .source_stride = 0x160u, .source_control = 0x8e140000u,
            .source_width = 86u, .source_height = 13u,
            .boundary = {
                0x40000000u, 0x42d60000u, 0x40000000u, 0x42ba0000u,
                0x42b20000u, 0x42d60000u, 0x42b20000u, 0x42ba0000u,
            },
            .quad = {
                0xe0000000u, 0xa4118001u, 0u, 0xa6884710u,
                0xa7718000u, 0u, 0xae504ea0u, 0x22250e80u,
                0x403a2398u, 0x42bbcd39u, 0x42b1d11du, 0x42bbcd39u,
                0x403a2398u, 0x42d5cd39u, 0x42b1d11du, 0x42d5cd39u,
                0u, 0u, 0u, 0u,
                0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
                0xff000000u, 0u, 0u, 0x3b3a2398u,
                0x3dbbcd39u, 0xff000000u, 0x3f2b0000u, 0u,
                0x3db1d11du, 0x3dbbcd39u, 0xff000000u, 0u,
                0x3f480000u, 0x3b3a2398u, 0x3dd5cd39u, 0xff000000u,
                0x3f2b0000u, 0x3f480000u, 0x3db1d11du, 0x3dd5cd39u,
            },
        },
        {
            .name = "Messages icon on transition surface",
            .xclip = 0x00500010u, .yclip = 0x00600020u,
            .target = 0x00a41000u,
            .semantic_sprite = true,
            .boundary_override = true,
            .tile_x0 = 2u, .tile_x1 = 9u,
            .tile_y0 = 2u, .tile_y1 = 5u,
            .left = 16u, .top = 32u, .width = 59u, .height = 62u,
            .source = 0x00933000u,
            .source_stride = 0x100u, .source_control = 0x8e100000u,
            .source_width = 59u, .source_height = 62u,
            .boundary = {
                0x41700000u, 0x42bc0000u, 0x41700000u, 0x41f80000u,
                0x42960000u, 0x42bc0000u, 0x42960000u, 0x41f80000u,
            },
            .quad = {
                0xe0000000u, 0xa3318000u, 0u, 0xa6884710u,
                0xa7718000u, 0u, 0xae504ea0u, 0x22250e80u,
                0x417e88e6u, 0x41ff34e4u, 0x4295d11du, 0x41ff34e4u,
                0x417e88e6u, 0x42bbcd39u, 0x4295d11du, 0x42bbcd39u,
                0u, 0u, 0u, 0u,
                0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
                0xff000000u, 0u, 0u, 0x3c7e88e6u,
                0x3cff34e4u, 0xff000000u, 0x3f6a0000u, 0u,
                0x3d95d11du, 0x3cff34e4u, 0xff000000u, 0u,
                0x3f760000u, 0x3c7e88e6u, 0x3dbbcd39u, 0xff000000u,
                0x3f6a0000u, 0x3f760000u, 0x3d95d11du, 0x3dbbcd39u,
            },
        },
        {
            .name = "Calendar label on transition surface",
            .xclip = 0x00a80048u, .yclip = 0x00700050u,
            .target = 0x00a41000u,
            .semantic_sprite = true,
            .boundary_override = true,
            .tile_x0 = 9u, .tile_x1 = 0x14u,
            .tile_y0 = 5u, .tile_y1 = 6u,
            .left = 79u, .top = 94u, .width = 86u, .height = 13u,
            .source = 0x00937080u,
            .source_stride = 0x160u, .source_control = 0x8e140000u,
            .source_width = 86u, .source_height = 13u,
            .boundary = {
                0x429c0000u, 0x42d60000u, 0x429c0000u, 0x42ba0000u,
                0x43250000u, 0x42d60000u, 0x43250000u, 0x42ba0000u,
            },
            .quad = {
                0xe0000000u, 0xa4118001u, 0u, 0xa6884710u,
                0xa7718000u, 0u, 0xae504ea0u, 0x22250e80u,
                0x429deb82u, 0x42bbbdfeu, 0x4324f5c1u, 0x42bbbdfeu,
                0x429deb82u, 0x42d5bdfeu, 0x4324f5c1u, 0x42d5bdfeu,
                0u, 0u, 0u, 0u,
                0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
                0xff000000u, 0u, 0u, 0x3d9deb82u,
                0x3dbbbdfeu, 0xff000000u, 0x3f2b0000u, 0u,
                0x3e24f5c1u, 0x3dbbbdfeu, 0xff000000u, 0u,
                0x3f480000u, 0x3d9deb82u, 0x3dd5bdfeu, 0xff000000u,
                0x3f2b0000u, 0x3f480000u, 0x3e24f5c1u, 0x3dd5bdfeu,
            },
        },
        {
            .name = "Calendar icon base on transition surface",
            .xclip = 0x00980058u, .yclip = 0x00600020u,
            .target = 0x00a41000u,
            .semantic_sprite = true,
            .boundary_override = true,
            .tile_x0 = 0x0bu, .tile_x1 = 0x12u,
            .tile_y0 = 2u, .tile_y1 = 5u,
            .left = 92u, .top = 32u, .width = 59u, .height = 62u,
            .source = 0x00939000u,
            .source_stride = 0x100u, .source_control = 0x8e100000u,
            .source_width = 59u, .source_height = 62u,
            .boundary = {
                0x42b60000u, 0x42bc0000u, 0x42b60000u, 0x41f80000u,
                0x43170000u, 0x42bc0000u, 0x43170000u, 0x41f80000u,
            },
            .quad = {
                0xe0000000u, 0xa3318000u, 0u, 0xa6884710u,
                0xa7718000u, 0u, 0xae504ea0u, 0x22250e80u,
                0x42b7eb82u, 0x41fef7fau, 0x4316f5c1u, 0x41fef7fau,
                0x42b7eb82u, 0x42bbbdfeu, 0x4316f5c1u, 0x42bbbdfeu,
                0u, 0u, 0u, 0u,
                0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
                0xff000000u, 0u, 0u, 0x3db7eb82u,
                0x3cfef7fau, 0xff000000u, 0x3f6a0000u, 0u,
                0x3e16f5c1u, 0x3cfef7fau, 0xff000000u, 0u,
                0x3f760000u, 0x3db7eb82u, 0x3dbbbdfeu, 0xff000000u,
                0x3f6a0000u, 0x3f760000u, 0x3e16f5c1u, 0x3dbbbdfeu,
            },
        },
        {
            .name = "Stocks label with producer alpha modulation",
            .xclip = 0x00a80048u, .yclip = 0x00d000b0u,
            .target = 0x00a41000u,
            .semantic_sprite = true,
            .boundary_override = true,
            .tile_x0 = 9u, .tile_x1 = 0x14u,
            .tile_y0 = 0x0bu, .tile_y1 = 0x0cu,
            .left = 79u, .top = 182u, .width = 86u, .height = 13u,
            .source = 0x00954080u,
            .source_stride = 0x160u, .source_control = 0x8e140000u,
            .source_width = 86u, .source_height = 13u,
            .boundary = {
                0x429c0000u, 0x43430000u, 0x429c0000u, 0x43350000u,
                0x43250000u, 0x43430000u, 0x43250000u, 0x43350000u,
            },
            .quad = {
                0xe0000000u, 0xa4118001u, 0u, 0xcd206c40u,
                0xa7718000u, 0u, 0xae504ea0u, 0x22250e80u,
                0x429dcd84u, 0x4335e866u, 0x4324e6c2u, 0x4335e866u,
                0x429dcd84u, 0x4342e866u, 0x4324e6c2u, 0x4342e866u,
                0u, 0u, 0u, 0u,
                0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
                0xfe000000u, 0u, 0u, 0x3d9dcd84u,
                0x3e35e866u, 0xfe000000u, 0x3f2b0000u, 0u,
                0x3e24e6c2u, 0x3e35e866u, 0xfe000000u, 0u,
                0x3f480000u, 0x3d9dcd84u, 0x3e42e866u, 0xfe000000u,
                0x3f2b0000u, 0x3f480000u, 0x3e24e6c2u, 0x3e42e866u,
            },
        },
        /* This is intentionally not described as a capture. It is constructed
         * from the recovered producer equations with a third size, pitch,
         * position and an otherwise unseen target, so a decoder that merely
         * moved the app or target literals into conditionals cannot pass it. */
        {
            .name = "producer-consistent 24x8 generic sprite",
            .xclip = 0x00e000c8u, .yclip = 0x00800070u,
            .target = 0x009b0000u,
            .semantic_sprite = true,
            .boundary_override = true,
            .tile_x0 = 25u, .tile_x1 = 27u,
            .tile_y0 = 7u, .tile_y1 = 7u,
            .left = 200u, .top = 120u, .width = 24u, .height = 8u,
            .source = 0x0093b000u,
            .source_stride = 0x60u, .source_control = 0x8e040000u,
            .source_width = 24u, .source_height = 8u,
            .boundary = {
                0x43480000u, 0x43000000u, 0x43480000u, 0x42f00000u,
                0x43600000u, 0x43000000u, 0x43600000u, 0x42f00000u,
            },
            .quad = {
                0xe0000000u, 0xa2018001u, 0u, 0xa6884710u,
                0xa7718000u, 0u, 0xae504ea0u, 0x22250e80u,
                0x43480000u, 0x42f00000u, 0x43600000u, 0x42f00000u,
                0x43480000u, 0x43000000u, 0x43600000u, 0x43000000u,
                0u, 0u, 0u, 0u,
                0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
                0xff000000u, 0u, 0u, 0x3e480000u,
                0x3df00000u, 0xff000000u, 0x3f3c0000u, 0u,
                0x3e600000u, 0x3df00000u, 0xff000000u, 0u,
                0x3f700000u, 0x3e480000u, 0x3e000000u, 0xff000000u,
                0x3f3c0000u, 0x3f700000u, 0x3e600000u, 0x3e000000u,
            },
        },
        {
            .name = "r415 page indicator on middle surface",
            .xclip = 0x00980088u, .yclip = 0x01900170u,
            .target = 0x00998000u,
            .semantic_sprite = true,
            .boundary_override = true,
            .tile_x0 = 0x11u, .tile_x1 = 0x12u,
            .tile_y0 = 0x17u, .tile_y1 = 0x18u,
            .left = 136u, .top = 375u, .width = 10u, .height = 10u,
            .source = 0x0092e000u,
            .source_stride = 0x40u, .source_control = 0x0e040000u,
            .boundary = {
                0x43080000u, 0x43c08000u, 0x43080000u, 0x43bb8000u,
                0x43120000u, 0x43c08000u, 0x43120000u, 0x43bb8000u,
            },
            .quad = {
                0xe0000000u, 0xa1118000u, 0u, 0xcd206c40u,
                0xa7718000u, 0u, 0xae504ea0u, 0x22250e80u,
                0x43080000u, 0x43c08000u, 0x43080000u, 0x43bb8000u,
                0x43120000u, 0x43c08000u, 0x43120000u, 0x43bb8000u,
                0u, 0u, 0u, 0u,
                0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
                0xdd000000u, 0u, 0x3f200000u, 0x3e080000u,
                0x3ec08000u, 0xdd000000u, 0u, 0u,
                0x3e080000u, 0x3ebb8000u, 0xdd000000u, 0x3f200000u,
                0x3f200000u, 0x3e120000u, 0x3ec08000u, 0xdd000000u,
                0x3f200000u, 0u, 0x3e120000u, 0x3ebb8000u,
            },
        },
        {
            .name = "r416 clipped Messages label on middle surface",
            .xclip = 0x00480000u, .yclip = 0x00600040u,
            .target = 0x00998000u,
            .semantic_sprite = true,
            .boundary_override = true,
            .tile_x0 = 0u, .tile_x1 = 8u,
            .tile_y0 = 4u, .tile_y1 = 5u,
            .left = 0u, .top = 73u, .width = 70u, .height = 13u,
            .source = 0x00931080u, .source_x0 = 16u,
            .source_stride = 0x160u, .source_control = 0x8e140000u,
            .source_width = 86u, .source_height = 13u,
            .boundary = {
                0x00000000u, 0x42ae0000u, 0x00000000u, 0x42920000u,
                0x428c0000u, 0x42ae0000u, 0x428c0000u, 0x42920000u,
            },
            .quad = {
                0xe0000000u, 0xa4118001u, 0u, 0xa6884710u,
                0xa7718000u, 0u, 0xae504ea0u, 0x22250e80u,
                0xc182c84cu, 0x42921807u, 0x428b4dedu, 0x42921807u,
                0xc182c84cu, 0x42ac1807u, 0x428b4dedu, 0x42ac1807u,
                0u, 0u, 0u, 0u,
                0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
                0xff000000u, 0u, 0u, 0xbc82c84cu,
                0x3d921807u, 0xff000000u, 0x3f2b0000u, 0u,
                0x3d8b4dedu, 0x3d921807u, 0xff000000u, 0u,
                0x3f480000u, 0xbc82c84cu, 0x3dac1807u, 0xff000000u,
                0x3f2b0000u, 0x3f480000u, 0x3d8b4dedu, 0x3dac1807u,
            },
        },
        {
            .name = "r417 guard-expanded Stocks label on middle surface",
            .xclip = 0x00900038u, .yclip = 0x00b000a0u,
            .target = 0x00998000u,
            .semantic_sprite = true,
            .boundary_override = true,
            .tile_x0 = 7u, .tile_x1 = 0x11u,
            .tile_y0 = 0x0au, .tile_y1 = 0x0au,
            .left = 58u, .top = 163u, .width = 86u, .height = 13u,
            .source = 0x00954080u,
            .source_stride = 0x160u, .source_control = 0x8e140000u,
            .source_width = 86u, .source_height = 13u,
            .boundary = {
                0x42680000u, 0x43300000u, 0x42680000u, 0x43220000u,
                0x43110000u, 0x43300000u, 0x43110000u, 0x43220000u,
            },
            .quad = {
                0xe0000000u, 0xa4118001u, 0u, 0xcd206c40u,
                0xa7718000u, 0u, 0xae504ea0u, 0x22250e80u,
                0x4268aca2u, 0x43228572u, 0x43102b28u, 0x43228572u,
                0x4268aca2u, 0x432f8572u, 0x43102b28u, 0x432f8572u,
                0u, 0u, 0u, 0u,
                0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
                0xbd000000u, 0u, 0u, 0x3d68aca2u,
                0x3e228572u, 0xbd000000u, 0x3f2b0000u, 0u,
                0x3e102b28u, 0x3e228572u, 0xbd000000u, 0u,
                0x3f480000u, 0x3d68aca2u, 0x3e2f8572u, 0xbd000000u,
                0x3f2b0000u, 0x3f480000u, 0x3e102b28u, 0x3e2f8572u,
            },
        },
        {
            .name = "r418 dock-clipped Settings label on middle surface",
            .xclip = 0x00480000u, .yclip = 0x01900170u,
            .target = 0x00998000u,
            .semantic_sprite = true,
            .boundary_override = true,
            .tile_x0 = 0u, .tile_x1 = 8u,
            .tile_y0 = 0x17u, .tile_y1 = 0x18u,
            .left = 0u, .top = 380u, .width = 71u, .height = 9u,
            .source = 0x0097e080u, .source_x0 = 15u,
            .source_stride = 0x160u, .source_control = 0x8e140000u,
            .source_width = 86u, .source_height = 13u,
            .boundary = {
                0x00000000u, 0x43c28000u, 0x00000000u, 0x43be0000u,
                0x428e0000u, 0x43c28000u, 0x428e0000u, 0x43be0000u,
            },
            .quad = {
                0xe0000000u, 0xa4118001u, 0u, 0xcd206c40u,
                0xa7718000u, 0u, 0xae504ea0u, 0x22250e80u,
                0xc170e2f4u, 0x43be09cau, 0x428de3a2u, 0x43be09cau,
                0xc170e2f4u, 0x43c489cau, 0x428de3a2u, 0x43c489cau,
                0u, 0u, 0u, 0u,
                0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
                0xbd000000u, 0u, 0u, 0xbc70e2f4u,
                0x3ebe09cau, 0xbd000000u, 0x3f2b0000u, 0u,
                0x3d8de3a2u, 0x3ebe09cau, 0xbd000000u, 0u,
                0x3f480000u, 0xbc70e2f4u, 0x3ec489cau, 0xbd000000u,
                0x3f2b0000u, 0x3f480000u, 0x3d8de3a2u, 0x3ec489cau,
            },
        },
        {
            .name = "r420 scaled filtered background on middle surface",
            .xclip = 0x00b00090u, .yclip = 0x011000d0u,
            .target = 0x00998000u,
            .semantic_sprite = true,
            .scaled_sprite = true,
            .boundary_override = true,
            .tile_x0 = 0x12u, .tile_x1 = 0x15u,
            .tile_y0 = 0x0du, .tile_y1 = 0x10u,
            .left = 146u, .top = 220u, .width = 28u, .height = 42u,
            .source = 0x00bad080u,
            .source_stride = 0x500u, .source_control = 0x8e500000u,
            .source_width = 320u, .source_height = 460u,
            .boundary = {
                0x43110000u, 0x43830000u, 0x43110000u, 0x435c0000u,
                0x432f0000u, 0x43830000u, 0x432f0000u, 0x435c0000u,
            },
            .quad = {
                0xe0000000u, 0xa6618000u, 0u, 0xd6887610u,
                0xa7718000u, 0u, 0xa3104620u, 0x22250e80u,
                0x4311982eu, 0x435c313fu, 0x432e67d2u, 0x435c313fu,
                0x4311982eu, 0x4382cddeu, 0x432e67d2u, 0x4382cddeu,
                0u, 0u, 0u, 0u,
                0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
                0x17000000u, 0u, 0u, 0x3e11982eu,
                0x3e5c313fu, 0x17000000u, 0x3f1fc000u, 0u,
                0x3e2e67d2u, 0x3e5c313fu, 0x17000000u, 0u,
                0x3f65c000u, 0x3e11982eu, 0x3e82cddeu, 0x17000000u,
                0x3f1fc000u, 0x3f65c000u, 0x3e2e67d2u, 0x3e82cddeu,
            },
        },
        {
            .name = "r421 scaled filtered status strip on middle surface",
            .xclip = 0x00b00090u, .yclip = 0x00e000d0u,
            .target = 0x00998000u,
            .semantic_sprite = true,
            .scaled_sprite = true,
            .boundary_override = true,
            .tile_x0 = 0x12u, .tile_x1 = 0x15u,
            .tile_y0 = 0x0du, .tile_y1 = 0x0du,
            .left = 146u, .top = 219u, .width = 28u, .height = 1u,
            .source = 0x00c44080u,
            .source_stride = 0x500u, .source_control = 0x8e500000u,
            .source_width = 320u, .source_height = 20u,
            .boundary = {
                0x43110000u, 0x435d0000u, 0x43110000u, 0x435a0000u,
                0x432f0000u, 0x435d0000u, 0x432f0000u, 0x435a0000u,
            },
            .quad = {
                0xe0000000u, 0xa6218000u, 0u, 0xcd206c40u,
                0xa7718000u, 0u, 0xae504ea0u, 0x22250e80u,
                0x4311982eu, 0x435a99e7u, 0x432e67d2u, 0x435a99e7u,
                0x4311982eu, 0x435c66e1u, 0x432e67d2u, 0x435c66e1u,
                0u, 0u, 0u, 0u,
                0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
                0x17000000u, 0u, 0u, 0x3e11982eu,
                0x3e5a99e7u, 0x17000000u, 0x3f1fc000u, 0u,
                0x3e2e67d2u, 0x3e5a99e7u, 0x17000000u, 0u,
                0x3f1c0000u, 0x3e11982eu, 0x3e5c66e1u, 0x17000000u,
                0x3f1fc000u, 0x3f1c0000u, 0x3e2e67d2u, 0x3e5c66e1u,
            },
        },
        {
            .name = "r422 mixed-edge filtered status sprite on middle surface",
            .xclip = 0x00a00090u, .yclip = 0x00e000d0u,
            .target = 0x00998000u,
            .semantic_sprite = true,
            .scaled_sprite = true,
            .boundary_override = true,
            .tile_x0 = 0x12u, .tile_x1 = 0x13u,
            .tile_y0 = 0x0du, .tile_y1 = 0x0du,
            .left = 146u, .top = 219u, .width = 7u, .height = 1u,
            .source = 0x00981080u,
            .source_stride = 0x140u, .source_control = 0x8e140000u,
            .source_width = 76u, .source_height = 16u,
            .boundary = {
                0x43110000u, 0x435d0000u, 0x43110000u, 0x435a0000u,
                0x43190000u, 0x435d0000u, 0x43190000u, 0x435a0000u,
            },
            .quad = {
                0xe0000000u, 0xa4118000u, 0u, 0xcd206c40u,
                0xa7718000u, 0u, 0xae504ea0u, 0x22250e80u,
                0x4311f460u, 0x435ab0f4u, 0x4318cc17u, 0x435ab0f4u,
                0x4311f460u, 0x435c21bcu, 0x4318cc17u, 0x435c21bcu,
                0u, 0u, 0u, 0u,
                0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
                0x17000000u, 0u, 0u, 0x3e11f460u,
                0x3e5ab0f4u, 0x17000000u, 0x3f170000u, 0u,
                0x3e18cc17u, 0x3e5ab0f4u, 0x17000000u, 0u,
                0x3f800000u, 0x3e11f460u, 0x3e5c21bcu, 0x17000000u,
                0x3f170000u, 0x3f800000u, 0x3e18cc17u, 0x3e5c21bcu,
            },
        },
        {
            .name = "r426 direct filtered subrectangle on CLCD surface",
            .xclip = 0x00f000e0u, .yclip = 0x00700050u,
            .target = 0x00897000u,
            .semantic_sprite = true,
            .boundary_override = true,
            .tile_x0 = 0x1cu, .tile_x1 = 0x1du,
            .tile_y0 = 5u, .tile_y1 = 6u,
            .left = 229u, .top = 83u, .width = 5u, .height = 27u,
            .source = 0x00997000u,
            .source_stride = 0x40u, .source_control = 0x8e040000u,
            .source_width = 5u, .source_height = 27u,
            .boundary = {
                0x43650000u, 0x42dc0000u, 0x43650000u, 0x42a60000u,
                0x436a0000u, 0x42dc0000u, 0x436a0000u, 0x42a60000u,
            },
            .quad = {
                0xe0000000u, 0xa1218000u, 0u, 0xa6884710u,
                0xa7718000u, 0u, 0xae504ea0u, 0x22250e80u,
                0x43650000u, 0x42a60000u, 0x436a0000u, 0x42a60000u,
                0x43650000u, 0x42dc0000u, 0x436a0000u, 0x42dc0000u,
                0u, 0u, 0u, 0u,
                0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
                0xff000000u, 0u, 0u, 0x3e650000u,
                0x3da60000u, 0xff000000u, 0x3ea00000u, 0u,
                0x3e6a0000u, 0x3da60000u, 0xff000000u, 0u,
                0x3f580000u, 0x3e650000u, 0x3ddc0000u, 0xff000000u,
                0x3ea00000u, 0x3f580000u, 0x3e6a0000u, 0x3ddc0000u,
            },
        },
        {
            .name = "r427 magnified filtered column on CLCD surface",
            .xclip = 0x013000e8u, .yclip = 0x00700050u,
            .target = 0x00897000u,
            .semantic_sprite = true,
            .boundary_override = true,
            .tile_x0 = 0x1du, .tile_x1 = 0x25u,
            .tile_y0 = 5u, .tile_y1 = 6u,
            .left = 234u, .top = 83u, .width = 63u, .height = 27u,
            .source = 0x00997000u,
            .source_stride = 0x40u, .source_control = 0x8e040000u,
            .source_width = 1u, .source_height = 27u,
            .boundary = {
                0x436a0000u, 0x42dc0000u, 0x436a0000u, 0x42a60000u,
                0x43948000u, 0x42dc0000u, 0x43948000u, 0x42a60000u,
            },
            .quad = {
                0xe0000000u, 0xa1218000u, 0u, 0xa6884710u,
                0xa7718000u, 0u, 0xae504ea0u, 0x22250e80u,
                0x436a0000u, 0x42a60000u, 0x43948000u, 0x42a60000u,
                0x436a0000u, 0x42dc0000u, 0x43948000u, 0x42dc0000u,
                0u, 0u, 0u, 0u,
                0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
                0xff000000u, 0x3ea00000u, 0u, 0x3e6a0000u,
                0x3da60000u, 0xff000000u, 0x3ec00000u, 0u,
                0x3e948000u, 0x3da60000u, 0xff000000u, 0x3ea00000u,
                0x3f580000u, 0x3e6a0000u, 0x3ddc0000u, 0xff000000u,
                0x3ec00000u, 0x3f580000u, 0x3e948000u, 0x3ddc0000u,
            },
        },
        {
            /* Captured from the unlock path that previously left AppleMBX's
             * 3DIdle field false and drove its watchdog into Graphics
             * Recovery Event forever. The direct filtered producer is not
             * magnifying here: it uniformly maps a 67x20 source rectangle to
             * a 49x15 covered destination. */
            .name = "unlock direct filtered minification",
            .xclip = 0x00f800c0u, .yclip = 0x01500130u,
            .target = 0x00897000u,
            .semantic_sprite = true,
            .scaled_sprite = true,
            .boundary_override = true,
            .arbitrary_bgra_probe = true,
            .tile_x0 = 0x18u, .tile_x1 = 0x1eu,
            .tile_y0 = 0x13u, .tile_y1 = 0x14u,
            .left = 199u, .top = 318u, .width = 49u, .height = 15u,
            .source = 0x00be2080u,
            .source_stride = 0x120u, .source_control = 0x8e100000u,
            .source_width = 67u, .source_height = 20u,
            .expected_covered_pixels = 735u,
            .boundary = {
                0x43470000u, 0x43a68000u, 0x43470000u, 0x439f0000u,
                0x43780000u, 0x43a68000u, 0x43780000u, 0x439f0000u,
            },
            .quad = {
                0xe0000000u, 0xa4218001u, 0u, 0xa6884710u,
                0xa7718000u, 0u, 0xae504ea0u, 0x22250e80u,
                0x43471a73u, 0x439f1a73u, 0x43779ed2u, 0x439f1a73u,
                0x43471a73u, 0x43a6583cu, 0x43779ed2u, 0x43a6583cu,
                0u, 0u, 0u, 0u,
                0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
                0xff000000u, 0u, 0u, 0x3e471a73u,
                0x3e9f1a73u, 0xff000000u, 0x3f050000u, 0u,
                0x3e779ed2u, 0x3e9f1a73u, 0xff000000u, 0u,
                0x3f1c0000u, 0x3e471a73u, 0x3ea6583cu, 0xff000000u,
                0x3f050000u, 0x3f1c0000u, 0x3e779ed2u, 0x3ea6583cu,
            },
        },
        {
            .name = "r434 rigid affine wiggle sprite",
            .xclip = 0x00600000u, .yclip = 0x01e001d0u,
            .target = 0x00998000u,
            .semantic_sprite = true,
            .affine_sprite = true,
            .scaled_sprite = true,
            .boundary_override = true,
            .tile_x0 = 0u, .tile_x1 = 0x0bu,
            .tile_y0 = 0x1du, .tile_y1 = 0x1du,
            .left = 2u, .top = 464u, .width = 87u, .height = 15u,
            .source = 0x00932080u,
            .source_stride = 0x160u, .source_control = 0x8e140000u,
            .source_width = 86u, .source_height = 13u,
            .expected_covered_pixels = 1118u,
            .boundary = {
                0x40000000u, 0x43ef8000u, 0x40000000u, 0x43e80000u,
                0x42b20000u, 0x43ef8000u, 0x42b20000u, 0x43e80000u,
            },
            .quad = {
                0xe0000000u, 0xa4118001u, 0u, 0xa6884710u,
                0xa7718000u, 0u, 0xae504ea0u, 0x22250e80u,
                0x4019f46cu, 0x43e8bfdeu, 0x42b0ce8bu, 0x43e87240u,
                0x401fd241u, 0x43ef3fd3u, 0x42b0fd7au, 0x43eef235u,
                0u, 0u, 0u, 0u,
                0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
                0xff000000u, 0u, 0u, 0x3b19f46cu,
                0x3ee8bfdeu, 0xff000000u, 0x3f2b0000u, 0u,
                0x3db0ce8bu, 0x3ee87240u, 0xff000000u, 0u,
                0x3f480000u, 0x3b1fd241u, 0x3eef3fd3u, 0xff000000u,
                0x3f2b0000u, 0x3f480000u, 0x3db0fd7au, 0x3eeef235u,
            },
        },
        {
            .name = "r437 uniformly magnified modulated wiggle label",
            .xclip = 0x00600000u, .yclip = 0x00700050u,
            .target = 0x00897000u,
            .semantic_sprite = true,
            .scaled_sprite = true,
            .boundary_override = true,
            .tile_x0 = 0u, .tile_x1 = 0x0bu,
            .tile_y0 = 5u, .tile_y1 = 6u,
            .left = 3u, .top = 94u, .width = 86u, .height = 13u,
            .source = 0x00bd7080u,
            .source_stride = 0x160u, .source_control = 0x8e140000u,
            .source_width = 86u, .source_height = 13u,
            .boundary = {
                0x40000000u, 0x42d80000u, 0x40000000u, 0x42bc0000u,
                0x42b40000u, 0x42d80000u, 0x42b40000u, 0x42bc0000u,
            },
            .quad = {
                0xe0000000u, 0xa4118001u, 0u, 0xcd206c40u,
                0xa7718000u, 0u, 0xae504ea0u, 0x22250e80u,
                0x402e01fau, 0x42bc4de5u, 0x42b29353u, 0x42bc4de5u,
                0x402e01fau, 0x42d679ecu, 0x42b29353u, 0x42d679ecu,
                0u, 0u, 0u, 0u,
                0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
                0xfc000000u, 0u, 0u, 0x3b2e01fau,
                0x3dbc4de5u, 0xfc000000u, 0x3f2b0000u, 0u,
                0x3db29353u, 0x3dbc4de5u, 0xfc000000u, 0u,
                0x3f480000u, 0x3b2e01fau, 0x3dd679ecu, 0xfc000000u,
                0x3f2b0000u, 0x3f480000u, 0x3db29353u, 0x3dd679ecu,
            },
        },
        {
            .name = "r438 rotated magnified modulated wiggle label",
            .xclip = 0x00680000u, .yclip = 0x00800060u,
            .target = 0x00af8000u,
            .semantic_sprite = true,
            .affine_sprite = true,
            .scaled_sprite = true,
            .boundary_override = true,
            .tile_x0 = 0u, .tile_x1 = 0x0cu,
            .tile_y0 = 6u, .tile_y1 = 7u,
            .left = 0u, .top = 97u, .width = 102u, .height = 21u,
            .source = 0x00bd7080u,
            .source_stride = 0x160u, .source_control = 0x8e140000u,
            .source_width = 86u, .source_height = 13u,
            .expected_covered_pixels = 1645u,
            .boundary = {
                0x00000000u, 0x42ec0000u, 0x00000000u, 0x42c20000u,
                0x42cc0000u, 0x42ec0000u, 0x42cc0000u, 0x42c20000u,
            },
            .quad = {
                0xe0000000u, 0xa4118001u, 0u, 0xcd206c40u,
                0xa7718000u, 0u, 0xae504ea0u, 0x22250e80u,
                0xc0d283e2u, 0x42c99c02u, 0x42c9b58eu, 0x42c207f1u,
                0xc0c02fa1u, 0x42ea16d6u, 0x42cadad2u, 0x42e282c6u,
                0u, 0u, 0u, 0u,
                0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
                0x80000000u, 0u, 0u, 0xbbd283e2u,
                0x3dc99c02u, 0x80000000u, 0x3f2b0000u, 0u,
                0x3dc9b58eu, 0x3dc207f1u, 0x80000000u, 0u,
                0x3f480000u, 0xbbc02fa1u, 0x3dea16d6u, 0x80000000u,
                0x3f2b0000u, 0x3f480000u, 0x3dcadad2u, 0x3de282c6u,
            },
        },
        {
            .name = "slider label on CLCD surface",
            .xclip = 0x01180070u, .yclip = 0x01c001a0u,
            .target = 0x00897000u,
            .variable_vertex_alpha = true,
            .tile_x0 = 0x0eu, .tile_x1 = 0x22u,
            .tile_y0 = 0x1au, .tile_y1 = 0x1bu,
            .left = 114u, .top = 417u, .width = 161u, .height = 30u,
            .source = 0x00a2e080u, .source_stride = 0x2a0u,
            .source_control = 0x0e280000u,
            .quad = {
                0xe0000000u, 0xa5218001u, 0u, 0xcd206c40u,
                0xa7718000u, 0u, 0xae504ea0u, 0x22250e80u,
                0x42e40000u, 0x43df8000u, 0x42e40000u, 0x43d08000u,
                0x43898000u, 0x43df8000u, 0x43898000u, 0x43d08000u,
                0u, 0u, 0u, 0u,
                0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
                0xb4000000u, 0u, 0x3f700000u, 0x3de40000u,
                0x3edf8000u, 0xb4000000u, 0u, 0u,
                0x3de40000u, 0x3ed08000u, 0xb4000000u, 0x3f210000u,
                0x3f700000u, 0x3e898000u, 0x3edf8000u, 0xb4000000u,
                0x3f210000u, 0u, 0x3e898000u, 0x3ed08000u,
            },
        },
        {
            .name = "slider label on back surface",
            .xclip = 0x01180070u, .yclip = 0x01c001a0u,
            .target = 0x00a33000u,
            .variable_vertex_alpha = true,
            .tile_x0 = 0x0eu, .tile_x1 = 0x22u,
            .tile_y0 = 0x1au, .tile_y1 = 0x1bu,
            .left = 114u, .top = 417u, .width = 161u, .height = 30u,
            .source = 0x00a2e080u, .source_stride = 0x2a0u,
            .source_control = 0x0e280000u,
            .quad = {
                0xe0000000u, 0xa5218001u, 0u, 0xcd206c40u,
                0xa7718000u, 0u, 0xae504ea0u, 0x22250e80u,
                0x42e40000u, 0x43df8000u, 0x42e40000u, 0x43d08000u,
                0x43898000u, 0x43df8000u, 0x43898000u, 0x43d08000u,
                0u, 0u, 0u, 0u,
                0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
                0x8a000000u, 0u, 0x3f700000u, 0x3de40000u,
                0x3edf8000u, 0x8a000000u, 0u, 0u,
                0x3de40000u, 0x3ed08000u, 0x8a000000u, 0x3f210000u,
                0x3f700000u, 0x3e898000u, 0x3edf8000u, 0x8a000000u,
                0x3f210000u, 0u, 0x3e898000u, 0x3ed08000u,
            },
        },
        {
            .name = "slider label on middle surface",
            .xclip = 0x01180070u, .yclip = 0x01c001a0u,
            .target = 0x00998000u,
            .variable_vertex_alpha = true,
            .tile_x0 = 0x0eu, .tile_x1 = 0x22u,
            .tile_y0 = 0x1au, .tile_y1 = 0x1bu,
            .left = 114u, .top = 417u, .width = 161u, .height = 30u,
            .source = 0x00a2e080u, .source_stride = 0x2a0u,
            .source_control = 0x0e280000u,
            .quad = {
                0xe0000000u, 0xa5218001u, 0u, 0xcd206c40u,
                0xa7718000u, 0u, 0xae504ea0u, 0x22250e80u,
                0x42e40000u, 0x43df8000u, 0x42e40000u, 0x43d08000u,
                0x43898000u, 0x43df8000u, 0x43898000u, 0x43d08000u,
                0u, 0u, 0u, 0u,
                0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
                0x8a000000u, 0u, 0x3f700000u, 0x3de40000u,
                0x3edf8000u, 0x8a000000u, 0u, 0u,
                0x3de40000u, 0x3ed08000u, 0x8a000000u, 0x3f210000u,
                0x3f700000u, 0x3e898000u, 0x3edf8000u, 0x8a000000u,
                0x3f210000u, 0u, 0x3e898000u, 0x3ed08000u,
            },
        },
        {
            .name = "slider label progressed back surface",
            .xclip = 0x01180070u, .yclip = 0x01c001a0u,
            .target = 0x00a33000u,
            .variable_vertex_alpha = true,
            .tile_x0 = 0x0eu, .tile_x1 = 0x22u,
            .tile_y0 = 0x1au, .tile_y1 = 0x1bu,
            .left = 114u, .top = 417u, .width = 161u, .height = 30u,
            .source = 0x00a2e080u, .source_stride = 0x2a0u,
            .source_control = 0x0e280000u,
            .quad = {
                0xe0000000u, 0xa5218001u, 0u, 0xcd206c40u,
                0xa7718000u, 0u, 0xae504ea0u, 0x22250e80u,
                0x42e40000u, 0x43df8000u, 0x42e40000u, 0x43d08000u,
                0x43898000u, 0x43df8000u, 0x43898000u, 0x43d08000u,
                0u, 0u, 0u, 0u,
                0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
                0x61000000u, 0u, 0x3f700000u, 0x3de40000u,
                0x3edf8000u, 0x61000000u, 0u, 0u,
                0x3de40000u, 0x3ed08000u, 0x61000000u, 0x3f210000u,
                0x3f700000u, 0x3e898000u, 0x3edf8000u, 0x61000000u,
                0x3f210000u, 0u, 0x3e898000u, 0x3ed08000u,
            },
        },
    };
    for (unsigned i = 0; i < sizeof forms / sizeof forms[0]; i++)
        test_captured_status_form(&forms[i]);

    struct mbx_test_status_form phase =
        forms[sizeof forms / sizeof forms[0] - 1u];
    phase.name = "slider label later back-surface phase";
    phase.quad[24] = phase.quad[29] = phase.quad[34] = phase.quad[39] =
        0x37000000u;
    test_captured_status_form(&phase);

    phase.name = "slider label progressed CLCD-surface phase";
    phase.target = 0x00897000u;
    phase.quad[24] = phase.quad[29] = phase.quad[34] = phase.quad[39] =
        0x61000000u;
    test_captured_status_form(&phase);

    test_captured_status_form(&safari_tabs_column_resample_form);
    test_captured_status_form(&safari_done_column_resample_form);
    test_captured_status_form(&spotlight_home_narrow_strip_resample_form);
    test_captured_status_form(&spotlight_entry_unfiltered_direct_crop_form);
    test_captured_status_form(&safari_keyboard_address_row_resample_form);
    test_captured_status_form(&safari_keyboard_surface_row_resample_form);
    for (unsigned i = 0;
         i < sizeof safari_unlock_padded_row_forms /
                 sizeof safari_unlock_padded_row_forms[0];
         i++)
        test_captured_status_form(&safari_unlock_padded_row_forms[i]);
    struct mbx_test_status_form keyboard_clip =
        safari_keyboard_surface_row_resample_form;
    keyboard_clip.name = "Safari keyboard lower-left row-resample clip";
    keyboard_clip.xclip = 0x00100000u;
    keyboard_clip.yclip = 0x00500040u;
    keyboard_clip.tile_x0 = 0u;
    keyboard_clip.tile_x1 = 1u;
    keyboard_clip.tile_y0 = keyboard_clip.tile_y1 = 4u;
    keyboard_clip.left = 6u;
    keyboard_clip.top = 64u;
    keyboard_clip.width = 10u;
    keyboard_clip.height = 10u;
    keyboard_clip.expected_covered_pixels = 100u;
    const uint32_t keyboard_left_boundary[8] = {
        0x40c00000u, 0x42960000u, 0x40c00000u, 0x42800000u,
        0x41800000u, 0x42960000u, 0x41800000u, 0x42800000u,
    };
    memcpy(keyboard_clip.boundary, keyboard_left_boundary,
           sizeof keyboard_clip.boundary);
    test_captured_status_form(&keyboard_clip);

    keyboard_clip.name = "Safari keyboard lower-right row-resample clip";
    keyboard_clip.xclip = 0x00d80010u;
    keyboard_clip.tile_x0 = 2u;
    keyboard_clip.tile_x1 = 0x1au;
    keyboard_clip.left = 16u;
    keyboard_clip.width = 199u;
    keyboard_clip.expected_covered_pixels = 1990u;
    const uint32_t keyboard_right_boundary[8] = {
        0x41800000u, 0x42960000u, 0x41800000u, 0x42800000u,
        0x43580000u, 0x42960000u, 0x43580000u, 0x42800000u,
    };
    memcpy(keyboard_clip.boundary, keyboard_right_boundary,
           sizeof keyboard_clip.boundary);
    test_captured_status_form(&keyboard_clip);
    for (unsigned i = 0;
         i < sizeof settings_transition_subpixel_forms /
                 sizeof settings_transition_subpixel_forms[0];
         i++)
        test_captured_status_form(&settings_transition_subpixel_forms[i]);
    test_captured_status_form(&zero_coverage_status_form);
    struct mbx_test_status_form relocated_zero = zero_coverage_status_form;
    relocated_zero.name = "relocated right-edge zero-coverage sprite";
    relocated_zero.source = 0x00b72000u;
    relocated_zero.target = 0x00c98000u;
    test_captured_status_form(&relocated_zero);
}

static void test_os1_empty_clip(void) {
    enum {
        LEFT = 319u, TOP = 380u, RIGHT = 320u, BOTTOM = 396u,
        SOURCE_STRIDE = 0x100u, TEXTURE_HEIGHT = 16u,
        TARGET_STRIDE = 0x500u,
    };
    const uint32_t table0 = 0x08003000u;
    const uint32_t table2 = 0x08004000u;
    const uint32_t region = 0x00001000u;
    const uint32_t object = 0x00014000u;
    const uint32_t source = 0x00b56080u;
    const uint32_t target = 0x00998000u;
    const uint32_t region_pa = 0x08010000u;
    const uint32_t object_pa = 0x08014000u;
    const uint32_t source_pa = 0x08020000u;
    const uint32_t target_pa = 0x080c0000u;

    s5l8900_t m;
    CHECK(s5l8900_init(&m, RAM_BASE, RAM_SIZE),
          "clipped zero-coverage machine init failed");
    if (!m.ram) return;
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_GART0, table0);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_GART2, table2);
    test_map_gpu_page(&m, table0, region, region_pa);
    test_map_gpu_page(&m, table0, object, object_pa);

    uint32_t source_page0 = source & ~0xfffu;
    uint32_t source_last = source + TEXTURE_HEIGHT * SOURCE_STRIDE - 1u;
    for (uint32_t page = source_page0;
         page <= (source_last & ~0xfffu); page += 0x1000u)
        test_map_gpu_page(&m, table2, page,
                          source_pa + (page - source_page0));

    uint32_t first = target + TOP * TARGET_STRIDE + LEFT * 4u;
    uint32_t last = target + (BOTTOM - 1u) * TARGET_STRIDE +
                    (RIGHT - 1u) * 4u;
    uint32_t target_page0 = first & ~0xfffu;
    for (uint32_t page = target_page0;
         page <= (last & ~0xfffu); page += 0x1000u)
        test_map_gpu_page(&m, table2, page,
                          target_pa + (page - target_page0));

    for (uint32_t off = 0u; off < 0x500u; off += 4u)
        test_gpu_write32(&m, object + off, 0u);
    for (unsigned i = 0; i < 26u; i++)
        test_gpu_write32(&m, object + i * 4u, 0x5a000000u ^ i);

    uint32_t list = object + 0x68u;
    test_gpu_write32(&m, list, 0x60200020u);
    test_gpu_write32(&m, list + 4u, 0x6020002du);
    test_gpu_write32(&m, list + 8u, 0x6120003au);
    test_gpu_write32(&m, list + 12u, 0xf0000000u);

    static const struct {
        uint16_t off;
        uint32_t value;
    } boundary_fixed[] = {
        {0x080u, 0x22206f80u}, {0x088u, 0x45800000u},
        {0x094u, 0x45800000u}, {0x098u, 0x45800000u},
        {0x09cu, 0x45800000u}, {0x0b4u, 0x22207f80u},
    };
    for (unsigned i = 0;
         i < sizeof boundary_fixed / sizeof boundary_fixed[0]; i++)
        test_gpu_write32(&m, object + boundary_fixed[i].off,
                         boundary_fixed[i].value);
    const uint32_t boundary[8] = {
        LEFT, BOTTOM, LEFT, TOP, RIGHT, BOTTOM, RIGHT, TOP,
    };
    for (unsigned i = 0; i < 8u; i++)
        test_gpu_write32(&m, object + 0x0b8u + i * 4u,
                         test_float_word((float)boundary[i]));

    const uint32_t record_offset = 0x0e8u;
    const uint32_t record[33] = {
        0xe0000000u, 0xa3118000u, 0x8e100000u | (source >> 7), 0xa6887610u,
        0x22220e80u, 0x439fc688u, 0x43be4e57u, 0x43bd4688u,
        0x43be4e57u, 0x439fc688u, 0x43c5ce57u, 0x43bd4688u,
        0x43c5ce57u, 0x00000000u, 0x00000000u, 0x00000000u,
        0x00000000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
        0x3f800000u, 0xff000000u, 0x3c000000u, 0x3d000000u,
        0xff000000u, 0x3f6a0000u, 0x3d000000u, 0xff000000u,
        0x3c000000u, 0x3f680000u, 0xff000000u, 0x3f6a0000u,
        0x3f680000u,
    };
    for (unsigned i = 0; i < 33u; i++)
        test_gpu_write32(&m, object + record_offset + i * 4u, record[i]);

    /* Empty x clip emits no tiles; stale region storage is not drawing work. */
    test_gpu_write32(&m, region, 0xdeadbeefu);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RGNBASE, region);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_OBJBASE, object);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_PIXSAMP, 0x00020007u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBCTL, 6u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBXCLIP, 0x01400140u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBYCLIP, 0x01900170u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBSTART, target);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBSTRIDE, 320u);

    test_gpu_write32(&m, first, 0x89abcdefu);
    test_gpu_write32(&m, last, 0x76543210u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
    CHECK(test_gpu_read32(&m, first) == 0x89abcdefu &&
          test_gpu_read32(&m, last) == 0x76543210u,
          "clipped zero-coverage draw changed its target row");
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0x4cu,
          "clipped zero-coverage draw did not raise all completion events");
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_ACK, 0x4cu);

    uint32_t late_source_page = source_last & ~0xfffu;
    uint32_t source_pte_address = table2 +
        (((late_source_page >> 12) & 0x3ffu) * 4u);
    uint32_t source_pte = m.bus.read32(m.bus.ctx, source_pte_address);
    m.bus.write32(m.bus.ctx, source_pte_address, 0u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
    m.bus.write32(m.bus.ctx, source_pte_address, source_pte);
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "missing zero-coverage source PTE raised completion");

    uint32_t target_pte_address = table2 +
        (((target_page0 >> 12) & 0x3ffu) * 4u);
    uint32_t target_pte = m.bus.read32(m.bus.ctx, target_pte_address);
    m.bus.write32(m.bus.ctx, target_pte_address, 0u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
    m.bus.write32(m.bus.ctx, target_pte_address, target_pte);
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "missing zero-coverage target PTE raised completion");

    CHECK(test_gpu_read32(&m, region) == 0xdeadbeefu,
          "empty clip changed stale tile storage");
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBXCLIP, 0x01380138u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "unrelated empty scissor bypassed producer validation");
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_FBXCLIP, 0x01400140u);
    test_gpu_write32(&m, object + record_offset + 4u, record[1] ^ 0x8000u);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0u,
          "unknown texture header bypassed empty-clip validation");
    test_gpu_write32(&m, object + record_offset + 4u, record[1]);
    m.bus.write32(m.bus.ctx, MBX_BASE + REG_RENDER, 1u);
    CHECK(m.bus.read32(m.bus.ctx, MBX_BASE + REG_STATUS) == 0x4cu &&
          test_gpu_read32(&m, first) == 0x89abcdefu &&
          test_gpu_read32(&m, last) == 0x76543210u,
          "valid empty clip failed after malformed submissions");
    s5l8900_free(&m);
}

static void test_os1_settings_clip(void) {
    struct mbx_test_status_form form = {
        .name = "OS1 captured Settings nearest-centre clip",
        .xclip = 0x00780070u, .yclip = 0x00c000a0u,
        .semantic_sprite = true, .scaled_sprite = true,
        .variable_vertex_alpha = true, .boundary_override = true,
        .tile_x0 = 14u, .tile_x1 = 14u, .tile_y0 = 10u, .tile_y1 = 11u,
        .left = 112u, .top = 174u, .width = 5u, .height = 6u,
        .expected_covered_pixels = 30u,
        .source = 0x00970000u, .source_stride = 96u,
        .source_control = 0x8e040000u, .source_width = 19u, .source_height = 20u,
        .quad = {
            0xe0000000u, 0xa2218001u, 0x8e0718e1u, 0xcd206c40u,
            0xa7718000u, 0x0e531ae0u, 0xae504ea0u, 0x22250e80u,
            0x42df023fu, 0x432df40bu, 0x42ead262u, 0x432df40bu,
            0x42df023fu, 0x43342bb2u, 0x42ead262u, 0x43342bb2u,
            0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
            0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
            0x14000000u, 0x3c800000u, 0x3c800000u, 0x3ddf023fu,
            0x3e2df40bu, 0x14000000u, 0x3f140000u, 0x3c800000u,
            0x3dead262u, 0x3e2df40bu, 0x14000000u, 0x3c800000u,
            0x3f1c0000u, 0x3ddf023fu, 0x3e342bb2u, 0x14000000u,
            0x3f140000u, 0x3f1c0000u, 0x3dead262u, 0x3e342bb2u,
        },
    };
    const float boundary[8] = {111,181,111,173,118,181,118,173};
    for(unsigned i=0;i<8;i++) form.boundary[i]=test_float_word(boundary[i]);
    test_captured_status_form(&form);
}

/* Exact failed OS1 Settings copy, from the three-command ring+dcf0 batch. */
static const uint32_t clipped_capture[14] = {
    0xa0060500u, 0x018d7000u, 0x94060500u, 0x01adb080u, 0x30000000u,
    1u,          0x01400000u, 0x01e00014u, 0x60800200u, 0x8004ccccu,
    0xffffffffu, 0x00000014u, 0x014001e0u, 0x70000000u};
#define CLIP_TARGET 0x018d7000u
#define CLIP_SOURCE 0x01adb080u
#define CLIP_TABLE 0x08003000u
#define CLIP_SOURCE_PA 0x08010000u
#define CLIP_TARGET_PA 0x080c0000u
#define CLIP_BYTES (320u * 480u * 4u)
static uint32_t clip_colour(unsigned x, unsigned y) {
    return 0xff000000u | (y << 12) | x;
}
static void clip_clear(s5l8900_t *m) {
    memset(m->ram + CLIP_TARGET_PA - RAM_BASE, 0x5a, CLIP_BYTES);
    m->bus.write32(m->bus.ctx, MBX_BASE + REG_ACK, 0x400u);
}
static bool clip_untouched(s5l8900_t *m) {
    uint8_t *p = m->ram + CLIP_TARGET_PA - RAM_BASE;
    for (unsigned i = 0; i < CLIP_BYTES; i++)
        if (p[i] != 0x5a)
            return false;
    return true;
}
static void clip_submit(s5l8900_t *m, uint32_t *packet, unsigned count) {
    write_packet(m, RING + 0xdd30u, packet, count);
    m->bus.write32(m->bus.ctx, MBX_BASE + RING, 0xf0000000u);
}
static bool clip_complete(s5l8900_t *m) {
    return (m->bus.read32(m->bus.ctx, MBX_BASE + REG_STATUS) & 0x400u) != 0;
}
static void clip_expect_rejected(s5l8900_t *m, uint32_t *packet, const char *name) {
    clip_clear(m);
    clip_submit(m, packet, 14u);
    CHECK(clip_untouched(m), "%s changed destination on rejection", name);
    CHECK(!clip_complete(m), "%s completed unsupported command", name);
}
static void test_clipped_os1_copy(void) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, RAM_BASE, RAM_SIZE), "clipped-copy init");
    if (!m.ram)
        return;
    m.bus.write32(m.bus.ctx, MBX_BASE + 0x1018u,
                  CLIP_TABLE); /* both captured VAs select root6 */
    for (unsigned off = 0; off < CLIP_BYTES + 0x1000u; off += 0x1000u)
        test_map_gpu_page(&m, CLIP_TABLE, (CLIP_SOURCE & ~0xfffu) + off,
                          CLIP_SOURCE_PA + off);
    for (unsigned off = 0; off < CLIP_BYTES; off += 0x1000u)
        test_map_gpu_page(&m, CLIP_TABLE, CLIP_TARGET + off, CLIP_TARGET_PA + off);
    for (unsigned y = 0; y < 480; y++)
        for (unsigned x = 0; x < 320; x++)
            test_gpu_write32(&m, CLIP_SOURCE + y * 1280u + x * 4u, clip_colour(x, y));
    uint32_t p[19];
    memcpy(p, clipped_capture, sizeof clipped_capture);
    clip_clear(&m);
    write_packet(&m, RING + 0xdd30u, p, 14u);
    CHECK(clip_untouched(&m) && !clip_complete(&m),
          "captured clipped copy executed before doorbell");
    m.bus.write32(m.bus.ctx, MBX_BASE + RING, 0xf0000000u);
    bool exact = true;
    for (unsigned y = 0; y < 480; y++)
        for (unsigned x = 0; x < 320; x++)
            if (test_gpu_read32(&m, CLIP_TARGET + y * 1280u + x * 4u) !=
                (y < 20 ? 0x5a5a5a5au : clip_colour(x, y - 20)))
                exact = false;
    CHECK(exact, "captured copy differs across full framebuffer/status guards");
    CHECK(clip_complete(&m), "captured clipped copy did not complete");
    CHECK(m.mbx_telemetry.completed_2d == 1 && m.mbx_telemetry.rejected_2d == 0,
          "captured copy telemetry");

    /* Intersect an interior viewport while retaining source offset in both axes. */
    memcpy(p, clipped_capture, sizeof clipped_capture);
    p[4] = 0x30008003u;
    p[6] = 0x0011000bu;
    p[7] = 0x00180010u;
    p[11] = 0x0008000cu;
    p[12] = 0x0014001cu;
    clip_clear(&m);
    clip_submit(&m, p, 14u);
    exact = true;
    for (unsigned y = 0; y < 480; y++)
        for (unsigned x = 0; x < 320; x++) {
            bool in = x >= 11 && x < 17 && y >= 16 && y < 24;
            uint32_t want = in ? clip_colour(x - 6, y - 9) : 0x5a5a5a5au;
            if (test_gpu_read32(&m, CLIP_TARGET + y * 1280u + x * 4u) != want)
                exact = false;
        }
    CHECK(exact && clip_complete(&m),
          "inset clipping did not preserve source/destination offset and guards");

    /* An original destination can extend beyond scanout; only the clipped source
     * footprint is addressed, so an unobserved off-screen row must not be read. */
    memcpy(p, clipped_capture, sizeof clipped_capture);
    p[7] = 0x01e00000u;
    p[11] = 0;
    p[12] = 0x01900258u;
    clip_clear(&m);
    clip_submit(&m, p, 14u);
    exact = true;
    for (unsigned y = 0; y < 480; y++)
        for (unsigned x = 0; x < 320; x++)
            if (test_gpu_read32(&m, CLIP_TARGET + y * 1280u + x * 4u) !=
                clip_colour(x, y))
                exact = false;
    CHECK(exact && clip_complete(&m),
          "clip failed to bound off-screen destination/source footprint");

    /* Equality survives the producer's strict > nonintersection branches. */
    memcpy(p, clipped_capture, sizeof clipped_capture);
    p[6] = 0x00080008u;
    clip_clear(&m);
    clip_submit(&m, p, 14u);
    CHECK(clip_untouched(&m) && clip_complete(&m),
          "valid empty clip moved pixels or failed completion");
    p[3] = 0x01bff000u;
    clip_expect_rejected(&m, p, "empty clip unmapped source");

    static const struct {
        unsigned index;
        uint32_t value;
        const char *name;
    } bad[] = {{2, 0x9c060500u, "unknown source descriptor state"},
               {5, 2, "unknown clip tag"},
               {9, 0x8000ccccu, "clip without mode bit"},
               {9, 0x8006ccccu, "unknown clipped blend mode"},
               {9, 0x8204ccccu, "rotated clip"},
               {8, 0x60800400u, "non-unity clip"},
               {10, 0xfffffffeu, "unknown colour"},
               {6, 0x01410000u, "clip outside scanout"},
               {6, 0x000b000cu, "reversed clip"},
               {7, 0x01e00013u, "clip outside destination intersection"},
               {4, 0x307f8000u, "clipped source footprint overflows row"},
               {13, 0, "missing clipped terminator"},
               {1, CLIP_TARGET + 1u, "unaligned target"},
               {3, CLIP_SOURCE + 1u, "unaligned source"}};
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        memcpy(p, clipped_capture, sizeof clipped_capture);
        p[bad[i].index] = bad[i].value;
        clip_expect_rejected(&m, p, bad[i].name);
    }

    /* A late source or destination page hole must reject before any earlier
     * row reaches guest RAM. Test both translated ranges independently. */
    uint32_t late_pages[] = {(CLIP_SOURCE + 459u * 1280u) & ~0xfffu,
                             (CLIP_TARGET + 479u * 1280u) & ~0xfffu};
    for (unsigned i = 0; i < 2; i++) {
        uint32_t entry = CLIP_TABLE + ((late_pages[i] >> 12) & 0x3ffu) * 4u;
        uint32_t saved = m.bus.read32(m.bus.ctx, entry);
        m.bus.write32(m.bus.ctx, entry, 0);
        memcpy(p, clipped_capture, sizeof clipped_capture);
        clip_expect_rejected(&m, p, i ? "late destination PTE" : "late source PTE");
        m.bus.write32(m.bus.ctx, entry, saved);
    }

    /* Adjacent compact14/11-word commands must advance to the real next head;
     * the clipped copy may be followed by either an old packet or another clip. */
    uint32_t q[19] = {0xa0060500u, CLIP_TARGET, 0x94060500u, CLIP_SOURCE,
                      0x30004001u, 0x60800200u, 0x8000ccccu, 0xffffffffu,
                      0x00010014u, 0x00020015u, 0x70000000u};
    memcpy(p, clipped_capture, sizeof clipped_capture);
    clip_clear(&m);
    write_packet(&m, RING + 0x100u, p, 14);
    write_packet(&m, RING + 0x138u, q, 11);
    CHECK(clip_untouched(&m), "mixed clipped batch wrote before submit");
    m.bus.write32(m.bus.ctx, MBX_BASE + RING, 0xf0000000u);
    CHECK(clip_complete(&m) &&
              test_gpu_read32(&m, CLIP_TARGET + 20 * 1280u + 4u) == clip_colour(1, 1),
          "compact clipped/old batch length/order incorrect");
    CHECK(test_gpu_read32(&m, CLIP_TARGET + 20 * 1280u) == clip_colour(0, 0),
          "second compact command corrupted prior pixels");

    clip_clear(&m);
    q[6] ^= 1u;
    write_packet(&m, RING + 0x100u, p, 14);
    write_packet(&m, RING + 0x138u, q, 11);
    m.bus.write32(m.bus.ctx, MBX_BASE + RING, 0xf0000000u);
    CHECK(clip_untouched(&m) && !clip_complete(&m),
          "late malformed old packet committed earlier clipped job");
    q[6] ^= 1u;

    /* Preserve the existing fail-closed target-as-source batch policy. */
    clip_clear(&m);
    q[3] = CLIP_TARGET;
    write_packet(&m, RING + 0x100u, p, 14);
    write_packet(&m, RING + 0x138u, q, 11);
    m.bus.write32(m.bus.ctx, MBX_BASE + RING, 0xf0000000u);
    CHECK(clip_untouched(&m) && !clip_complete(&m),
          "aliased batch source committed clipped job");
    q[3] = CLIP_SOURCE;

    /* A 19-word padded clipped copy can cross the 64 KiB cursor boundary. The next
     * command begins at ring0; its head is replaced by the actual doorbell. */
    for (unsigned i = 14; i < 19; i++)
        p[i] = 0x70000000u;
    clip_clear(&m);
    write_packet(&m, RING + 0xfff8u, p, 19);
    write_packet(&m, RING, q, 11);
    m.bus.write32(m.bus.ctx, MBX_BASE + RING, 0xf0000000u);
    CHECK(clip_complete(&m) &&
              test_gpu_read32(&m, CLIP_TARGET + 20 * 1280u + 4u) == clip_colour(1, 1),
          "padded clipped command ring wrap failed");

    /* The decoded clip extension preserves the already proved ARGB1555 layout,
     * using two-byte source offsets rather than the BGRA8 destination stride. */
    memcpy(p, clipped_capture, sizeof clipped_capture);
    p[2] = 0x94048008u;
    p[6] = 0x00030001u;
    p[7] = 0x00160015u;
    p[11] = 0x00000014u;
    p[12] = 0x00040016u;
    test_gpu_write16(&m, CLIP_SOURCE + 10u, 0xfc00u);
    test_gpu_write16(&m, CLIP_SOURCE + 12u, 0x03e0u);
    clip_clear(&m);
    clip_submit(&m, p, 14);
    CHECK(clip_complete(&m) &&
              test_gpu_read32(&m, CLIP_TARGET + 21u * 1280u + 4u) == 0xffff0000u &&
              test_gpu_read32(&m, CLIP_TARGET + 21u * 1280u + 8u) == 0x0000ff00u,
          "clipped ARGB1555 sampling/conversion differs from original layout");
    CHECK(test_gpu_read32(&m, CLIP_TARGET + 21u * 1280u) == 0x5a5a5a5au &&
              test_gpu_read32(&m, CLIP_TARGET + 21u * 1280u + 12u) == 0x5a5a5a5au,
          "clipped ARGB1555 wrote outside its bounds");
    s5l8900_free(&m);
}


static void test_os1_calendar_tall_crop(void) {
    struct mbx_test_status_form form = {
        .name = "OS1 captured Calendar 2048-row day texture",
        .xclip = 0x01400000u, .yclip = 0x01e00060u,
        .semantic_sprite = true, .variable_vertex_alpha = true,
        .tile_x0 = 0u, .tile_x1 = 39u, .tile_y0 = 6u, .tile_y1 = 29u,
        .left = 0u, .top = 109u, .width = 320u, .height = 371u,
        .source = 0x00980000u, .source_stride = 1280u,
        .source_control = 0x0e500000u, .source_width = 320u,
        .source_height = 371u, .source_row0 = 694u,
        .quad = {
            0xe0000000u, 0xa6818000u, 0x0e537421u, 0xcd206c40u,
            0xa7718000u, 0x0e531ae0u, 0xae504ea0u, 0x22250e80u,
            0x00000000u, 0x43f00000u, 0x00000000u, 0x42da0000u,
            0x43a00000u, 0x43f00000u, 0x43a00000u, 0x42da0000u,
            0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
            0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
            0xff000000u, 0x00000000u, 0x3f052000u, 0x00000000u,
            0x3ef00000u, 0xff000000u, 0x00000000u, 0x3ead8000u,
            0x00000000u, 0x3dda0000u, 0xff000000u, 0x3f200000u,
            0x3f052000u, 0x3ea00000u, 0x3ef00000u, 0xff000000u,
            0x3f200000u, 0x3ead8000u, 0x3ea00000u, 0x3dda0000u,
        },
    };
    test_captured_status_form(&form);
    form.name = "OS1 captured Calendar 1024-row resized day texture";
    form.yclip = 0x01b00060u;
    form.tile_y1 = 26u;
    form.height = form.source_height = 323u;
    const uint32_t resized[44] = {
        0xe0000000u, 0xa6718000u, 0x0e537421u, 0xcd206c40u,
        0xa7718000u, 0x0e5303a0u, 0xae504ea0u, 0x22250e80u,
        0x00000000u, 0x43d80000u, 0x00000000u, 0x42da0000u,
        0x43a00000u, 0x43d80000u, 0x43a00000u, 0x42da0000u,
        0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
        0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
        0xff000000u, 0x00000000u, 0x3f7e4000u, 0x00000000u,
        0x3ed80000u, 0xff000000u, 0x00000000u, 0x3f2d8000u,
        0x00000000u, 0x3dda0000u, 0xff000000u, 0x3f200000u,
        0x3f7e4000u, 0x3ea00000u, 0x3ed80000u, 0xff000000u,
        0x3f200000u, 0x3f2d8000u, 0x3ea00000u, 0x3dda0000u,
    };
    memcpy(form.quad, resized, sizeof resized);
    test_captured_status_form(&form);

}

static void test_os1_calendar_month_gradient(void) {
    struct mbx_test_status_form form = {
        .name = "OS1 captured Calendar Month horizontal gradient",
        .xclip = 0x01400000u, .yclip = 0x01600140u,
        .semantic_sprite = true, .variable_vertex_alpha = true,
        .boundary_override = true, .scaled_sprite = true, .row_resample = true,
        .tile_x0 = 0u, .tile_x1 = 39u, .tile_y0 = 20u, .tile_y1 = 21u,
        .left = 0u, .top = 329u, .width = 320u, .height = 18u,
        .source = 0x00980000u, .source_stride = 32u,
        .source_control = 0x8e000000u, .source_width = 8u,
        .source_height = 18u,
        .quad = {
            0xe0000000u, 0xa0218001u, 0x8e036c00u, 0xcd206c40u,
            0xa7718000u, 0x0e532e40u, 0xae504ea0u, 0x22250e80u,
            0x00000000u, 0x43a48000u, 0x43a00000u, 0x43a48000u,
            0x00000000u, 0x43ad8000u, 0x43a00000u, 0x43ad8000u,
            0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
            0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
            0xff000000u, 0x00000000u, 0x00000000u, 0x00000000u,
            0x3ea48000u, 0xff000000u, 0x3f800000u, 0x00000000u,
            0x3ea00000u, 0x3ea48000u, 0xff000000u, 0x00000000u,
            0x3f100000u, 0x00000000u, 0x3ead8000u, 0xff000000u,
            0x3f800000u, 0x3f100000u, 0x3ea00000u, 0x3ead8000u,
        },
    };
    const float boundary[8] = {0,347,0,329,320,347,320,329};
    for(unsigned i=0;i<8;i++) form.boundary[i]=test_float_word(boundary[i]);
    test_captured_status_form(&form);
}
static void test_os1_safari_constant_v_rows(void) {
    {
      struct mbx_test_status_form form = {
        .constant_v_row = true, .name = "OS1 Safari constant-V row 1",
        .xclip = 0x01400000u, .yclip = 0x01800170u,
        .semantic_sprite = true, .variable_vertex_alpha = true,
        .boundary_override = true, .scaled_sprite = true,
        .tile_x0 = 0u, .tile_x1 = 39u, .tile_y0 = 23u, .tile_y1 = 23u,
        .left = 0u, .top = 375u, .width = 320u, .height = 1u,
        .source = 0x00980000u, .source_stride = 1280u,
        .source_control = 0x8e500000u, .source_width = 320u, .source_height = 1u,
        .quad = {
            0xe0000000u, 0xa6018000u, 0x8e536080u, 0xcd206c40u,
            0xa7718000u, 0x0e5303a0u, 0xae504ea0u, 0x22250e80u,
            0x00000000u, 0x43bb5f70u, 0x43a00000u, 0x43bb5f70u,
            0x00000000u, 0x43bbdf70u, 0x43a00000u, 0x43bbdf70u,
            0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
            0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
            0xff000000u, 0x00000000u, 0x3d800000u, 0x00000000u,
            0x3ebb5f70u, 0xff000000u, 0x3f200000u, 0x3d800000u,
            0x3ea00000u, 0x3ebb5f70u, 0xff000000u, 0x00000000u,
            0x3d800000u, 0x00000000u, 0x3ebbdf70u, 0xff000000u,
            0x3f200000u, 0x3d800000u, 0x3ea00000u, 0x3ebbdf70u,
        },
      };
      const float boundary[8] = {0,376,0,374,320,376,320,374};
      for(unsigned i=0;i<8;i++) form.boundary[i]=test_float_word(boundary[i]);
      test_captured_status_form(&form);
    }
    {
      struct mbx_test_status_form form = {
        .constant_v_row = true, .name = "OS1 Safari constant-V row 2",
        .xclip = 0x01400000u, .yclip = 0x01b001a0u,
        .semantic_sprite = true, .variable_vertex_alpha = true,
        .boundary_override = true, .scaled_sprite = true,
        .tile_x0 = 0u, .tile_x1 = 39u, .tile_y0 = 26u, .tile_y1 = 26u,
        .left = 0u, .top = 423u, .width = 320u, .height = 1u,
        .source = 0x00980000u, .source_stride = 1280u,
        .source_control = 0x8e500000u, .source_width = 320u, .source_height = 1u,
        .quad = {
            0xe0000000u, 0xa6018000u, 0x8e5360a0u, 0xcd206c40u,
            0xa7718000u, 0x0e5303a0u, 0xae504ea0u, 0x22250e80u,
            0x00000000u, 0x43d35f70u, 0x43a00000u, 0x43d35f70u,
            0x00000000u, 0x43d3df70u, 0x43a00000u, 0x43d3df70u,
            0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
            0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
            0xff000000u, 0x00000000u, 0x3d800000u, 0x00000000u,
            0x3ed35f70u, 0xff000000u, 0x3f200000u, 0x3d800000u,
            0x3ea00000u, 0x3ed35f70u, 0xff000000u, 0x00000000u,
            0x3d800000u, 0x00000000u, 0x3ed3df70u, 0xff000000u,
            0x3f200000u, 0x3d800000u, 0x3ea00000u, 0x3ed3df70u,
        },
      };
      const float boundary[8] = {0,424,0,422,320,424,320,422};
      for(unsigned i=0;i<8;i++) form.boundary[i]=test_float_word(boundary[i]);
      test_captured_status_form(&form);
    }
    {
      struct mbx_test_status_form form = {
        .constant_v_row = true, .name = "OS1 Safari constant-V row 3",
        .xclip = 0x01400000u, .yclip = 0x01700160u,
        .semantic_sprite = true, .variable_vertex_alpha = true,
        .boundary_override = true, .scaled_sprite = true,
        .tile_x0 = 0u, .tile_x1 = 39u, .tile_y0 = 22u, .tile_y1 = 22u,
        .left = 0u, .top = 362u, .width = 320u, .height = 1u,
        .source = 0x00980000u, .source_stride = 1280u,
        .source_control = 0x8e500000u, .source_width = 320u, .source_height = 1u,
        .quad = {
            0xe0000000u, 0xa6018000u, 0x8e536080u, 0xcd206c40u,
            0xa7718000u, 0x0e531ae0u, 0xae504ea0u, 0x22250e80u,
            0x00000000u, 0x43b5321cu, 0x43a00000u, 0x43b5321cu,
            0x00000000u, 0x43b5b21cu, 0x43a00000u, 0x43b5b21cu,
            0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
            0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
            0xff000000u, 0x00000000u, 0x3d800000u, 0x00000000u,
            0x3eb5321cu, 0xff000000u, 0x3f200000u, 0x3d800000u,
            0x3ea00000u, 0x3eb5321cu, 0xff000000u, 0x00000000u,
            0x3d800000u, 0x00000000u, 0x3eb5b21cu, 0xff000000u,
            0x3f200000u, 0x3d800000u, 0x3ea00000u, 0x3eb5b21cu,
        },
      };
      const float boundary[8] = {0,364,0,362,320,364,320,362};
      for(unsigned i=0;i<8;i++) form.boundary[i]=test_float_word(boundary[i]);
      test_captured_status_form(&form);
    }
    {
      struct mbx_test_status_form form = {
        .constant_v_row = true, .name = "OS1 Safari constant-V row 4",
        .xclip = 0x01400000u, .yclip = 0x01a00190u,
        .semantic_sprite = true, .variable_vertex_alpha = true,
        .boundary_override = true, .scaled_sprite = true,
        .tile_x0 = 0u, .tile_x1 = 39u, .tile_y0 = 25u, .tile_y1 = 25u,
        .left = 0u, .top = 410u, .width = 320u, .height = 1u,
        .source = 0x00980000u, .source_stride = 1280u,
        .source_control = 0x8e500000u, .source_width = 320u, .source_height = 1u,
        .quad = {
            0xe0000000u, 0xa6018000u, 0x8e5360a0u, 0xcd206c40u,
            0xa7718000u, 0x0e531ae0u, 0xae504ea0u, 0x22250e80u,
            0x00000000u, 0x43cd321cu, 0x43a00000u, 0x43cd321cu,
            0x00000000u, 0x43cdb21cu, 0x43a00000u, 0x43cdb21cu,
            0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
            0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
            0xff000000u, 0x00000000u, 0x3d800000u, 0x00000000u,
            0x3ecd321cu, 0xff000000u, 0x3f200000u, 0x3d800000u,
            0x3ea00000u, 0x3ecd321cu, 0xff000000u, 0x00000000u,
            0x3d800000u, 0x00000000u, 0x3ecdb21cu, 0xff000000u,
            0x3f200000u, 0x3d800000u, 0x3ea00000u, 0x3ecdb21cu,
        },
      };
      const float boundary[8] = {0,412,0,410,320,412,320,410};
      for(unsigned i=0;i<8;i++) form.boundary[i]=test_float_word(boundary[i]);
      test_captured_status_form(&form);
    }
}
/* Exact captured main records; relocatable GART and conservative integer
 * boundaries exercise the unchanged sampler, staging, and blend reference. */
static void test_os1_safari_address_scales(void) {
    {
      struct mbx_test_status_form form = {
        .safari_address_scale = true, .row_resample = true, .name = "OS1 Safari address scale 1",
        .xclip = 0x01180028u, .yclip = 0x00500020u,
        .semantic_sprite = true, .variable_vertex_alpha = true,
        .boundary_override = true, .scaled_sprite = true,
        .tile_x0 = 5u, .tile_x1 = 34u,
        .tile_y0 = 2u, .tile_y1 = 4u,
        .left = 42u, .top = 47u, .width = 236u, .height = 31u,
        .source = 0x00980000u, .source_stride = 1184u,
        .source_control = 0x8e480000u,
        .source_width = 290u,
        .source_height = 31u,
        .quad = {
        0xe0000000u, 0xa6218001u, 0x8e4b5b61u, 0xcd206c40u,
        0xa7718000u, 0x0e531ae0u, 0xae504ea0u, 0x22250e80u,
        0x4229a0e6u, 0x423b29b9u, 0x438acbe3u, 0x423b29b9u,
        0x4229a0e6u, 0x429b94dcu, 0x438acbe3u, 0x429b94dcu,
        0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
        0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
        0x16000000u, 0x3a800000u, 0x3c800000u, 0x3d29a0e6u,
        0x3d3b29b9u, 0x16000000u, 0x3f10c000u, 0x3c800000u,
        0x3e8acbe3u, 0x3d3b29b9u, 0x16000000u, 0x3a800000u,
        0x3f740000u, 0x3d29a0e6u, 0x3d9b94dcu, 0x16000000u,
        0x3f10c000u, 0x3f740000u, 0x3e8acbe3u, 0x3d9b94dcu,
        },
      };
      const float boundary[8] = {42,78,42,46,278,78,278,46};
      for(unsigned i=0;i<8;i++) form.boundary[i]=test_float_word(boundary[i]);
      test_captured_status_form(&form);
    }
    {
      struct mbx_test_status_form form = {
        .safari_address_scale = true, .row_resample = true, .name = "OS1 Safari address scale 2",
        .xclip = 0x01180028u, .yclip = 0x00500020u,
        .semantic_sprite = true, .variable_vertex_alpha = true,
        .boundary_override = true, .scaled_sprite = true,
        .tile_x0 = 5u, .tile_x1 = 34u,
        .tile_y0 = 2u, .tile_y1 = 4u,
        .left = 42u, .top = 47u, .width = 236u, .height = 31u,
        .source = 0x00980000u, .source_stride = 1184u,
        .source_control = 0x8e480000u,
        .source_width = 290u,
        .source_height = 31u,
        .quad = {
        0xe0000000u, 0xa6218001u, 0x8e4b5ce1u, 0xcd206c40u,
        0xa7718000u, 0x0e531ae0u, 0xae504ea0u, 0x22250e80u,
        0x4229a0e6u, 0x423b29b9u, 0x438acbe3u, 0x423b29b9u,
        0x4229a0e6u, 0x429b94dcu, 0x438acbe3u, 0x429b94dcu,
        0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
        0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
        0x16000000u, 0x3a800000u, 0x3c800000u, 0x3d29a0e6u,
        0x3d3b29b9u, 0x16000000u, 0x3f10c000u, 0x3c800000u,
        0x3e8acbe3u, 0x3d3b29b9u, 0x16000000u, 0x3a800000u,
        0x3f740000u, 0x3d29a0e6u, 0x3d9b94dcu, 0x16000000u,
        0x3f10c000u, 0x3f740000u, 0x3e8acbe3u, 0x3d9b94dcu,
        },
      };
      const float boundary[8] = {42,78,42,46,278,78,278,46};
      for(unsigned i=0;i<8;i++) form.boundary[i]=test_float_word(boundary[i]);
      test_captured_status_form(&form);
    }
    {
      struct mbx_test_status_form form = {
        .safari_address_scale = true, .row_resample = true, .name = "OS1 Safari address scale 3",
        .xclip = 0x01180028u, .yclip = 0x00500020u,
        .semantic_sprite = true, .variable_vertex_alpha = true,
        .boundary_override = true, .scaled_sprite = true,
        .tile_x0 = 5u, .tile_x1 = 34u,
        .tile_y0 = 2u, .tile_y1 = 4u,
        .left = 42u, .top = 44u, .width = 236u, .height = 31u,
        .source = 0x00980000u, .source_stride = 1184u,
        .source_control = 0x8e480000u,
        .source_width = 290u,
        .source_height = 31u,
        .quad = {
        0xe0000000u, 0xa6218001u, 0x8e4b5e21u, 0xcd206c40u,
        0xa7718000u, 0x0e531ae0u, 0xae504ea0u, 0x22250e80u,
        0x4229a0e6u, 0x422e5f1au, 0x438acbe3u, 0x422e5f1au,
        0x4229a0e6u, 0x42952f8du, 0x438acbe3u, 0x42952f8du,
        0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
        0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
        0xff000000u, 0x3a800000u, 0x3c800000u, 0x3d29a0e6u,
        0x3d2e5f1au, 0xff000000u, 0x3f10c000u, 0x3c800000u,
        0x3e8acbe3u, 0x3d2e5f1au, 0xff000000u, 0x3a800000u,
        0x3f740000u, 0x3d29a0e6u, 0x3d952f8du, 0xff000000u,
        0x3f10c000u, 0x3f740000u, 0x3e8acbe3u, 0x3d952f8du,
        },
      };
      const float boundary[8] = {42,75,42,43,278,75,278,43};
      for(unsigned i=0;i<8;i++) form.boundary[i]=test_float_word(boundary[i]);
      test_captured_status_form(&form);
    }
    {
      struct mbx_test_status_form form = {
        .safari_address_scale = true, .row_resample = true, .name = "OS1 Safari address scale 4",
        .xclip = 0x01400000u, .yclip = 0x00300020u,
        .semantic_sprite = true, .variable_vertex_alpha = true,
        .boundary_override = true, .scaled_sprite = true,
        .tile_x0 = 0u, .tile_x1 = 39u,
        .tile_y0 = 2u, .tile_y1 = 2u,
        .left = 0u, .top = 43u, .width = 320u, .height = 2u,
        .source = 0x00980000u, .source_stride = 1280u,
        .source_control = 0x8e500000u,
        .source_width = 320u,
        .source_height = 2u,
        .quad = {
        0xe0000000u, 0xa6018000u, 0x8e5360e0u, 0xd6887610u,
        0xa7718000u, 0x0e531ae0u, 0xa3104620u, 0x22250e80u,
        0x00000000u, 0x422b9b13u, 0x43a00000u, 0x422b9b13u,
        0x00000000u, 0x42339b13u, 0x43a00000u, 0x42339b13u,
        0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
        0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
        0x16000000u, 0x00000000u, 0x3d800000u, 0x00000000u,
        0x3d2b9b13u, 0x16000000u, 0x3f200000u, 0x3d800000u,
        0x3ea00000u, 0x3d2b9b13u, 0x16000000u, 0x00000000u,
        0x3e400000u, 0x00000000u, 0x3d339b13u, 0x16000000u,
        0x3f200000u, 0x3e400000u, 0x3ea00000u, 0x3d339b13u,
        },
      };
      const float boundary[8] = {0,45,0,42,320,45,320,42};
      for(unsigned i=0;i<8;i++) form.boundary[i]=test_float_word(boundary[i]);
      test_captured_status_form(&form);
    }
    {
      struct mbx_test_status_form form = {
        .safari_address_scale = true, .row_resample = true, .name = "OS1 Safari address scale 5",
        .xclip = 0x01200020u, .yclip = 0x00600030u,
        .semantic_sprite = true, .variable_vertex_alpha = true,
        .boundary_override = true, .scaled_sprite = true,
        .tile_x0 = 4u, .tile_x1 = 35u,
        .tile_y0 = 3u, .tile_y1 = 5u,
        .left = 38u, .top = 56u, .width = 244u, .height = 31u,
        .source = 0x00980000u, .source_stride = 1184u,
        .source_control = 0x8e480000u,
        .source_width = 290u,
        .source_height = 31u,
        .quad = {
        0xe0000000u, 0xa6218001u, 0x8e4b5b61u, 0xcd206c40u,
        0xa7718000u, 0x0e532e40u, 0xae504ea0u, 0x22250e80u,
        0x42192624u, 0x425ff79du, 0x438cdb3cu, 0x425ff79du,
        0x42192624u, 0x42adfbceu, 0x438cdb3cu, 0x42adfbceu,
        0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
        0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
        0x39000000u, 0x3a800000u, 0x3c800000u, 0x3d192624u,
        0x3d5ff79du, 0x39000000u, 0x3f10c000u, 0x3c800000u,
        0x3e8cdb3cu, 0x3d5ff79du, 0x39000000u, 0x3a800000u,
        0x3f740000u, 0x3d192624u, 0x3dadfbceu, 0x39000000u,
        0x3f10c000u, 0x3f740000u, 0x3e8cdb3cu, 0x3dadfbceu,
        },
      };
      const float boundary[8] = {38,87,38,55,282,87,282,55};
      for(unsigned i=0;i<8;i++) form.boundary[i]=test_float_word(boundary[i]);
      test_captured_status_form(&form);
    }
}
static void test_os1_text_wide_stretch(void) {
    struct mbx_test_status_form form = {
        .name = "OS1 Text 214x clipped horizontal texture stretch",
        .text_wide_stretch = true, .row_resample = true,
        .xclip = 0x00f00010u, .yclip = 0x01d001c0u,
        .semantic_sprite = true, .scaled_sprite = true,
        .variable_vertex_alpha = true, .boundary_override = true,
        .tile_x0 = 2u, .tile_x1 = 29u, .tile_y0 = 28u, .tile_y1 = 28u,
        .left = 19u, .top = 450u, .width = 214u, .height = 13u,
        .source = 0x00980000u, .source_stride = 128u,
        .source_control = 0x8e080000u, .source_width = 32u, .source_height = 26u,
        .boundary = {0x41980000u,0x43e78000u,0x41980000u,0x43e10000u,0x43690000u,0x43e78000u,0x43690000u,0x43e10000u},
        .quad = {
            0xe0000000u, 0xa2218000u, 0x8e0b5e20u, 0xcd206c40u,
            0xa7718000u, 0x0e5303a0u, 0xae504ea0u, 0x22250e80u,
            0xc52cb000u, 0x43e10000u, 0x457f5000u, 0x43e10000u,
            0xc52cb000u, 0x43ee0000u, 0x457f5000u, 0x43ee0000u,
            0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
            0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
            0xff000000u, 0x00000000u, 0x00000000u, 0xc02cb000u,
            0x3ee10000u, 0xff000000u, 0x3f800000u, 0x00000000u,
            0x407f5000u, 0x3ee10000u, 0xff000000u, 0x00000000u,
            0x3f500000u, 0xc02cb000u, 0x3eee0000u, 0xff000000u,
            0x3f800000u, 0x3f500000u, 0x407f5000u, 0x3eee0000u,
        },
    };
    test_captured_status_form(&form);
}

/* Original Clock World Clock launch capture, repeated on three blend targets.
 * The 44 words are unchanged here; the shared fixture relocates source/target
 * addresses into a synthetic GART and supplies an independently valid integer
 * boundary. Raw command and register witness: clock-mbx/quad.bin and witnesses.log
 * in the investigation artifacts. */
static void test_os1_clock_mirrored_column(void) {
    struct mbx_test_status_form form = {
        .name = "OS1 Clock mirrored constant-column gradient",
        .semantic_sprite = true, .affine_sprite = true,
        .scaled_sprite = true, .column_resample = true,
        .clock_column = true, .boundary_override = true,
        .xclip = 0x01400000u, .yclip = 0x00c00090u,
        .tile_x0 = 0u, .tile_x1 = 39u, .tile_y0 = 9u, .tile_y1 = 11u,
        .left = 0u, .top = 156u, .width = 320u, .height = 30u,
        .source = 0x0093fe00u, .source_stride = 32u,
        .source_control = 0x8e000000u,
        .source_width = 1u, .source_height = 30u,
        .expected_covered_pixels = 9600u,
        .boundary = {
            0u, 0x433a0000u, 0u, 0x431c0000u,
            0x43a00000u, 0x433a0000u, 0x43a00000u, 0x431c0000u,
        },
        .quad = {
            0xe0000000u, 0xa0218001u, 0x8e038b40u, 0xcd206c40u,
            0xa7718000u, 0x0e532e40u, 0xae504ea0u, 0x22250e80u,
            0x43af0000u, 0x433a0000u, 0xc1f00000u, 0x4339fffeu,
            0x43af0000u, 0x431c0000u, 0xc1f00000u, 0x431bfffeu,
            0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
            0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
            0xff000000u, 0x3d800000u, 0x3c800000u, 0x3eaf0000u,
            0x3e3a0000u, 0xff000000u, 0x3d800000u, 0x3c800000u,
            0xbcf00000u, 0x3e39fffeu, 0xff000000u, 0x3d800000u,
            0x3f6c0000u, 0x3eaf0000u, 0x3e1c0000u, 0xff000000u,
            0x3d800000u, 0x3f6c0000u, 0xbcf00000u, 0x3e1bfffeu,
        },
    };
    test_captured_status_form(&form);
    /* The exact 180-degree limit has no float rotation residual. It must
     * still pass the same family gate, rather than enter generic reversed
     * axis-aligned rendering. */
    form.name = "OS1 Clock exact 180-degree constant-column limit";
    form.quad[11] = form.quad[9];
    form.quad[15] = form.quad[13];
    form.quad[33] = form.quad[28];
    form.quad[43] = form.quad[38];
    test_captured_status_form(&form);
}

/* Actual first retained Safari packet; only GART physical backing is synthetic.
 * Source width472 differs from the fixed320-pixel screen, not its pixel layout. */
static void test_os1_safari_472_source_copy(void) {
    const uint32_t table0=0x08001000u,table6=0x08002000u;
    const uint32_t source=0x00026080u,target=0x0196d000u;
    const uint32_t source_pa=0x080e0000u,target_pa=0x08040000u;
    uint32_t packet[16]={0xa0060500u,0x0196d000u,0x94060760u,0x00026080u,
        0x301bc155u,0x60800200u,0x8000ccccu,0xffffffffu,
        0x00690080u,0x006d0081u,0x70000000u,0x70000000u,
        0x70000000u,0x70000000u,0x70000000u,0x70000000u};
    s5l8900_t m;CHECK(s5l8900_init(&m,RAM_BASE,RAM_SIZE),"wide source init failed");if(!m.ram)return;
    m.bus.write32(m.bus.ctx,MBX_BASE+0x1000u,table0);
    m.bus.write32(m.bus.ctx,MBX_BASE+0x1018u,table6);
    for(unsigned n=0;n<0x80+1888*480;n+=4096)
        test_map_gpu_page(&m,table0,(source&~4095u)+n,source_pa+((n/4096)^1u)*4096);
    for(unsigned n=0;n<1280*480;n+=4096)test_map_gpu_page(&m,table6,target+n,target_pa+n);
    for(unsigned y=0;y<480;y++)for(unsigned x=0;x<472;x++)
        test_gpu_write32(&m,source+y*1888+x*4,0xff000000u|(y<<12)|x);
    uint8_t*expected=malloc(1280*480),*before=malloc(1280*480);
    CHECK(expected&&before,"wide source expected allocation");if(!expected||!before){free(expected);free(before);s5l8900_free(&m);return;}
    memset(m.ram+target_pa-RAM_BASE,0x69,1280*480);memcpy(expected,m.ram+target_pa-RAM_BASE,1280*480);memcpy(before,m.ram+target_pa-RAM_BASE,1280*480);
    for(unsigned x=0;x<4;x++){uint32_t v=0xff000000u|(341u<<12)|(111u+x);memcpy(expected+128*1280+(105+x)*4,&v,4);}
    write_packet(&m,RING+0x2cu,packet,16);
    CHECK(!memcmp(before,m.ram+target_pa-RAM_BASE,1280*480),"wide packet changed before submit");
    CHECK(m.bus.read32(m.bus.ctx,MBX_BASE+REG_STATUS)==0,"wide packet completed before submit");
    m.bus.write32(m.bus.ctx,MBX_BASE+RING,0xf0000000u);
    CHECK(!memcmp(expected,m.ram+target_pa-RAM_BASE,1280*480),"actual wide packet pixels or outside rectangle differ");
    CHECK(m.bus.read32(m.bus.ctx,MBX_BASE+REG_STATUS)==0x400u&&m.mbx_telemetry.bytes_2d==16&&m.mbx_telemetry.completed_2d==1,"wide packet exact completion missing");
    /* Last four pixels in each source row; swapped physical pages prove GART
     * lookup for each row, rather than flat pointer arithmetic at320 pixels. */
    packet[4]=0x30000000u|(468u<<14)|300u;packet[8]=(12u<<16)|470u;packet[9]=(16u<<16)|474u;
    for(unsigned y=0;y<4;y++)for(unsigned x=0;x<4;x++){uint32_t v=0xff000000u|((300+y)<<12)|(468+x);memcpy(expected+(470+y)*1280+(12+x)*4,&v,4);}
    m.bus.write32(m.bus.ctx,MBX_BASE+REG_ACK,0x400u);write_packet(&m,RING+0x200u,packet,16);m.bus.write32(m.bus.ctx,MBX_BASE+RING,0xf0000000u);
    CHECK(!memcmp(expected,m.ram+target_pa-RAM_BASE,1280*480),"wide source row boundary/noncontiguous mapping differs");
    CHECK(m.bus.read32(m.bus.ctx,MBX_BASE+REG_STATUS)==0x400u,"wide source multirow completion missing");
    memcpy(before,m.ram+target_pa-RAM_BASE,1280*480);
    for(unsigned variant=0;variant<10;variant++){
        uint32_t bad[16];memcpy(bad,packet,sizeof bad);uint32_t pte=0,old=0;
        if(variant==0)bad[2]=0x94060764u;
        if(variant==1)bad[2]=0x94060761u;
        if(variant==2)bad[2]=0x94060000u;
        if(variant==3)bad[4]=0x30000000u|(470u<<14)|300u;
        if(variant==4)bad[4]=0x30000000u|(468u<<14)|478u;
        if(variant==5){bad[8]=(318u<<16)|470u;bad[9]=(322u<<16)|474u;}
        if(variant==6)bad[2]=0x94048760u; /* no wider1555 admission */
        if(variant==7)bad[6]^=0x10000u;
        if(variant==8)pte=table0+(((source+303u*1888+468u*4)>>12)&1023u)*4;
        if(variant==9)pte=table6+(((target+473u*1280+12u*4)>>12)&1023u)*4;
        if(pte){old=m.bus.read32(m.bus.ctx,pte);m.bus.write32(m.bus.ctx,pte,0);}
        m.bus.write32(m.bus.ctx,MBX_BASE+REG_ACK,0x400u);write_packet(&m,RING+0x400u,bad,16);m.bus.write32(m.bus.ctx,MBX_BASE+RING,0xf0000000u);
        CHECK(!memcmp(before,m.ram+target_pa-RAM_BASE,1280*480),"wide source negative%u partially changed target",variant);
        CHECK(m.bus.read32(m.bus.ctx,MBX_BASE+REG_STATUS)==0,"wide source negative%u completed",variant);
        if(pte)m.bus.write32(m.bus.ctx,pte,old);
    }
    /* A valid earlier copy must not commit before a later malformed packet,
     * including unmeasured target-as-source semantics in the same batch. */
    for(unsigned alias=0;alias<2;alias++){
        uint32_t later[16];memcpy(later,packet,sizeof later);
        if(alias){later[3]=target;later[4]=0x30000000u;}else later[2]=0x94060764u;
        test_gpu_write32(&m,target+470*1280+12*4,0x13572468u);memcpy(before,m.ram+target_pa-RAM_BASE,1280*480);
        write_packet(&m,RING+0x800u,packet,11);write_packet(&m,RING+0x82cu,later,11);m.bus.write32(m.bus.ctx,MBX_BASE+RING,0xf0000000u);
        CHECK(!memcmp(before,m.ram+target_pa-RAM_BASE,1280*480),"wide batch%u partially committed",alias);
        CHECK(m.bus.read32(m.bus.ctx,MBX_BASE+REG_STATUS)==0,"wide batch%u completed",alias);
    }
    free(before);free(expected);s5l8900_free(&m);
}

int main(void) {
    test_os1_safari_472_source_copy();
    test_os1_clock_mirrored_column();
    test_os1_text_wide_stretch();
    test_os1_safari_address_scales();
    test_os1_safari_constant_v_rows();
    test_clipped_os1_copy();
    printf("PowerVR MBX2D tests\n");
    test_translated_copy_and_completion_boundary();
    test_unknown_packet_and_bad_gart_are_atomic();
    test_wallpaper_argb1555_simple_copy();
    test_full_lower_surface_opaque_fill();
    test_safari_tabs_opaque_fill_batch();
    test_safari_tabs_bounded_source_stride_copy();
    test_split_lower_surface_black_fill();
    test_settings_transition_transparent_clear_batch();
    test_springboard_settings_black_fill_batch();
    test_status_write_to_set_and_ack();
    test_ta_context_reset_handshake();
    test_ta_context_store_handshake();
    test_ta_context_load_handshake();
    test_ta_submission_completion();
    test_ta_stream_axis_aligned_atomic_scene();
    test_ta_stream_orthographic_affine_texture();
    test_ta_stream_immediate_state_reuse_and_alias_bounds();
    test_premultiplied_2d_clock_form();
    test_opaque_global_alpha_2d_form();
    test_ordered_atomic_2d_batches();
    test_first_tiled_premultiplied_over();
    test_second_tiled_status_glyph();
    test_compact_opaque_blit_copy();
    test_compact_column_resample();
    test_compact_full_extent_uniform_minification();
    test_compact_clipped_zero_coverage();
    test_pointer_selected_solid_quad();
    test_os1_clipped_solid();
    test_later_tiled_status_sprites();
    test_os1_empty_clip();
    test_os1_calendar_month_gradient();
    test_os1_calendar_tall_crop();
    test_os1_settings_clip();
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
