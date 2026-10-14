/*
 * S5LBox — S5L8900 machine: the system bus tying the CPU to RAM and devices.
 *
 * Every guest access lands here after MMU translation, and is routed by
 * physical address to either RAM or a peripheral window. Accesses outside the
 * map are counted rather than silently swallowed, so a misbehaving guest (or a
 * gap in our memory map) is visible instead of mysterious.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#include "soc.h"
#ifdef S5LBOX_WASM_JIT
#include "arm_wasm_jit.h"
#endif
#ifdef S5LBOX_ADAPTIVE_JIT
#include "arm_adaptive_dispatch.h"
#endif
#include <stdio.h>
#include "board.h"
#include <stdlib.h>
#include <string.h>


/* ------------------------------------------------------- the window map ---
 *
 * Every window this machine decodes, in one place. It exists so that "is
 * anything shadowed?" is a question with a single answer rather than a property
 * that has to be re-derived from the if-chain in bus_read() every time someone
 * changes the memory map — which is exactly how the NOR came to be unreachable
 * at -R 512 without anyone noticing.
 *
 * NOR's size here is the constant rather than m->nor.size. They are always
 * equal (s5l8900_init passes the constant), and using the constant is what lets
 * s5l8900_ram_conflict() answer before a machine exists.
 */
static const s5l_window_t DEVICE_WINDOWS[] = {
    { S5L8900_NOR_BASE,   S5L8900_NOR_SIZE,   "nor"   },
    { S5L8900_CLCD_BASE,  S5L8900_DEV_SIZE,   "clcd"  },
    { S5L8900_TVOUT_CTRL_BASE,  S5L_TVOUT_BANK_SIZE, "tvout-control" },
    { S5L8900_TVOUT_MIXER_BASE, S5L_TVOUT_BANK_SIZE, "tvout-mixer"   },
    { S5L8900_TVOUT_SDO_BASE,   S5L_TVOUT_BANK_SIZE, "tvout-sdo"     },
    { S5L8900_MBX_BASE,   S5L_MBX_APERTURE,   "mbx"   },
    { S5L8900_I2C0_BASE,  S5L8900_DEV_SIZE,   "i2c0"  },
    { S5L8900_I2C1_BASE,  S5L8900_DEV_SIZE,   "i2c1"  },
    /*
     * The two I2S windows. Decoded rather than stubbed for the same reason
     * uart4 is: run62's census shows zero traffic on either page, but "no
     * traffic yet" is a statement about how far the boot got, not about the
     * window. The codec's identify() fails before any transfer is configured,
     * so the first writes to these pages are downstream of the WM8991 model
     * that lands with them — which is precisely why the two are one change.
     */
    { S5L8900_I2S0_BASE,  S5L8900_DEV_SIZE,   "i2s0"  },
    { S5L8900_I2S1_BASE,  S5L8900_DEV_SIZE,   "i2s1"  },
    { S5L8900_SPI0_BASE,  S5L8900_DEV_SIZE,   "spi0"  },
    { S5L8900_SPI1_BASE,  S5L8900_DEV_SIZE,   "spi1"  },
    { S5L8900_SPI2_BASE,  S5L8900_DEV_SIZE,   "spi2"  },
    { S5L8900_USB_OTG_BASE, S5L8900_DEV_SIZE, "usb-otg" },
    { S5L8900_VIC0_BASE,  S5L8900_DEV_SIZE,   "vic0"  },
    { S5L8900_VIC1_BASE,  S5L8900_DEV_SIZE,   "vic1"  },
    { S5L8900_POWER_BASE, S5L8900_POWER_SIZE, "power" },
    /* The rest of the power page. Two blocks, two drivers, one page: see the
     * GPIOIC section of soc.h for why the register offsets this window carries
     * are stated relative to the page base rather than to this base. */
    { S5L8900_GPIOIC_BASE, S5L8900_GPIOIC_SIZE, "gpioic" },
    { S5L8900_GPIO_BASE,  S5L8900_DEV_SIZE,   "gpio"  },
    { S5L8900_UART0_BASE, S5L8900_DEV_SIZE,   "uart0" },
    /*
     * uart4, the PPP line. Decoded unconditionally rather than behind the
     * harness toggle that provisions the guest job, and that is deliberate:
     * the toggle decides whether anything TALKS to this port, never whether
     * the port answers correctly. A window that appears and disappears with a
     * command-line flag would make every recorded MMIO census conditional on
     * it, and the run that has the flag off is exactly the run that proves the
     * port is quiet.
     *
     * Before this entry existed the page was not merely unmodelled, it was
     * undeclared: run59's census recorded `0x3cc10000 r=8 w=15` falling
     * through to the unmapped path, so every UTRSTAT read answered 0 — i.e.
     * "transmitter busy" — and any driver that waited for room before writing
     * would have waited forever. Decoding the window is what makes the
     * transmit path terminate at all.
     */
    { S5L8900_UART4_BASE, S5L8900_DEV_SIZE,   "uart4" },
    { S5L8900_TIMER_BASE, S5L8900_TIMER_SIZE, "timer" },
    /*
     * The two PL080 DMA controllers. Neither page was decoded or even declared
     * before, so AppleARMPL080DMAC::start mapped it, printed a base address,
     * initialised ten channels into it and had every one of those writes
     * discarded — including the two the device tree points at the I2S FIFOs.
     * See the PL080 block in soc.h for the whole derivation.
     */
    { S5L8900_DMAC0_BASE, S5L8900_DEV_SIZE,   "dmac0" },
    { S5L8900_DMAC1_BASE, S5L8900_DEV_SIZE,   "dmac1" },
};
#define NDEVICE_WINDOWS (sizeof DEVICE_WINDOWS / sizeof DEVICE_WINDOWS[0])

/*
 * Physical regions the SoC has, from the shipped device tree. Modelled or not:
 * the point of listing the unmodelled ones is that an oversized DRAM window
 * covering them is a stated property of the configuration, not folklore. See
 * the memory-map block at the top of soc.h for the derivation of each.
 */
static const s5l_window_t SOC_REGIONS[] = {
    { S5L8900_SDRAM_BASE, 0x08000000u,        "dram (128 MB fitted)" },
    { S5L8900_EDRAM_BASE, S5L8900_EDRAM_SIZE, "edram"                },
    { S5L8900_VROM_BASE,  S5L8900_VROM_SIZE,  "vrom"                 },
    { S5L8900_SRAM_BASE,  S5L8900_SRAM_SIZE,  "sram/amc"             },
    { S5L8900_ARMIO_BASE, S5L8900_ARMIO_SIZE, "arm-io"               },
};

bool s5l8900_overlaps(uint32_t a, uint32_t alen, uint32_t b, uint32_t blen) {
    /* 64-bit throughout: a window near the top of the space must not wrap its
     * end below its base and read as disjoint from everything. */
    uint64_t a0 = a, a1 = a0 + alen;
    uint64_t b0 = b, b1 = b0 + blen;
    if (!alen || !blen) return false;
    return b0 < a1 && a0 < b1;
}

unsigned s5l8900_soc_regions(const s5l_window_t **out) {
    if (out) *out = SOC_REGIONS;
    return (unsigned)(sizeof SOC_REGIONS / sizeof SOC_REGIONS[0]);
}

const s5l_window_t *s5l8900_ram_conflict(uint32_t ram_base, uint32_t ram_size) {
    for (unsigned i = 0; i < NDEVICE_WINDOWS; i++)
        if (s5l8900_overlaps(ram_base, ram_size,
                             DEVICE_WINDOWS[i].base, DEVICE_WINDOWS[i].size))
            return &DEVICE_WINDOWS[i];
    return NULL;
}

unsigned s5l8900_windows(const s5l8900_t *m, s5l_window_t *out, unsigned max) {
    unsigned n = 0;
    for (unsigned i = 0; i < NDEVICE_WINDOWS; i++, n++)
        if (out && n < max) out[n] = DEVICE_WINDOWS[i];
    for (unsigned i = 0; i < m->stub_count; i++, n++)
        if (out && n < max) {
            out[n].base = m->stubs[i].base;
            out[n].size = m->stubs[i].size;
            out[n].name = m->stubs[i].name;
        }
    return n;
}

/*
 * Bounds checks are done in 64-bit deliberately. The guest controls every
 * address, so a 32-bit "(a - base) + len <= size" can be made to wrap: an
 * access at 0xFFFFFFFE with len 4 sums to 2, passes the test, and then indexes
 * ram[0xFFFFFFFE]. Widening makes that impossible.
 */
static inline bool in_ram(const s5l8900_t *m, uint32_t a, uint32_t len) {
    if (a < m->ram_base) return false;
    return (uint64_t)(a - m->ram_base) + (uint64_t)len <= (uint64_t)m->ram_size;
}
static inline bool in_window(uint32_t a, unsigned bytes,
                             uint32_t base, uint32_t size) {
    if (!bytes || a < base) return false;
    return (uint64_t)(a - base) + bytes <= (uint64_t)size;
}
static inline bool in_dev(uint32_t a, unsigned bytes, uint32_t base) {
    return in_window(a, bytes, base, S5L8900_DEV_SIZE);
}
static inline bool mmio_word(uint32_t a, unsigned bytes,
                             uint32_t base, uint32_t size) {
    return bytes == 4u && (a & 3u) == 0u && in_window(a, bytes, base, size);
}
/*
 * THE DATA PORTS: SPI and I2S, which are DMA destinations and therefore see
 * accesses NARROWER THAN A WORD.
 *
 * mmio_word() demands bytes == 4, and for a control register that is right --
 * the CPU only ever reads and writes those a word at a time, and admitting a
 * narrow access would hide a guest doing something surprising. But a DMA
 * destination is not a control register. /arm-io/spi1's own dma-channels
 * template is 0x00089000: four-byte source, ONE-BYTE destination. So the DMAC
 * issues write8, mmio_word() rejects every one of them, and they fall past
 * every device to m->unmapped_writes.
 *
 * That was not theoretical. run114 measured the Z2 firmware download landing
 * there in full: channel 5, destination 0x3ce00010 which is SPI1's TXDATA,
 * `runs 210 bytes 812340` -- against `spi1 words 176, tx-drops 0`, and
 * `unmapped writes 813135` with 0x3ce00000 named in the outside-the-map list.
 * The controller did its work, the port never heard a byte of it, and the
 * digitizer has been echoing its idle pattern ever since. Every explanation
 * this project wrote for that -- and there were several -- looked at the guest.
 *
 * Natural alignment rather than word alignment, because that is what the width
 * means. An offset this device does not decode still reaches its own switch and
 * is counted there, so a surprising access is visible rather than dropped.
 */
static inline bool mmio_data(uint32_t a, unsigned bytes,
                             uint32_t base, uint32_t size) {
    return (bytes == 1u || bytes == 2u || bytes == 4u) &&
           (a & (bytes - 1u)) == 0u && in_window(a, bytes, base, size);
}
/* ------------------------------------------------ I2S transmit streams ---
 * See s5l_i2s_stream_t in soc.h for what is measured and what is assumed. */
static const unsigned I2S_LRCK_LINE[S5L8900_I2S_COUNT] = {
    S5L_GPIOIC_LINE_I2S0_LRCK, S5L_GPIOIC_LINE_I2S1_LRCK
};

static uint64_t audio_tb_hz(const s5l8900_t *m) {
    return m->tb_hz ? m->tb_hz : S5L8900_TB_HZ;
}

static void audio_ring_push(s5l8900_t *m, uint8_t b) {
    if (!m->audio_ring) { m->audio_dropped++; return; }
    if (m->audio_fill == S5L_AUDIO_RING_BYTES) {
        /* A host that stops reading loses the oldest audio, not the newest. */
        m->audio_rd = (m->audio_rd + 1u) % S5L_AUDIO_RING_BYTES;
        m->audio_fill--;
        m->audio_dropped++;
    }
    m->audio_ring[(m->audio_rd + m->audio_fill) % S5L_AUDIO_RING_BYTES] = b;
    m->audio_fill++;
}

/* The shifter takes what its credit allows, oldest byte first. */
static void i2s_tx_drain(s5l8900_t *m, unsigned i) {
    s5l_i2s_stream_t *s = &m->i2s_tx[i];
    while (s->credit && s->fifo_level) {
        audio_ring_push(m, s->fifo[s->fifo_head]);
        s->fifo_head = (s->fifo_head + 1u) % S5L_I2S_TX_FIFO_BYTES;
        s->fifo_level--;
        s->credit--;
        s->tx_bytes++;
    }
}

static void i2s_tx_store(s5l8900_t *m, unsigned i, uint32_t val,
                         unsigned bytes) {
    s5l_i2s_stream_t *s = &m->i2s_tx[i];
    s->tx_stores++;
    for (unsigned k = 0; k < bytes; k++) {
        if (s->fifo_level == S5L_I2S_TX_FIFO_BYTES) {
            s->overflow_bytes++;
            continue;
        }
        s->fifo[(s->fifo_head + s->fifo_level) % S5L_I2S_TX_FIFO_BYTES] =
            (uint8_t)(val >> (8u * k));
        s->fifo_level++;
    }
    i2s_tx_drain(m, i);
}

/* Before the DMA controllers: turn elapsed guest time into shifter credit. */
static void i2s_tx_advance(s5l8900_t *m, uint32_t tb) {
    uint64_t hz = audio_tb_hz(m);
    for (unsigned i = 0; i < S5L8900_I2S_COUNT; i++) {
        s5l_i2s_stream_t *s = &m->i2s_tx[i];
        if (!s5l_i2s_tx_running(&m->i2s[i])) {
            s->credit = 0u;
            s->drain_phase = 0u;
            continue;
        }
        s->drain_phase += (uint64_t)tb * S5L_I2S_FRAME_HZ * S5L_I2S_FRAME_BYTES;
        uint64_t add = s->drain_phase / hz;
        s->drain_phase %= hz;
        uint64_t c = (uint64_t)s->credit + add;
        s->credit = c > UINT32_MAX ? UINT32_MAX : (uint32_t)c;
        i2s_tx_drain(m, i);
    }
}

/* After them: credit the FIFO could not satisfy is an underrun and is gone,
 * so a late refill cannot replay time that has already passed. */
static void i2s_tx_settle(s5l8900_t *m) {
    for (unsigned i = 0; i < S5L8900_I2S_COUNT; i++) {
        s5l_i2s_stream_t *s = &m->i2s_tx[i];
        if (s->credit && !s->fifo_level) {
            s->underruns++;
            s->credit = 0u;
        }
    }
}

/* The external frame clock, a square wave at S5L_I2S_FRAME_HZ while the port
 * is configured to synchronise to it. A refresh spanning two or more
 * half-periods still presents one rising edge to the edge latch. */
