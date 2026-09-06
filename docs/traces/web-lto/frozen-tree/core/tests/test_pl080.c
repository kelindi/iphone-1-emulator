/*
 * S5LBox — the PL080 DMA controllers.
 *
 * The property being defended is not "the registers store what was written".
 * It is that the exact sequence AppleARMPL080DMAC performs — power the block,
 * clear every channel, program four registers from a linked-list item, set the
 * enable bit — moves the exact bytes to the exact addresses and then raises the
 * exact interrupt the driver's filter reads, and that everything this model
 * does NOT implement is refused by name with a counter rather than quietly
 * doing nothing.
 *
 * The register offsets and bit positions below are written as literals rather
 * than through the PL080_* macros on purpose, for the reason
 * core/tests/test_buttons.c transcribes the device tree by hand: a test that
 * spells its expectations with the model's own constants agrees with the model
 * by construction and checks nothing. Each literal's provenance is the
 * disassembly address in the PL080 block of soc.h.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#include "soc.h"
#include "snapshot.h"
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

/* ------------------------------------------------------------ a fake bus ---
 *
 * Addresses below FAKE_RAM are memory; everything above is "a peripheral", and
 * every store to one is recorded with its address, value AND width. The width
 * matters: the device tree programs the I2S FIFOs at 16 bits, and a model that
 * silently promoted that to a word would look correct in every byte-count
 * assertion while writing something no peripheral asked for.
 */
#define FAKE_RAM   0x1000u
#define FAKE_STORES 64u

typedef struct {
    uint8_t  mem[FAKE_RAM];
    uint32_t addr[FAKE_STORES];
    uint32_t val[FAKE_STORES];
    unsigned width[FAKE_STORES];
    unsigned stores;
    unsigned overflow;
    unsigned loads;
} fake_t;

static uint32_t fake_load(fake_t *f, uint32_t a, unsigned w) {
    f->loads++;
    uint32_t v = 0;
    if ((uint64_t)a + w > FAKE_RAM) return 0u;
    memcpy(&v, &f->mem[a], w);   /* little-endian host, as bus_read() does */
    return v;
}
static void fake_store(fake_t *f, uint32_t a, uint32_t v, unsigned w) {
    if ((uint64_t)a + w <= FAKE_RAM) { memcpy(&f->mem[a], &v, w); return; }
    if (f->stores >= FAKE_STORES) { f->overflow++; return; }
    f->addr[f->stores] = a;
    f->val[f->stores] = v;
    f->width[f->stores] = w;
    f->stores++;
}
static uint32_t fr32(void *c, uint32_t a) { return fake_load(c, a, 4); }
static uint16_t fr16(void *c, uint32_t a) { return (uint16_t)fake_load(c, a, 2); }
static uint8_t  fr8 (void *c, uint32_t a) { return (uint8_t) fake_load(c, a, 1); }
static void fw32(void *c, uint32_t a, uint32_t v) { fake_store(c, a, v, 4); }
static void fw16(void *c, uint32_t a, uint16_t v) { fake_store(c, a, v, 2); }
static void fw8 (void *c, uint32_t a, uint8_t  v) { fake_store(c, a, v, 1); }

static void fake_bus(arm_bus_t *b, fake_t *f) {
    memset(b, 0, sizeof *b);
    memset(f, 0, sizeof *f);
    b->ctx = f;
    b->read32 = fr32; b->read16 = fr16; b->read8 = fr8;
    b->write32 = fw32; b->write16 = fw16; b->write8 = fw8;
}

/* Offsets, spelled out. See soc.h for the call site each was read from. */
#define OFF_INTSTATUS   0x000u
#define OFF_INTTCSTATUS 0x004u
#define OFF_INTTCCLEAR  0x008u
#define OFF_RAWTC       0x014u
#define OFF_ENBLDCHNS   0x01cu
#define OFF_DMACCONFIG  0x030u
#define CH(n, r)        (0x100u + 0x20u * (n) + (r))
#define R_SRC  0x00u
#define R_DST  0x04u
#define R_LLI  0x08u
#define R_CTRL 0x0cu
#define R_CFG  0x10u

/* Bits, spelled out. */
#define B_EN       0x00000001u
#define B_ITC      0x00008000u
#define B_ACTIVE   0x00020000u
#define B_HALT     0x00040000u
#define B_FLOW_M2P 0x00000800u  /* 0xc070e458: direction "out" writes 0x800  */
#define B_FLOW_P2M 0x00001000u  /* 0xc070e454: direction "in"  writes 0x1000 */
#define C_SI       0x04000000u
#define C_DI       0x08000000u
#define C_I        0x80000000u
#define C_W16      ((1u << 18) | (1u << 21))
#define C_W32      ((2u << 18) | (2u << 21))
#define C_W8       0u

/* ------------------------------------------------------------------------ */

static void test_the_register_map_is_the_drivers_own(void) {
    s5l_pl080_t d;
    s5l_pl080_reset(&d);

    /* Eight channels, 0x20 apart, five registers each. The power-on path at
     * 0xc070edb8 clears 0x130..0x1f0 in steps of 0x20 after doing 0x110/0x108
     * explicitly, which is what fixes both the count and the stride. */
    for (unsigned n = 0; n < 8u; n++) {
        s5l_pl080_write(&d, CH(n, R_SRC),  0xa0000000u + n);
        s5l_pl080_write(&d, CH(n, R_DST),  0xb0000000u + n);
        s5l_pl080_write(&d, CH(n, R_LLI),  0xc0000000u + n);
        s5l_pl080_write(&d, CH(n, R_CTRL), 0x00249000u + n);
        s5l_pl080_write(&d, CH(n, R_CFG),  0x00000800u + n);
    }
    for (unsigned n = 0; n < 8u; n++) {
        CHECK(s5l_pl080_read(&d, CH(n, R_SRC))  == 0xa0000000u + n,
              "channel %u SrcAddr does not read back", n);
        CHECK(s5l_pl080_read(&d, CH(n, R_DST))  == 0xb0000000u + n,
              "channel %u DestAddr does not read back", n);
        CHECK(s5l_pl080_read(&d, CH(n, R_LLI))  == 0xc0000000u + n,
              "channel %u LLI does not read back", n);
        CHECK(s5l_pl080_read(&d, CH(n, R_CTRL)) == 0x00249000u + n,
              "channel %u Control does not read back", n);
        CHECK(s5l_pl080_read(&d, CH(n, R_CFG))  == 0x00000800u + n,
              "channel %u Configuration does not read back", n);
    }
    CHECK(d.unknown_reads == 0 && d.unknown_writes == 0,
          "the forty per-channel registers the driver uses are not all "
          "recognised: %llu unknown reads, %llu unknown writes",
          (unsigned long long)d.unknown_reads,
          (unsigned long long)d.unknown_writes);

    /*
     * A ninth channel is not one. startDMACommand at 0xc070e0f8 refuses an
     * index above 7 outright, so 0x200 is past the end of the file and must be
     * logged rather than answered — otherwise a driver that miscomputed a
     * channel index would get a plausible register instead of a symptom.
     */
    uint64_t before = d.unknown_writes;
    s5l_pl080_write(&d, 0x200u, 0xdeadbeefu);
    CHECK(d.unknown_writes == before + 1u,
          "offset 0x200 was accepted as a ninth channel");
    CHECK(s5l_pl080_read(&d, 0x200u) == 0u, "offset 0x200 answered non-zero");

    /* And the reserved words inside a channel block, 0x14-0x1c. */
    before = d.unknown_reads;
    (void)s5l_pl080_read(&d, CH(3, 0x14u));
    (void)s5l_pl080_read(&d, CH(3, 0x18u));
    (void)s5l_pl080_read(&d, CH(3, 0x1cu));
    CHECK(d.unknown_reads == before + 3u,
          "the reserved words in a channel block were answered as registers");

    /* The controller-wide file the driver touches. 0x000 is read in the
     * interrupt filter (0xc070f16c) and 0x030 written by the power path
     * (0xc070ed7c); the rest are the status words those two imply. */
    s5l_pl080_reset(&d);
    s5l_pl080_write(&d, OFF_DMACCONFIG, 1u);
    CHECK(s5l_pl080_read(&d, OFF_DMACCONFIG) == 1u,
          "DMACConfiguration does not read back");
    CHECK(s5l_pl080_read(&d, OFF_ENBLDCHNS) == 0u,
          "EnbldChns is non-zero with no channel enabled");
    s5l_pl080_write(&d, CH(5, R_CFG), B_EN);
    CHECK(s5l_pl080_read(&d, OFF_ENBLDCHNS) == (1u << 5),
          "EnbldChns does not mirror the enable bits: %#x",
          s5l_pl080_read(&d, OFF_ENBLDCHNS));
    CHECK(d.unknown_reads == 0 && d.unknown_writes == 0,
          "a controller-wide register the driver uses is unrecognised");

    /* The two identification blocks are deliberately NOT implemented — we have
     * no dump of Apple's values and the driver reads none of them — so they
     * must land in the unknown log rather than answering a made-up constant. */
    CHECK(s5l_pl080_read(&d, 0xfe0u) == 0u, "PeriphID0 answered a value");
    CHECK(s5l_pl080_read(&d, 0xff0u) == 0u, "PCellID0 answered a value");
    CHECK(d.unknown_reads == 2u,
          "the identification registers were not counted as unmodelled");
}

