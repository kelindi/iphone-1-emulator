/* Actual CPU stores and machine bus/DMA/MBX notification contracts.
 * MBX packet and discontiguous GART fixture: core/tests/test_mbx.c.
 * DMA register literals: core/tests/test_pl080.c. */
#include "soc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { \
    fprintf(stderr, "FAIL line %u: %s\n", __LINE__, #x); exit(1); \
} } while (0)

typedef struct { uint32_t offset, length; uint8_t bytes[16]; } event_t;
static event_t events[64];
static unsigned event_count;
static s5l8900_t *observed;

static void notified(void *context, const uint8_t *host, uint32_t length) {
    s5l8900_t *m = context;
    CHECK(m == observed);
    CHECK(host >= m->ram && host < m->ram + m->ram_size);
    CHECK(length && length <= 16 && length <= (uint32_t)(m->ram + m->ram_size-host));
    CHECK(event_count < 64);
    event_t *e = &events[event_count++];
    e->offset = (uint32_t)(host-m->ram); e->length = length;
    /* Captured during callback: proves notification follows commitment. */
    memcpy(e->bytes, host, length);
}

static void observe(s5l8900_t *m) {
    observed = m; event_count = 0;
    memset(events, 0, sizeof events);
    m->bus.ram_written = notified;
}

static void event(unsigned i, uint32_t offset, uint32_t length,
                  const uint8_t *expected) {
    CHECK(i < event_count);
    CHECK(events[i].offset == offset);
    CHECK(events[i].length == length);
    CHECK(!memcmp(events[i].bytes, expected, length));
}

static void cpu_store(unsigned width, bool direct, bool crossing) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, 0x08000000u, 0x200000u));
    CHECK(s5l8900_set_direct_ram_writes(&m, direct));
    /* STRB/STRH/STR r0,[r1], twice: second aligned store uses dwrite
     * when consent is enabled. Both values differ in every written byte. */
    uint32_t instruction = width == 1 ? 0xe5c10000u :
                           width == 2 ? 0xe1c100b0u : 0xe5810000u;
    uint32_t program[2] = {instruction,instruction};
    s5l8900_load(&m,m.ram_base,program,sizeof program);
    CHECK(!memcmp(m.ram,program,sizeof program));
    const uint32_t offset = crossing ? 0x3feu : 0x800u;
    const uint32_t values[2] = {0x78563412u,0xefcdab90u};
    const uint8_t bytes[2][4] = {{0x12,0x34,0x56,0x78},
                                {0x90,0xab,0xcd,0xef}};
    m.cpu.cpsr = ARM_MODE_SYS | ARM_CPSR_I | ARM_CPSR_F;
    m.cpu.cp15.sctlr &= ~(ARM_SCTLR_M | ARM_SCTLR_A);
    m.cpu.cp15.sctlr |= ARM_SCTLR_U;
    m.cpu.r[15] = m.ram_base;
    m.cpu.r[1] = m.ram_base+offset;
    const uint64_t hits_before = m.cpu.dwrite_hits;
    observe(&m);
    for (unsigned i=0; i<2; ++i) {
        m.cpu.r[0] = values[i];
        CHECK(arm_step(&m.cpu) == ARM_OK);
        CHECK(!m.cpu.abort_pending);
        CHECK(m.cpu.r[15] == m.ram_base+4*(i+1));
        CHECK(m.cpu.r[0] == values[i]);
        CHECK(m.cpu.r[1] == m.ram_base+offset);
        CHECK(event_count == i+1);
        event(i,offset,width,bytes[i]);
        CHECK(!memcmp(m.ram+offset,bytes[i],width));
        CHECK(m.ram[offset-1] == 0 && m.ram[offset+width] == 0);
    }
    /* Cross-1KiB U=1/A=0 writes cannot use the one-granule dwrite cache.
     * They still commit as one 4-byte bus transaction (not a 4KiB crossing). */
    CHECK(m.cpu.dwrite_hits-hits_before == (direct && !crossing ? 1u : 0u));
    CHECK(event_count == 2);
    m.bus.ram_written = NULL;
    s5l8900_free(&m);
}