static void i2s_lrck_drive(s5l8900_t *m, uint32_t tb) {
    uint64_t hz = audio_tb_hz(m);
    for (unsigned i = 0; i < S5L8900_I2S_COUNT; i++) {
        s5l_i2s_stream_t *s = &m->i2s_tx[i];
        unsigned line = I2S_LRCK_LINE[i];
        if (!s5l_i2s_frame_sync(&m->i2s[i])) {
            if (s->lrck_driven && s->lrck) {
                s->lrck = false;
                s5l_gpioic_set_line(&m->gpioic, line, false);
            }
            s->lrck_phase = 0u;
            continue;
        }
        s->lrck_phase += (uint64_t)tb * 2u * S5L_I2S_FRAME_HZ;
        uint64_t toggles = s->lrck_phase / hz;
        s->lrck_phase %= hz;
        if (!s->lrck_driven) {
            s->lrck_driven = true;
            s5l_gpioic_set_line(&m->gpioic, line, s->lrck);
        }
        if (!toggles) continue;
        bool end = (toggles & 1u) ? !s->lrck : s->lrck;
        if (toggles >= 2u || !s->lrck) {
            if (s->lrck) s5l_gpioic_set_line(&m->gpioic, line, false);
            s5l_gpioic_set_line(&m->gpioic, line, true);
            s->lrck_rising++;
        }
        s5l_gpioic_set_line(&m->gpioic, line, end);
        s->lrck = end;
    }
}

static bool i2s_tx_fifo_addr(uint32_t addr, unsigned *index) {
    for (unsigned i = 0; i < S5L8900_I2S_COUNT; i++) {
        if (addr == s5l_i2s_fifo_pa(i, S5L_I2S_FIFO_TX)) {
            if (index) *index = i;
            return true;
        }
    }
    return false;
}

size_t s5l8900_audio_available(const s5l8900_t *m) {
    return m ? m->audio_fill / S5L_I2S_FRAME_BYTES : 0u;
}

size_t s5l8900_audio_read(s5l8900_t *m, int16_t *out, size_t max_frames) {
    if (!m || !out || !m->audio_ring) return 0u;
    size_t n = m->audio_fill / S5L_I2S_FRAME_BYTES;
    if (n > max_frames) n = max_frames;
    for (size_t f = 0; f < n * 2u; f++) {
        uint8_t lo = m->audio_ring[m->audio_rd];
        uint8_t hi = m->audio_ring[(m->audio_rd + 1u) % S5L_AUDIO_RING_BYTES];
        m->audio_rd = (m->audio_rd + 2u) % S5L_AUDIO_RING_BYTES;
        out[f] = (int16_t)(uint16_t)(lo | (uint16_t)hi << 8);
    }
    m->audio_fill -= (uint32_t)(n * S5L_I2S_FRAME_BYTES);
    return n;
}

unsigned s5l8900_audio_rate(const s5l8900_t *m) {
    (void)m;
    return S5L_I2S_FRAME_HZ;
}

/* The timer is the one peripheral that does not fit the uniform 4 KB window:
 * its interrupt-status alias sits at offset 0x10000. */
static inline bool in_power(uint32_t a, unsigned bytes) {
    return mmio_word(a, bytes, S5L8900_POWER_BASE, S5L8900_POWER_SIZE);
}
static inline bool in_mbx(uint32_t a, unsigned bytes) {
    return mmio_word(a, bytes, S5L8900_MBX_BASE, S5L_MBX_APERTURE);
}
static inline bool in_gpioic(uint32_t a, unsigned bytes) {
    return mmio_word(a, bytes, S5L8900_GPIOIC_BASE, S5L8900_GPIOIC_SIZE);
}
static inline bool in_timer(uint32_t a, unsigned bytes) {
    return mmio_word(a, bytes, S5L8900_TIMER_BASE, S5L8900_TIMER_SIZE);
}

/* ------------------------------------------------------- stub windows --- */

bool s5l8900_add_stub(s5l8900_t *m, uint32_t base, uint32_t size,
                      const char *name) {
    if (!m || m->stub_count >= S5L_STUB_MAX || !size) return false;
    /* A window that extends beyond the 32-bit physical address space is only
     * partially reachable and used to make the rounded register count wrap. */
    if ((uint64_t)base + size > 0x100000000ull) return false;
    /*
     * Refuse to shadow, or be shadowed by, anything already on the bus: a
     * modelled device, another stub, or RAM. A stub that quietly overlays a
     * real device model would be far harder to notice than a rejected call —
     * and a stub inside the RAM aperture would be unreachable, because RAM is
     * on the fast path (see the routing contract in soc.h).
     */
    for (unsigned i = 0; i < NDEVICE_WINDOWS; i++)
        if (s5l8900_overlaps(base, size,
                             DEVICE_WINDOWS[i].base, DEVICE_WINDOWS[i].size))
            return false;
    for (unsigned i = 0; i < m->stub_count; i++)
        if (s5l8900_overlaps(base, size, m->stubs[i].base, m->stubs[i].size))
            return false;
    if (s5l8900_overlaps(base, size, m->ram_base, m->ram_size)) return false;
    uint64_t nregs64 = ((uint64_t)size + 3u) / 4u;
    if (nregs64 > 0xffffffffu ||
        nregs64 > (uint64_t)SIZE_MAX / sizeof(uint32_t)) return false;
    uint32_t nregs = (uint32_t)nregs64;
    uint32_t *regs = calloc((size_t)nregs, sizeof *regs);
    if (!regs) return false;

    s5l_stub_t *s = &m->stubs[m->stub_count++];
    memset(s, 0, sizeof *s);
    s->base = base; s->size = size; s->name = name;
    s->regs = regs; s->nregs = nregs;
    return true;
}

static s5l_stub_t *find_stub(s5l8900_t *m, uint32_t a, unsigned bytes) {
    for (unsigned i = 0; i < m->stub_count; i++)
        if (in_window(a, bytes, m->stubs[i].base, m->stubs[i].size))
            return &m->stubs[i];
    return NULL;
}

/* Stub windows are honest byte-addressable storage. Assemble accesses a byte
 * at a time so 8/16-bit and unaligned transactions update the correct lanes
 * without ever indexing beyond the rounded backing-register array. */
static uint32_t stub_read(const s5l_stub_t *s, uint32_t addr, unsigned bytes) {
    uint32_t v = 0;
    uint32_t rel = addr - s->base;
    for (unsigned i = 0; i < bytes; i++) {
        uint32_t at = rel + i;
        uint32_t byte = (s->regs[at >> 2] >> ((at & 3u) * 8u)) & 0xffu;
        v |= byte << (i * 8u);
    }
    return v;
}

static void stub_write(s5l_stub_t *s, uint32_t addr,
                       uint32_t val, unsigned bytes) {
    uint32_t rel = addr - s->base;
    for (unsigned i = 0; i < bytes; i++) {
        uint32_t at = rel + i;
        uint32_t shift = (at & 3u) * 8u;
        uint32_t *reg = &s->regs[at >> 2];
        *reg = (*reg & ~(0xffu << shift)) |
               (((val >> (i * 8u)) & 0xffu) << shift);
    }
}

/* ------------------------------------------------------------- reads --- */

/* Record the first distinct out-of-map addresses so a wandering guest tells us
 * which peripheral is missing rather than leaving us to guess. */
static void note_unmapped(s5l8900_t *m, uint32_t addr) {
    uint32_t page = addr & ~0xfffu;
    for (unsigned i = 0; i < m->unmapped_addr_count; i++)
        if (m->unmapped_addr[i] == page) return;
    if (m->unmapped_addr_count < S5L_UNMAPPED_LOG)
        m->unmapped_addr[m->unmapped_addr_count++] = page;
}

static void note_device(s5l8900_t *m, uint32_t addr, uint32_t val, bool is_write) {
    if (m->mmio_watch_page && (addr & ~0xfffu) == m->mmio_watch_page)
        fprintf(stderr, "[mmio] %llu %c 0x%08x = 0x%08x pc=0x%08x ttbr0=0x%08x\n",
                (unsigned long long)m->cpu.cycles, is_write ? 'W' : 'R',
                addr, val, m->cpu.r[15], m->cpu.cp15.ttbr0);
    if (!m->trace_devices || m->dev_count >= S5L_DEVLOG) return;
    m->dev_addr[m->dev_count]     = addr;
    m->dev_value[m->dev_count]    = val;
    m->dev_is_write[m->dev_count] = is_write;
    m->dev_count++;
}

/*
 * RAM is tested first, and that is safe ONLY because s5l8900_init() has already
 * refused to build a machine whose RAM aperture overlaps a device window (and
 * s5l8900_add_stub() refuses a window inside RAM). Under that invariant "RAM
 * first" and "device first" route identically, so the cheap test can go first.
 *
 * Do not relax the invariant and leave this ordering. It used to be the case
 * that RAM won by accident rather than by proof, and at -R 512 the DRAM window
 * reached 0x28000000 and swallowed the NOR: every NOR read returned RAM-disk
 * bytes, no fault, no log, nothing to find.
 */
/*
 * A host pointer for a physical range, but ONLY when it is plain DRAM.
 *
 * The caller uses this to turn a translation into a pointer once and reuse it,
 * skipping the indirect bus call, the range check and the variable-size memcpy
 * that a fetch otherwise pays on every instruction. Returning NULL is always
 * correct and simply keeps it on the slow path.
 *
 * in_ram() is the same predicate bus_read uses, and it is deliberately reused
 * rather than restated: it already widens to 64 bits so a length that would
 * wrap a 32-bit sum cannot pass. Anything that is not DRAM -- MMIO, NOR, a
 * stub window, or a range running off the end -- gets NULL, which is what
 * keeps device semantics on the path that models them.
 */
static uint8_t *machine_host_ram(void *ctx, uint32_t pa, uint32_t len) {
    s5l8900_t *m = ctx;
    if (!m || !m->ram || !len) return NULL;
    if (!in_ram(m, pa, len)) return NULL;
    return m->ram + (pa - m->ram_base);
}

static uint32_t bus_read(void *ctx, uint32_t addr, unsigned bytes) {
    s5l8900_t *m = ctx;

    if (in_ram(m, addr, bytes)) {
        uint32_t v = 0;
        memcpy(&v, &m->ram[addr - m->ram_base], bytes);   /* little-endian host */
        return v;
    }
    /*
     * Past the RAM aperture is a device, and this is the ONE place every guest
     * device access passes through — which is what makes `level_dirty` a
     * complete account of guest-caused level changes rather than a list of
     * windows somebody has to remember to extend. Set on reads as well as
     * writes: draining URXH, popping the SPI receive FIFO and reading
     * VICADDRESS all change what a line asserts, and nine of the fifteen read
     * entry points take a non-const device pointer. Distinguishing the pure
     * ones would buy a store per MMIO read and cost the guarantee.
     */
    m->level_dirty = true;
    uint32_t v;
    if ((bytes == 1u || bytes == 2u || bytes == 4u) && (addr & 3u) == 0u &&
        in_dev(addr, bytes, S5L8900_UART0_BASE)) {
        v = s5l_uart_read(&m->uart0, addr - S5L8900_UART0_BASE);
    } else if ((bytes == 1u || bytes == 2u || bytes == 4u) && (addr & 3u) == 0u &&
               in_dev(addr, bytes, S5L8900_UART4_BASE)) {
        /* Same access widths as uart0's, for the same reason: the console
         * driver reaches this register file with byte, halfword and word
         * loads, and the two ports run the same driver. */
        v = s5l_uart_read(&m->uart4, addr - S5L8900_UART4_BASE);
    } else if (mmio_word(addr, bytes, S5L8900_VIC0_BASE, S5L8900_DEV_SIZE)) {
        uint32_t off = addr - S5L8900_VIC0_BASE;
        /* VIC0's VICADDRESS surfaces its own sources first, then daisy-chains
         * to VIC1 (global sources 32-63) — the standard PL192 cascade, and how
         * the driver reads a single global 0-63 source number from VIC0. */
        if (off == VIC_VECTADDR) {
            v = s5l_vic_vectaddr(&m->vic[0], 0);
            if (!v) v = s5l_vic_vectaddr(&m->vic[1], 32);
        } else {
            v = s5l_vic_read(&m->vic[0], off);
        }
    } else if (mmio_word(addr, bytes, S5L8900_VIC1_BASE, S5L8900_DEV_SIZE)) {
        uint32_t off = addr - S5L8900_VIC1_BASE;
        if (off == VIC_VECTADDR) v = s5l_vic_vectaddr(&m->vic[1], 32);
        else                     v = s5l_vic_read(&m->vic[1], off);
    } else if (mmio_word(addr, bytes, S5L8900_CLCD_BASE, S5L8900_DEV_SIZE)) {
        v = s5l_clcd_read(&m->clcd, addr - S5L8900_CLCD_BASE);
    } else if ((bytes == 1u || bytes == 2u || bytes == 4u) &&
               in_window(addr, bytes, S5L8900_TVOUT_CTRL_BASE,
                         S5L_TVOUT_BANK_SIZE)) {
        v = s5l_tvout_read(&m->tvout, S5L_TVOUT_BANK_CTRL,
                           addr - S5L8900_TVOUT_CTRL_BASE, bytes);
    } else if ((bytes == 1u || bytes == 2u || bytes == 4u) &&
               in_window(addr, bytes, S5L8900_TVOUT_MIXER_BASE,
                         S5L_TVOUT_BANK_SIZE)) {
        v = s5l_tvout_read(&m->tvout, S5L_TVOUT_BANK_MIXER,
                           addr - S5L8900_TVOUT_MIXER_BASE, bytes);
    } else if ((bytes == 1u || bytes == 2u || bytes == 4u) &&
               in_window(addr, bytes, S5L8900_TVOUT_SDO_BASE,
                         S5L_TVOUT_BANK_SIZE)) {
        v = s5l_tvout_read(&m->tvout, S5L_TVOUT_BANK_SDO,
                           addr - S5L8900_TVOUT_SDO_BASE, bytes);
    } else if (in_timer(addr, bytes)) {
        v = s5l_timer_read(&m->timer, addr - S5L8900_TIMER_BASE);
    } else if (in_power(addr, bytes)) {
        v = s5l_power_read(&m->power, addr - S5L8900_POWER_BASE);
    } else if (in_mbx(addr, bytes)) {
        v = s5l_mbx_read(&m->mbx, addr - S5L8900_MBX_BASE);
    } else if (in_gpioic(addr, bytes)) {
        /* Offset from the PAGE, not from the window: the driver's own
         * 0x80/0xA0/0xC0/0xE0 are page-relative and rebasing them onto the
         * window would silently shift the whole map by 0x80. */
        v = s5l_gpioic_read(&m->gpioic, addr - S5L8900_GPIOIC_PAGE);
    } else if ((bytes == 1u || bytes == 2u || bytes == 4u) &&
               in_dev(addr, bytes, S5L8900_GPIO_BASE)) {
        v = s5l_gpio_read(&m->gpio, addr - S5L8900_GPIO_BASE, bytes);
    } else if (mmio_word(addr, bytes, S5L8900_I2C0_BASE, S5L8900_DEV_SIZE)) {
        v = s5l_i2c_read(&m->i2c[0], addr - S5L8900_I2C0_BASE);
    } else if (mmio_word(addr, bytes, S5L8900_I2C1_BASE, S5L8900_DEV_SIZE)) {
        v = s5l_i2c_read(&m->i2c[1], addr - S5L8900_I2C1_BASE);
    } else if (mmio_data(addr, bytes, S5L8900_I2S0_BASE, S5L8900_DEV_SIZE)) {
        /* Word accesses only, as for the I2C controllers: the stock accessors
         * are a bare `ldr`/`str` of a 32-bit word at a byte offset. */
        v = s5l_i2s_read(&m->i2s[0], addr - S5L8900_I2S0_BASE);
    } else if (mmio_data(addr, bytes, S5L8900_I2S1_BASE, S5L8900_DEV_SIZE)) {
        v = s5l_i2s_read(&m->i2s[1], addr - S5L8900_I2S1_BASE);
    } else if (mmio_data(addr, bytes, S5L8900_SPI0_BASE, S5L8900_DEV_SIZE)) {
        v = s5l_spi_read(&m->spi[0], addr - S5L8900_SPI0_BASE);
    } else if (mmio_data(addr, bytes, S5L8900_SPI1_BASE, S5L8900_DEV_SIZE)) {
        v = s5l_spi_read(&m->spi[1], addr - S5L8900_SPI1_BASE);
    } else if (mmio_data(addr, bytes, S5L8900_SPI2_BASE, S5L8900_DEV_SIZE)) {
        v = s5l_spi_read(&m->spi[2], addr - S5L8900_SPI2_BASE);
    } else if (mmio_word(addr, bytes, S5L8900_USB_OTG_BASE, S5L8900_DEV_SIZE)) {
        /* Word accesses only, as for the VICs, the CLCD and the I2C
         * controllers. The driver uses 32-bit accessors throughout; a narrower
         * or unaligned access to this page stays unmapped-and-counted rather
         * than being answered with a fabricated lane. */
        v = s5l_usbotg_read(&m->usbotg, addr - S5L8900_USB_OTG_BASE);
    } else if (mmio_word(addr, bytes, S5L8900_DMAC0_BASE, S5L8900_DEV_SIZE)) {
        /* Word accesses only: AppleARMPL080DMAC reaches this file through one
         * `ldr r0,[off, base]` at 0xc070ecd4 and one `str r2,[off, base]` at
         * 0xc070ed08, both 32-bit. */
        v = s5l_pl080_read(&m->dmac[0], addr - S5L8900_DMAC0_BASE);
    } else if (mmio_word(addr, bytes, S5L8900_DMAC1_BASE, S5L8900_DEV_SIZE)) {
        v = s5l_pl080_read(&m->dmac[1], addr - S5L8900_DMAC1_BASE);
    } else if ((bytes == 1u || bytes == 2u || bytes == 4u) &&
               in_window(addr, bytes, m->nor_base, m->nor.size)) {
        v = s5l_nor_read(&m->nor, addr - m->nor_base, bytes);
    } else {
        s5l_stub_t *s = find_stub(m, addr, bytes);
        if (!s) {
            m->unmapped_reads++;
            note_unmapped(m, addr);
            note_device(m, addr, 0, false);
            return 0;
        }
        s->reads++;
        v = stub_read(s, addr, bytes);
    }
    note_device(m, addr, v, false);
    return v;
}