static void test_reset_is_total(void) {
    s5l_pl080_t d;
    memset(&d, 0xa5, sizeof d);
    s5l_pl080_reset(&d);
    static const uint8_t zero[sizeof(s5l_pl080_t)] = {0};
    CHECK(memcmp(&d, zero, sizeof d) == 0,
          "reset left a poisoned object non-zero — the part comes up with the "
          "controller disabled and every channel disabled, which is why the "
          "driver's power-on path writes 0x030 = 1 before anything else");
}

static void test_the_active_bit_is_read_only_and_reads_zero(void) {
    s5l_pl080_t d;
    s5l_pl080_reset(&d);

    /*
     * This is the assertion with a hang behind it. queryDMACommand at
     * 0xc070efb4 re-reads Configuration and spins on `tst r0,#0x20000` with no
     * iteration count and no deadline; a model that ever let bit 17 stick would
     * not fail a boot, it would stop one.
     */
    s5l_pl080_write(&d, CH(2, R_CFG), B_EN | B_ITC | B_ACTIVE);
    CHECK((s5l_pl080_read(&d, CH(2, R_CFG)) & B_ACTIVE) == 0u,
          "a guest write set the Active bit — queryDMACommand would spin "
          "forever on it");
    CHECK((s5l_pl080_read(&d, CH(2, R_CFG)) & (B_EN | B_ITC)) == (B_EN | B_ITC),
          "masking Active also lost the bits beside it");
}

/* Program one channel for a memory-to-peripheral transfer and enable it. */
static void program(s5l_pl080_t *d, unsigned c, uint32_t src, uint32_t dst,
                    uint32_t ctrl, uint32_t cfg, uint32_t lli) {
    s5l_pl080_write(d, CH(c, R_SRC),  src);
    s5l_pl080_write(d, CH(c, R_DST),  dst);
    s5l_pl080_write(d, CH(c, R_LLI),  lli);
    s5l_pl080_write(d, CH(c, R_CTRL), ctrl);
    s5l_pl080_write(d, CH(c, R_CFG),  cfg);
}

static void test_a_memory_to_peripheral_transfer_moves_the_exact_bytes(void) {
    s5l_pl080_t d;
    fake_t f;
    arm_bus_t bus;
    s5l_pl080_reset(&d);
    fake_bus(&bus, &f);

    /* Eight halfwords, recognisable, at 0x40. */
    static const uint16_t sample[8] = {
        0x0001u, 0x8000u, 0x1234u, 0xfffeu,
        0x5555u, 0x0000u, 0xaaaau, 0x7fffu
    };
    memcpy(&f.mem[0x40], sample, sizeof sample);

    /*
     * The exact program the guest builds for /arm-io/i2s0's transmit channel.
     * `dma-channels` entry 0 is {0x00000800, 0x00249000, 0x0, 0x3ca00010, ...};
     * startDMACommand ORs the source-increment bit for direction "out"
     * (0xc070e4bc), the terminal-count bit on the last item (0xc070fbfc) and
     * the transfer count into Control[11:0] (0xc070e4b4), and starts the
     * channel with the Configuration template | 0x8001 (0xc070e9d0).
     */
    program(&d, 0, 0x40u, 0x3ca00010u,
            0x00249000u | C_SI | C_I | 8u,
            0x00000800u | B_ITC | B_EN, 0u);
    s5l_pl080_write(&d, OFF_DMACCONFIG, 1u);

    /* Nothing may have moved yet: the register write does not transfer. */
    CHECK(d.bytes_moved == 0u,
          "bytes moved from inside the register store — the model is "
          "re-entering the bus from a bus write");

    (void)s5l_pl080_run(&d, &bus, NULL, NULL);

    CHECK(f.stores == 8u, "expected 8 peripheral stores, got %u", f.stores);
    CHECK(f.overflow == 0u, "the recorder overflowed");
    unsigned bad_addr = 0, bad_width = 0, bad_val = 0;
    for (unsigned i = 0; i < f.stores && i < 8u; i++) {
        if (f.addr[i] != 0x3ca00010u) bad_addr++;
        if (f.width[i] != 2u) bad_width++;
        if (f.val[i] != sample[i]) bad_val++;
    }
    CHECK(bad_addr == 0,
          "%u stores went somewhere other than the I2S0 transmit FIFO — the "
          "destination does not increment, because Control bit 27 is clear",
          bad_addr);
    CHECK(bad_width == 0,
          "%u stores were not 16-bit — Control[23:21] says 1 and the driver's "
          "own byte-limit arithmetic at 0xc070e2fc agrees (0xe00 << 1)",
          bad_width);
    CHECK(bad_val == 0, "%u stores carried the wrong halfword", bad_val);

    CHECK(d.bytes_moved == 16u, "bytes_moved is %llu, expected 16",
          (unsigned long long)d.bytes_moved);
    CHECK(d.transfers == 8u, "transfers is %llu, expected 8",
          (unsigned long long)d.transfers);
    CHECK(d.items == 1u, "items is %llu, expected 1",
          (unsigned long long)d.items);

    /* And the source advanced by the bytes it read, once per transfer. */
    CHECK(s5l_pl080_read(&d, CH(0, R_SRC)) == 0x40u + 16u,
          "SrcAddr did not advance to %#x: %#x", 0x40u + 16u,
          s5l_pl080_read(&d, CH(0, R_SRC)));
    CHECK(s5l_pl080_read(&d, CH(0, R_DST)) == 0x3ca00010u,
          "DestAddr advanced with DI clear");
    CHECK((s5l_pl080_read(&d, CH(0, R_CTRL)) & 0xfffu) == 0u,
          "the remaining count did not reach zero — the driver reads exactly "
          "this field (0xc070f9f8, `and r0,r0,#0xfff`) to report progress");
}

