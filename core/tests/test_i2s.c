/*
 * The I2S transmit stream: the frame clock AppleS5L8900XI2SController waits
 * on before it starts a transfer, and the FIFO that paces the PL080 at the
 * sample rate. See s5l_i2s_stream_t in soc.h for what is measured and what is
 * an approximation.
 */
#include "soc.h"
#include <stdio.h>
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

#define I2S1      S5L8900_I2S1_BASE
#define TX_FIFO   (S5L8900_I2S1_BASE + S5L_I2S_TX_FIFO_OFF)
#define CH(n, r)  (0x100u + 0x20u * (n) + (r))
#define LRCK_GROUP (S5L_GPIOIC_LINE_I2S1_LRCK >> 5)
#define LRCK_BIT   (1u << (S5L_GPIOIC_LINE_I2S1_LRCK & 31u))

static void w32(s5l8900_t *m, uint32_t a, uint32_t v) {
    m->bus.write32(m->bus.ctx, a, v);
}

/* Advance guest time by `tb` timebase ticks, in short refreshes. */
static void advance_tb(s5l8900_t *m, uint32_t tb) {
    uint32_t per = m->cpu_hz / m->tb_hz;
    for (uint32_t i = 0; i < tb; i++) s5l8900_tick(m, per + 1u);
}

/* configure() on i2s1, as 1A543a writes it for the baseband path. */
static void configure_i2s1(s5l8900_t *m) {
    w32(m, I2S1 + 0x00u, 1u);
    w32(m, I2S1 + 0x40u, 0u);
    w32(m, I2S1 + 0x04u, 0x01100301u);
    w32(m, I2S1 + 0x30u, 0u);
    w32(m, I2S1 + 0x08u, 0u);
    w32(m, I2S1 + 0x34u, 0u);
    w32(m, I2S1 + 0x3cu, 1u);
}

static uint32_t wake_ticks(const s5l8900_t *m, const char *name) {
    const s5l_wake_source_t *src;
    unsigned n = s5l8900_wake_sources(&src);
    for (unsigned i = 0; i < n; i++) {
        if (strcmp(src[i].name, name)) continue;
        uint32_t at = 0;
        return src[i].next_edge(m, &at) == S5L_WAKE_AT ? at : 0u;
    }
    return 0u;
}

static void test_frame_clock_only_when_configured(void) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, 0u, 1u << 16), "init failed");
    m.gpioic.en[LRCK_GROUP] |= LRCK_BIT;
    advance_tb(&m, 1000u);
    CHECK(!(m.gpioic.driven[LRCK_GROUP] & LRCK_BIT),
          "an unconfigured port drove its frame-clock line");
    CHECK(wake_ticks(&m, "gpio-group5") == 0u,
          "an unconfigured port named a frame-clock wake edge");

    configure_i2s1(&m);
    uint32_t at = wake_ticks(&m, "gpio-group5");
    CHECK(at > 0u && at <= S5L8900_TB_HZ / S5L_I2S_FRAME_HZ + 1u,
          "next rising edge %u ticks away", at);
    /* Two callbacks move the driver's state 1 -> 2 -> 3. */
    unsigned edges = 0;
    for (unsigned i = 0; i < 400u; i++) {
        advance_tb(&m, 1u);
        if (m.gpioic.stat[LRCK_GROUP] & LRCK_BIT) {
            edges++;
            m.gpioic.stat[LRCK_GROUP] &= ~LRCK_BIT;
        }
    }
    CHECK(edges >= 2u, "only %u frame-clock edges in 400 ticks", edges);
    uint64_t per_sec = m.i2s_tx[1].lrck_rising * S5L8900_TB_HZ / 400u;
    CHECK(per_sec + 2u * S5L8900_TB_HZ / 400u >= S5L_I2S_FRAME_HZ &&
          per_sec <= S5L_I2S_FRAME_HZ + 2u * S5L8900_TB_HZ / 400u,
          "frame clock ran at %llu Hz", (unsigned long long)per_sec);
    s5l8900_free(&m);
}