static void bus_write(void *ctx, uint32_t addr, uint32_t val, unsigned bytes) {
    s5l8900_t *m = ctx;

    if (in_ram(m, addr, bytes)) {
        memcpy(&m->ram[addr - m->ram_base], &val, bytes);
        if (m->bus.ram_written)
            m->bus.ram_written(m->bus.ctx, &m->ram[addr - m->ram_base], bytes);
        return;
    }
    m->level_dirty = true;      /* a device store; see bus_read() */
    if ((bytes == 1u || bytes == 2u || bytes == 4u) && (addr & 3u) == 0u &&
        in_dev(addr, bytes, S5L8900_UART0_BASE)) {
        if (addr - S5L8900_UART0_BASE == UART_UTXH && m->uart0_host_tx)
            m->uart0_host_tx(m->uart0_host_ctx, (uint8_t)val);
        note_device(m, addr, val, true);
        s5l_uart_write(&m->uart0, addr - S5L8900_UART0_BASE, val);
        return;
    }
    if ((bytes == 1u || bytes == 2u || bytes == 4u) && (addr & 3u) == 0u &&
        in_dev(addr, bytes, S5L8900_UART4_BASE)) {
        note_device(m, addr, val, true);
        uint32_t off = addr - S5L8900_UART4_BASE;
        s5l_uart_write(&m->uart4, off, val);
        if (off == UART_UTXH && m->uart4_host_tx)
            m->uart4_host_tx(m->uart4_host_ctx, (uint8_t)val);
        return;
    }
    if (mmio_word(addr, bytes, S5L8900_VIC0_BASE, S5L8900_DEV_SIZE)) {
        s5l_vic_write(&m->vic[0], addr - S5L8900_VIC0_BASE, val);
        return;
    }
    if (mmio_word(addr, bytes, S5L8900_VIC1_BASE, S5L8900_DEV_SIZE)) {
        s5l_vic_write(&m->vic[1], addr - S5L8900_VIC1_BASE, val);
        return;
    }
    if (mmio_word(addr, bytes, S5L8900_CLCD_BASE, S5L8900_DEV_SIZE)) {
        note_device(m, addr, val, true);
        s5l_clcd_write(&m->clcd, addr - S5L8900_CLCD_BASE, val);
        return;
    }
    if ((bytes == 1u || bytes == 2u || bytes == 4u) &&
        in_window(addr, bytes, S5L8900_TVOUT_CTRL_BASE,
                  S5L_TVOUT_BANK_SIZE)) {
        note_device(m, addr, val, true);
        s5l_tvout_write(&m->tvout, S5L_TVOUT_BANK_CTRL,
                        addr - S5L8900_TVOUT_CTRL_BASE, val, bytes);
        return;
    }
    if ((bytes == 1u || bytes == 2u || bytes == 4u) &&
        in_window(addr, bytes, S5L8900_TVOUT_MIXER_BASE,
                  S5L_TVOUT_BANK_SIZE)) {
        note_device(m, addr, val, true);
        s5l_tvout_write(&m->tvout, S5L_TVOUT_BANK_MIXER,
                        addr - S5L8900_TVOUT_MIXER_BASE, val, bytes);
        return;
    }
    if ((bytes == 1u || bytes == 2u || bytes == 4u) &&
        in_window(addr, bytes, S5L8900_TVOUT_SDO_BASE,
                  S5L_TVOUT_BANK_SIZE)) {
        note_device(m, addr, val, true);
        s5l_tvout_write(&m->tvout, S5L_TVOUT_BANK_SDO,
                        addr - S5L8900_TVOUT_SDO_BASE, val, bytes);
        return;
    }
    if (in_timer(addr, bytes)) {
        note_device(m, addr, val, true);
        s5l_timer_write(&m->timer, addr - S5L8900_TIMER_BASE, val);
        return;
    }
    if (in_power(addr, bytes)) {
        note_device(m, addr, val, true);
        s5l_power_write(&m->power, addr - S5L8900_POWER_BASE, val);
        return;
    }
    if (in_mbx(addr, bytes)) {
        note_device(m, addr, val, true);
        uint32_t off = addr - S5L8900_MBX_BASE;
        (void)s5l_mbx_stage_ta_write(&m->mbx, &m->bus, off, val);
        s5l_mbx_write(&m->mbx, off, val);
        /* The MBX owns its register/EDRAM aperture; the machine owns DRAM and
         * the observer-aware bus. Keep that ownership boundary explicit. */
        (void)s5l_mbx_process_2d(&m->mbx, &m->bus, off, val,
                                 &m->mbx_telemetry);
        (void)s5l_mbx_process_3d(&m->mbx, &m->bus, off, val,
                                 &m->mbx_telemetry);
        return;
    }
    if (in_gpioic(addr, bytes)) {
        note_device(m, addr, val, true);
        s5l_gpioic_write(&m->gpioic, addr - S5L8900_GPIOIC_PAGE, val);
        return;
    }
    if ((bytes == 1u || bytes == 2u || bytes == 4u) &&
        in_dev(addr, bytes, S5L8900_GPIO_BASE)) {
        note_device(m, addr, val, true);
        s5l_gpio_write(&m->gpio, addr - S5L8900_GPIO_BASE, val, bytes);
        return;
    }
    if (mmio_word(addr, bytes, S5L8900_I2C0_BASE, S5L8900_DEV_SIZE)) {
        note_device(m, addr, val, true);
        s5l_i2c_write(&m->i2c[0], addr - S5L8900_I2C0_BASE, val);
        return;
    }
    if (mmio_word(addr, bytes, S5L8900_I2C1_BASE, S5L8900_DEV_SIZE)) {
        note_device(m, addr, val, true);
        s5l_i2c_write(&m->i2c[1], addr - S5L8900_I2C1_BASE, val);
        return;
    }
    if (mmio_data(addr, bytes, S5L8900_I2S0_BASE, S5L8900_DEV_SIZE)) {
        note_device(m, addr, val, true);
        s5l_i2s_write(&m->i2s[0], addr - S5L8900_I2S0_BASE, val);
        if (addr - S5L8900_I2S0_BASE == S5L_I2S_TX_FIFO_OFF &&
            s5l_i2s_configured(&m->i2s[0]))
            i2s_tx_store(m, 0u, val, bytes);
        return;
    }
    if (mmio_data(addr, bytes, S5L8900_I2S1_BASE, S5L8900_DEV_SIZE)) {
        note_device(m, addr, val, true);
        s5l_i2s_write(&m->i2s[1], addr - S5L8900_I2S1_BASE, val);
        if (addr - S5L8900_I2S1_BASE == S5L_I2S_TX_FIFO_OFF &&
            s5l_i2s_configured(&m->i2s[1]))
            i2s_tx_store(m, 1u, val, bytes);
        return;
    }
    if (mmio_data(addr, bytes, S5L8900_SPI0_BASE, S5L8900_DEV_SIZE)) {
        note_device(m, addr, val, true);
        s5l_spi_write(&m->spi[0], addr - S5L8900_SPI0_BASE, val);
        return;
    }
    if (mmio_data(addr, bytes, S5L8900_SPI1_BASE, S5L8900_DEV_SIZE)) {
        note_device(m, addr, val, true);
        s5l_spi_write(&m->spi[1], addr - S5L8900_SPI1_BASE, val);
        return;
    }
    if (mmio_data(addr, bytes, S5L8900_SPI2_BASE, S5L8900_DEV_SIZE)) {
        note_device(m, addr, val, true);
        s5l_spi_write(&m->spi[2], addr - S5L8900_SPI2_BASE, val);
        return;
    }
    if (mmio_word(addr, bytes, S5L8900_USB_OTG_BASE, S5L8900_DEV_SIZE)) {
        note_device(m, addr, val, true);
        s5l_usbotg_write(&m->usbotg, addr - S5L8900_USB_OTG_BASE, val);
        return;
    }
    /*
     * The DMACs. The store is recorded and applied here; NOTHING is transferred
     * from inside it. A channel enabled by this store moves its bytes in
     * s5l_pl080_run(), which s5l8900_tick() calls — see the comment at the top
     * of pl080.c for why the bus is never re-entered from a bus write.
     */
    if (mmio_word(addr, bytes, S5L8900_DMAC0_BASE, S5L8900_DEV_SIZE)) {
        note_device(m, addr, val, true);
        s5l_pl080_write(&m->dmac[0], addr - S5L8900_DMAC0_BASE, val);
        return;
    }
    if (mmio_word(addr, bytes, S5L8900_DMAC1_BASE, S5L8900_DEV_SIZE)) {
        note_device(m, addr, val, true);
        s5l_pl080_write(&m->dmac[1], addr - S5L8900_DMAC1_BASE, val);
        return;
    }
    if ((bytes == 1u || bytes == 2u || bytes == 4u) &&
        in_window(addr, bytes, m->nor_base, m->nor.size)) {
        /* Guest writes program the flash (bits can only be cleared). Note this
         * is a simplification: on real hardware the NOR is SPI-attached and is
         * programmed through controller commands rather than by storing to a
         * memory window. Modelling it as a direct write keeps the path a guest
         * payload needs — persisting itself into NOR, as an untethered
         * jailbreak does — without a full SPI protocol model. */
        s5l_nor_write(&m->nor, addr - m->nor_base, val, bytes);
        return;
    }
    {
        s5l_stub_t *s = find_stub(m, addr, bytes);
        if (s) {
            s->writes++;
            note_device(m, addr, val, true);
            stub_write(s, addr, val, bytes);
            return;
        }
    }
    m->unmapped_writes++;
    note_unmapped(m, addr);
    note_device(m, addr, val, true);
}

static uint32_t r32(void *c, uint32_t a) { return bus_read(c, a, 4); }
static uint16_t r16(void *c, uint32_t a) { return (uint16_t)bus_read(c, a, 2); }
static uint8_t  r8 (void *c, uint32_t a) { return (uint8_t) bus_read(c, a, 1); }
/*
 * THE DMA REQUEST LINE, which this machine answers on its peripherals' behalf.
 *
 * A PL080 moves a burst into a peripheral only when that peripheral asks for
 * one. This model has no request wires, and until run116 it did not need them:
 * narrow stores to the two data ports were being dropped by the bus decode, so
 * nothing ever arrived fast enough to overflow anything.
 *
 * With that fixed the controller delivered a whole 54,156-byte Z2 firmware
 * image into an eight-deep transmit FIFO inside a single tick, before the
 * driver had armed the port -- `tx-drops 54140`. Real hardware cannot do that,
 * because the SPI only raises its request when it has room.
 *
 * Only the transmit FIFOs are gated. Everything else -- memory, control
 * registers, devices with no FIFO -- is always ready, which is both true of the
 * hardware and the behaviour every existing test was written against.
 */