static void test_a_peripheral_to_memory_transfer_runs_the_other_way(void) {
    s5l_pl080_t d;
    fake_t f;
    arm_bus_t bus;
    s5l_pl080_reset(&d);
    fake_bus(&bus, &f);

    /* i2s0 `dma-channels` entry 1: {0x00001042, 0x00249000, 0x3ca00038, 0x0}.
     * Flow control 2, source fixed, destination incrementing. */
    program(&d, 1, 0x3ca00038u, 0x80u,
            0x00249000u | C_DI | C_I | 4u,
            0x00001042u | B_ITC | B_EN, 0u);
    s5l_pl080_write(&d, OFF_DMACCONFIG, 1u);
    (void)s5l_pl080_run(&d, &bus, NULL, NULL);

    CHECK(f.stores == 0u,
          "a peripheral-to-memory transfer stored outside memory");
    CHECK(d.bytes_moved == 8u, "bytes_moved is %llu, expected 8",
          (unsigned long long)d.bytes_moved);
    CHECK(s5l_pl080_read(&d, CH(1, R_SRC)) == 0x3ca00038u,
          "SrcAddr advanced with SI clear — the receive FIFO is one address");
    CHECK(s5l_pl080_read(&d, CH(1, R_DST)) == 0x80u + 8u,
          "DestAddr did not advance");
    CHECK((d.raw_tc & (1u << 1)) != 0u,
          "the receive channel did not latch its terminal count");
}

static void test_a_linked_list_is_followed_and_reloaded_from_memory(void) {
    s5l_pl080_t d;
    fake_t f;
    arm_bus_t bus;
    s5l_pl080_reset(&d);
    fake_bus(&bus, &f);

    /*
     * Two items. The builder at 0xc070fba0 stores {Src, Dst, Next, Control} in
     * that order at +0/+4/+8/+0xc, sets Next to zero on the last one, and puts
     * the terminal-count bit only there.
     */
    for (unsigned i = 0; i < 8u; i++) f.mem[0x100u + i] = (uint8_t)(0x10u + i);
    uint32_t lli[4] = { 0x100u, 0x3ca00010u, 0x00000000u,
                        0x00249000u | C_SI | C_I | 4u };
    memcpy(&f.mem[0x200], lli, sizeof lli);

    for (unsigned i = 0; i < 8u; i++) f.mem[0x180u + i] = (uint8_t)(0x90u + i);
    /* The LLI register's low two bits are the AHB master select, which the
     * driver masks off on every read-back (`bic r0,#3`, 0xc070e718). Set them
     * here so a model that failed to mask would fetch from 0x202. */
    program(&d, 0, 0x180u, 0x3ca00010u,
            0x00249000u | C_SI | 4u,
            0x00000800u | B_ITC | B_EN, 0x200u | 3u);
    s5l_pl080_write(&d, OFF_DMACCONFIG, 1u);
    (void)s5l_pl080_run(&d, &bus, NULL, NULL);

    CHECK(d.items == 2u, "items is %llu, expected 2 — the chain was not "
          "followed", (unsigned long long)d.items);
    CHECK(f.stores == 8u, "expected 8 stores across both items, got %u",
          f.stores);
    CHECK(d.bytes_moved == 16u, "bytes_moved is %llu, expected 16",
          (unsigned long long)d.bytes_moved);
    if (f.stores >= 8u) {
        CHECK(f.val[0] == 0x9190u && f.val[3] == 0x9796u,
              "the first item did not read from 0x180: %#x %#x",
              f.val[0], f.val[3]);
        CHECK(f.val[4] == 0x1110u && f.val[7] == 0x1716u,
              "the second item did not read from the address in the LLI: "
              "%#x %#x — the low two bits of the LLI register are the master "
              "select and must be masked off", f.val[4], f.val[7]);
    }
    CHECK(d.completions == 1u,
          "the terminal count fired %llu times; only the LAST item carries "
          "Control bit 31", (unsigned long long)d.completions);
    CHECK(s5l_pl080_read(&d, CH(0, R_LLI)) == 0u,
          "the LLI register did not end at zero — the driver's progress walk "
          "compares it against the items it queued (0xc070f954)");
}

static void test_mismatched_widths_pack_through_the_channel(void) {
    s5l_pl080_t d;
    fake_t f;
    arm_bus_t bus;
    s5l_pl080_reset(&d);
    fake_bus(&bus, &f);

    /*
     * /arm-io/spi1's `dma-channels` entry, verbatim:
     *   {0x00000b00, 0x00089000, 0x00000000, 0x3ce00010, 0,0,0,0}
     * 0x00089000 is SWidth 2 (four bytes) and DWidth 0 (one byte) — the ONLY
     * entry in the shipped tree where the two differ, and it went live the
     * first boot this model existed: AppleMultitouchZ2SPI announced "using DMA
     * for bootloading", which it had never said before. 0x3ce00010 is spi1's
     * SPI_TXDATA. Control[11:0] counts source transfers, so three here means
     * three words in and twelve bytes out.
     */
    static const uint32_t words[3] = { 0x44332211u, 0x88776655u, 0xccbbaa99u };
    memcpy(&f.mem[0x60], words, sizeof words);
    program(&d, 0, 0x60u, 0x3ce00010u,
            0x00089000u | C_SI | C_I | 3u,
            0x00000b00u | B_ITC | B_EN, 0u);
    s5l_pl080_write(&d, OFF_DMACCONFIG, 1u);
    (void)s5l_pl080_run(&d, &bus, NULL, NULL);

    CHECK(d.refused_width == 0u,
          "a four-into-one width change was refused; spi1 uses exactly this");
    CHECK(f.stores == 12u, "expected 12 byte stores, got %u", f.stores);
    CHECK(d.transfers == 3u,
          "transfers is %llu — Control[11:0] counts SOURCE transfers",
          (unsigned long long)d.transfers);
    CHECK(d.bytes_moved == 12u, "bytes_moved is %llu, expected 12",
          (unsigned long long)d.bytes_moved);

    unsigned bad = 0;
    for (unsigned i = 0; i < f.stores && i < 12u; i++) {
        if (f.width[i] != 1u) { bad++; continue; }
        if (f.addr[i] != 0x3ce00010u) { bad++; continue; }
        /* Little-endian bus: the low byte of a word goes out first. */
        uint32_t want = (words[i / 4u] >> (8u * (i % 4u))) & 0xffu;
        if (f.val[i] != want) bad++;
    }
    CHECK(bad == 0,
          "%u of the twelve byte stores were wrong — a word must unpack low "
          "byte first, at one fixed address, one byte at a time", bad);

    /* And the other direction: bytes packed into words. Nothing in the shipped
     * tree asks for it, but it is the same rule and must not silently differ. */
    s5l_pl080_reset(&d);
    fake_bus(&bus, &f);
    for (unsigned i = 0; i < 8u; i++) f.mem[0x60u + i] = (uint8_t)(0x11u * i);
    program(&d, 0, 0x60u, 0x3ce00010u,
            (0u << 18) | (2u << 21) | C_SI | C_I | 8u,
            B_FLOW_M2P | B_ITC | B_EN, 0u);
    s5l_pl080_write(&d, OFF_DMACCONFIG, 1u);
    (void)s5l_pl080_run(&d, &bus, NULL, NULL);
    CHECK(f.stores == 2u, "expected 2 word stores, got %u", f.stores);
    if (f.stores >= 2u) {
        CHECK(f.width[0] == 4u && f.val[0] == 0x33221100u,
              "the first packed word is %#x/%u wide", f.val[0], f.width[0]);
        CHECK(f.width[1] == 4u && f.val[1] == 0x77665544u,
              "the second packed word is %#x/%u wide", f.val[1], f.width[1]);
    }
    CHECK(d.bytes_moved == 8u, "bytes_moved is %llu, expected 8",
          (unsigned long long)d.bytes_moved);
}