static void test_dma_is_paced_by_the_fifo(void) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, 0u, 1u << 16), "init failed");
    for (unsigned i = 0; i < 128u; i++) m.ram[0x100u + i] = (uint8_t)(i * 7u + 1u);
    configure_i2s1(&m);

    /* start(): the DMA is submitted before the transmitter runs. */
    w32(&m, S5L8900_DMAC1_BASE + 0x30u, 1u);
    w32(&m, S5L8900_DMAC1_BASE + CH(5, 0x00u), 0x100u);
    w32(&m, S5L8900_DMAC1_BASE + CH(5, 0x04u), TX_FIFO);
    w32(&m, S5L8900_DMAC1_BASE + CH(5, 0x08u), 0u);
    w32(&m, S5L8900_DMAC1_BASE + CH(5, 0x0cu),
        0x00249000u | PL080_CTRL_SI | PL080_CTRL_I | 64u);
    w32(&m, S5L8900_DMAC1_BASE + CH(5, 0x10u),
        (1u << PL080_CFG_FLOW_SHIFT) | PL080_CFG_ITC | PL080_CFG_EN);
    advance_tb(&m, 2000u);
    CHECK(m.i2s_tx[1].fifo_level == S5L_I2S_TX_FIFO_BYTES,
          "stopped transmitter: FIFO holds %u bytes", m.i2s_tx[1].fifo_level);
    CHECK(m.dmac[1].ch[5].cfg & PL080_CFG_EN,
          "the channel completed into a stopped transmitter");
    CHECK(s5l8900_audio_available(&m) == 0u, "stopped transmitter shifted");
    CHECK(wake_ticks(&m, "dmac1") == 0u, "stopped transmitter named a wake");

    w32(&m, I2S1 + 0x08u, 6u);
    uint32_t at = wake_ticks(&m, "dmac1");
    /* The item retires when its last store fits: 96 bytes after the FIFO
     * filled, not when the FIFO later empties. */
    uint32_t expect = (uint32_t)((96ull * S5L8900_TB_HZ +
        S5L_I2S_FRAME_HZ * S5L_I2S_FRAME_BYTES - 1u) /
        (S5L_I2S_FRAME_HZ * S5L_I2S_FRAME_BYTES));
    CHECK(at >= expect - 2u && at <= expect + 2u,
          "dmac wake %u ticks, expected about %u", at, expect);
    advance_tb(&m, expect - 300u);
    CHECK(m.dmac[1].ch[5].cfg & PL080_CFG_EN,
          "the item retired faster than the sample clock");
    advance_tb(&m, 400u);
    CHECK(!(m.dmac[1].ch[5].cfg & PL080_CFG_EN),
          "the item did not retire at the sample clock");
    CHECK(m.dmac[1].raw_tc & (1u << 5), "no terminal count");
    CHECK(m.i2s_tx[1].overflow_bytes == 0u, "DMA overflowed the FIFO");

    CHECK(m.i2s_tx[1].fifo_level > 0u,
          "the FIFO was empty at terminal count");
    advance_tb(&m, 1200u);   /* 32 bytes at the byte rate: ~1088 ticks */
    CHECK(m.i2s_tx[1].fifo_level == 0u, "the FIFO did not drain");
    CHECK(m.i2s_tx[1].underruns > 0u, "an empty running FIFO is an underrun");
    int16_t pcm[64];
    size_t frames = s5l8900_audio_read(&m, pcm, 64u);
    CHECK(frames == 32u, "%zu frames shifted out", frames);
    CHECK(!memcmp(pcm, m.ram + 0x100u, 128u),
          "shifted PCM differs from the DMA source");
    s5l8900_free(&m);
}

/* A whole idle interval skipped by WFI still retires the item: credit is
 * consumed by the refill in the same refresh rather than lost. */
static void test_one_long_refresh_keeps_time(void) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, 0u, 1u << 16), "init failed");
    configure_i2s1(&m);
    w32(&m, S5L8900_DMAC1_BASE + 0x30u, 1u);
    w32(&m, S5L8900_DMAC1_BASE + CH(5, 0x00u), 0x100u);
    w32(&m, S5L8900_DMAC1_BASE + CH(5, 0x04u), TX_FIFO);
    w32(&m, S5L8900_DMAC1_BASE + CH(5, 0x0cu),
        0x00249000u | PL080_CTRL_SI | PL080_CTRL_I | 512u);
    w32(&m, S5L8900_DMAC1_BASE + CH(5, 0x10u),
        (1u << PL080_CFG_FLOW_SHIFT) | PL080_CFG_ITC | PL080_CFG_EN);
    w32(&m, I2S1 + 0x08u, 6u);
    s5l8900_tick(&m, 0u);
    uint32_t at = wake_ticks(&m, "dmac1");
    CHECK(at > 0u, "no dmac wake for a running transmitter");
    s5l8900_tick(&m, (uint32_t)((uint64_t)at * m.cpu_hz / m.tb_hz + 1u));
    CHECK(!(m.dmac[1].ch[5].cfg & PL080_CFG_EN),
          "the item was not retired at its named wake edge");
    CHECK(s5l8900_audio_available(&m) +
          m.i2s_tx[1].fifo_level / S5L_I2S_FRAME_BYTES == 256u,
          "%zu frames shifted, %u bytes still queued",
          s5l8900_audio_available(&m), m.i2s_tx[1].fifo_level);
    s5l8900_free(&m);
}

int main(void) {
    test_frame_clock_only_when_configured();
    test_dma_is_paced_by_the_fifo();
    test_one_long_refresh_keeps_time();
    printf("test_i2s: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