static bool dma_dst_ready(void *ctx, uint32_t dst, unsigned width) {
    const s5l8900_t *m = ctx;
    for (unsigned i = 0; i < S5L8900_SPI_COUNT; i++) {
        static const uint32_t base[] = {
            S5L8900_SPI0_BASE, S5L8900_SPI1_BASE, S5L8900_SPI2_BASE
        };
        if (i >= sizeof base / sizeof base[0]) break;
        if (dst == base[i] + SPI_TXDATA)
            return m->spi[i].tx_level < S5L_SPI_FIFO_DEPTH;
    }
    unsigned i2s;
    if (i2s_tx_fifo_addr(dst, &i2s) && s5l_i2s_configured(&m->i2s[i2s]))
        return m->i2s_tx[i2s].fifo_level + width <= S5L_I2S_TX_FIFO_BYTES;
    return true;
}

static void w32(void *c, uint32_t a, uint32_t v) { bus_write(c, a, v, 4); }
static void w16(void *c, uint32_t a, uint16_t v) { bus_write(c, a, v, 2); }
static void w8 (void *c, uint32_t a, uint8_t  v) { bus_write(c, a, v, 1); }

bool s5l8900_set_direct_ram_writes(s5l8900_t *m, bool enabled) {
    if (!m) return false;
    if (enabled &&
        (m->bus.ctx != m || m->cpu.bus != &m->bus ||
         m->bus.write32 != w32 || m->bus.write16 != w16 ||
         m->bus.write8 != w8 || m->bus.host_ram != machine_host_ram)) {
        /* Fail closed even if a caller asks to re-enable after installing an
         * interposer without first revoking an earlier grant. The interposer
         * still owns the ordering contract, but this API must never make that
         * mistake persist once it can see it. */
        m->bus.host_ram_write = NULL;
        memset(m->cpu.dwrite, 0, sizeof m->cpu.dwrite);
        return false;
    }
    m->bus.host_ram_write = enabled ? machine_host_ram : NULL;
    /* A pointer granted under an earlier frontend contract must never survive
     * a mode change. Generation tags cannot express revoked consent. */
    memset(m->cpu.dwrite, 0, sizeof m->cpu.dwrite);
    return true;
}

bool s5l8900_set_wfi_host_pacing(s5l8900_t *m,
                                 s5l_wfi_host_sleep_fn sleep, void *ctx) {
    if (!m) return false;
    if (!sleep && ctx) return false;

    /* Publish the callback last when enabling and clear it first when
     * disabling. The machine is single-owner, but this ordering also prevents
     * a diagnostic observer from ever seeing a live callback with stale
     * context. */
    if (!sleep) m->wfi_host_sleep = NULL;
    m->wfi_host_sleep_ctx = sleep ? ctx : NULL;
    m->wfi_paced_waits = 0u;
    m->wfi_paced_wait_ns = 0u;
    m->wfi_paced_partial_advances = 0u;
    m->wfi_paced_failures = 0u;
    m->wfi_pace_yield = false;
    if (sleep) m->wfi_host_sleep = sleep;
    return true;
}

/* Keep the historical discard contract unless a frontend opts in. Changing
 * policy discards pending host-only debt; ordinary sync retains every trusted
 * foreground interval while still advancing devices at most 8ms per sync. */
bool s5l8900_set_active_clock_carry(s5l8900_t *m, bool enabled) {
    /* One CPU-clock tick must fit the 8ms payment bound. */
    if (!m || (enabled && m->cpu_hz < 125u)) return false;
    m->active_clock_carry = enabled;
    m->active_clock_debt_ticks = 0u;
    return true;
}

bool s5l8900_set_active_host_clock(s5l8900_t *m,
                                   s5l_active_host_now_fn now, void *ctx) {
    if (!m) return false;
    if (!now && ctx) return false;

    /* Match the WFI policy's publication order: a diagnostic observer cannot
     * see a live callback paired with stale context or anchor state. */
    if (!now) m->active_host_now = NULL;
    m->active_host_now_ctx = now ? ctx : NULL;
    m->active_clock_last_host_ns = 0u;
    m->active_clock_guest_ticks_since_sync = 0u;
    m->active_clock_fraction = 0u;
    m->active_clock_debt_ticks = 0u;
    m->active_clock_updates = 0u;
    m->active_clock_added_ticks = 0u;
    m->active_clock_clamps = 0u;
    m->active_clock_failures = 0u;
    m->active_clock_anchor_valid = false;
    if (now) m->active_host_now = now;
    return true;
}

bool s5l8900_set_uart4_host(s5l8900_t *m, s5l_uart4_host_tx_fn tx,
                            s5l_uart4_host_service_fn service, void *ctx) {
    if (!m) return false;
    if (!tx && !service) {
        if (ctx) return false;
        /* Clear the callable fields first, then their borrowed context. */
        m->uart4_host_tx = NULL;
        m->uart4_host_service = NULL;
        m->uart4_host_ctx = NULL;
        return true;
    }
    if (!tx || !service) return false;

    /* Publish the context before either callback becomes callable. */
    m->uart4_host_ctx = ctx;
    m->uart4_host_tx = tx;
    m->uart4_host_service = service;
    return true;
}

bool s5l8900_set_uart0_host(s5l8900_t *m, s5l_uart4_host_tx_fn tx,
                            void *ctx) {
    if (!m) return false;
    if (!tx) {
        if (ctx) return false;
        m->uart0_host_tx = NULL;
        m->uart0_host_ctx = NULL;
        return true;
    }
    m->uart0_host_ctx = ctx;
    m->uart0_host_tx = tx;
    return true;
}

static unsigned pre_step_filter_bit(uint32_t pc) {
    uint32_t word = pc >> 1u;
    return (unsigned)((word ^ (word >> 10u) ^ (word >> 20u)) & 63u);
}

static bool pre_step_target_matches(const s5l8900_t *m, uint32_t pc) {
    if (!m || !m->pre_step_hook || !m->pre_step_target_count) return false;
    pc &= ~UINT32_C(1);
    if ((m->pre_step_filter &
         (UINT64_C(1) << pre_step_filter_bit(pc))) == 0u)
        return false;
    for (unsigned i = 0u; i < m->pre_step_target_count; i++) {
        if (m->pre_step_target[i] == pc) return true;
    }
    return false;
}

bool s5l8900_pre_step_target(const s5l8900_t *m, uint32_t pc) {
    return pre_step_target_matches(m, pc);
}

bool s5l8900_set_pre_step_hook(s5l8900_t *m, s5l_pre_step_fn fn, void *ctx,
                               const uint32_t *targets, unsigned count) {
    if (!m) return false;
    if (!fn) {
        if (targets || count) return false;
        m->pre_step_hook = NULL;
        m->pre_step_ctx = NULL;
        memset(m->pre_step_target, 0, sizeof m->pre_step_target);
        m->pre_step_target_count = 0u;
        m->pre_step_filter = 0u;
        m->pre_step_matches = 0u;
        m->pre_step_handled = 0u;
        return true;
    }
    if (!targets || !count || count > S5L_PRE_STEP_TARGET_MAX) return false;
    for (unsigned i = 0u; i < count; i++) {
        if (targets[i] & 1u) return false;
        for (unsigned j = 0u; j < i; j++) {
            if (targets[j] == targets[i]) return false;
        }
    }

    /* Validate the whole request before changing any live policy. */
    memcpy(m->pre_step_target, targets, count * sizeof targets[0]);
    memset(m->pre_step_target + count, 0,
           (S5L_PRE_STEP_TARGET_MAX - count) * sizeof targets[0]);
    m->pre_step_target_count = count;
    m->pre_step_filter = 0u;
    for (unsigned i = 0u; i < count; i++)
        m->pre_step_filter |=
            UINT64_C(1) << pre_step_filter_bit(targets[i]);
    m->pre_step_ctx = ctx;
    m->pre_step_hook = fn;
    m->pre_step_matches = 0u;
    m->pre_step_handled = 0u;
    return true;
}

uint64_t s5l8900_pre_step_matches(const s5l8900_t *m) {
    return m ? m->pre_step_matches : 0u;
}

uint64_t s5l8900_pre_step_handled(const s5l8900_t *m) {
    return m ? m->pre_step_handled : 0u;
}

/* -------------------------------------------------------- wake sources ---
 *
 * Which modelled devices can end a WFI, and how far away each one's next
 * interrupt edge is.  See the wake-source block in soc.h for the contract;
 * what follows is why this is a table at all.
 *
 * It used to be an if-chain inside machine_wait_for_interrupt() naming the
 * timer, the CLCD and TV-out.  That made the fast-forward the real definition
 * of "things that can interrupt this machine", and it was a definition nobody
 * would think to come and update: a device could be modelled correctly, assert
 * its line correctly, and still never be observed, because the idle skip
 * stepped over the tick it fired on.  Declaring a source here is now the whole
 * of wiring one into WFI, exactly as DEVICE_WINDOWS is the whole of putting a
 * window on the bus.
 *
 * These three functions are the old if-chain's conditions, unchanged.  Each
 * runs only when its VIC line is enabled, which is the test the chain applied
 * to all three by hand.
 */

/*
 * Can `line` reach the CPU at all?  The flat 0-63 numbering is the device
 * tree's own and both VICs drive the core (see s5l8900_tick), so the gate is
 * simply "enabled in the VIC that carries it".  A line outside the pair is not
 * one this machine can route: s5l_vic_set_line() drops it, so nothing could
 * ever assert it and there is nothing to wait for.
 *
 * CPSR is deliberately not consulted — an ARM1176 completes WFI on the raw
 * line whether or not the exception is masked — and neither is INTSELECT,
 * because IRQ and FIQ both wake the core.
 */
static bool wake_line_enabled(const s5l8900_t *m, unsigned line) {
    if (line >= 32u * S5L8900_VIC_COUNT) return false;
    return (m->vic[line / 32u].enable & (1u << (line % 32u))) != 0u;
}

/* Timer 4's decrementer.  A zero live value has not been loaded yet, so the
 * first tick reloads it from the buffered count; zero in both is a timer that
 * has stopped and will not expire again. */
static s5l_wake_kind_t wake_edge_timer(const s5l8900_t *m, uint32_t *ticks) {
    if ((m->timer.t4_state & TIMER4_STATE_START) == 0u) return S5L_WAKE_NEVER;
    uint32_t until = m->timer.t4_value ? m->timer.t4_value : m->timer.t4_count;
    if (until == 0u) return S5L_WAKE_NEVER;
    *ticks = until;
    return S5L_WAKE_AT;
}

/* The CLCD's frame (VBL) latch, which only exists while scanout is live and
 * the controller's own mask lets it through. */
static s5l_wake_kind_t wake_edge_clcd(const s5l8900_t *m, uint32_t *ticks) {
    if ((m->clcd.intmask & CLCD_INT_FRAME) == 0u ||
        !s5l_clcd_running(&m->clcd) || m->clcd.frame_ticks == 0u)
        return S5L_WAKE_NEVER;
    /* frame_accum is normally strictly below frame_ticks.  If a malformed
     * in-memory caller violates that invariant, one tick is the only safe
     * boundary: the CLCD normalizes it and asserts the frame latch there. */
    *ticks = m->clcd.frame_accum < m->clcd.frame_ticks
           ? m->clcd.frame_ticks - m->clcd.frame_accum
           : 1u;
    return S5L_WAKE_AT;
}

/* TV-out SDO VSYNC.  The device answers "no deliverable VSYNC" with zero,
 * which is a statement about its state (stopped, masked, already pending) and
 * therefore S5L_WAKE_NEVER rather than an unknown. */
static s5l_wake_kind_t wake_edge_tvout(const s5l8900_t *m, uint32_t *ticks) {
    uint32_t until = s5l_tvout_ticks_to_vsync(&m->tvout);
    if (until == 0u) return S5L_WAKE_NEVER;
    *ticks = until;
    return S5L_WAKE_AT;
}

/*
 * The two SPI controllers.
 *
 * Both answer S5L_WAKE_NEVER unconditionally, and that is a statement about the
 * model rather than a placeholder. An SPI word completes as a direct
 * consequence of a guest store into the transmit FIFO, and a core in WFI issues
 * no stores; a line that was already high before the WFI is caught by
 * machine_wait_for_interrupt()'s own pre-check, before any source is consulted.
 * So there is no future edge to name, and NEVER — the only answer that lets
 * guest time be skipped — is the correct one. Answering S5L_WAKE_UNKNOWN
 * instead would be safe but would stop the machine fast-forwarding ANY idle
 * period for the whole boot.
 *
 * They are declared anyway, because the table is this machine's definition of
 * what can interrupt it (see the wake-source block in soc.h). A touch device
 * that raises a report on its own schedule rather than in reply to a store is
 * exactly the case the table exists for, and this is the entry it edits.
 */
static s5l_wake_kind_t wake_edge_spi0(const s5l8900_t *m, uint32_t *ticks) {
    (void)m; (void)ticks;
    return S5L_WAKE_NEVER;
}
static s5l_wake_kind_t wake_edge_spi1(const s5l8900_t *m, uint32_t *ticks) {
    (void)m; (void)ticks;
    return S5L_WAKE_NEVER;
}

/*
 * The seven GPIO interrupt groups.
 *
 * All seven are declared, not just group 4. Nothing else in the shipped tree
 * claims VIC lines 0, 1, 2, 3, 31, 32 or 33, so declaring the whole cascade
 * costs nothing and removes the question of whether /arm-io/gpio's descending
 * `interrupts` array is indexed by group or reversed — get that wrong for one
 * entry and the touch line is routed to a VIC line nobody enabled, silently.
 * Lines 32 and 33 are on VIC1, which is why s5l8900_tick() has to route by
 * line/32 rather than hardcoding vic[0] the way every older device does.
 *
 * Every one answers S5L_WAKE_NEVER, and that survives the touch controller
 * gaining a report to deliver. The reasoning is the same as the SPI
 * controllers': a future edge is only worth naming if this machine can produce
 * one while the core sleeps, and the multitouch attention line moves at exactly
 * two moments — a host call to s5l_mtz2_set_contacts(), which happens BETWEEN
 * guest instructions and therefore never inside a skipped interval, and the
 * last byte of a data read, which is a consequence of a guest store the sleeping
 * core is not making. A line that is ALREADY asserted is caught by
 * machine_wait_for_interrupt()'s own pre-check — it refreshes at zero elapsed
 * time, which routes s5l_mtz2_irq() through the cascade before any source is
 * consulted — so NEVER cannot lose an interrupt that has already happened, and
 * an injection made during a WFI is observed on the very next refresh.
 *
 * The entry that matters is `gpio-group4`, and the number on it is 2 — the
 * CASCADE VIC line, not 155. wake_line_enabled() rejects anything at or above
 * 32 * S5L8900_VIC_COUNT, so a source written as `{ "multitouch", 155, ... }`
 * would return false silently and could never wake the core.
 */