static void test_a_channel_disables_itself_at_the_end_of_the_chain(void) {
    s5l_pl080_t d;
    fake_t f;
    arm_bus_t bus;
    s5l_pl080_reset(&d);
    fake_bus(&bus, &f);

    program(&d, 4, 0x40u, 0x3ca00010u, 0x00249000u | C_SI | C_I | 2u,
            0x00000800u | B_ITC | B_EN, 0u);
    s5l_pl080_write(&d, OFF_DMACCONFIG, 1u);
    CHECK((s5l_pl080_read(&d, CH(4, R_CFG)) & B_EN) != 0u,
          "setup: the channel was not enabled");
    (void)s5l_pl080_run(&d, &bus, NULL, NULL);

    /*
     * The part clears the enable bit when the transfer ends, and the driver
     * depends on it: at 0xc070e950 it re-reads Configuration and only rewrites
     * SrcAddr/DestAddr/Control when bit 0 is CLEAR. Leaving it set would make
     * every later command believe the hardware was still busy with the first.
     */
    CHECK((s5l_pl080_read(&d, CH(4, R_CFG)) & B_EN) == 0u,
          "the channel stayed enabled after its chain ended");
    CHECK(s5l_pl080_read(&d, OFF_ENBLDCHNS) == 0u,
          "EnbldChns still reports the finished channel");

    /* A second run must not repeat the transfer. */
    unsigned was = f.stores;
    (void)s5l_pl080_run(&d, &bus, NULL, NULL);
    CHECK(f.stores == was, "a finished channel transferred again");
}

static void test_the_controller_enable_gates_every_channel(void) {
    s5l_pl080_t d;
    fake_t f;
    arm_bus_t bus;
    s5l_pl080_reset(&d);
    fake_bus(&bus, &f);

    program(&d, 0, 0x40u, 0x3ca00010u, 0x00249000u | C_SI | C_I | 4u,
            0x00000800u | B_ITC | B_EN, 0u);
    /* 0x030 left at its reset zero. */
    (void)s5l_pl080_run(&d, &bus, NULL, NULL);
    CHECK(f.stores == 0u && d.bytes_moved == 0u,
          "a disabled controller transferred: %u stores", f.stores);
    CHECK(s5l_pl080_irq(&d) == false, "a disabled controller raised a line");

    s5l_pl080_write(&d, OFF_DMACCONFIG, 1u);
    (void)s5l_pl080_run(&d, &bus, NULL, NULL);
    CHECK(f.stores == 4u, "enabling the controller did not release the "
          "channel: %u stores", f.stores);
}

static void test_halt_holds_a_channel_without_disabling_it(void) {
    s5l_pl080_t d;
    fake_t f;
    arm_bus_t bus;
    s5l_pl080_reset(&d);
    fake_bus(&bus, &f);

    /*
     * The abort path at 0xc070ef04 writes the Configuration template with
     * 0x00040001 — enable AND halt — and then polls the Active bit. A model
     * that treated any write with bit 0 set as "go" would start the very
     * transfer that write exists to stop.
     */
    program(&d, 6, 0x40u, 0x3ca00010u, 0x00249000u | C_SI | C_I | 4u,
            0x00000800u | B_ITC | 0x00040001u, 0u);
    s5l_pl080_write(&d, OFF_DMACCONFIG, 1u);
    (void)s5l_pl080_run(&d, &bus, NULL, NULL);

    CHECK(f.stores == 0u && d.bytes_moved == 0u,
          "a halted channel transferred: %u stores", f.stores);
    CHECK((s5l_pl080_read(&d, CH(6, R_CFG)) & B_EN) != 0u,
          "halt cleared the enable bit; the driver clears it separately");
    CHECK((s5l_pl080_read(&d, CH(6, R_CFG)) & B_ACTIVE) == 0u,
          "a halted channel reports data still in its FIFO — 0xc070ef20 polls "
          "exactly this and reports kIOReturnBusy on it");

    /* Releasing halt releases the transfer. */
    s5l_pl080_write(&d, CH(6, R_CFG), 0x00000800u | B_ITC | B_EN);
    (void)s5l_pl080_run(&d, &bus, NULL, NULL);
    CHECK(f.stores == 4u, "clearing halt did not release the channel: %u",
          f.stores);
}