static void dma(unsigned width, bool direct) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, 0x08000000u, 0x200000u));
    CHECK(s5l8900_set_direct_ram_writes(&m, direct));
    const uint8_t source[16] = {0x19,0x2a,0x3b,0x4c,0x5d,0x6e,0x7f,0x80,
                                0x91,0xa2,0xb3,0xc4,0xd5,0xe6,0xf7,0x08};
    s5l8900_load(&m, 0x08000200u, source, sizeof source);
    CHECK(!memcmp(m.ram+0x200u, source, sizeof source));
    unsigned encoding = width == 4 ? 2 : width == 2 ? 1 : 0;
    uint32_t dest = 0x08000400u-width; /* Four stores cross physical 1KiB. */
    s5l_pl080_t *d = &m.dmac[0];
    s5l_pl080_write(d, 0x100u, 0x08000200u);
    s5l_pl080_write(d, 0x104u, dest);
    s5l_pl080_write(d, 0x108u, 0);
    s5l_pl080_write(d, 0x10cu, 4u | (encoding<<18) | (encoding<<21) |
                                    0x04000000u | 0x08000000u | 0x80000000u);
    s5l_pl080_write(d, 0x110u, 0x8001u);
    observe(&m);
    (void)s5l_pl080_run(d, &m.bus, NULL, NULL);
    CHECK(event_count == 0); /* Controller globally disabled. */
    s5l_pl080_write(d, 0x030u, 1);
    (void)s5l_pl080_run(d, &m.bus, NULL, NULL);
    CHECK(event_count == 4);
    for (unsigned i=0; i<4; ++i)
        event(i, dest-m.ram_base+i*width, width, source+i*width);
    CHECK(!memcmp(m.ram+dest-m.ram_base, source, 4*width));
    (void)s5l_pl080_run(d, &m.bus, NULL, NULL);
    CHECK(event_count == 4); /* Completed channel does not replay writes. */
    m.bus.write32(m.bus.ctx, S5L8900_VIC0_BASE+VIC_INTENABLE, 1);
    CHECK(event_count == 4); /* MMIO is not a main-RAM notification. */
    m.bus.ram_written = NULL;
    s5l8900_free(&m);
}

static void mbx(bool direct, bool missing_mapping) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, 0x08000000u, 0x200000u));
    CHECK(s5l8900_set_direct_ram_writes(&m, direct));
    const uint32_t packet[16] = {
        0xa0060500u,0x00810000u,0x94060500u,0x00800080u,
        0x30008003u,0x60800200u,0x8000ccccu,0xffffffffu,
        0x00050006u,0x00090008u,0x70000000u,0x70000000u,
        0x70000000u,0x70000000u,0x70000000u,0x70000000u
    };
    const uint32_t source[2][4] = {
        {0x11223344,0x55667788,0x99aabbcc,0xddeeff00},
        {0x01234567,0x89abcdef,0x13579bdf,0x2468ace0}
    };
    const uint32_t dest[2] = {0x40e14u,0x50314u};
    m.bus.write32(m.bus.ctx,0x3b001008u,0x08001000u);
    m.bus.write32(m.bus.ctx,0x08001000u,0x08020000u);
    m.bus.write32(m.bus.ctx,0x08001004u,0x08021000u);
    m.bus.write32(m.bus.ctx,0x08001044u,0x08040000u);
    if (!missing_mapping)
        m.bus.write32(m.bus.ctx,0x08001048u,0x08050000u);
    s5l8900_load(&m,0x08020f88u,source[0],16);
    s5l8900_load(&m,0x08021488u,source[1],16);
    CHECK(!memcmp(m.ram+0x20f88u,source[0],16));
    CHECK(!memcmp(m.ram+0x21488u,source[1],16));
    observe(&m);
    for (unsigned i=0; i<16; ++i)
        m.bus.write32(m.bus.ctx,0x3ba00000u+i*4,packet[i]);
    CHECK(event_count == 0); /* Complete packet is not submission. */
    m.bus.write32(m.bus.ctx,0x3ba00000u,0xf0000000u);
    if (missing_mapping) {
        CHECK(event_count == 0);
        const uint8_t zero[16] = {0};
        CHECK(!memcmp(m.ram+dest[0],zero,16));
        CHECK(!memcmp(m.ram+dest[1],zero,16));
        CHECK(!(s5l_mbx_read(&m.mbx,0x12cu) & 0x400u));
    } else {
        CHECK(event_count == (direct ? 2u : 8u));
        for (unsigned row=0; row<2; ++row) {
            CHECK(!memcmp(m.ram+dest[row],source[row],16));
            if (direct) event(row,dest[row],16,(const uint8_t *)source[row]);
            else for (unsigned i=0; i<4; ++i)
                event(row*4+i,dest[row]+i*4,4,(const uint8_t *)&source[row][i]);
        }
        CHECK(s5l_mbx_read(&m.mbx,0x12cu) & 0x400u);
    }
    m.bus.ram_written = NULL;
    s5l8900_free(&m);
}

int main(void) {
    for (unsigned direct=0; direct<2; ++direct) {
        for (unsigned width=1; width<=4; width*=2) cpu_store(width,direct,false);
        cpu_store(4,direct,true);
        for (unsigned width=1; width<=4; width*=2) dma(width,direct);
        mbx(direct,false);
        mbx(direct,true);
    }
    printf("device-write-notify: %u checks PASS (8 CPU, 6 DMA, 4 MBX cases)\n", checks);
    return 0;
}