static s5l_wake_kind_t wake_edge_gpio(const s5l8900_t *m, uint32_t *ticks) {
    /* The one GPIOIC input with a schedule of its own: an I2S frame clock the
     * guest has unmasked. Its next rising edge is a known distance away. */
    uint64_t hz = audio_tb_hz(m), best = 0;
    bool have = false;
    for (unsigned i = 0; i < S5L8900_I2S_COUNT; i++) {
        unsigned line = I2S_LRCK_LINE[i];
        if (!s5l_i2s_frame_sync(&m->i2s[i]) ||
            !(m->gpioic.en[line >> 5] & (1u << (line & 31u)))) continue;
        const s5l_i2s_stream_t *s = &m->i2s_tx[i];
        uint64_t need = (s->lrck ? 2u : 1u) * hz - s->lrck_phase;
        uint64_t rate = 2u * S5L_I2S_FRAME_HZ;
        uint64_t at = (need + rate - 1u) / rate;
        if (!at) at = 1u;
        if (!have || at < best) { best = at; have = true; }
    }
    if (!have) return S5L_WAKE_NEVER;
    if (ticks) *ticks = best > UINT32_MAX ? UINT32_MAX : (uint32_t)best;
    return S5L_WAKE_AT;
}

/*
 * uart4's receive line, and the answer docs/derivations.md §23.5.1 asked for:
 * "S5L_WAKE_NEVER when the RX FIFO is empty and an immediate edge when it is
 * not". Both halves of that sentence are NEVER here, and the difference is only
 * in who catches the second half.
 *
 * A host-delivered byte has no schedule — the host pushes it between run
 * slices, which is a moment a sleeping core is not inside — so there is no
 * future distance to name and this source can never produce one. A FIFO with an
 * UNACKNOWLEDGED arrival in it has already asserted the line, and
 * machine_wait_for_interrupt() refreshes every level at zero elapsed time
 * before consulting any source, so it ends the wait there. (A FIFO the driver
 * has acknowledged but not drained asserts nothing — the latch is edge
 * triggered, see the UART block in soc.h — and there is still nothing to wait
 * for either: only an arrival can latch it, and only the host can produce one.)
 * Answering S5L_WAKE_AT with 0 would be rejected as "not a future distance"
 * anyway (s5l8900_next_wake), and answering UNKNOWN would stop the machine
 * fast-forwarding any idle period for the whole boot — on a source that is
 * inert on every run without a peer.
 *
 * It is declared rather than omitted for the reason the SPI block gives: this
 * table is the machine's definition of what can interrupt it, and a device
 * missing from it is a device the next reader has to rediscover.
 */
static s5l_wake_kind_t wake_edge_uart4(const s5l8900_t *m, uint32_t *ticks) {
    (void)m; (void)ticks;
    return S5L_WAKE_NEVER;
}

/*
 * The two PL080 DMA controllers, and the answer is NEVER for the same reason
 * the SPI controllers' is — but the reasoning has one extra step, because a DMA
 * transfer is the one thing in this machine that looks like it might complete
 * on its own schedule.
 *
 * It does not, here. A channel becomes runnable only when a guest store sets
 * its enable bit, that store sets `level_dirty`, and the very next
 * s5l8900_tick() — which cannot early-out while `level_dirty` is set — runs
 * s5l_pl080_run() to the END of the chain and latches any terminal count. So by
 * the time a core could reach WFI, either the interrupt is already asserted (in
 * which case machine_wait_for_interrupt()'s own pre-check ends the wait before
 * any source is consulted) or there is no transfer in flight at all. There is
 * no future edge to name.
 *
 * That would stop being true the day this model paces transfers against a
 * peripheral's DMA request line instead of completing them in one call, which
 * is exactly the change the burst note in soc.h says has not been made. This
 * entry is where that change would be felt: it would have to start answering
 * S5L_WAKE_AT with the remaining distance, or the machine would fast-forward
 * straight over the completion.
 */
static s5l_wake_kind_t wake_edge_dmac(const s5l8900_t *m, uint32_t *ticks) {
    /* Except for a channel paced by a running I2S transmitter: it retires its
     * item when the shifter has taken the remaining bytes, and that moment is
     * a computable distance at the stream's byte rate. */
    uint64_t hz = audio_tb_hz(m), best = 0;
    bool have = false;
    for (unsigned d = 0; d < S5L8900_DMAC_COUNT; d++) {
        for (unsigned c = 0; c < S5L_PL080_CHANNELS; c++) {
            const s5l_pl080_chan_t *ch = &m->dmac[d].ch[c];
            unsigned i;
            if (!(ch->cfg & PL080_CFG_EN) || !i2s_tx_fifo_addr(ch->dst, &i) ||
                !s5l_i2s_tx_running(&m->i2s[i])) continue;
            uint32_t sw = (ch->ctrl >> PL080_CTRL_SWIDTH_SHIFT) &
                          PL080_CTRL_WIDTH_MASK;
            /* The last store lands once the shifter has made room for it. */
            uint64_t left = (uint64_t)(ch->ctrl & PL080_CTRL_SIZE_MASK) << sw;
            uint64_t room = S5L_I2S_TX_FIFO_BYTES - m->i2s_tx[i].fifo_level;
            uint64_t need = left > room ? left - room : 0u;
            uint64_t rate = (uint64_t)S5L_I2S_FRAME_HZ * S5L_I2S_FRAME_BYTES;
            uint64_t at = (need * hz + rate - 1u) / rate;
            if (!at) at = 1u;
            if (!have || at < best) { best = at; have = true; }
        }
    }
    if (!have) return S5L_WAKE_NEVER;
    if (ticks) *ticks = best > UINT32_MAX ? UINT32_MAX : (uint32_t)best;
    return S5L_WAKE_AT;
}

static const s5l_wake_source_t WAKE_SOURCES[] = {
    { "timer", S5L8900_IRQ_TIMER, wake_edge_timer },
    { "clcd",  S5L8900_IRQ_CLCD,  wake_edge_clcd  },
    { "tvout", S5L8900_IRQ_TVOUT, wake_edge_tvout },
    { "spi0",  S5L8900_IRQ_SPI0,  wake_edge_spi0  },
    { "spi1",  S5L8900_IRQ_SPI1,  wake_edge_spi1  },
    /* Group order, so entry k is group k. The lines are /arm-io/gpio's own
     * `interrupts` = {33,32,31,3,2,1,0}; group 4 -> VIC line 2 carries touch. */
    { "gpio-group0", 33u, wake_edge_gpio },
    { "gpio-group1", 32u, wake_edge_gpio },
    { "gpio-group2", 31u, wake_edge_gpio },
    { "gpio-group3",  3u, wake_edge_gpio },
    { "gpio-group4",  2u, wake_edge_gpio },
    { "gpio-group5",  1u, wake_edge_gpio },
    { "gpio-group6",  0u, wake_edge_gpio },
    { "uart4-rx", S5L8900_IRQ_UART4, wake_edge_uart4 },
    { "dmac0", S5L8900_IRQ_DMAC0, wake_edge_dmac },
    { "dmac1", S5L8900_IRQ_DMAC1, wake_edge_dmac },
};
#define NWAKE_SOURCES (sizeof WAKE_SOURCES / sizeof WAKE_SOURCES[0])

unsigned s5l8900_wake_sources(const s5l_wake_source_t **out) {
    if (out) *out = WAKE_SOURCES;
    return (unsigned)NWAKE_SOURCES;
}

s5l_wake_kind_t s5l8900_next_wake(const s5l8900_t *m,
                                  const s5l_wake_source_t *src, unsigned n,
                                  uint32_t *ticks) {
    /* Nothing to reason about is not the same as nothing to wait for, so both
     * of these decline to skip time rather than reporting a clear horizon. */
    if (!m) return S5L_WAKE_UNKNOWN;
    if (n != 0u && !src) return S5L_WAKE_UNKNOWN;

    bool have = false;
    uint32_t best = 0;
    for (unsigned i = 0; i < n; i++) {
        /* A masked line cannot reach the CPU, so its device cannot end this
         * wait however close its next edge is.  Skipping it here is what makes
         * the old chain's per-source VIC test unnecessary. */
        if (!wake_line_enabled(m, src[i].line)) continue;
        if (!src[i].next_edge) return S5L_WAKE_UNKNOWN;   /* declared, silent */

        uint32_t at = 0;
        s5l_wake_kind_t kind = src[i].next_edge(m, &at);
        if (kind == S5L_WAKE_NEVER) continue;
        /* Anything that is not a future distance stops the fast-forward: an
         * unknown, or an "at" of zero, which cannot be a moment still to come
         * (the caller has already refreshed every line at zero elapsed time). */
        if (kind != S5L_WAKE_AT || at == 0u) return S5L_WAKE_UNKNOWN;
        if (!have || at < best) { best = at; have = true; }
    }

    if (!have) return S5L_WAKE_NEVER;
    if (ticks) *ticks = best;
    return S5L_WAKE_AT;
}

/*
 * Complete one ARM1176 Wait For Interrupt operation without manufacturing CPU
 * work.  The earliest edge any declared wake source can route through the VIC
 * is the earliest point at which this model can wake the core.  Advancing
 * farther would coalesce guest-visible work across an interrupt; advancing
 * less would merely replace the kernel's idle loop with a host idle loop.
 */
static bool machine_wait_for_interrupt(void *ctx) {
    s5l8900_t *m = ctx;
    if (!m) return false;

    if (m->batch_pending) {
        unsigned pending = m->batch_pending;
        m->batch_pending = 0u;
        s5l8900_tick(m, pending);
    }

    /* A line that is already high completes WFI even when CPSR masks it.  This
     * check precedes the controller refresh so an externally injected CPU line
     * is not lost. */
    if (m->cpu.irq_line || m->cpu.fiq_line) return true;

    /* Guest writes can change a VIC enable/mask or acknowledge a source after
     * the preceding instruction's tick.  Refresh level outputs at zero elapsed
     * time before deciding whether a future edge is necessary. */
    s5l8900_tick(m, 0);
    if (m->cpu.irq_line || m->cpu.fiq_line) return true;

    /*
     * With no enabled source able to name a future edge — because none has one
     * (NEVER), or because one of them cannot say (UNKNOWN) — there is nothing
     * safe to fast-forward to.  Return to the interpreter's documented no-op
     * fallback rather than hanging the host forever, inventing an interrupt, or
     * skipping guest time past a source that might have fired inside it.  The
     * guest still makes progress: the WFI retires, and the run loop's own tick
     * moves time on by one.
     */
    uint32_t edge_tb = 0;
    const s5l_wake_source_t *sources = NULL;
    unsigned nsources = s5l8900_wake_sources(&sources);
    if (s5l8900_next_wake(m, sources, nsources, &edge_tb) != S5L_WAKE_AT)
        return false;

    uint64_t cpu_ticks;
    if (m->cpu_hz && m->tb_hz) {
        /* s5l8900_tick's integer converter can cross at most one timebase edge
         * per CPU tick only in this direction.  Refuse unusual inverted or
         * corrupt ratios instead of jumping past the earliest edge. */
        if (m->tb_hz > m->cpu_hz || m->tb_accum >= m->cpu_hz) return false;
        uint64_t need = (uint64_t)edge_tb * m->cpu_hz - m->tb_accum;
        cpu_ticks = need / m->tb_hz + (need % m->tb_hz != 0u);
    } else {
        /* Existing zero-clock fallback: s5l8900_tick treats its input as raw
         * timebase ticks when either advertised rate is zero. */
        cpu_ticks = edge_tb;
    }

    /*
     * Deterministic harnesses deliberately have no callback and still jump to
     * the edge immediately.  An interactive frontend may instead make each
     * autonomous interval consume the same order of host time as guest time.
     *
     * Never wait more than one short host slice. A stopped CLCD can leave the
     * next timer edge seconds away, while the frontend can inject touch or a
     * stop request only between s5l8900_run() calls. Advancing a shorter,
     * already-waited interval is safe because next_wake proved that no modeled
     * source can fire before the selected edge. The WFI may complete spuriously
     * at that intermediate point; ARM permits that, and the historical UNKNOWN
     * fallback already relies on the same harmless behavior.
     */
    if (m->wfi_host_sleep) {
        if (!m->cpu_hz || !m->tb_hz || m->tb_hz > m->cpu_hz) {
            m->wfi_pace_yield = true;
            m->wfi_paced_failures++;
            return false;
        }

        uint64_t slice_ticks =
            (S5L8900_WFI_PACE_SLICE_NS * m->cpu_hz) / UINT64_C(1000000000);
        bool partial = cpu_ticks > slice_ticks;
        uint64_t paced_ticks = partial ? slice_ticks : cpu_ticks;
        uint64_t wait_ns;

        if (!paced_ticks) {
            /* At a synthetic clock below 125 Hz, even one CPU tick is longer
             * than the responsiveness ceiling. Wait one slice, advance no
             * guest time, and yield. Real S5L8900 clocks never take this path. */
            wait_ns = S5L8900_WFI_PACE_SLICE_NS;
        } else {
            /* paced_ticks is bounded by the 8 ms slice, so this product cannot
             * overflow for any uint32_t CPU rate. Round up: waiting a fraction
             * too long is harmless; advancing before the requested interval
             * completed would recreate the bug this policy exists to stop. */
            uint64_t scaled = paced_ticks * UINT64_C(1000000000);
            wait_ns = scaled / m->cpu_hz + (scaled % m->cpu_hz != 0u);
        }

        m->wfi_pace_yield = true;
        m->wfi_paced_waits++;
        if (UINT64_MAX - m->wfi_paced_wait_ns < wait_ns)
            m->wfi_paced_wait_ns = UINT64_MAX;
        else
            m->wfi_paced_wait_ns += wait_ns;

        if (!m->wfi_host_sleep(m->wfi_host_sleep_ctx, wait_ns)) {
            m->wfi_paced_failures++;
            s5l8900_tick(m, 0u);
            return m->cpu.irq_line || m->cpu.fiq_line;
        }

        if (partial) {
            m->wfi_paced_partial_advances++;
            if (paced_ticks) s5l8900_tick(m, (uint32_t)paced_ticks);
            else s5l8900_tick(m, 0u);
            return m->cpu.irq_line || m->cpu.fiq_line;
        }
    }

    /* The public tick API is 32-bit.  Split a very long wait without changing
     * the exact fractional accumulator; no selected wake edge occurs before
     * the total derived above. */
    while (cpu_ticks > UINT32_MAX) {
        s5l8900_tick(m, UINT32_MAX);
        cpu_ticks -= UINT32_MAX;
        if (m->cpu.irq_line || m->cpu.fiq_line) return true;
    }
    if (cpu_ticks) s5l8900_tick(m, (uint32_t)cpu_ticks);
    return m->cpu.irq_line || m->cpu.fiq_line;
}