static void test_the_completion_interrupt_is_the_one_the_filter_reads(void) {
    s5l_pl080_t d;
    fake_t f;
    arm_bus_t bus;
    s5l_pl080_reset(&d);
    fake_bus(&bus, &f);
    s5l_pl080_write(&d, OFF_DMACCONFIG, 1u);

    /* An item WITHOUT the terminal-count bit raises nothing. */
    program(&d, 3, 0x40u, 0x3ca00010u, 0x00249000u | C_SI | 2u,
            0x00000800u | B_ITC | B_EN, 0u);
    (void)s5l_pl080_run(&d, &bus, NULL, NULL);
    CHECK(d.bytes_moved == 4u, "setup: the transfer did not run");
    CHECK(s5l_pl080_read(&d, OFF_INTSTATUS) == 0u,
          "an item with Control bit 31 clear raised a terminal count");
    CHECK(s5l_pl080_irq(&d) == false, "the line asserted without a completion");

    /* With it, exactly the channel's bit, in all three status words. */
    program(&d, 3, 0x40u, 0x3ca00010u, 0x00249000u | C_SI | C_I | 2u,
            0x00000800u | B_ITC | B_EN, 0u);
    (void)s5l_pl080_run(&d, &bus, NULL, NULL);
    CHECK(s5l_pl080_read(&d, OFF_RAWTC) == (1u << 3),
          "RawIntTCStatus is %#x, expected bit 3",
          s5l_pl080_read(&d, OFF_RAWTC));
    CHECK(s5l_pl080_read(&d, OFF_INTTCSTATUS) == (1u << 3),
          "IntTCStatus is %#x, expected bit 3",
          s5l_pl080_read(&d, OFF_INTTCSTATUS));
    CHECK(s5l_pl080_read(&d, OFF_INTSTATUS) == (1u << 3),
          "IntStatus is %#x — this is the word the interrupt filter reads "
          "first, at 0xc070f16c", s5l_pl080_read(&d, OFF_INTSTATUS));
    CHECK(s5l_pl080_irq(&d), "the line did not assert on a terminal count");

    /*
     * The filter's second act: write the word it just read back to 0x008.
     * If that did not clear the raw status the line would never drop and the
     * guest would take the same interrupt forever.
     */
    s5l_pl080_write(&d, OFF_INTTCCLEAR, s5l_pl080_read(&d, OFF_INTSTATUS));
    CHECK(s5l_pl080_read(&d, OFF_INTSTATUS) == 0u,
          "writing IntTCClear did not clear the status");
    CHECK(s5l_pl080_irq(&d) == false, "the line stayed high after the clear");

    /* And the ITC mask really masks: raw set, masked clear, no line. */
    s5l_pl080_reset(&d);
    s5l_pl080_write(&d, OFF_DMACCONFIG, 1u);
    program(&d, 3, 0x40u, 0x3ca00010u, 0x00249000u | C_SI | C_I | 2u,
            0x00000800u | B_EN, 0u);          /* no ITC */
    (void)s5l_pl080_run(&d, &bus, NULL, NULL);
    CHECK(s5l_pl080_read(&d, OFF_RAWTC) == (1u << 3),
          "the raw status is masked by ITC — it must not be");
    CHECK(s5l_pl080_read(&d, OFF_INTSTATUS) == 0u,
          "a channel with ITC clear reached IntStatus");
    CHECK(s5l_pl080_irq(&d) == false, "a masked completion raised the line");
}

static void test_refusals_are_counted_and_named(void) {
    s5l_pl080_t d;
    fake_t f;
    arm_bus_t bus;

    /* Flow control 4-7: the peripheral supplies the transfer size, and this
     * model has no request lines to hear it with. */
    s5l_pl080_reset(&d);
    fake_bus(&bus, &f);
    s5l_pl080_write(&d, OFF_DMACCONFIG, 1u);
    program(&d, 0, 0x40u, 0x3ca00010u, 0x00249000u | C_SI | C_I | 4u,
            (6u << 11) | B_ITC | B_EN, 0u);
    (void)s5l_pl080_run(&d, &bus, NULL, NULL);
    CHECK(d.refused_flow == 1u, "peripheral-controlled flow was not refused");
    CHECK(f.stores == 0u && d.bytes_moved == 0u,
          "a refused channel still transferred");
    CHECK((s5l_pl080_read(&d, CH(0, R_CFG)) & B_EN) == 0u,
          "a refused channel was left enabled — the driver would wait on it "
          "forever");
    CHECK(s5l_pl080_irq(&d) == false, "a refusal fabricated a completion");

    /* All four DMA-controller-as-flow-controller encodings the shipped device
     * tree actually uses do run. 0 (mbx), 1 (i2s0 tx), 2 (i2s0 rx), 3 (uart0
     * peripheral-to-peripheral). */
    for (uint32_t fc = 0; fc <= 3u; fc++) {
        s5l_pl080_reset(&d);
        fake_bus(&bus, &f);
        s5l_pl080_write(&d, OFF_DMACCONFIG, 1u);
        program(&d, 0, 0x40u, 0x3ca00010u, 0x00249000u | C_SI | C_I | 2u,
                (fc << 11) | B_ITC | B_EN, 0u);
        (void)s5l_pl080_run(&d, &bus, NULL, NULL);
        CHECK(d.refused_flow == 0u && d.bytes_moved == 4u,
              "flow control %u was refused; the shipped tree uses it", fc);
    }

    /* A reserved width code, and a total that does not divide into whole
     * destination transfers. Mismatched widths themselves are NOT refused —
     * see test_mismatched_widths_pack_through_the_channel. */
    s5l_pl080_reset(&d);
    fake_bus(&bus, &f);
    s5l_pl080_write(&d, OFF_DMACCONFIG, 1u);
    program(&d, 1, 0x40u, 0x3ca00010u, (5u << 18) | (5u << 21) | C_SI | 2u,
            B_FLOW_M2P | B_ITC | B_EN, 0u);
    (void)s5l_pl080_run(&d, &bus, NULL, NULL);
    CHECK(d.refused_width == 1u, "a reserved width code was accepted");

    /* Three byte-wide source transfers into a word-wide destination: eight
     * bits left over in a FIFO this model does not have. */
    program(&d, 2, 0x40u, 0x3ca00010u, (0u << 18) | (2u << 21) | C_SI | 3u,
            B_FLOW_M2P | B_ITC | B_EN, 0u);
    (void)s5l_pl080_run(&d, &bus, NULL, NULL);
    CHECK(d.refused_width == 2u,
          "a transfer with a partial destination word was accepted — the "
          "remainder would sit in a channel FIFO that does not exist here");
    CHECK(d.bytes_moved == 0u, "a refused width still moved bytes");

    /* The four software-request registers. The stock driver writes none. */
    s5l_pl080_reset(&d);
    s5l_pl080_write(&d, 0x020u, 1u);
    s5l_pl080_write(&d, 0x024u, 1u);
    s5l_pl080_write(&d, 0x028u, 1u);
    s5l_pl080_write(&d, 0x02cu, 1u);
    CHECK(d.refused_softreq == 4u,
          "software DMA requests were not refused: %llu",
          (unsigned long long)d.refused_softreq);
    CHECK(d.unknown_writes == 0u,
          "a named refusal was also counted as an unknown offset — a reader "
          "would see two different problems where there is one");

    /* Big-endian AHB masters. */
    s5l_pl080_reset(&d);
    s5l_pl080_write(&d, OFF_DMACCONFIG, 1u | 0x2u);
    CHECK(d.refused_endian == 1u, "a big-endian request was not refused");

    /* A linked list that points at itself. Real hardware would follow it
     * forever; the cap is ours and it must be counted, not silent. */
    s5l_pl080_reset(&d);
    fake_bus(&bus, &f);
    s5l_pl080_write(&d, OFF_DMACCONFIG, 1u);
    {
        uint32_t self[4] = { 0x40u, 0x3ca00010u, 0x300u, 0x00249000u | 0u };
        memcpy(&f.mem[0x300], self, sizeof self);
    }
    program(&d, 7, 0x40u, 0x3ca00010u, 0x00249000u | 0u,
            B_FLOW_M2P | B_ITC | B_EN, 0x300u);
    (void)s5l_pl080_run(&d, &bus, NULL, NULL);
    CHECK(d.refused_chain == 1u, "a self-referential chain was not refused");
    CHECK(d.items <= (uint64_t)S5L_PL080_MAX_ITEMS + 1u,
          "the chain cap did not bound the walk: %llu items",
          (unsigned long long)d.items);
    CHECK((s5l_pl080_read(&d, CH(7, R_CFG)) & B_EN) == 0u,
          "a channel refused for chain length was left enabled");
}