/* ----------------------------------------------------------- lifecycle --- */

/*
 * S5LBOX_TICK_EAGER=1 restores the per-instruction refresh s5l8900_tick() used
 * to do unconditionally. It exists so the two paths can be run against each
 * other on the same binary and the same boot — which is the only way to claim
 * the skip changes nothing the guest can see — and it is read once per machine
 * rather than per tick. A machine-independent static because it describes the
 * BUILD under test, not a property of a machine, and therefore must not enter
 * a snapshot.
 */
static bool g_tick_eager;
static bool g_tick_eager_read;

bool s5l8900_init_soc(s5l8900_t *m, uint32_t ram_base, uint32_t ram_size) {
    if (!m) return false;
#ifdef S5LBOX_ADAPTIVE_JIT
    if (!arm_adaptive_forget(&m->cpu)) return false;
#endif
    memset(m, 0, sizeof *m);

    /*
     * Refuse an aliasing configuration before allocating anything.
     *
     * This is the whole routing contract in three lines (soc.h explains why it
     * lives here rather than in bus_read). A machine whose DRAM window covers a
     * device window can only ever be one of two wrong things: a device that
     * cannot be reached, or a hole in the DRAM bank the guest was promised.
     * Neither is worth having, and both are invisible at run time, so the
     * machine simply does not exist.
     */
    if (!ram_size || (uint64_t)ram_base + ram_size > 0x100000000ull ||
        s5l8900_ram_conflict(ram_base, ram_size)) return false;

    m->ram = calloc(ram_size, 1);
    /* The MBX aperture's edram, owned here like RAM. See S5L_MBX_APERTURE.
     * A failure is handled below with the RAM failure, not ignored. */
    m->mbx.edram = calloc(S5L_MBX_EDRAM_SIZE, 1);
    /* Optional: without it shifted-out samples are counted and dropped. */
    m->audio_ring = calloc(S5L_AUDIO_RING_BYTES, 1);
    if (!m->ram || !m->mbx.edram) {
        free(m->audio_ring);
        m->audio_ring = NULL;
        free(m->ram);
        m->ram = NULL;
        free(m->mbx.edram);
        m->mbx.edram = NULL;
        return false;
    }
    m->ram_base = ram_base;
    m->ram_size = ram_size;
    m->cpu_hz   = S5L8900_CPU_HZ;
    m->tb_hz    = S5L8900_TB_HZ;

    s5l_uart_reset(&m->uart0);
    s5l_uart_reset(&m->uart4);
    for (unsigned i = 0; i < S5L8900_VIC_COUNT; i++) s5l_vic_reset(&m->vic[i]);
    s5l_timer_reset(&m->timer);
    s5l_power_reset(&m->power);
    s5l_mbx_reset(&m->mbx);
    s5l_clcd_reset(&m->clcd);
    s5l_tvout_reset(&m->tvout, m->tb_hz);
    for (unsigned i = 0; i < S5L8900_I2C_COUNT; i++)
        s5l_i2c_reset(&m->i2c[i]);
    s5l_pcf50635_reset(&m->pmu, m->tb_hz);
    s5l_wm8991_reset(&m->codec);
    for (unsigned i = 0; i < S5L8900_I2S_COUNT; i++) s5l_i2s_reset(&m->i2s[i]);
    for (unsigned i = 0; i < S5L8900_SPI_COUNT; i++) s5l_spi_reset(&m->spi[i]);
    s5l_gpioic_reset(&m->gpioic);
    s5l_gpio_reset(&m->gpio);
    /*
     * The board's five switches, and the ORDER MATTERS: both blocks above
     * power up all-zero, and two of the five buttons are active low, so their
     * released level is HIGH. Applying the rest state here — after the resets
     * that would wipe it, before the guest has configured anything — is what
     * stops the volume keys from reading as held down for the whole boot. See
     * s5l_buttons_reset() for why zeroing the struct is not enough.
     */
    s5l_buttons_reset(&m->buttons);
    s5l_buttons_apply(&m->buttons, &m->gpio, &m->gpioic);
    for (unsigned i = 0; i < S5L8900_I2S_COUNT; i++)
        s5l_i2s_stream_reset(&m->i2s_tx[i]);
    s5l_usbotg_reset(&m->usbotg);
    for (unsigned i = 0; i < S5L8900_DMAC_COUNT; i++)
        s5l_pl080_reset(&m->dmac[i]);
    /* Refresh in the guest's own time: the CLCD is ticked with timebase ticks,
     * so the period is expressed in them. */
    m->clcd.frame_ticks = m->tb_hz / S5L_CLCD_REFRESH_HZ;
    m->nor_base = S5L8900_NOR_BASE;
    if (!s5l_nor_init(&m->nor, S5L8900_NOR_SIZE)) {
        free(m->ram);
        m->ram = NULL;
        free(m->mbx.edram);
        m->mbx.edram = NULL;
        return false;
    }

    /*
     * Peripheral windows we have identified but not modelled. Each base was
     * resolved twice — from the VA the guest's own driver printed, walked
     * through its live page tables, and from the shipped device tree's arm-io
     * ranges (child + 0x38000000) — and the two agree. They are declared so
     * their traffic is named and stored instead of reading back as the zero an
     * unmapped access returns. See s5l_stub_t for why a stub is honest here and
     * where the line is that turns one into a real device model.
     *
     * "Declared" is deliberately weaker than "harmless". SPI2 carries both
     * M68's Z1 touch controller and the baseband transport
     * com.apple.driver.BasebandSPI. The touch model is attached by the M68
     * board; the baseband side remains only a controller endpoint. Its reset
     * IOInterruptEventSource sits on GPIO interrupt 75, which has never fired.
     * Modelling the window does not claim to complete either protocol; it stops
     * identified peripherals from answering the shipped driver with a
     * fabricated zero.
     *
     * A failure to declare one is not fatal but must not be silent, so the
     * result is folded into a counter the caller can see.
     */
    {
        static const struct { uint32_t base, size; const char *name; } STUBS[] = {
            /* AppleS5L8900XClockController _ccBaseAddress. */
            { S5L8900_CLOCK_BASE,  S5L8900_DEV_SIZE,   "clkrstgen" },
            /* _miuBaseAddress, the clock controller's second reg range.
             * Offsets 0x008 and 0x404 are the ones actually touched. */
            { S5L8900_MIU_BASE,    S5L8900_DEV_SIZE,   "miu"       },
            { S5L8900_EDGEIC_BASE, S5L8900_DEV_SIZE,   "edgeic"    },
            /* gpio and gpioic used to be here. Both are device models now
             * (DEVICE_WINDOWS above) — a duplicate declaration would be
             * refused as an overlap and counted as a failure rather than
             * shadowing them. */
        };
        for (unsigned i = 0; i < sizeof STUBS / sizeof STUBS[0]; i++)
            if (!s5l8900_add_stub(m, STUBS[i].base, STUBS[i].size, STUBS[i].name))
                m->stub_declare_failures++;
    }

    /* Nothing has refreshed the levels yet: the resets and s5l_buttons_apply()
     * above drove pins, and the VIC outputs still hold the memset's zero. The
     * first tick must be a full one however small it is. */
    m->level_dirty = true;

    if (!g_tick_eager_read) {
        const char *eager = getenv("S5LBOX_TICK_EAGER");
        g_tick_eager = eager && *eager && *eager != '0';
        g_tick_eager_read = true;
    }

    m->bus.ctx = m;
    m->bus.read32 = r32; m->bus.read16 = r16; m->bus.read8 = r8;
    m->bus.write32 = w32; m->bus.write16 = w16; m->bus.write8 = w8;
    m->bus.wait_for_interrupt = machine_wait_for_interrupt;
    m->bus.host_ram = machine_host_ram;

    arm_reset(&m->cpu, &m->bus);
    return true;
}

bool s5l8900_init(s5l8900_t *m, uint32_t ram_base, uint32_t ram_size) {
    return s5l8900_init_soc(m, ram_base, ram_size) &&
           s5l_board_n82.attach(m);
}

void s5l8900_free(s5l8900_t *m) {
    if (!m) return;
#ifdef S5LBOX_ADAPTIVE_JIT
    if (!arm_adaptive_forget(&m->cpu)) return;
#endif
    for (unsigned i = 0; i < m->stub_count; i++) {
        free(m->stubs[i].regs);
        m->stubs[i].regs = NULL;
    }
    m->stub_count = 0;
    free(m->ram);
    m->ram = NULL;
    free(m->mbx.edram);
    m->mbx.edram = NULL;
    free(m->audio_ring);
    m->audio_ring = NULL;
    s5l_nor_free(&m->nor);
}

bool s5l8900_set_cycles_per_insn(s5l8900_t *m, unsigned cpi) {
    if (!m || cpi < 1u || cpi > 16u) return false;
    /* CPU-clock debt/fractions cannot change units while foreground carry is
     * live. A frontend may pause (clearing the anchor/debt), set CPI, resume. */
    if (m->active_host_now && m->active_clock_carry &&
        m->cpu_hz != S5L8900_CPU_HZ / cpi) return false;
    m->cpu_hz = S5L8900_CPU_HZ / cpi;
    m->tb_accum %= m->cpu_hz;
    return true;
}

void s5l8900_load(s5l8900_t *m, uint32_t addr, const void *data, size_t len) {
    if (!m || !data || !len) return;
    /* Check before narrowing: a >4 GiB length must not truncate into range. */
    if (len > 0xffffffffu) return;
    if (!in_ram(m, addr, (uint32_t)len)) return;
    memcpy(&m->ram[addr - m->ram_base], data, len);
    if (m->bus.ram_written)
        m->bus.ram_written(m->bus.ctx, &m->ram[addr - m->ram_base], (uint32_t)len);
}

/*
 * The inputs a host can move behind the bus, in one word. See `ext_seen` in
 * soc.h for why the machine has to WATCH these rather than be told about them,
 * and why the list is exactly three long: they are the three s5l_*_t sub-
 * structs a host is handed a pointer to without the machine that owns them.
 *
 * uart0's receive FIFO is deliberately absent. Its line is 24 and s5l8900_tick
 * does not connect it (see the uart4 block there), so a byte pushed into it
 * cannot change any level and watching it would be watching nothing.
 */
static uint32_t ext_inputs(const s5l8900_t *m) {
    return (uint32_t)m->uart4.rx_count
         | ((uint32_t)m->buttons.pressed << 8)
         | ((uint32_t)(m->mtz2.atn ? 1u : 0u) << 16);
}

static void s5l8900_refresh(s5l8900_t *m, uint32_t tb);

void s5l8900_tick(s5l8900_t *m, uint32_t ticks) {
    /*
     * Convert elapsed emulated CPU-clock ticks into timebase ticks at the
     * guest's own CPU:timebase ratio, carrying the remainder so it stays exact
     * rather than drifting. Active execution contributes one such tick per
     * retired instruction; WFI contributes elapsed idle ticks without
     * changing cpu.cycles. Feeding ticks straight into the timebase runs guest
     * time ~68x fast and livelocks the kernel's decrementer.
     */
    if (m->active_host_now && ticks) {
        if (UINT64_MAX - m->active_clock_guest_ticks_since_sync < ticks)
            m->active_clock_guest_ticks_since_sync = UINT64_MAX;
        else
            m->active_clock_guest_ticks_since_sync += ticks;
    }

    uint32_t tb = ticks;
    if (m->cpu_hz && m->tb_hz) {
        m->tb_accum += (uint64_t)ticks * m->tb_hz;
        /*
         * THE EARLY OUT, and the whole reason this function is affordable per
         * instruction. See `level_dirty` in soc.h for the measurement and for
         * why the rest of this function is idempotent over unchanged inputs.
         *
         * Four conditions, and each is load-bearing:
         *
         *   tb_accum < cpu_hz   no timebase tick elapsed, so every `tb`
         *                       below would be 0 and every device's advance a
         *                       no-op. At 412:6 this holds for 68 of every 69
         *                       calls, which is where the time goes.
         *   !level_dirty        no guest access has touched a device since the
         *                       last refresh, so re-deriving the levels would
         *                       reproduce the ones already there.
         *   ext_inputs == seen  and neither has a host, on the three inputs it
         *                       can reach behind the bus. Four loads; see
         *                       ext_inputs() above.
         *   ticks               a caller asking for zero ticks is asking for a
         *                       refresh and nothing else; that is the only
         *                       reason to call with zero, and both WFI and the
         *                       host injection paths depend on it.
         *
         * The modulo below is skipped rather than deferred: tb_accum is
         * already less than cpu_hz here, so `tb_accum %= cpu_hz` would not
         * change it. Time is still carried exactly — the accumulator was
         * advanced above, before this returns.
         *
         * g_tick_eager is the A/B switch, and it is here rather than around
         * the whole function so that "eager" means the pre-change code path
         * exactly: same conversion, same order, same everything below.
         */
        if (ticks && !m->level_dirty && !g_tick_eager &&
            m->tb_accum < m->cpu_hz && ext_inputs(m) == m->ext_seen) return;
        tb = (uint32_t)(m->tb_accum / m->cpu_hz);
        m->tb_accum %= m->cpu_hz;
    }

    s5l8900_refresh(m, tb);
}

bool s5l8900_wake_from_standby(s5l8900_t *m) {
    if (!m || !s5l_pcf50635_in_standby(&m->pmu)) return false;

    /* XNU copied its reset trampoline to the first retained DRAM page before
     * writing OOCSHDWN.GO_STANDBY, then entered an intentional infinite branch.
     * ONKEY powers the ARM core back up from reset; it is not an IRQ capable
     * of escaping that branch. This machine has no low-address DRAM alias, so
     * the hardware reset vector is represented by the actual DRAM base. */
    s5l_pcf50635_wake_onkey(&m->pmu);

    uint64_t cycles = m->cpu.cycles;
    arm_reset(&m->cpu, &m->bus);
    m->cpu.cycles = cycles;
    m->cpu.r[15] = m->ram_base;

    /* CPU translation state changed discontinuously even though retained RAM
     * did not. Flush host-derived execution state and detach the new guest
     * instant from any pre-sleep host-clock anchor. Lifetime evidence counters
     * remain intact, including the monotonic retired-instruction count above. */
    m->batch_pending = 0u;
    m->wfi_pace_yield = false;
    m->active_clock_last_host_ns = 0u;
    m->active_clock_guest_ticks_since_sync = 0u;
    m->active_clock_fraction = 0u;
    m->active_clock_debt_ticks = 0u;
    m->active_clock_anchor_valid = false;
    m->level_dirty = true;
    s5l8900_tick(m, 0u);
    return true;
}

bool s5l8900_set_button(s5l8900_t *m, unsigned which, bool pressed) {
    if (!m) return false;

    if (!s5l_pcf50635_in_standby(&m->pmu)) {
        /* AppleM68Buttons reads the PMU wake reason before dispatching the
         * synthetic Power press. Do not let the host's queued release overtake
         * that read: once INT2 has cleared, the release proceeds through the
         * ordinary GPIO debounce path below. */
        if (which == S5L_BUTTON_HOLD && !pressed &&
            s5l_buttons_held(&m->buttons, S5L_BUTTON_HOLD) &&
            (m->pmu.regs[PCF50635_INT2] & PCF50635_INT2_ONKEYR) != 0u) {
            m->buttons.refused++;
            return false;
        }
        bool accepted = s5l_buttons_set(&m->buttons, &m->gpio, &m->gpioic,
                                        which, pressed);
        if (accepted) s5l8900_tick(m, 0u);
        return accepted;
    }

    if (which >= S5L_BUTTON_COUNT) {
        m->buttons.refused++;
        return false;
    }

    /* The application processor is powered down. A GPIO transition cannot be
     * serviced in this state, and retaining it at the head of a host FIFO
     * would prevent the real wake source behind it from ever arriving. Count
     * and consume it, but do not pretend it reached a GPIO pin or interrupt. */
    if (which != S5L_BUTTON_HOLD || !pressed) {
        m->buttons.sets++;
        return true;
    }

    /* AppleM68Buttons' setPowerState path reads function-wake_button_hold and
     * dispatches a Power press when PMU STAT says ONKEY. Leaving the same edge
     * pending on GPIO line 45 dispatches a second press after debounce; the
     * real-device reproduction went straight back to _ml_arm_sleep. Retain
     * the held electrical level without that duplicate edge, and select the
     * line's auto-flipped release polarity. The app's queued release is then
     * one ordinary, guest-visible GPIO transition. */
    m->buttons.sets++;
    if (!s5l_buttons_held(&m->buttons, S5L_BUTTON_HOLD) &&
        s5l_gpioic_consume_autoflip_level(
            &m->gpioic, s5l_button_line(S5L_BUTTON_HOLD),
            s5l_button_level(S5L_BUTTON_HOLD, true))) {
        m->buttons.pressed |= (uint8_t)(1u << S5L_BUTTON_HOLD);
        m->buttons.edges++;
        s5l_gpio_drive(&m->gpio, s5l_button_pin(S5L_BUTTON_HOLD),
                       s5l_button_level(S5L_BUTTON_HOLD, true));
    }

    return s5l8900_wake_from_standby(m);
}

bool s5l8900_set_touch(s5l8900_t *m, const s5l_mt_contact_t *c,
                       unsigned n) {
    bool accepted;
    if (!m) return false;
    if (m->mtz1.present)
        accepted = s5l_mtz1_set_contacts(&m->mtz1, c, n);
    else
        accepted = s5l_mtz2_set_contacts(&m->mtz2, c, n);
    s5l8900_tick(m, 0u);
    return accepted;
}

/* One implementation of the observable device work, kept out of the public
 * clock converter's common path. Besides avoiding duplication, this split
 * leaves s5l8900_tick() small enough for an optimizing compiler to inline its
 * ticks=1 conversion and early-out into s5l8900_run() without also pulling the
 * entire device graph into the interpreter loop. */
static void s5l8900_refresh(s5l8900_t *m, uint32_t tb) {
    m->level_dirty = false;

    /* Devices advance, then the controllers recompute what the CPU sees. */
    bool timer_irq = s5l_timer_tick(&m->timer, tb);
    s5l_vic_set_line(&m->vic[0], S5L8900_IRQ_TIMER, timer_irq);

    bool clcd_irq = s5l_clcd_tick(&m->clcd, tb);
    s5l_vic_set_line(&m->vic[0], S5L8900_IRQ_CLCD, clcd_irq);

    /*
     * The MBX completion line. Like i2c and spi below, the device completes
     * inside the store that kicks it, so the status is already pending by the
     * time this runs and a zero-tick refresh is enough for the guest to see
     * both the assertion and its own acknowledge.
     */
    s5l_vic_set_line(&m->vic[0], S5L8900_IRQ_MBX, s5l_mbx_irq(&m->mbx));

    bool tvout_irq = s5l_tvout_tick(&m->tvout, tb);
    s5l_vic_set_line(&m->vic[0], S5L8900_IRQ_TVOUT, tvout_irq);

    /* I2C transfers complete synchronously with their command store, then
     * remain level-asserted until the driver's W1C acknowledge. A zero-tick
     * refresh is therefore enough for WFI to observe both assertion and
     * deassertion without advancing guest time. */
    s5l_pcf50635_tick(&m->pmu, tb);
    if (m->mtz1.present) s5l_mtz1_tick(&m->mtz1, tb);
    s5l_vic_set_line(&m->vic[0], S5L8900_IRQ_I2C0,
                     s5l_i2c_irq(&m->i2c[0]));
    s5l_vic_set_line(&m->vic[0], S5L8900_IRQ_I2C1,
                     s5l_i2c_irq(&m->i2c[1]));

    /* SPI words complete inside the store that pushes them, for the same reason
     * and with the same consequence: a zero-tick refresh is all WFI needs to
     * observe both the assertion and the guest's W1C acknowledge. */
    s5l_spi_irq_note(&m->spi[0]);
    s5l_spi_irq_note(&m->spi[1]);
    s5l_spi_irq_note(&m->spi[2]);
    s5l_vic_set_line(&m->vic[0], S5L8900_IRQ_SPI0, s5l_spi_irq(&m->spi[0]));
    s5l_vic_set_line(&m->vic[0], S5L8900_IRQ_SPI1, s5l_spi_irq(&m->spi[1]));
    s5l_vic_set_line(&m->vic[0], S5L8900_IRQ_SPI2, s5l_spi_irq(&m->spi[2]));

    /*
     * uart4's receive FIFO — the ONLY interrupt in this machine whose source is
     * outside the machine. It is a level, asserted while a byte the host pushed
     * is still waiting, and it drops when the guest drains URXH.
     *
     * uart0 runs the identical model and has an identical FIFO, and is
     * deliberately NOT wired: it is the kprintf console, its VIC line is 24,
     * and nothing in this project has any business injecting console input.
     * A line nobody can assert is worse than no line at all, so it does not get
     * one until something needs it.
     *
     * On a run with no host peer this is a refresh of `false` over `false`
     * forever: s5l_uart_rx_push() is the only producer and core/ never calls
     * it. core/tests/test_uart4.c pins that.
     */
    s5l_vic_set_line(&m->vic[0], S5L8900_IRQ_UART4, s5l_uart_rx_irq(&m->uart4));

    /*
     * The two DMA controllers, and the ONLY device in this machine that reaches
     * back through the bus while it runs. It is given `&m->bus` rather than
     * holding it, so that the pointer it uses is always the one installed NOW:
     * tools/bootkernel.c interposes on m->bus AFTER s5l8900_init returns, and a
     * copy taken at init would route every DMA store past that interposer —
     * which is precisely the tap the guest's audio capture is listening on.
     *
     * This is before the GPIO cascade for no reason other than that both DMAC
     * lines are VIC0's and neither feeds the cascade; the order is free.
     */
    /* Before the controllers, so a port that can drain has drained and the
     * request line below reads true rather than stale. */
    for (unsigned i = 0; i < S5L8900_SPI_COUNT; i++) s5l_spi_step(&m->spi[i]);
    i2s_tx_advance(m, tb);

    for (unsigned i = 0; i < S5L8900_DMAC_COUNT; i++) {
        bool dmac_irq = s5l_pl080_run(&m->dmac[i], &m->bus, dma_dst_ready, m);
        s5l_vic_set_line(&m->vic[0],
                         i == 0u ? S5L8900_IRQ_DMAC0 : S5L8900_IRQ_DMAC1,
                         dmac_irq);
    }
    i2s_tx_settle(m);

    /*
     * The GPIO interrupt cascade. Seven group outputs, seven VIC lines, and
     * two of those lines are 32 and 33 — the FIRST sources this machine has
     * ever routed to VIC1. s5l_vic_set_line() takes a per-controller 0-31 line
     * and silently drops anything larger, so the flat device-tree number has
     * to be split here; passing 33 to vic[0] would enable nothing and fail
     * without a symptom.
     */
    /* Device interrupt lines first, so the cascade below sees them in the same
     * refresh. */
    /* `/arm-io/i2c0/pmu` declares `interrupts {85, 1}`: GPIOIC line 85,
     * level-sensitive and active low. This is the PMU chip's INT_N output,
     * distinct from the I2C controller's transfer-complete VIC interrupt. */
    s5l_gpioic_set_line(&m->gpioic, S5L_GPIOIC_LINE_PMU,
                        !s5l_pcf50635_irq(&m->pmu));

    /* Touch attention is asserted only when the device has a frame waiting,
     * which nothing produces yet. */
    s5l_gpioic_set_line(&m->gpioic, S5L_GPIOIC_LINE_MULTITOUCH,
                        s5l_mtz2_irq(&m->mtz2));
    if (m->mtz1.present)
        s5l_gpioic_set_line(&m->gpioic, S5L_GPIOIC_LINE_MULTITOUCH_M68,
                            s5l_mtz1_irq(&m->mtz1));

    /*
     * The five switches, refreshed for the same reason: the guest's own stores
     * to INTTYPE and INTLEVEL change what a level line asserts, and a switch
     * that has not moved must still be presenting its level when that happens.
     * s5l_buttons_set() has already driven any change at the exact instruction
     * the host chose, so this is idempotent — re-driving an unchanged level
     * latches nothing on either an edge line or a level one.
     */
    s5l_buttons_apply(&m->buttons, &m->gpio, &m->gpioic);
    i2s_lrck_drive(m, tb);

    for (unsigned g = 0; g < S5L_GPIOIC_GROUPS; g++) {
        unsigned line = s5l_gpioic_cascade(g);
        if (line >= 32u * S5L8900_VIC_COUNT) continue;
        s5l_vic_set_line(&m->vic[line / 32u], line % 32u,
                         s5l_gpioic_group_irq(&m->gpioic, g));
    }
    /*
     * BOTH VICs drive the CPU.
     *
     * Only VIC0 used to, which was fine while VIC0 was the only one mapped. It
     * is not defensible now: the device tree numbers interrupts flat across the
     * pair — /arm-io/gpio lists lines 0x20 and 0x21, /arm-io/wdt 0x33,
     * /arm-io/sdio 0x2a, /arm-io/edgeic 0x23 and 0x29 — and every one of those
     * is past VIC0's 32 lines, so it lives on VIC1. Leaving VIC1's outputs
     * disconnected would mean the watchdog, the SD controller and the GPIO
     * interrupt controller could be correctly programmed, correctly asserted,
     * and still never reach the CPU: a silent failure, and precisely the kind
     * this core exists to avoid.
     *
     * OR-ing the two is the standard PL192 cascade. Nothing asserts a VIC1 line
     * today, so this changes no behaviour now; it removes a trap for the next
     * device that needs a line above 31.
     */
    bool irq = false, fiq = false;
    for (unsigned i = 0; i < S5L8900_VIC_COUNT; i++) {
        irq |= s5l_vic_irq(&m->vic[i]);
        fiq |= s5l_vic_fiq(&m->vic[i]);
    }
    m->cpu.irq_line = irq;
    m->cpu.fiq_line = fiq;

    /* Sampled LAST, not first: s5l_buttons_apply() above can move a pin whose
     * watcher is s5l_mtz2_reset_pin(), and that clears the attention line this
     * word carries. Recording what the refresh started from would leave the
     * next call convinced a host had moved it. */
    m->ext_seen = ext_inputs(m);
}

/* No execution path may defer the device graph across the first timebase edge
 * the literal arm_step()+s5l8900_tick(1) loop would have observed. Dirty levels
 * and host inputs force the ordinary one-instruction path first. This bound is
 * shared by the signed engine and the interpreter batch below; it is timing
 * policy, not an engine-specific optimization.
 */
static unsigned retirement_batch_limit(const s5l8900_t *m,
                                        unsigned remaining) {
    uint64_t until_edge;
    if (!remaining || g_tick_eager || m->level_dirty ||
        ext_inputs(m) != m->ext_seen || !m->cpu_hz || !m->tb_hz ||
        m->tb_hz > m->cpu_hz || m->tb_accum >= m->cpu_hz)
        return 0u;

    until_edge = ((uint64_t)m->cpu_hz - m->tb_accum + m->tb_hz - 1u) /
                 m->tb_hz;
    if (until_edge < remaining) remaining = (unsigned)until_edge;
    return remaining;
}

/*
 * Active wall-clock mode no longer needs an artificial retirement boundary at
 * each 6 MHz timebase edge: the next host sample advances every elapsed edge
 * together.  Device dirtiness and host inputs remain immediate boundaries.
 * The fixed ceiling limits both interrupt latency and clock callback cost.
 */
static unsigned run_retirement_batch_limit(const s5l8900_t *m,
                                           unsigned remaining,
                                           bool active_clock) {
    if (!active_clock) return retirement_batch_limit(m, remaining);
    if (!remaining || m->level_dirty || ext_inputs(m) != m->ext_seen)
        return 0u;
    if (remaining > S5L8900_ACTIVE_CLOCK_BATCH_INSNS)
        remaining = S5L8900_ACTIVE_CLOCK_BATCH_INSNS;
    return remaining;
}

/*
 * Collapse only the calls to s5l8900_tick(), never ARM execution itself.
 *
 * WFI can advance device time from inside arm_step(), so the pending
 * retirement count is flushed before the wait. A privileged host SVC can do
 * the same through an opaque callback, so its presence disables this path.
 * A pre-step hook can have a target anywhere in the interval, and the signed
 * engine owns its own boundary contract, so either policy disables this path
 * completely.
 */
static unsigned interpreter_tick_batch_limit(const s5l8900_t *m,
                                              unsigned remaining,
                                              bool active_clock) {
    if (m->pre_step_hook ||
        (m->cpu.bus && m->cpu.bus->privileged_svc_handler))
        return 0u;
    return run_retirement_batch_limit(m, remaining, active_clock);
}


static void active_clock_counter_add(uint64_t *counter, uint64_t value) {
    if (UINT64_MAX - *counter < value) *counter = UINT64_MAX;
    else *counter += value;
}