static void test_a_null_bus_moves_nothing(void) {
    s5l_pl080_t d;
    s5l_pl080_reset(&d);
    s5l_pl080_write(&d, OFF_DMACCONFIG, 1u);
    program(&d, 0, 0x40u, 0x3ca00010u, 0x00249000u | C_SI | C_I | 4u,
            B_FLOW_M2P | B_ITC | B_EN, 0u);
    CHECK(s5l_pl080_run(&d, NULL, NULL, NULL) == false, "a null bus raised a line");
    CHECK(d.bytes_moved == 0u && d.transfers == 0u,
          "a null bus moved %llu bytes", (unsigned long long)d.bytes_moved);
    CHECK(s5l_pl080_run(NULL, NULL, NULL, NULL) == false, "a null device answered true");
    s5l_pl080_reset(NULL);
    CHECK(s5l_pl080_read(NULL, 0u) == 0u, "a null device answered a read");
    s5l_pl080_write(NULL, 0u, 0u);
    g_pass++;   /* survived the four null calls */
}

static void test_the_drivers_own_power_on_sequence(void) {
    s5l_pl080_t d;
    s5l_pl080_reset(&d);

    /* Dirty every channel first, so "cleared" is a change rather than the
     * reset value seen twice. */
    for (unsigned n = 0; n < 8u; n++) {
        s5l_pl080_write(&d, CH(n, R_LLI), 0xdeadbee0u + n);
        s5l_pl080_write(&d, CH(n, R_CFG), 0xffffu);
    }

    /*
     * 0xc070ed34, verbatim: 0x030 = 1, then 0x110 = 0 and 0x108 = 0, then for
     * r5 = 0x130 stepping 0x20 while r5 != 0x210, 0x?30 = 0 and 0x?28 = 0.
     */
    s5l_pl080_write(&d, 0x030u, 1u);
    s5l_pl080_write(&d, 0x110u, 0u);
    s5l_pl080_write(&d, 0x108u, 0u);
    for (uint32_t r5 = 0x130u; r5 != 0x210u; r5 += 0x20u) {
        s5l_pl080_write(&d, r5, 0u);
        s5l_pl080_write(&d, r5 - 8u, 0u);
    }

    CHECK(d.unknown_writes == 0u,
          "the driver's power-on sequence touched %llu offsets this model does "
          "not recognise — the loop bound 0x210 is what fixes the channel "
          "count at eight", (unsigned long long)d.unknown_writes);
    for (unsigned n = 0; n < 8u; n++) {
        CHECK(s5l_pl080_read(&d, CH(n, R_CFG)) == 0u,
              "channel %u Configuration was not cleared by the power-on "
              "sequence — offset %#x is not where this model puts it", n,
              CH(n, R_CFG));
        CHECK(s5l_pl080_read(&d, CH(n, R_LLI)) == 0u,
              "channel %u LLI was not cleared", n);
    }
    CHECK(s5l_pl080_read(&d, OFF_DMACCONFIG) == 1u,
          "the controller did not come up enabled");

    /* And the power-off half. */
    s5l_pl080_write(&d, 0x110u, 0u);
    for (uint32_t r5 = 0x130u; r5 != 0x210u; r5 += 0x20u)
        s5l_pl080_write(&d, r5, 0u);
    s5l_pl080_write(&d, 0x030u, 0u);
    CHECK(s5l_pl080_read(&d, OFF_DMACCONFIG) == 0u,
          "the controller did not power down");
    CHECK(d.unknown_writes == 0u, "the power-off sequence hit an unknown "
          "offset");
}

/* ------------------------------------------------- through a real machine --- */

static void test_the_machine_decodes_both_windows(void) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, 0u, 1u << 16), "init failed");

    /*
     * The bases are /arm-io/dmac0 reg {0x00200000,0x1000} and /arm-io/dmac1
     * {0x01900000,0x1000}, plus /arm-io's own ranges mapping child+0x38000000.
     * Written out here rather than taken from the header, so a typo in the
     * header is a failure rather than an agreement.
     */
    CHECK(S5L8900_DMAC0_BASE == 0x38000000u + 0x00200000u,
          "dmac0 is not at the device tree's address: %#x", S5L8900_DMAC0_BASE);
    CHECK(S5L8900_DMAC1_BASE == 0x38000000u + 0x01900000u,
          "dmac1 is not at the device tree's address: %#x", S5L8900_DMAC1_BASE);
    CHECK(S5L8900_IRQ_DMAC0 == 0x10u && S5L8900_IRQ_DMAC1 == 0x11u,
          "the interrupt numbers are not the tree's {0x10} and {0x11}");

    uint64_t unmapped = m.unmapped_writes;
    m.bus.write32(m.bus.ctx, S5L8900_DMAC0_BASE + 0x030u, 1u);
    m.bus.write32(m.bus.ctx, S5L8900_DMAC1_BASE + 0x030u, 1u);
    CHECK(m.unmapped_writes == unmapped,
          "a DMAC register write fell through to the unmapped path");
    CHECK(m.bus.read32(m.bus.ctx, S5L8900_DMAC0_BASE + 0x030u) == 1u,
          "dmac0 did not answer a read of DMACConfiguration");
    CHECK(m.bus.read32(m.bus.ctx, S5L8900_DMAC1_BASE + 0x030u) == 1u,
          "dmac1 did not answer a read of DMACConfiguration");
    CHECK(m.dmac[0].config == 1u && m.dmac[1].config == 1u,
          "the two windows are not routed to two different controllers");

    /* Both are in the declared window map, and the map still fits. */
    s5l_window_t w[S5L_WINDOW_MAX];
    unsigned nw = s5l8900_windows(&m, w, S5L_WINDOW_MAX);
    CHECK(nw > 0 && nw <= S5L_WINDOW_MAX,
          "window count %u is outside S5L_WINDOW_MAX", nw);
    unsigned found = 0;
    for (unsigned i = 0; i < nw; i++)
        if (w[i].base == S5L8900_DMAC0_BASE || w[i].base == S5L8900_DMAC1_BASE)
            found++;
    CHECK(found == 2u,
          "the DMAC windows are not both declared: found %u", found);

    s5l8900_free(&m);
}