/*
 * Convert host nanoseconds plus the carried billionth-of-a-tick fraction into
 * whole guest CPU ticks without multiplying the potentially unbounded whole
 * interval by cpu_hz.  false means the whole-tick result cannot fit uint64_t;
 * callers can still apply their bounded catch-up policy without trusting a
 * wrapped value.
 */
static bool active_clock_elapsed_ticks(uint64_t elapsed_ns, uint32_t cpu_hz,
                                       uint64_t fraction,
                                       uint64_t *whole_ticks,
                                       uint64_t *next_fraction) {
    const uint64_t billion = UINT64_C(1000000000);
    uint64_t seconds = elapsed_ns / billion;
    uint64_t sub_ns = elapsed_ns % billion;

    if (!cpu_hz || !whole_ticks || !next_fraction || fraction >= billion ||
        seconds > UINT64_MAX / cpu_hz)
        return false;

    uint64_t whole = seconds * cpu_hz;
    /* sub_ns < 1e9 and cpu_hz is uint32_t, so this product plus fraction is
     * below 4.3e18 and cannot overflow uint64_t. */
    uint64_t scaled_sub = sub_ns * cpu_hz + fraction;
    uint64_t carry = scaled_sub / billion;
    if (whole > UINT64_MAX - carry) return false;

    *whole_ticks = whole + carry;
    *next_fraction = scaled_sub % billion;
    return true;
}

/*
 * Synchronize active guest time to one monotonic host sample. `fallback_ticks`
 * are the successfully retired instructions not yet represented in guest
 * time. If the host clock cannot provide a trustworthy sample, those ticks
 * are applied through the old exact contract and the caller disables active
 * timing for the rest of this run call.
 */
static bool active_host_clock_sync(s5l8900_t *m,
                                   uint32_t fallback_ticks) {
    uint64_t now_ns = 0u;
    if (!m->active_host_now || !m->cpu_hz ||
        !m->active_host_now(m->active_host_now_ctx, &now_ns) ||
        (m->active_clock_anchor_valid &&
         now_ns < m->active_clock_last_host_ns)) {
        active_clock_counter_add(&m->active_clock_failures, 1u);
        m->active_clock_anchor_valid = false;
        m->active_clock_last_host_ns = 0u;
        m->active_clock_fraction = 0u;
        m->active_clock_debt_ticks = 0u;
        m->active_clock_guest_ticks_since_sync = 0u;
        s5l8900_tick(m, fallback_ticks);
        return false;
    }

    if (!m->active_clock_anchor_valid) {
        m->active_clock_last_host_ns = now_ns;
        m->active_clock_guest_ticks_since_sync = 0u;
        m->active_clock_fraction = 0u;
        m->active_clock_debt_ticks = 0u;
        m->active_clock_anchor_valid = true;
        /* Establish the same level/input boundary the first ordinary device
         * tick in this run would have supplied, without manufacturing time. */
        s5l8900_tick(m, 0u);
        return true;
    }

    uint64_t elapsed_ns = now_ns - m->active_clock_last_host_ns;
    m->active_clock_last_host_ns = now_ns;
    uint64_t prior_fraction = m->active_clock_fraction;
    uint64_t observed_ticks = m->active_clock_guest_ticks_since_sync;
    uint64_t due_ticks = 0u, due_fraction = 0u;
    uint64_t max_added_ticks = 0u, max_fraction = 0u;
    bool converted = active_clock_elapsed_ticks(
        elapsed_ns, m->cpu_hz, prior_fraction, &due_ticks, &due_fraction);
    /* This conversion is guaranteed to fit: the interval is 8 ms and the
     * frequency is uint32_t. Keeping it in the same helper pins the fraction
     * semantics to the ordinary, unclamped path. */
    bool max_converted = active_clock_elapsed_ticks(
        S5L8900_ACTIVE_CLOCK_MAX_STEP_NS, m->cpu_hz, prior_fraction,
        &max_added_ticks, &max_fraction);
    uint64_t added_ticks = 0u;
    /* Foreground carry keeps complete elapsed time; only an individual device
     * update is bounded. An unrepresentable interval fails closed like an
     * invalid host sample instead of wrapping or inventing elapsed time. */
    if (m->active_clock_carry) {
        /* Conversion fractions belong to elapsed time, not the payment cap.
         * The carry branch never rounds an individual update above 8ms. */
        max_added_ticks = (S5L8900_ACTIVE_CLOCK_MAX_STEP_NS * m->cpu_hz) /
                          UINT64_C(1000000000);
        if (!converted || !max_converted || m->cpu_hz < 125u ||
            UINT64_MAX - due_ticks < m->active_clock_debt_ticks) {
            active_clock_counter_add(&m->active_clock_failures, 1u);
            m->active_clock_anchor_valid = false;
            m->active_clock_last_host_ns = 0u;
            m->active_clock_guest_ticks_since_sync = 0u;
            m->active_clock_fraction = 0u;
            m->active_clock_debt_ticks = 0u;
            s5l8900_tick(m, fallback_ticks);
            return false;
        }
        due_ticks += m->active_clock_debt_ticks;
        uint64_t unpaid = due_ticks > observed_ticks ? due_ticks - observed_ticks : 0u;
        added_ticks = unpaid > max_added_ticks ? max_added_ticks : unpaid;
        m->active_clock_debt_ticks = unpaid - added_ticks;
        if (unpaid > max_added_ticks)
            active_clock_counter_add(&m->active_clock_clamps, 1u);
        m->active_clock_fraction = due_fraction;
        active_clock_counter_add(&m->active_clock_updates, 1u);
        active_clock_counter_add(&m->active_clock_added_ticks, added_ticks);
        s5l8900_tick(m, (uint32_t)added_ticks);
        m->active_clock_guest_ticks_since_sync = 0u;
        return true;
    }
    bool clamp = !converted || !max_converted;

    if (!clamp && due_ticks > observed_ticks) {
        added_ticks = due_ticks - observed_ticks;
        /* Cap only host time that the guest has not already advanced. Paced
         * WFI ticks are observed above; clipping the raw host interval before
         * subtracting them discarded normal scheduler oversleep every slice
         * and made idle guest time fall progressively behind wall time. */
        clamp = added_ticks > max_added_ticks ||
                (added_ticks == max_added_ticks &&
                 due_fraction > max_fraction);
    }
    if (clamp) {
        added_ticks = max_added_ticks;
        due_fraction = max_fraction;
        active_clock_counter_add(&m->active_clock_clamps, 1u);
    }
    m->active_clock_fraction = due_fraction;

    active_clock_counter_add(&m->active_clock_updates, 1u);
    active_clock_counter_add(&m->active_clock_added_ticks, added_ticks);

    /* added_ticks is bounded by 8 ms at a uint32_t CPU rate, hence it fits the
     * public tick width. A zero call is still required to refresh MMIO levels
     * and newly arrived host inputs at this synchronization boundary. */
    s5l8900_tick(m, (uint32_t)added_ticks);
    m->active_clock_guest_ticks_since_sync = 0u;
    return true;
}

static void run_clock_retired(s5l8900_t *m, bool *active_clock,
                              unsigned *pending_retired, unsigned retired,
                              bool force_clock_sync,
                              bool force_device_refresh) {
    if (!*active_clock) {
        s5l8900_tick(m, retired);
        return;
    }

    *pending_retired += retired;
    if (!force_clock_sync &&
        *pending_retired < S5L8900_ACTIVE_CLOCK_SAMPLE_INSNS) {
        /* Signed negative witnesses, MMIO and mode exits still require the
         * post-retirement device boundary they historically received. A zero
         * tick supplies that exact refresh without calling clock_gettime tens
         * of millions of times per second. Retirements remain pending for the
         * next periodic host-time sample. */
        if (force_device_refresh) s5l8900_tick(m, 0u);
        return;
    }

    unsigned fallback_ticks = *pending_retired;
    *pending_retired = 0u;
    if (!active_host_clock_sync(m, (uint32_t)fallback_ticks))
        *active_clock = false;
}


unsigned s5l8900_run(s5l8900_t *m, unsigned max_steps, arm_status_t *status) {
#ifdef S5LBOX_ADAPTIVE_JIT
    /* A latched fatal result must also block single-step and hook paths.
     * Healthy zero-budget admission is required to have no guest effects. */
    arm_adaptive_result_t adaptive_probe;
    if (arm_adaptive_enabled &&
        arm_adaptive_try(&m->cpu, 0u, &adaptive_probe) < 0) {
        if (status) *status = ARM_HALT;
        return 0u;
    }
#endif
    arm_status_t st = ARM_OK;
    unsigned n = 0;
    unsigned active_pending_retired = 0u;
    bool active_clock = false;
    /* A direct arm_step() may have used the same machine between run calls.
     * Only a wait reached during THIS bounded slice may shorten it. */
    m->batch_pending = 0u;
    m->wfi_pace_yield = false;
    if (max_steps && m->active_host_now)
        active_clock = active_host_clock_sync(m, 0u);
    while (n < max_steps) {
        if (pre_step_target_matches(m, m->cpu.r[15])) {
            m->pre_step_matches++;
            if (m->pre_step_hook(m->pre_step_ctx)) {
                m->pre_step_handled++;
                /* A replacement completes a whole guest operation but does
                 * not pretend those skipped instructions retired.  One device
                 * tick matches bootkernel's established HLE timing contract. */
                run_clock_retired(m, &active_clock,
                                  &active_pending_retired, 1u,
                                  false,
                                  m->level_dirty ||
                                  ext_inputs(m) != m->ext_seen);
                continue;
            }
        }

        /* A limit of one cannot save a tick call, so retain the smaller
         * ordinary path at the edge itself. Inside a real batch, inspect every
         * retirement boundary for the exact events the public tick would have
         * observed: guest MMIO, a host-driven input, WFI, or another host-time
         * callback. The final lump contains only successfully retired
         * instructions; a non-OK arm_step() receives no device tick, matching
         * the literal loop exactly. */
        unsigned limit = interpreter_tick_batch_limit(
            m, max_steps - n, active_clock);
        if (limit > 1u) {
            unsigned retired = 0u;
            unsigned total_retired = 0u;
            bool flushed = false;
#ifdef S5LBOX_ADAPTIVE_JIT
            bool adaptive_boundary = false;
            if (arm_adaptive_enabled) {
            do {
                /* Never enlarge the original batch after an internal edge.
                 * Repeat machine admission before trying the successor; CPU,
                 * fetch and code admission remain the runtime's ownership. */
                unsigned remaining = interpreter_tick_batch_limit(
                    m, limit - retired, active_clock);
                if (!remaining) break;
                arm_adaptive_result_t result;
                int invoked = arm_adaptive_try(&m->cpu, remaining, &result);
                if (invoked < 0) {
                    st = ARM_HALT;
                    m->batch_pending = 0u;
                    break;
                }
                if (invoked > 0) {
                    retired += result.retired;
                    total_retired += result.retired;
                    if (result.reason == ADAPT_EXIT_BOUNDARY) {
                        adaptive_boundary = true;
                        break;
                    }
                    if (result.reason == ADAPT_EXIT_BUDGET || retired == limit)
                        break;
                    if (result.reason == ADAPT_EXIT_EDGE)
                        continue;
                }
                /* Refusal is BEFORE the instruction. The complete original
                 * step owns fetch, faults, cycles and partial memory effects. */
                unsigned retired_before = retired;
                m->batch_pending = retired_before;
                st = arm_step(&m->cpu);
                if (st != ARM_OK) {
                    m->batch_pending = 0u;
                    break;
                }
                flushed = m->batch_pending != retired_before;
                retired = m->batch_pending + 1u;
                total_retired++;
                if (m->level_dirty || ext_inputs(m) != m->ext_seen ||
                    flushed || m->wfi_pace_yield)
                    break;
            } while (retired < limit);
            } else
#endif
#ifdef S5LBOX_WASM_JIT
            if (arm_wasm_jit_enabled) {
            do {
                unsigned prefix = arm_wasm_jit_try(&m->cpu, limit - retired);
                retired += prefix;
                total_retired += prefix;
                if (retired == limit) break;
                unsigned retired_before = retired;
                m->batch_pending = retired_before;
                st = arm_step(&m->cpu);
                if (st != ARM_OK) {
                    m->batch_pending = 0u;
                    break;
                }
                flushed = m->batch_pending != retired_before;
                retired = m->batch_pending + 1u;
                total_retired++;
                if (m->level_dirty || ext_inputs(m) != m->ext_seen ||
                    flushed || m->wfi_pace_yield)
                    break;
            } while (retired < limit);
            } else
#endif
            {
            do {
                unsigned retired_before = retired;
                m->batch_pending = retired_before;
                st = arm_step(&m->cpu);
                if (st != ARM_OK) {
                    m->batch_pending = 0u;
                    break;
                }
                flushed = m->batch_pending != retired_before;
                retired = m->batch_pending + 1u;
                total_retired++;
                if (m->level_dirty || ext_inputs(m) != m->ext_seen ||
                    flushed || m->wfi_pace_yield)
                    break;
            } while (retired < limit);
            }

            if (total_retired) {
                n += total_retired;
                m->interpreter_tick_batches++;
                m->interpreter_tick_batched_retired += total_retired;
                run_clock_retired(m, &active_clock,
                                  &active_pending_retired, retired,
                                  st != ARM_OK || m->wfi_pace_yield,
                                  m->level_dirty ||
                                  ext_inputs(m) != m->ext_seen ||
#ifdef S5LBOX_ADAPTIVE_JIT
                                  adaptive_boundary ||
#endif
                                  flushed || m->wfi_pace_yield);
            }
            if (st != ARM_OK) break;
            if (m->wfi_pace_yield) {
                m->wfi_pace_yield = false;
                break;
            }
            m->batch_pending = 0u;
            continue;
        }

        st = arm_step(&m->cpu);
        if (st != ARM_OK) break;
        n++;
        run_clock_retired(m, &active_clock, &active_pending_retired, 1u,
                          m->wfi_pace_yield,
                          m->level_dirty ||
                          ext_inputs(m) != m->ext_seen);
        if (m->wfi_pace_yield) {
            m->wfi_pace_yield = false;
            break;
        }
    }
    if (active_clock && active_pending_retired)
        (void)active_host_clock_sync(
            m, (uint32_t)active_pending_retired);
    if (m->uart4_host_service)
        m->uart4_host_service(m->uart4_host_ctx, n);
    m->batch_pending = 0u;
    if (status) *status = st;
    return n;
}

uint64_t s5l8900_interpreter_tick_batches(const s5l8900_t *m) {
    return m ? m->interpreter_tick_batches : 0u;
}

uint64_t s5l8900_interpreter_tick_batched_retired(const s5l8900_t *m) {
    return m ? m->interpreter_tick_batched_retired : 0u;
}