static void test_a_dma_store_lands_where_a_cpu_store_lands(void) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, 0u, 1u << 16), "init failed");

    /*
     * The whole point of the exercise: a DMA store must go through the same
     * decode a CPU store does, so a peripheral's write handler runs. i2s0
     * offset 0x00 is one of the seven the I2S model stores, so the byte that
     * lands there is visible without believing anything the DMAC says about
     * itself.
     */
    uint32_t word = 0xfeedface;
    m.bus.write32(m.bus.ctx, 0x200u, word);
    m.bus.write32(m.bus.ctx, S5L8900_DMAC0_BASE + CH(0, R_SRC), 0x200u);
    m.bus.write32(m.bus.ctx, S5L8900_DMAC0_BASE + CH(0, R_DST),
                  S5L8900_I2S0_BASE + 0x00u);
    m.bus.write32(m.bus.ctx, S5L8900_DMAC0_BASE + CH(0, R_LLI), 0u);
    m.bus.write32(m.bus.ctx, S5L8900_DMAC0_BASE + CH(0, R_CTRL),
                  C_W32 | C_SI | C_I | 1u);
    m.bus.write32(m.bus.ctx, S5L8900_DMAC0_BASE + CH(0, R_CFG),
                  B_FLOW_M2P | B_ITC | B_EN);
    m.bus.write32(m.bus.ctx, S5L8900_DMAC0_BASE + 0x030u, 1u);

    CHECK(m.i2s[0].regs[0] != word,
          "setup: the I2S register already held the value");
    s5l8900_tick(&m, 1u);
    CHECK(m.i2s[0].regs[0] == word,
          "the DMA store did not reach s5l_i2s_write(): %#x", m.i2s[0].regs[0]);
    CHECK(m.i2s[0].writes >= 1u,
          "the I2S model did not count the DMA store as a write");

    /*
     * THE NARROW ACCESS, which this test used to pin in its broken form.
     *
     * The device tree programs these FIFOs at SIXTEEN bits (Control[23:18] of
     * 0x00249000) and /arm-io/spi1's template asks for EIGHT. machine.c used to
     * decode both windows with mmio_word(), which takes 32-bit accesses only,
     * so every narrow DMA store fell past every device onto the unmapped path.
     * The comment here called that "the correct answer for this machine today
     * and also the next thing to fix if the FIFO is ever modelled".
     *
     * It was not merely latent. run114 measured the Z2 firmware download dying
     * in exactly this gap: channel 5, destination SPI1's TXDATA, `runs 210
     * bytes 812340`, against `spi1 words 176` and `unmapped writes 813135`.
     * The DMA controller did all of its work and the port never heard a byte,
     * which is why the digitizer has only ever echoed its idle pattern.
     *
     * So the invariant this test is named for is unchanged and is now asserted
     * the right way round: a DMA store lands where a CPU store lands, and both
     * of them reach the peripheral.
     */
    uint64_t before   = m.unmapped_writes;
    uint64_t i2swrote = m.i2s[0].writes;
    m.bus.write32(m.bus.ctx, S5L8900_DMAC0_BASE + CH(1, R_SRC), 0x200u);
    m.bus.write32(m.bus.ctx, S5L8900_DMAC0_BASE + CH(1, R_DST),
                  S5L8900_I2S0_BASE + 0x10u);
    m.bus.write32(m.bus.ctx, S5L8900_DMAC0_BASE + CH(1, R_LLI), 0u);
    m.bus.write32(m.bus.ctx, S5L8900_DMAC0_BASE + CH(1, R_CTRL),
                  0x00249000u | C_SI | C_I | 4u);
    m.bus.write32(m.bus.ctx, S5L8900_DMAC0_BASE + CH(1, R_CFG),
                  B_FLOW_M2P | B_ITC | B_EN);
    s5l8900_tick(&m, 1u);
    CHECK(m.dmac[0].bytes_moved == 4u + 8u,
          "the 16-bit transfer to the transmit FIFO did not run: %llu bytes",
          (unsigned long long)m.dmac[0].bytes_moved);
    CHECK(m.unmapped_writes == before,
          "%llu halfword store(s) to 0x3ca00010 still took the unmapped path",
          (unsigned long long)(m.unmapped_writes - before));
    CHECK(m.i2s[0].writes == i2swrote + 4u,
          "the I2S model saw %llu of the four halfword DMA stores",
          (unsigned long long)(m.i2s[0].writes - i2swrote));

    /* And the other half of the invariant, measured rather than assumed: the
     * CPU's own halfword store to that address reaches the device too. */
    uint64_t cpu_before = m.unmapped_writes, cpu_wrote = m.i2s[0].writes;
    m.bus.write16(m.bus.ctx, S5L8900_I2S0_BASE + 0x10u, 0x1234u);
    CHECK(m.unmapped_writes == cpu_before,
          "a CPU halfword store to 0x3ca00010 took the unmapped path");
    CHECK(m.i2s[0].writes == cpu_wrote + 1u,
          "a CPU halfword store to 0x3ca00010 did not reach the I2S model");

    s5l8900_free(&m);
}

static void test_a_completion_reaches_the_cpu(void) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, 0u, 1u << 16), "init failed");

    /* dmac0's line is VIC0 bit 16; enable it the way the guest would. */
    m.bus.write32(m.bus.ctx, S5L8900_VIC0_BASE + VIC_INTENABLE,
                  1u << S5L8900_IRQ_DMAC0);
    s5l8900_tick(&m, 1u);
    CHECK(m.cpu.irq_line == false, "setup: the CPU line was already asserted");

    m.bus.write32(m.bus.ctx, 0x200u, 0x11223344u);
    m.bus.write32(m.bus.ctx, S5L8900_DMAC0_BASE + CH(2, R_SRC), 0x200u);
    m.bus.write32(m.bus.ctx, S5L8900_DMAC0_BASE + CH(2, R_DST), 0x300u);
    m.bus.write32(m.bus.ctx, S5L8900_DMAC0_BASE + CH(2, R_LLI), 0u);
    m.bus.write32(m.bus.ctx, S5L8900_DMAC0_BASE + CH(2, R_CTRL),
                  C_W32 | C_SI | C_DI | C_I | 1u);
    m.bus.write32(m.bus.ctx, S5L8900_DMAC0_BASE + CH(2, R_CFG),
                  B_ITC | B_EN);           /* flow 0: memory to memory */
    m.bus.write32(m.bus.ctx, S5L8900_DMAC0_BASE + 0x030u, 1u);

    s5l8900_tick(&m, 1u);
    CHECK(m.bus.read32(m.bus.ctx, 0x300u) == 0x11223344u,
          "the memory-to-memory transfer did not land: %#x",
          m.bus.read32(m.bus.ctx, 0x300u));
    CHECK(m.cpu.irq_line,
          "the terminal count did not reach the CPU through VIC0 line 16");

    /* The driver's filter reads 0x000 and writes it straight back to 0x008. */
    uint32_t status = m.bus.read32(m.bus.ctx, S5L8900_DMAC0_BASE + 0x000u);
    CHECK(status == (1u << 2), "IntStatus is %#x, expected bit 2", status);
    m.bus.write32(m.bus.ctx, S5L8900_DMAC0_BASE + 0x008u, status);
    s5l8900_tick(&m, 1u);
    CHECK(m.cpu.irq_line == false,
          "the line stayed high after the filter's acknowledge — the guest "
          "would take this interrupt forever");

    /* dmac1 is a different line and must not have been touched. */
    CHECK(m.dmac[1].bytes_moved == 0u, "dmac1 moved bytes it was not given");

    s5l8900_free(&m);
}

static void test_both_controllers_are_declared_wake_sources(void) {
    const s5l_wake_source_t *src = NULL;
    unsigned n = s5l8900_wake_sources(&src);
    unsigned found = 0;
    for (unsigned i = 0; i < n; i++)
        if (src[i].line == S5L8900_IRQ_DMAC0 || src[i].line == S5L8900_IRQ_DMAC1)
            found++;
    CHECK(found == 2u,
          "the two DMA controllers are not both in the wake table: found %u. "
          "A source missing from it is a source the next reader has to "
          "rediscover", found);

    /*
     * And both answer NEVER, which is a claim about this model and not a
     * placeholder: a transfer completes inside the tick that the enabling store
     * dirties, so there is never one in flight when a core reaches WFI. The day
     * transfers are paced against a peripheral request line, this assertion is
     * the one that must change first.
     */
    s5l8900_t m;
    CHECK(s5l8900_init(&m, 0u, 1u << 16), "init failed");
    m.bus.write32(m.bus.ctx, S5L8900_VIC0_BASE + VIC_INTENABLE,
                  (1u << S5L8900_IRQ_DMAC0) | (1u << S5L8900_IRQ_DMAC1));
    for (unsigned i = 0; i < n; i++) {
        if (src[i].line != S5L8900_IRQ_DMAC0 &&
            src[i].line != S5L8900_IRQ_DMAC1) continue;
        uint32_t ticks = 0xffffffffu;
        CHECK(src[i].next_edge(&m, &ticks) == S5L_WAKE_NEVER,
              "%s named a future edge; if that is now true the tick pacing "
              "changed and this test is the record of it", src[i].name);
        CHECK(ticks == 0xffffffffu, "%s wrote *ticks while answering NEVER",
              src[i].name);
    }
    s5l8900_free(&m);
}

static void test_snapshot_carries_a_programmed_channel(void) {
    s5l8900_t src, dst;
    CHECK(s5l8900_init(&src, 0u, 1u << 16), "source init failed");
    CHECK(s5l8900_init(&dst, 0u, 1u << 16), "destination init failed");

    /*
     * Program a two-item chain and leave the CONTROLLER disabled, so the
     * transfer is genuinely pending across the checkpoint. This is the case
     * that cannot be defaulted: restoring these five registers as zero would
     * resume a guest waiting on a channel that no longer has anything queued.
     */
    src.bus.write32(src.bus.ctx, 0x400u, 0xcafebabeu);
    {
        uint32_t item[4] = { 0x400u, 0x500u, 0u, C_W32 | C_SI | C_DI | C_I | 1u };
        for (unsigned i = 0; i < 4u; i++)
            src.bus.write32(src.bus.ctx, 0x600u + 4u * i, item[i]);
    }
    src.bus.write32(src.bus.ctx, 0x408u, 0x0badf00du);
    src.bus.write32(src.bus.ctx, S5L8900_DMAC1_BASE + CH(5, R_SRC), 0x408u);
    src.bus.write32(src.bus.ctx, S5L8900_DMAC1_BASE + CH(5, R_DST), 0x508u);
    src.bus.write32(src.bus.ctx, S5L8900_DMAC1_BASE + CH(5, R_LLI), 0x600u);
    src.bus.write32(src.bus.ctx, S5L8900_DMAC1_BASE + CH(5, R_CTRL),
                    C_W32 | C_SI | C_DI | 1u);
    src.bus.write32(src.bus.ctx, S5L8900_DMAC1_BASE + CH(5, R_CFG),
                    B_ITC | B_EN);
    src.dmac[1].refused_softreq = 3u;     /* a refusal that must travel */
    s5l8900_tick(&src, 1u);
    CHECK(src.dmac[1].bytes_moved == 0u,
          "setup: the disabled controller ran the chain anyway");

    uint8_t *blob = NULL;
    size_t blob_len = 0;
    CHECK(snapshot_save_mem(&src, &blob, &blob_len) == SNAP_OK,
          "could not save");
    CHECK(snapshot_load_mem(&dst, blob, blob_len) == SNAP_OK,
          "could not restore");

    for (unsigned r = 0; r < 5u; r++) {
        uint32_t off = CH(5, r * 4u);
        CHECK(dst.bus.read32(dst.bus.ctx, S5L8900_DMAC1_BASE + off) ==
              src.bus.read32(src.bus.ctx, S5L8900_DMAC1_BASE + off),
              "channel 5 register %#x did not survive the checkpoint", off);
    }
    CHECK(dst.dmac[1].refused_softreq == 3u,
          "a refusal counter did not survive — a restored run would report a "
          "guest that had been told no as one that never asked");

    /* The restored machine finishes the transfer the source had queued. */
    dst.bus.write32(dst.bus.ctx, S5L8900_DMAC1_BASE + 0x030u, 1u);
    s5l8900_tick(&dst, 1u);
    CHECK(dst.bus.read32(dst.bus.ctx, 0x508u) == 0x0badf00du,
          "the restored machine did not run the first item: %#x",
          dst.bus.read32(dst.bus.ctx, 0x508u));
    CHECK(dst.bus.read32(dst.bus.ctx, 0x500u) == 0xcafebabeu,
          "the restored machine did not follow the linked list: %#x",
          dst.bus.read32(dst.bus.ctx, 0x500u));
    CHECK(dst.dmac[1].bytes_moved == 8u,
          "the restored transfer moved %llu bytes, expected 8",
          (unsigned long long)dst.dmac[1].bytes_moved);

    free(blob);
    s5l8900_free(&src);
    s5l8900_free(&dst);
}

static void test_snapshot_rejects_an_impossible_channel(void) {
    s5l8900_t src, dst;
    CHECK(s5l8900_init(&src, 0u, 1u << 16), "source init failed");
    CHECK(s5l8900_init(&dst, 0u, 1u << 16), "destination init failed");

    /* The Active bit is derived on read and never stored, so a file carrying it
     * was not written by this model. Reject rather than restore a channel whose
     * FIFO claims to hold data no transfer ever put there. */
    src.dmac[0].ch[1].cfg |= B_ACTIVE;

    uint8_t *blob = NULL;
    size_t blob_len = 0;
    CHECK(snapshot_save_mem(&src, &blob, &blob_len) == SNAP_OK,
          "could not save");
    if (blob)
        CHECK(snapshot_load_mem(&dst, blob, blob_len) == SNAP_ERR_CORRUPT,
              "a channel with the Active bit set was accepted");

    free(blob);
    s5l8900_free(&src);
    s5l8900_free(&dst);
}

int main(void) {
    printf("S5LBox PL080 DMA controller tests\n");
    test_the_register_map_is_the_drivers_own();
    test_reset_is_total();
    test_the_active_bit_is_read_only_and_reads_zero();
    test_a_memory_to_peripheral_transfer_moves_the_exact_bytes();
    test_a_peripheral_to_memory_transfer_runs_the_other_way();
    test_a_linked_list_is_followed_and_reloaded_from_memory();
    test_mismatched_widths_pack_through_the_channel();
    test_a_channel_disables_itself_at_the_end_of_the_chain();
    test_the_controller_enable_gates_every_channel();
    test_halt_holds_a_channel_without_disabling_it();
    test_the_completion_interrupt_is_the_one_the_filter_reads();
    test_refusals_are_counted_and_named();
    test_a_null_bus_moves_nothing();
    test_the_drivers_own_power_on_sequence();
    test_the_machine_decodes_both_windows();
    test_a_dma_store_lands_where_a_cpu_store_lands();
    test_a_completion_reaches_the_cpu();
    test_both_controllers_are_declared_wake_sources();
    test_snapshot_carries_a_programmed_channel();
    test_snapshot_rejects_an_impossible_channel();
    printf("  %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
