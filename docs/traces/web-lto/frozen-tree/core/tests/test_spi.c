/*
 * S5LBox — S5L8900 SPI controller focused tests.
 *
 * The property that matters most here is not that a byte moves. It is that the
 * interrupt this controller raises is one the stock handler will act on: the
 * handler body reads STATUS, and when the receive-level field is zero it
 * returns without acknowledging anything, so nothing calls finishTransfer and
 * nothing calls commandWakeup. A model that raises the line over an empty
 * receive FIFO reproduces the unbounded IOCommandGate::commandSleep() the
 * multitouch kext has been stuck in rather than ending it, and would look
 * entirely healthy from every other angle. Several tests below exist only to
 * make that state representable and distinguishable.
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

/* A slave that answers with a value derived from what it was sent, so a test
 * can tell a real round trip from a fabricated zero. */
typedef struct { uint8_t last_out; unsigned words; } echo_t;

static uint8_t echo_transfer(void *ctx, uint8_t out) {
    echo_t *e = ctx;
    e->last_out = out;
    e->words++;
    return (uint8_t)(out ^ 0xa5u);
}

static void bind_echo(s5l_spi_slave_t *slave, echo_t *e) {
    memset(slave, 0, sizeof *slave);
    memset(e, 0, sizeof *e);
    slave->ctx = e;
    slave->transfer = echo_transfer;
}

static uint32_t tx_level(const s5l_spi_t *bus) {
    uint32_t s = s5l_spi_read((s5l_spi_t *)bus, SPI_STATUS);
    return (s >> SPI_STATUS_TX_SHIFT) & SPI_STATUS_LEVEL;
}
static uint32_t rx_level(const s5l_spi_t *bus) {
    uint32_t s = s5l_spi_read((s5l_spi_t *)bus, SPI_STATUS);
    return (s >> SPI_STATUS_RX_SHIFT) & SPI_STATUS_LEVEL;
}

/* ------------------------------------------------------------------------- */

static void test_reset_and_attachment_are_bounded(void) {
    s5l_spi_t bus, expected;
    memset(&bus, 0x5a, sizeof bus);
    memset(&expected, 0, sizeof expected);
    s5l_spi_reset(&bus);
    CHECK(memcmp(&bus, &expected, sizeof bus) == 0,
          "reset did not totally initialize a poisoned object");

    echo_t e;
    s5l_spi_slave_t slave;
    bind_echo(&slave, &e);
    CHECK(!s5l_spi_attach(NULL, 0u, &slave), "NULL bus accepted");
    CHECK(!s5l_spi_attach(&bus, 0u, NULL), "NULL slave accepted");
    CHECK(!s5l_spi_attach(&bus, S5L_SPI_SLAVES, &slave),
          "out-of-range chip select accepted");
    s5l_spi_slave_t silent;
    memset(&silent, 0, sizeof silent);
    CHECK(!s5l_spi_attach(&bus, 0u, &silent),
          "slave without a transfer callback accepted");

    CHECK(s5l_spi_attach(&bus, 0u, &slave), "first device refused");
    CHECK(!s5l_spi_attach(&bus, 0u, &slave),
          "a second device silently shadowed chip select 0");
    for (unsigned cs = 1; cs < S5L_SPI_SLAVES; cs++)
        CHECK(s5l_spi_attach(&bus, cs, &slave),
              "chip select %u refused", cs);

    s5l_spi_reset(&bus);
    CHECK(bus.slaves[0].transfer == NULL && bus.cs == 0u,
          "reset preserved host callback wiring");

    /* Every entry point must survive a NULL controller: the machine's routing
     * is the only caller today, but these are public. */
    CHECK(s5l_spi_read(NULL, SPI_STATUS) == 0u && !s5l_spi_irq(NULL),
          "NULL controller helpers were unsafe");
    s5l_spi_write(NULL, SPI_CONTROL, 0u);
    s5l_spi_null_bind(NULL);
    s5l_spi_lcd_bind(NULL, NULL);
}

/*
 * The bit positions of the two level fields, which are the fields the stock
 * handler decodes. Swapping them is the single easiest way to build a model
 * that passes a data round trip and still never wakes the guest, so they are
 * pinned separately, asymmetrically, and against literal shift counts rather
 * than against the macros the model itself uses.
 */
static void test_status_level_fields_sit_where_the_driver_looks(void) {
    s5l_spi_t bus;
    echo_t e;
    s5l_spi_slave_t slave;
    s5l_spi_reset(&bus);
    bind_echo(&slave, &e);
    CHECK(s5l_spi_attach(&bus, 0u, &slave), "attach failed");

    CHECK(s5l_spi_read(&bus, SPI_STATUS) == 0u, "STATUS nonzero at reset");

    /* Three words in, none drained: receive level 3, transmit level 0 because
     * the shifter emptied the transmit FIFO as fast as it was filled. */
    for (unsigned i = 0; i < 3u; i++)
        s5l_spi_write(&bus, SPI_TXDATA, 0x10u + i);
    uint32_t status = s5l_spi_read(&bus, SPI_STATUS);
    CHECK(((status >> 8) & 0xfu) == 3u,
          "receive level must be bits[11:8]; status=%08x", status);
    CHECK(((status >> 4) & 0xfu) == 0u,
          "transmit level must be bits[7:4] and must be empty; status=%08x",
          status);
    /* Both fields are OCCUPANCY. The driver's transmit decoder subtracts from
     * the depth itself (`rsb r0, r0, #8`), so publishing free space here would
     * make a drained FIFO look full and the filter would never refill it. */
    CHECK(S5L_SPI_FIFO_DEPTH - ((status >> 4) & 0xfu) == S5L_SPI_FIFO_DEPTH,
          "the driver would compute %u bytes of transmit room in an empty "
          "FIFO", S5L_SPI_FIFO_DEPTH - ((status >> 4) & 0xfu));
    CHECK((status & 0xfu) != 0u,
          "a completed word must latch an event bit inside 0x0f; status=%08x",
          status);
    CHECK((status & ~0xfffu) == 0u,
          "STATUS produced bits outside events+levels: %08x", status);

    /* Now make the two fields differ, by filling the receive FIFO and then
     * pushing more: the extra words can only sit in the transmit FIFO. */
    for (unsigned i = 3u; i < S5L_SPI_FIFO_DEPTH; i++)
        s5l_spi_write(&bus, SPI_TXDATA, 0x10u + i);
    s5l_spi_write(&bus, SPI_TXDATA, 0xf0u);
    s5l_spi_write(&bus, SPI_TXDATA, 0xf1u);
    status = s5l_spi_read(&bus, SPI_STATUS);
    CHECK(((status >> 8) & 0xfu) == S5L_SPI_FIFO_DEPTH &&
          ((status >> 4) & 0xfu) == 2u,
          "levels rx=%u tx=%u, expected rx=%u tx=2; status=%08x",
          (status >> 8) & 0xfu, (status >> 4) & 0xfu,
          S5L_SPI_FIFO_DEPTH, status);
    CHECK(S5L_SPI_FIFO_DEPTH - ((status >> 4) & 0xfu) == 6u,
          "the driver would compute %u bytes of transmit room with two queued",
          S5L_SPI_FIFO_DEPTH - ((status >> 4) & 0xfu));
}

/*
 * Depth eight, on both sides, and the exact number run59 measured: the driver
 * pushed 8 bytes of its 16-byte probe before it stopped and slept. Overflow of
 * either FIFO must be bounded and counted, never a wrapped index.
 */
static void test_fifo_depth_boundary_is_eight_and_bounded(void) {
    s5l_spi_t bus;
    echo_t e;
    s5l_spi_slave_t slave;
    s5l_spi_reset(&bus);
    bind_echo(&slave, &e);
    CHECK(s5l_spi_attach(&bus, 0u, &slave), "attach failed");

    CHECK(S5L_SPI_FIFO_DEPTH == 8u,
          "FIFO depth is %u; run59's 19 writes decompose as 11 + 8",
          S5L_SPI_FIFO_DEPTH);

    for (unsigned i = 0; i < S5L_SPI_FIFO_DEPTH; i++)
        s5l_spi_write(&bus, SPI_TXDATA, (uint32_t)i);
    CHECK(rx_level(&bus) == S5L_SPI_FIFO_DEPTH && tx_level(&bus) == 0u,
          "eight words did not fill exactly the receive FIFO");
    CHECK(e.words == S5L_SPI_FIFO_DEPTH,
          "the device saw %u words, expected %u", e.words,
          S5L_SPI_FIFO_DEPTH);

    /* Sixteen more with nothing drained: eight back up in the transmit FIFO
     * and the remaining eight are refused and counted. */
    for (unsigned i = 0; i < 16u; i++)
        s5l_spi_write(&bus, SPI_TXDATA, 0x80u + i);
    CHECK(tx_level(&bus) == S5L_SPI_FIFO_DEPTH &&
          rx_level(&bus) == S5L_SPI_FIFO_DEPTH,
          "a jammed controller did not saturate at depth: tx=%u rx=%u",
          tx_level(&bus), rx_level(&bus));
    CHECK(bus.tx_drops == 8u, "transmit overflow counted %llu, expected 8",
          (unsigned long long)bus.tx_drops);
    CHECK(e.words == S5L_SPI_FIFO_DEPTH,
          "the shifter ran past a full receive FIFO: %u words", e.words);

    /* Draining one word is what makes room, and it must restart the shifter —
     * that is the hardware backpressure the whole transfer depends on. */
    uint32_t got = s5l_spi_read(&bus, SPI_RXDATA);
    CHECK(got == (0x00u ^ 0xa5u), "first received byte was %02x", got);
    CHECK(rx_level(&bus) == S5L_SPI_FIFO_DEPTH &&
          tx_level(&bus) == S5L_SPI_FIFO_DEPTH - 1u,
          "draining did not restart the shifter: tx=%u rx=%u",
          tx_level(&bus), rx_level(&bus));
    CHECK(e.words == S5L_SPI_FIFO_DEPTH + 1u,
          "exactly one further word should have shifted, saw %u", e.words);

    /* Reading an empty receive FIFO is visible rather than silently zero. */
    s5l_spi_reset(&bus);
    CHECK(s5l_spi_read(&bus, SPI_RXDATA) == 0u, "empty RXDATA was not zero");
    CHECK(bus.rx_underruns == 1u, "empty RXDATA read was not counted");
    CHECK(rx_level(&bus) == 0u, "an empty pop moved the level");
}

/*
 * Write-one-to-clear, and only over the four event latches. Both acknowledge
 * styles a driver can plausibly use have to work: the bare mask run23 caught
 * BasebandSPI storing, and storing back the whole word that was just read —
 * which carries the level fields with it and must not zero them.
 */
static void test_status_writeback_is_write_one_to_clear(void) {
    s5l_spi_t bus;
    echo_t e;
    s5l_spi_slave_t slave;
    s5l_spi_reset(&bus);
    bind_echo(&slave, &e);
    CHECK(s5l_spi_attach(&bus, 0u, &slave), "attach failed");

    s5l_spi_write(&bus, SPI_TXDATA, 0x11u);
    CHECK((s5l_spi_read(&bus, SPI_STATUS) & SPI_STATUS_EVENTS) != 0u,
          "no event latched for a completed word");

    /* Zero clears nothing: this is W1C, not a plain store. */
    s5l_spi_write(&bus, SPI_STATUS, 0u);
    CHECK((s5l_spi_read(&bus, SPI_STATUS) & SPI_STATUS_EVENTS) != 0u,
          "storing zero into STATUS cleared the event latches");

    /* Storing back the whole word must clear the events and leave the levels
     * derived from the FIFOs, which still hold a byte. */
    uint32_t whole = s5l_spi_read(&bus, SPI_STATUS);
    s5l_spi_write(&bus, SPI_STATUS, whole);
    uint32_t after = s5l_spi_read(&bus, SPI_STATUS);
    CHECK((after & SPI_STATUS_EVENTS) == 0u,
          "whole-word acknowledge did not clear the events: %08x", after);
    CHECK(((after >> SPI_STATUS_RX_SHIFT) & SPI_STATUS_LEVEL) == 1u,
          "whole-word acknowledge zeroed the receive level: %08x", after);
    CHECK(bus.rx_level == 1u, "the receive FIFO itself lost a byte");

    /* And the bare-mask style, on a fresh event. */
    s5l_spi_write(&bus, SPI_TXDATA, 0x22u);
    CHECK((s5l_spi_read(&bus, SPI_STATUS) & SPI_STATUS_EVENTS) != 0u,
          "second word did not latch");
    s5l_spi_write(&bus, SPI_STATUS, SPI_STATUS_EVENTS);
    CHECK((s5l_spi_read(&bus, SPI_STATUS) & SPI_STATUS_EVENTS) == 0u,
          "bare-mask acknowledge did not clear the events");

    /* A store must never inject a bit that was not set, and RXDATA is
     * read-only: a guest must not be able to manufacture a received byte. */
    s5l_spi_write(&bus, SPI_STATUS, 0xffffffffu);
    CHECK((s5l_spi_read(&bus, SPI_STATUS) & SPI_STATUS_EVENTS) == 0u,
          "an all-ones acknowledge set latches instead of clearing them");
    uint8_t depth_before = bus.rx_level;
    s5l_spi_write(&bus, SPI_RXDATA, 0xdeadbeefu);
    CHECK(bus.rx_level == depth_before,
          "a store into RXDATA injected a byte the device never sent");
}

/*
 * The register file. Values, offsets and widths come from the SPI note in
 * soc.h; the word count is checked specifically because it is the one register
 * this model deliberately does NOT let gate the shifter.
 */
static void test_register_file_and_word_count(void) {
    s5l_spi_t bus;
    echo_t e;
    s5l_spi_slave_t slave;
    s5l_spi_reset(&bus);
    bind_echo(&slave, &e);
    CHECK(s5l_spi_attach(&bus, 0u, &slave), "attach failed");

    s5l_spi_write(&bus, SPI_CONTROL, SPI_CONTROL_START);
    s5l_spi_write(&bus, SPI_SETUP,
                  SPI_SETUP_BASE | SPI_SETUP_ARM | SPI_SETUP_GO);
    s5l_spi_write(&bus, SPI_PIN, 0x2u);
    s5l_spi_write(&bus, SPI_CLKDIV, 0x2u);
    s5l_spi_write(&bus, SPI_IDD, 0x7u);
    CHECK(s5l_spi_read(&bus, SPI_CONTROL) == SPI_CONTROL_START &&
          s5l_spi_read(&bus, SPI_SETUP) ==
              (SPI_SETUP_BASE | SPI_SETUP_ARM | SPI_SETUP_GO) &&
          s5l_spi_read(&bus, SPI_PIN) == 0x2u &&
          s5l_spi_read(&bus, SPI_CLKDIV) == 0x2u &&
          s5l_spi_read(&bus, SPI_IDD) == 0x7u,
          "the configuration registers did not read back");
    CHECK((SPI_SETUP_BASE | SPI_SETUP_ARM | SPI_SETUP_GO) == 0x11b8u,
          "the driver's assembled SETUP word is 0x%04x, expected 0x11b8 "
          "(0x1000|0x18, then |0x20 to arm and |0x180 to go)",
          SPI_SETUP_BASE | SPI_SETUP_ARM | SPI_SETUP_GO);

    /* The word count latches a remaining length and reads back. */
    s5l_spi_write(&bus, SPI_CNT, 16u);
    CHECK(s5l_spi_read(&bus, SPI_CNT) == 16u && bus.words_left == 16u,
          "the word count did not latch: cnt=%u left=%u",
          s5l_spi_read(&bus, SPI_CNT), bus.words_left);
    for (unsigned i = 0; i < 4u; i++)
        s5l_spi_write(&bus, SPI_TXDATA, (uint32_t)i);
    CHECK(bus.words_left == 12u,
          "four words left %u remaining, expected 12", bus.words_left);

    /*
     * A count of zero must NOT stop the controller. run23 caught BasebandSPI
     * storing zero here while configuring a controller it went on to use, and a
     * count this model misread into a gate would stall a transfer the guest is
     * asleep waiting for — which is the failure being fixed, reintroduced.
     */
    s5l_spi_reset(&bus);
    bind_echo(&slave, &e);
    CHECK(s5l_spi_attach(&bus, 0u, &slave), "attach failed");
    s5l_spi_write(&bus, SPI_CNT, 0u);
    s5l_spi_write(&bus, SPI_TXDATA, 0x5au);
    CHECK(e.words == 1u && rx_level(&bus) == 1u && bus.words_left == 0u,
          "a zero word count gated the shifter");

    /* Unknown offsets are counted and named, bounded. TXDATA reads zero
     * without being called unknown: it is a decoded, write-only register. */
    (void)s5l_spi_read(&bus, 0x24u);
    (void)s5l_spi_read(&bus, 0x24u);
    (void)s5l_spi_read(&bus, 0x4cu);   /* the spi-version 1 register */
    s5l_spi_write(&bus, 0x40u, 1u);
    CHECK(bus.unknown_reads == 3u && bus.unknown_writes == 1u,
          "unknown access counters r=%llu w=%llu",
          (unsigned long long)bus.unknown_reads,
          (unsigned long long)bus.unknown_writes);
    CHECK(bus.unknown_off_count == 3u,
          "distinct unknown offsets=%u expect 3", bus.unknown_off_count);
    uint64_t unknown_before = bus.unknown_reads;
    CHECK(s5l_spi_read(&bus, SPI_TXDATA) == 0u &&
          bus.unknown_reads == unknown_before,
          "TXDATA is a decoded write-only register, not an unknown offset");
    for (uint32_t off = 0x100u; off < 0x100u + 32u * 4u; off += 4u)
        (void)s5l_spi_read(&bus, off);
    CHECK(bus.unknown_off_count == S5L_SPI_UNKNOWN_OFF,
          "the bounded unknown-offset set grew to %u", bus.unknown_off_count);
}

/*
 * THE BAIL-OUT. The stock handler body reads STATUS and returns 0 immediately
 * when the receive-level field is zero, without acknowledging anything, so
 * nothing wakes the sleeping transfer. This test asserts that the model can
 * represent that state, that it is distinguishable from a deliverable one, and
 * that the line is never raised in it.
 */
static void test_interrupt_requires_a_byte_the_handler_can_drain(void) {
    s5l_spi_t bus;
    echo_t e;
    s5l_spi_slave_t slave;
    s5l_spi_reset(&bus);
    bind_echo(&slave, &e);
    CHECK(s5l_spi_attach(&bus, 0u, &slave), "attach failed");
    s5l_spi_write(&bus, SPI_SETUP,
                  SPI_SETUP_BASE | SPI_SETUP_ARM | SPI_SETUP_GO);

    CHECK(!s5l_spi_irq(&bus), "an idle controller asserted its line");

    /* Events pending, receive FIFO empty. This is the state that would hang the
     * guest, and it is reachable: latch an event, then drain the byte. */
    s5l_spi_write(&bus, SPI_TXDATA, 0x33u);
    CHECK(s5l_spi_irq(&bus), "a completed word did not assert the line");
    (void)s5l_spi_read(&bus, SPI_RXDATA);
    uint32_t status = s5l_spi_read(&bus, SPI_STATUS);
    CHECK((status & SPI_STATUS_EVENTS) != 0u &&
          ((status >> SPI_STATUS_RX_SHIFT) & SPI_STATUS_LEVEL) == 0u,
          "the handler's bail-out state is not representable: status=%08x",
          status);
    CHECK(!s5l_spi_irq(&bus),
          "the line was asserted with an empty receive FIFO — the stock "
          "handler returns 0 there and the guest sleeps forever");

    /* And it is not merely that the events happen to be clear: set them again
     * with the FIFO still empty and the line must stay down. */
    s5l_spi_write(&bus, SPI_STATUS, SPI_STATUS_EVENTS);
    bus.status = SPI_STATUS_EVENTS;
    CHECK(!s5l_spi_irq(&bus),
          "a pending event alone asserted the line over an empty FIFO");
    bus.status = 0u;

    /* The two terms are independent: a byte with no pending event is also not
     * an interrupt, because the handler has nothing to acknowledge. */
    s5l_spi_write(&bus, SPI_TXDATA, 0x44u);
    s5l_spi_write(&bus, SPI_STATUS, SPI_STATUS_EVENTS);
    CHECK(rx_level(&bus) == 1u && !s5l_spi_irq(&bus),
          "an acknowledged event re-asserted on a still-full FIFO");

    /* A fresh word re-arms both terms, and the level survives until the guest
     * clears it — it is a level, not a pulse. */
    s5l_spi_write(&bus, SPI_TXDATA, 0x55u);
    CHECK(s5l_spi_irq(&bus), "a second word did not re-assert the line");
    CHECK(s5l_spi_irq(&bus) && s5l_spi_irq(&bus),
          "the line is a pulse rather than a level");
    s5l_spi_write(&bus, SPI_STATUS, SPI_STATUS_EVENTS);
    CHECK(!s5l_spi_irq(&bus), "the W1C acknowledge did not drop the line");
}

/*
 * The other half of the gate: SETUP's 0x100. The driver fills the transmit
 * FIFO, arms, and only THEN enables the completion interrupt, precisely so the
 * filter cannot run while the transfer counts are still being set up. A line
 * that ignored the enable would fire on the very first prefill store. Both the
 * places the driver stops wanting the interrupt — the filter's `bic #0x100`
 * and finishTransfer's write of the bare base word — must drop it again.
 */
static void test_interrupt_enable_holds_the_line_through_the_prefill(void) {
    s5l_spi_t bus;
    echo_t e;
    s5l_spi_slave_t slave;
    s5l_spi_reset(&bus);
    bind_echo(&slave, &e);
    CHECK(s5l_spi_attach(&bus, 0u, &slave), "attach failed");

    CHECK((SPI_SETUP_GO & SPI_SETUP_IRQ) == 0x100u &&
          SPI_SETUP_GO == 0x180u,
          "the go word is 0x%04x with enable 0x%04x, expected 0x180/0x100",
          SPI_SETUP_GO, SPI_SETUP_IRQ);

    /* Base and arm carry no enable bit. */
    s5l_spi_write(&bus, SPI_CNT, 16u);
    s5l_spi_write(&bus, SPI_CONTROL, SPI_CONTROL_START);
    s5l_spi_write(&bus, SPI_SETUP, SPI_SETUP_BASE);
    s5l_spi_write(&bus, SPI_SETUP, SPI_SETUP_BASE | SPI_SETUP_ARM);
    for (unsigned i = 0; i < S5L_SPI_FIFO_DEPTH; i++) {
        s5l_spi_write(&bus, SPI_TXDATA, 0x20u + i);
        CHECK(!s5l_spi_irq(&bus),
              "the line rose during the prefill at byte %u — the filter would "
              "run against half-built transfer counts", i);
    }
    CHECK(rx_level(&bus) == S5L_SPI_FIFO_DEPTH,
          "the prefill did not shift: rx=%u", rx_level(&bus));

    /* The go store is what releases it. */
    s5l_spi_write(&bus, SPI_SETUP,
                  SPI_SETUP_BASE | SPI_SETUP_ARM | SPI_SETUP_GO);
    CHECK(s5l_spi_irq(&bus), "enabling the completion interrupt did not "
          "surface the eight words already received");

    /* The filter's completion path clears exactly 0x100 and nothing else. */
    s5l_spi_write(&bus, SPI_SETUP,
                  (SPI_SETUP_BASE | SPI_SETUP_ARM | SPI_SETUP_GO) &
                      ~(uint32_t)SPI_SETUP_IRQ);
    CHECK(!s5l_spi_irq(&bus),
          "clearing the enable did not drop the line, even with events and "
          "eight received bytes still pending");
    CHECK(rx_level(&bus) == S5L_SPI_FIFO_DEPTH &&
          (s5l_spi_read(&bus, SPI_STATUS) & SPI_STATUS_EVENTS) != 0u,
          "clearing the enable discarded state it must only mask");

    /* And finishTransfer's bare base word does the same. */
    s5l_spi_write(&bus, SPI_SETUP,
                  SPI_SETUP_BASE | SPI_SETUP_ARM | SPI_SETUP_GO);
    CHECK(s5l_spi_irq(&bus), "re-enabling did not restore the line");
    s5l_spi_write(&bus, SPI_SETUP, SPI_SETUP_BASE);
    CHECK(!s5l_spi_irq(&bus),
          "the bare base setup word left the line asserted");
}

/*
 * A controller with nothing attached must not answer. spi0's devices are
 * unmodelled and it stalled at exactly this point in run59 (`r=0 w=13`);
 * inventing a reply for it would be fabricating the value the guest is waiting
 * for, which is the one thing this core's unmodelled windows may not do.
 */
/*
 * A TRANSMIT-ONLY DMA BURST, which is how the Z2's firmware is downloaded and
 * the reason none of it ever arrived.
 *
 * The receive FIFO is eight deep and, in DMA, nothing empties it: the DMAC's
 * destination is TXDATA and there is no second channel reading RXDATA back.
 * The shifter used to stall on that, so a burst delivered eight octets, left
 * eight more in the transmit FIFO, and counted every write after those in
 * tx_drops. run104 measured it at the extreme -- 812,340 octets moved by the
 * DMAC into SPI1, sixteen of them reaching the device.
 *
 * Silicon overruns rather than stalls, and this checks both halves of that:
 * everything is delivered in DMA, and NOTHING about the PIO path changes,
 * because there the CPU really is the reader and stalling is what makes a
 * sixteen-octet command work against an eight-deep FIFO.
 */
/*
 * A transfer that begins with the previous one's answers still queued cannot
 * return its own — and run157 showed the guest will not clear them by hand.
 *
 * The Z2 download opens with an 8-octet write-only header. Its replies are
 * meaningless, so the driver never reads them; `rxdata-reads 80` against five
 * 16-octet transactions is every command it DOES care about drained exactly.
 * Those eight then blocked the two-octet ATN_ACK that follows, which is PIO and
 * cannot shift past a full receive FIFO, and the bootload stopped there for
 * 2.44 G instructions.
 *
 * This is that sequence in miniature, plus the reason it matters beyond the
 * deadlock: 0xc0445284 compares the ATN_ACK's reply against 0x4BC1, so stale
 * octets would fail the compare even on hardware that shifted fine.
 */
static void test_a_new_transfer_does_not_inherit_the_last_ones_answers(void) {
    s5l_spi_t bus;
    s5l_spi_slave_t slave;
    echo_t echo;

    s5l_spi_reset(&bus);
    memset(&echo, 0, sizeof echo);
    bind_echo(&slave, &echo);
    s5l_spi_attach(&bus, 0u, &slave);
    s5l_spi_write(&bus, SPI_SETUP, SPI_SETUP_BASE);

    /* A write-only transfer whose answers nobody reads. */
    s5l_spi_write(&bus, SPI_CNT, 8u);
    for (unsigned i = 0; i < 8u; i++)
        s5l_spi_write(&bus, SPI_TXDATA, (uint8_t)(0xa0u + i));
    CHECK(bus.rx_level == S5L_SPI_FIFO_DEPTH,
          "the write-only transfer left %u octets queued, expected the FIFO "
          "full at %u", bus.rx_level, (unsigned)S5L_SPI_FIFO_DEPTH);

    /* Now the next transfer starts. Writing the count is the boundary. */
    s5l_spi_write(&bus, SPI_CNT, 2u);
    CHECK(bus.rx_level == 0u,
          "starting a transfer left %u stale octet(s) in the receive FIFO; the "
          "ATN_ACK would read those instead of the device's 0x4BC1",
          bus.rx_level);

    /* And it can actually move. */
    const unsigned seen_before = echo.words;
    s5l_spi_write(&bus, SPI_TXDATA, 0x1au);
    s5l_spi_write(&bus, SPI_TXDATA, 0xa1u);
    CHECK(echo.words == seen_before + 2u,
          "the device saw %u octets of the following transfer, expected 2 -- "
          "this is the ATN_ACK deadlock run156 measured",
          echo.words - seen_before);
    CHECK(bus.tx_level == 0u,
          "%u octet(s) stuck in the transmit FIFO after the flush",
          bus.tx_level);
    CHECK(bus.rx_level == 2u,
          "the receive FIFO holds %u octets, expected exactly the 2 this "
          "transfer produced", bus.rx_level);
}

static void test_a_dma_burst_is_not_stalled_by_a_full_receive_fifo(void) {
    s5l_spi_t bus;
    s5l_spi_slave_t slave;
    echo_t echo;
    const unsigned N = 200u;         /* far past both FIFOs */

    /* --- PIO first: the stall is load-bearing and must survive. --- */
    s5l_spi_reset(&bus);
    memset(&echo, 0, sizeof echo);
    bind_echo(&slave, &echo);
    s5l_spi_attach(&bus, 0u, &slave);
    s5l_spi_write(&bus, SPI_SETUP, SPI_SETUP_BASE);      /* no DMA bit */
    for (unsigned i = 0; i < N; i++)
        s5l_spi_write(&bus, SPI_TXDATA, (uint8_t)i);

    CHECK(bus.words == S5L_SPI_FIFO_DEPTH,
          "PIO shifted %llu octets into a full receive FIFO; it must stop at "
          "%u and wait for a reader, or a sixteen-octet command loses half its "
          "reply", (unsigned long long)bus.words,
          (unsigned)S5L_SPI_FIFO_DEPTH);
    CHECK(bus.rx_overruns == 0u,
          "PIO overran the receive FIFO %llu times; it is supposed to stall",
          (unsigned long long)bus.rx_overruns);
    CHECK(bus.tx_drops > 0u,
          "PIO accepted all %u octets, so the transmit FIFO is not bounded", N);

    /* And draining RXDATA restarts it, one octet at a time. */
    uint64_t before = bus.words;
    (void)s5l_spi_read(&bus, SPI_RXDATA);
    CHECK(bus.words == before + 1u,
          "a RXDATA read did not restart the shifter exactly once (%llu -> %llu)",
          (unsigned long long)before, (unsigned long long)bus.words);

    /* --- DMA: the same burst, and now all of it must land. --- */
    s5l_spi_reset(&bus);
    memset(&echo, 0, sizeof echo);
    bind_echo(&slave, &echo);
    s5l_spi_attach(&bus, 0u, &slave);
    s5l_spi_write(&bus, SPI_SETUP, SPI_SETUP_BASE | SPI_SETUP_DMA);
    for (unsigned i = 0; i < N; i++)
        s5l_spi_write(&bus, SPI_TXDATA, (uint8_t)i);

    CHECK(bus.words == N,
          "a DMA burst delivered %llu of %u octets -- this is the Z2 firmware "
          "download, and short-changing it is why the bootload never sent a "
          "packet", (unsigned long long)bus.words, N);
    CHECK(bus.tx_drops == 0u,
          "%llu octets of a DMA burst were dropped at the transmit FIFO",
          (unsigned long long)bus.tx_drops);
    CHECK(echo.words == N,
          "the device saw %u octets, not %u", echo.words, N);

    /*
     * The answers that had nowhere to go are COUNTED, not pretended away --
     * and NONE of them are queued.
     *
     * This block used to expect the first eight to fit, on the reasoning that
     * a receive FIFO fills before it overruns. run155 measured what that costs.
     * A TX-only DMA burst left `rx level 8`, and the very next transfer is the
     * ATN_ACK, which is PIO and therefore needs `rx_level < DEPTH` to shift at
     * all. It never could: its two octets sat in the transmit FIFO for 2.44 G
     * instructions, the device never saw `1A A1`, and the driver slept forever
     * in a call that had no way to complete.
     *
     * Nothing drains those eight, either -- 91 register reads against 54,351
     * writes on spi1 across the whole run. So queueing them does not model a
     * FIFO, it wedges the bus, and the shipped driver plainly runs this exact
     * sequence on real hardware.
     */
    CHECK(bus.rx_level == 0u,
          "a DMA burst queued %u octets into the receive FIFO; with no receive "
          "channel there is nothing to drain them and the next PIO transfer "
          "can never shift", bus.rx_level);
    CHECK(bus.rx_overruns == N,
          "%llu overruns counted, expected %u -- every discarded answer must "
          "still be visible", (unsigned long long)bus.rx_overruns, N);

    /*
     * THE PROPERTY THE ABOVE EXISTS FOR: the bus still works afterwards.
     *
     * This is the regression test for run155's deadlock. A PIO transfer
     * following a DMA burst must shift, because that is the ATN_ACK the whole
     * Z2 bootload waits on.
     */
    s5l_spi_write(&bus, SPI_SETUP, SPI_SETUP_BASE);   /* DMA bit cleared */
    const unsigned echoed_before = echo.words;
    s5l_spi_write(&bus, SPI_TXDATA, 0x1au);
    s5l_spi_write(&bus, SPI_TXDATA, 0xa1u);
    CHECK(echo.words == echoed_before + 2u,
          "after a DMA burst the device saw %u more octets, expected 2 -- a "
          "PIO transfer following DMA cannot shift, which is exactly the "
          "ATN_ACK deadlock run155 measured",
          echo.words - echoed_before);
    CHECK(bus.tx_level == 0u,
          "%u octet(s) of the following PIO transfer are stuck in the transmit "
          "FIFO", bus.tx_level);
}

static void test_a_bus_with_no_device_shifts_nothing(void) {
    s5l_spi_t bus;
    s5l_spi_reset(&bus);

    s5l_spi_write(&bus, SPI_CONTROL, SPI_CONTROL_START);
    s5l_spi_write(&bus, SPI_CNT, 2u);
    s5l_spi_write(&bus, SPI_SETUP,
                  SPI_SETUP_BASE | SPI_SETUP_ARM | SPI_SETUP_GO);
    s5l_spi_write(&bus, SPI_TXDATA, 0x01u);
    s5l_spi_write(&bus, SPI_TXDATA, 0x02u);
    CHECK(bus.words == 0u && rx_level(&bus) == 0u && tx_level(&bus) == 2u,
          "an unattached bus manufactured a reply: words=%llu rx=%u tx=%u",
          (unsigned long long)bus.words, rx_level(&bus), tx_level(&bus));
    CHECK(!s5l_spi_irq(&bus), "an unattached bus asserted its line");
    CHECK(bus.words_left == 2u, "an unshifted word decremented the count");

    /* Attaching afterwards must not retroactively drain the queue; the shifter
     * runs on guest stores and loads, not on host wiring. */
    echo_t e;
    s5l_spi_slave_t slave;
    bind_echo(&slave, &e);
    CHECK(s5l_spi_attach(&bus, 0u, &slave), "attach failed");
    CHECK(bus.words == 0u, "attaching a device shifted queued words by itself");
    s5l_spi_write(&bus, SPI_TXDATA, 0x03u);
    CHECK(bus.words == 3u && rx_level(&bus) == 3u,
          "the backlog did not flush once a device existed: words=%llu",
          (unsigned long long)bus.words);
}

/* The null device the machine attaches: 0x00 for every word, whatever it is
 * sent. That all-zero reply is what makes the Z2 probe fail to recognise HBPP
 * and return a definite false. */
static void test_null_device_answers_zero(void) {
    s5l_spi_t bus;
    s5l_spi_slave_t null_dev;
    s5l_spi_reset(&bus);
    s5l_spi_null_bind(&null_dev);
    CHECK(null_dev.transfer != NULL && null_dev.ctx == NULL,
          "the null device was not bound");
    CHECK(s5l_spi_attach(&bus, 0u, &null_dev), "attach failed");

    /* isInHBPP()'s own buffer, built at 0xc0441030: 0x1A,0xA1 then 0x18,0xE1
     * seven times over. It accepts the reply only if the big-endian halfwords
     * rx[0..1] and rx[2..3] both pass its test, so all zeros is a rejection. */
    static const uint8_t PROBE[16] = {
        0x1a, 0xa1, 0x18, 0xe1, 0x18, 0xe1, 0x18, 0xe1,
        0x18, 0xe1, 0x18, 0xe1, 0x18, 0xe1, 0x18, 0xe1,
    };
    s5l_spi_write(&bus, SPI_CNT, 16u);
    unsigned received = 0;
    bool all_zero = true;
    for (unsigned i = 0; i < 16u; i++) {
        s5l_spi_write(&bus, SPI_TXDATA, PROBE[i]);
        while (rx_level(&bus)) {
            if (s5l_spi_read(&bus, SPI_RXDATA) != 0u) all_zero = false;
            received++;
        }
    }
    CHECK(received == 16u, "the 16-byte probe returned %u bytes", received);
    CHECK(all_zero, "the null device answered with something other than 0x00");
    CHECK(bus.words == 16u && bus.words_left == 0u && bus.tx_drops == 0u,
          "the probe did not consume the word count cleanly: words=%llu "
          "left=%u drops=%llu", (unsigned long long)bus.words,
          bus.words_left, (unsigned long long)bus.tx_drops);
}

/*
 * lcd0 is deliberately not a command-level panel model. This pins the two
 * narrow contracts measured in AppleMerlotLCD: transmit-only requests still
 * clock disposable receive bytes, and register 0x15 bit 0 reports ready. No
 * unrelated register is fabricated as ready.
 */
static void test_lcd_transport_clocks_receive_bytes(void) {
    s5l_spi_t bus;
    s5l_spi_slave_t lcd;
    s5l_spi_reset(&bus);
    s5l_spi_lcd_bind(&lcd, &bus);
    CHECK(lcd.transfer != NULL && lcd.ctx == &bus,
          "the lcd0 transport endpoint was not bound");
    CHECK(s5l_spi_attach(&bus, S5L_SPI0_LCD_CS, &lcd),
          "lcd0 refused its device-tree chip select");
    bus.cs = S5L_SPI0_LCD_CS;

    s5l_spi_write(&bus, SPI_CNT, 2u);
    s5l_spi_write(&bus, SPI_TXDATA, 0x55u);
    s5l_spi_write(&bus, SPI_TXDATA, 0x02u);
    CHECK(bus.words == 2u && bus.tx_level == 0u && bus.rx_level == 2u,
          "lcd0 did not clock the 55 02 request: words=%llu tx=%u rx=%u",
          (unsigned long long)bus.words, bus.tx_level, bus.rx_level);
    CHECK(s5l_spi_read(&bus, SPI_RXDATA) == 0u &&
          s5l_spi_read(&bus, SPI_RXDATA) == 0u && bus.rx_level == 0u,
          "lcd0 exposed a semantic reply to a transmit-only request");

    s5l_spi_write(&bus, SPI_CNT, 2u);
    s5l_spi_write(&bus, SPI_TXDATA, 0x95u);
    CHECK(bus.words_left == 1u && bus.rx_level == 1u && bus.rx[0] == 0u &&
          (bus.cs & S5L_SPI_LCD_READ_PENDING) != 0u,
          "lcd0 did not retain the register-0x15 phase in serialized state");
    /* The real interrupt filter drains the ignored first byte before the API
     * queues its dummy clock. The pending bit, not that FIFO byte, must carry
     * the register identity across the gap. */
    CHECK(s5l_spi_read(&bus, SPI_RXDATA) == 0u && bus.rx_level == 0u &&
          (bus.cs & S5L_SPI_LCD_READ_PENDING) != 0u,
          "draining the ignored byte lost the pending LCD read");
    s5l_spi_write(&bus, SPI_TXDATA, 0xffu);
    CHECK(s5l_spi_read(&bus, SPI_RXDATA) == 0x01u &&
          (bus.cs & S5L_SPI_LCD_READ_PENDING) == 0u,
          "lcd0 did not report register 0x15 bit 0 ready");

    s5l_spi_write(&bus, SPI_CNT, 2u);
    s5l_spi_write(&bus, SPI_TXDATA, 0x96u);
    CHECK(s5l_spi_read(&bus, SPI_RXDATA) == 0u,
          "lcd0 fabricated the ignored byte of an unrelated read");
    s5l_spi_write(&bus, SPI_TXDATA, 0xffu);
    CHECK(s5l_spi_read(&bus, SPI_RXDATA) == 0u,
          "lcd0 fabricated a reply for unrelated register 0x16");
}

/*
 * The whole path, through the machine: the window decodes, a completed word
 * reaches VIC0 line 10, an idle core in WFI wakes on it, and the guest's W1C
 * acknowledge drops it again.
 */
static void test_machine_routes_spi_windows_and_irq_lines(void) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, 0u, 1u << 20), "machine init failed");
    CHECK(m.stub_declare_failures == 0u,
          "%u stub declarations were refused", m.stub_declare_failures);
    CHECK(m.spi[0].cs == S5L_SPI0_LCD_CS &&
          m.spi[0].slaves[S5L_SPI0_LCD_CS].transfer != NULL &&
          m.spi[0].slaves[S5L_SPI0_LCD_CS].ctx == &m.spi[0],
          "spi0 is not routed to lcd0 chip select %u", S5L_SPI0_LCD_CS);
    for (unsigned cs = 0; cs < S5L_SPI_SLAVES; cs++)
        if (cs != S5L_SPI0_LCD_CS)
            CHECK(m.spi[0].slaves[cs].transfer == NULL,
                  "unexpected spi0 device on chip select %u", cs);

    s5l_window_t windows[S5L_WINDOW_MAX];
    unsigned count = s5l8900_windows(&m, windows, S5L_WINDOW_MAX);
    unsigned seen0 = 0, seen1 = 0;
    for (unsigned i = 0; i < count && i < S5L_WINDOW_MAX; i++) {
        if (windows[i].base == S5L8900_SPI0_BASE) seen0++;
        if (windows[i].base == S5L8900_SPI1_BASE) seen1++;
    }
    CHECK(seen0 == 1u && seen1 == 1u,
          "window map has spi0=%u spi1=%u — a leftover stub would shadow the "
          "model or be shadowed by it", seen0, seen1);
    CHECK(!s5l8900_add_stub(&m, S5L8900_SPI1_BASE, S5L8900_DEV_SIZE, "shadow"),
          "a stub was allowed to shadow spi1");
    const s5l_window_t *conflict =
        s5l8900_ram_conflict(S5L8900_SPI1_BASE, 4u);
    CHECK(conflict && strcmp(conflict->name, "spi1") == 0,
          "RAM conflict did not identify spi1");

    /*
     * ALIGNMENT AND BOUNDS still gate the decode. WIDTH no longer does, for the
     * data ports, and that is a fix rather than a relaxation.
     *
     * This block used to assert "only aligned 32-bit accesses decode", and the
     * SPI window was decoded with mmio_word(). But /arm-io/spi1's own
     * dma-channels template is 0x00089000 -- four-byte source, ONE-BYTE
     * destination -- so every DMA store into this port was a write8 that the
     * decode threw away. run114 measured the cost: channel 5 `runs 210 bytes
     * 812340` into SPI1's TXDATA, against `spi1 words 176, tx-drops 0` and
     * `unmapped writes 813135`. The Z2 firmware download had never once
     * reached the device, and every explanation this project wrote for that
     * looked at the guest instead.
     *
     * The narrow stores are aimed at spi0, which nothing else here uses, so
     * spi1's FIFO stays pristine for the loopback below. lcd0 is attached now,
     * so each access completes one word and leaves one disposable receive byte.
     */
    uint64_t ur = m.unmapped_reads, uw = m.unmapped_writes;

    uint64_t words0 = m.spi[0].words;
    unsigned rx0 = m.spi[0].rx_level;
    m.bus.write8(m.bus.ctx, S5L8900_SPI0_BASE + SPI_TXDATA, 0xaau);
    m.bus.write16(m.bus.ctx, S5L8900_SPI0_BASE + SPI_TXDATA, 0xbbccu);
    CHECK(m.spi[0].words == words0 + 2u && m.spi[0].tx_level == 0u &&
          m.spi[0].rx_level == rx0 + 2u,
          "narrow stores did not complete through lcd0: words=%llu tx=%u rx=%u",
          (unsigned long long)(m.spi[0].words - words0),
          m.spi[0].tx_level, m.spi[0].rx_level);
    CHECK(m.unmapped_writes == uw,
          "a narrow store to a data port still took the unmapped path");
    /* A narrow READ decodes too, and answers the low half of the register. */
    uint32_t st32 = m.bus.read32(m.bus.ctx, S5L8900_SPI0_BASE + SPI_STATUS);
    uint32_t st16 = m.bus.read16(m.bus.ctx, S5L8900_SPI0_BASE + SPI_STATUS);
    CHECK(st16 == (st32 & 0xffffu),
          "a halfword read of STATUS gave %#x, not the low half of %#x",
          st16, st32);
    CHECK(m.unmapped_reads == ur,
          "a narrow read of a data port still took the unmapped path");

    /* Unaligned and out-of-window are unchanged, and still mutate nothing. */
    s5l_spi_t before = m.spi[1];
    m.bus.write32(m.bus.ctx, S5L8900_SPI1_BASE + 1u, 0xdeadbeefu);
    (void)m.bus.read32(m.bus.ctx, S5L8900_SPI1_BASE + S5L8900_DEV_SIZE - 2u);
    CHECK(memcmp(&before, &m.spi[1], sizeof before) == 0,
          "an unaligned or out-of-window access mutated the controller");
    CHECK(m.unmapped_writes == uw + 1u && m.unmapped_reads == ur + 1u,
          "malformed MMIO counts r=%llu w=%llu",
          (unsigned long long)(m.unmapped_reads - ur),
          (unsigned long long)(m.unmapped_writes - uw));

    /*
     * The exact nineteen stores AppleS5L8900XSPIController issues for a 16-byte
     * PIO transfer, in its own order: three at power-on, six of setup, the arm,
     * eight FIFO bytes, and the go. This is run59's `w=19` reproduced against
     * the model, and the eleven-plus-eight decomposition that identified the
     * stall in the first place.
     */
    uint64_t writes_before = 0;   /* counted by hand below, not by the machine */
    m.bus.write32(m.bus.ctx, S5L8900_SPI1_BASE + SPI_CONTROL, 0u);
    m.bus.write32(m.bus.ctx, S5L8900_SPI1_BASE + SPI_PIN, 0u);
    m.bus.write32(m.bus.ctx, S5L8900_SPI1_BASE + SPI_STATUS, SPI_STATUS_EVENTS);
    m.bus.write32(m.bus.ctx, S5L8900_SPI1_BASE + SPI_CLKDIV, 2u);
    m.bus.write32(m.bus.ctx, S5L8900_SPI1_BASE + SPI_IDD, 0u);
    m.bus.write32(m.bus.ctx, S5L8900_SPI1_BASE + SPI_STATUS, SPI_STATUS_EVENTS);
    m.bus.write32(m.bus.ctx, S5L8900_SPI1_BASE + SPI_SETUP, SPI_SETUP_BASE);
    m.bus.write32(m.bus.ctx, S5L8900_SPI1_BASE + SPI_CONTROL, SPI_CONTROL_START);
    m.bus.write32(m.bus.ctx, S5L8900_SPI1_BASE + SPI_CNT, 16u);
    m.bus.write32(m.bus.ctx, S5L8900_SPI1_BASE + SPI_SETUP,
                  SPI_SETUP_BASE | SPI_SETUP_ARM);
    writes_before += 10u;
    /*
     * Release the touch controller's reset line before expecting it to answer.
     * A part held in its reset pin drives nothing, and the pin block powers up
     * all-zero with this line active low -- so a machine that has only just
     * been built has the device held down, exactly as the board does before
     * the driver's "Deasserting reset line". This is the store the guest makes.
     */
    m.bus.write32(m.bus.ctx, S5L8900_GPIO_BASE + S5L_GPIO_FSEL,
                  (6u << 16) | (6u << 8) | 0xfu);
    /* The driver's own sixteen probe bytes, `1A A1` then `18 E1` seven times
     * (0xc0441014-0xc0441048). The first eight are the prefill. Sending the
     * real pattern rather than an arbitrary ramp matters now that the attached
     * device is a protocol model: what comes back is only a round trip if the
     * device recognised what went out. */
    static const uint8_t PROBE[S5L_SPI_FIFO_DEPTH] = {
        0x1a, 0xa1, 0x18, 0xe1, 0x18, 0xe1, 0x18, 0xe1
    };
    for (unsigned i = 0; i < S5L_SPI_FIFO_DEPTH; i++) {
        m.bus.write32(m.bus.ctx, S5L8900_SPI1_BASE + SPI_TXDATA, PROBE[i]);
        writes_before++;
        s5l8900_tick(&m, 0u);
        /* The raw VIC input, not the CPU line: line 10 is still masked here,
         * so testing the CPU would pass for the wrong reason. */
        CHECK((m.vic[0].raw & (1u << S5L8900_IRQ_SPI1)) == 0u,
              "the prefill raised line 10 at byte %u, before the driver had "
              "finished setting the transfer up", i);
    }
    m.bus.write32(m.bus.ctx, S5L8900_SPI1_BASE + SPI_SETUP,
                  SPI_SETUP_BASE | SPI_SETUP_ARM | SPI_SETUP_GO);
    writes_before++;
    CHECK(writes_before == 19u,
          "the modelled transfer prologue is %llu stores, expected run59's 19",
          (unsigned long long)writes_before);

    /* Masked: the VIC sees the level, the CPU does not. */
    s5l8900_tick(&m, 0u);
    CHECK((m.vic[0].raw & (1u << S5L8900_IRQ_SPI1)) != 0u && !m.cpu.irq_line,
          "masked IRQ10 raw/CPU behaviour is wrong");
    CHECK((m.vic[0].raw & (1u << S5L8900_IRQ_SPI0)) == 0u,
          "spi1 traffic asserted spi0's line 9");

    /* Enabled: an idle core must come out of WFI without guest time moving. */
    s5l_vic_write(&m.vic[0], VIC_INTENABLE, 1u << S5L8900_IRQ_SPI1);
    uint64_t ticks_before = m.timer.ticks, accum_before = m.tb_accum;
    CHECK(m.bus.wait_for_interrupt && m.bus.wait_for_interrupt(m.bus.ctx),
          "a pending SPI level did not wake WFI");
    CHECK(m.timer.ticks == ticks_before && m.tb_accum == accum_before,
          "the SPI WFI wake advanced guest time");
    CHECK(m.cpu.irq_line, "enabled IRQ10 did not reach the CPU");

    /* The handler's own sequence: read STATUS, see a non-zero receive level,
     * drain it, acknowledge. */
    uint32_t status = m.bus.read32(m.bus.ctx, S5L8900_SPI1_BASE + SPI_STATUS);
    unsigned level = (status >> SPI_STATUS_RX_SHIFT) & SPI_STATUS_LEVEL;
    CHECK(level == S5L_SPI_FIFO_DEPTH,
          "the handler would read receive level %u, expected %u",
          level, S5L_SPI_FIFO_DEPTH);
    /* The device on spi1 chip select 0 is the touch controller, and it answers
     * the HBPP probe with a loopback — so the bytes that come back are the
     * bytes that went out. This test is about the CONTROLLER, so it checks the
     * round trip rather than what the answer means; test_mtz2.c owns that. */
    for (unsigned i = 0; i < level; i++)
        CHECK(m.bus.read32(m.bus.ctx, S5L8900_SPI1_BASE + SPI_RXDATA) ==
              PROBE[i],
              "the touch controller did not loop byte %u back", i);
    m.bus.write32(m.bus.ctx, S5L8900_SPI1_BASE + SPI_STATUS, status);
    s5l8900_tick(&m, 0u);
    CHECK((m.vic[0].raw & (1u << S5L8900_IRQ_SPI1)) == 0u && !m.cpu.irq_line,
          "the W1C acknowledge did not deassert IRQ10 and the CPU");

    /*
     * The late AppleMerlotLCD::_lcdEnable request that exposed the real bug:
     * 55 02, transmit-only, synchronous. The stock filter still needs two
     * receive FIFO bytes to run and wake that requester. Drain them exactly as
     * the filter does, then prove its acknowledge drops line 9 again.
     */
    s5l_vic_write(&m.vic[0], VIC_INTENABLE, 1u << S5L8900_IRQ_SPI0);
    m.bus.write32(m.bus.ctx, S5L8900_SPI0_BASE + SPI_STATUS, SPI_STATUS_EVENTS);
    m.bus.write32(m.bus.ctx, S5L8900_SPI0_BASE + SPI_CNT, 2u);
    m.bus.write32(m.bus.ctx, S5L8900_SPI0_BASE + SPI_CONTROL, SPI_CONTROL_START);
    m.bus.write32(m.bus.ctx, S5L8900_SPI0_BASE + SPI_SETUP,
                  SPI_SETUP_BASE | SPI_SETUP_ARM);
    uint64_t panel_before = m.spi[0].words;
    m.bus.write32(m.bus.ctx, S5L8900_SPI0_BASE + SPI_TXDATA, 0x55u);
    m.bus.write32(m.bus.ctx, S5L8900_SPI0_BASE + SPI_TXDATA, 0x02u);
    m.bus.write32(m.bus.ctx, S5L8900_SPI0_BASE + SPI_SETUP,
                  SPI_SETUP_BASE | SPI_SETUP_ARM | SPI_SETUP_GO);
    s5l8900_tick(&m, 0u);
    CHECK((m.vic[0].raw & (1u << S5L8900_IRQ_SPI0)) != 0u &&
          m.spi[0].words == panel_before + 2u && m.spi[0].rx_level == 2u,
          "lcd0 completion did not assert line 9: words=%llu rx=%u",
          (unsigned long long)(m.spi[0].words - panel_before),
          m.spi[0].rx_level);
    uint32_t panel_status =
        m.bus.read32(m.bus.ctx, S5L8900_SPI0_BASE + SPI_STATUS);
    unsigned panel_rx =
        (panel_status >> SPI_STATUS_RX_SHIFT) & SPI_STATUS_LEVEL;
    CHECK(panel_rx == 2u, "the stock filter would see %u lcd0 bytes, not 2",
          panel_rx);
    bool panel_zero = true;
    for (unsigned i = 0; i < panel_rx; i++)
        if (m.bus.read32(m.bus.ctx, S5L8900_SPI0_BASE + SPI_RXDATA) != 0u)
            panel_zero = false;
    CHECK(panel_zero, "a transmit-only lcd0 request exposed semantic reply data");
    m.bus.write32(m.bus.ctx, S5L8900_SPI0_BASE + SPI_STATUS, panel_status);
    m.bus.write32(m.bus.ctx, S5L8900_SPI0_BASE + SPI_SETUP, SPI_SETUP_BASE);
    s5l8900_tick(&m, 0u);
    CHECK((m.vic[0].raw & (1u << S5L8900_IRQ_SPI0)) == 0u &&
          m.spi[0].rx_level == 0u,
          "the completed lcd0 request left line 9 or receive data pending");

    s5l8900_free(&m);
}

/*
 * The stock filter's own algorithm, transcribed from 0xc05a6688 and driven
 * against the model through the machine. This is the test that predicts the
 * boot: everything else checks a register in isolation, and this checks that
 * the loop the guest will actually execute TERMINATES, having moved all 16
 * bytes, with the line down and nothing left pending.
 *
 * The transcription, in the driver's order:
 *
 *   status  = read(0x08)
 *   txFree  = depth - ((status >> 4) & 0xF)
 *   rxLevel =          (status >> 8) & 0xF
 *   if (rxLevel == 0) return false                 <- 0xc05a66e4, the bail-out
 *   repeat rxLevel times: *rxPtr++ = read(0x20)    <- 0xc05a66ec
 *   up to txFree times: write(0x10, *txPtr++), else write(0x10, 0xFF) filler
 *   write(0x08, status)                            <- 0xc05a67d0, the raw word
 *   if (txLeft || rxLeft || dummyLeft) goto top    <- 0xc05a67d4..0xc05a67ec
 *   write(0x04, setup & ~0x100); return true       <- 0xc05a6808
 */
static void test_the_stock_filter_algorithm_terminates(void) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, 0u, 1u << 20), "machine init failed");
    void *c = m.bus.ctx;
    const uint32_t B = S5L8900_SPI1_BASE;

    m.bus.write32(c, S5L8900_GPIO_BASE + S5L_GPIO_FSEL,
                  (6u << 16) | (6u << 8) | 0xfu);   /* release reset; see above */

    /* The driver's own probe bytes. The attached device is a protocol model,
     * so what comes back is only a round trip if it recognised what went out. */
    uint8_t tx[16], rx[16];
    for (unsigned i = 0; i < 16u; i++) {
        tx[i] = (i == 0) ? 0x1au : (i == 1) ? 0xa1u : ((i & 1u) ? 0xe1u : 0x18u);
        rx[i] = 0xcc;
    }
    unsigned tx_left = 16u, rx_left = 16u, dummy_left = 0u, tx_at = 0, rx_at = 0;

    /* The driver's prologue, ending with prefill-arm-go. */
    m.bus.write32(c, B + SPI_CONTROL, 0u);
    m.bus.write32(c, B + SPI_PIN, 0u);
    m.bus.write32(c, B + SPI_STATUS, SPI_STATUS_EVENTS);
    m.bus.write32(c, B + SPI_CLKDIV, 2u);
    m.bus.write32(c, B + SPI_IDD, 0u);
    m.bus.write32(c, B + SPI_STATUS, SPI_STATUS_EVENTS);
    m.bus.write32(c, B + SPI_SETUP, SPI_SETUP_BASE);
    m.bus.write32(c, B + SPI_CONTROL, SPI_CONTROL_START);
    m.bus.write32(c, B + SPI_CNT, 16u);
    m.bus.write32(c, B + SPI_SETUP, SPI_SETUP_BASE | SPI_SETUP_ARM);
    for (unsigned i = 0; i < S5L_SPI_FIFO_DEPTH && tx_left; i++) {
        m.bus.write32(c, B + SPI_TXDATA, tx[tx_at++]);
        tx_left--;
    }
    uint32_t setup = SPI_SETUP_BASE | SPI_SETUP_ARM | SPI_SETUP_GO;
    m.bus.write32(c, B + SPI_SETUP, setup);

    s5l_vic_write(&m.vic[0], VIC_INTENABLE, 1u << S5L8900_IRQ_SPI1);
    s5l8900_tick(&m, 0u);
    CHECK(m.cpu.irq_line, "the go store did not raise the completion interrupt");

    /* The filter. Bounded so a model that never finishes fails loudly rather
     * than hanging the test the way it would hang the guest. */
    unsigned iterations = 0;
    bool done = false;
    while (iterations++ < 64u) {
        uint32_t status = m.bus.read32(c, B + SPI_STATUS);
        unsigned tx_free = S5L_SPI_FIFO_DEPTH -
                           ((status >> SPI_STATUS_TX_SHIFT) & SPI_STATUS_LEVEL);
        unsigned rx_have = (status >> SPI_STATUS_RX_SHIFT) & SPI_STATUS_LEVEL;
        if (rx_have == 0u) break;             /* the bail-out; must not happen */
        for (unsigned i = 0; i < rx_have; i++) {
            uint32_t byte = m.bus.read32(c, B + SPI_RXDATA);
            if (rx_left)          { rx[rx_at++] = (uint8_t)byte; rx_left--; }
            else if (dummy_left)  { dummy_left--; }
        }
        for (unsigned i = 0; i < tx_free; i++) {
            if (tx_left)          { m.bus.write32(c, B + SPI_TXDATA, tx[tx_at++]);
                                    tx_left--; }
            else if (rx_left > (16u - rx_at))
                                  { m.bus.write32(c, B + SPI_TXDATA, 0xffu); }
            else break;
        }
        m.bus.write32(c, B + SPI_STATUS, status);
        if (!tx_left && !rx_left && !dummy_left) {
            setup &= ~(uint32_t)SPI_SETUP_IRQ;
            m.bus.write32(c, B + SPI_SETUP, setup);
            done = true;
            break;
        }
    }

    CHECK(done, "the filter did not complete in %u iterations — this is the "
          "guest sleeping forever", iterations);
    CHECK(tx_left == 0u && rx_left == 0u && rx_at == 16u,
          "the transfer moved tx_left=%u rx_left=%u received=%u of 16",
          tx_left, rx_left, rx_at);
    CHECK(iterations <= 4u,
          "the filter needed %u passes for a 16-byte transfer over an 8-deep "
          "FIFO", iterations);

    /* Every one of the sixteen bytes came from the device. The attached device
     * answers the HBPP probe with a loopback, so the whole buffer must be the
     * probe pattern back again — a controller that dropped or duplicated a
     * word would show up here as a shifted or repeated byte. What the pattern
     * MEANS to the driver is test_mtz2.c's business. */
    bool looped = true;
    for (unsigned i = 0; i < 16u; i++) if (rx[i] != tx[i]) looped = false;
    CHECK(looped, "the controller did not carry all sixteen bytes both ways");

    /* finishTransfer, and then nothing may still be asserted. */
    m.bus.write32(c, B + SPI_SETUP, SPI_SETUP_BASE);
    m.bus.write32(c, B + SPI_STATUS, SPI_STATUS_EVENTS);
    s5l8900_tick(&m, 0u);
    CHECK(!m.cpu.irq_line &&
          (m.vic[0].raw & (1u << S5L8900_IRQ_SPI1)) == 0u,
          "the completed transfer left line 10 asserted — an interrupt storm");
    CHECK(m.spi[1].words == 16u && m.spi[1].tx_drops == 0u &&
          m.spi[1].rx_underruns == 0u && m.spi[1].tx_level == 0u &&
          m.spi[1].rx_level == 0u,
          "the controller did not end clean: words=%llu drops=%llu "
          "underruns=%llu tx=%u rx=%u",
          (unsigned long long)m.spi[1].words,
          (unsigned long long)m.spi[1].tx_drops,
          (unsigned long long)m.spi[1].rx_underruns,
          m.spi[1].tx_level, m.spi[1].rx_level);

    s5l8900_free(&m);
}

/*
 * The wake-source table. Lines 9 and 10 must be declared: a source the wait
 * does not know about can never end a WFI however correctly its device asserts,
 * and that is the class of bug the table was made data-driven to prevent. Both
 * must answer NEVER, because an SPI word completes inside a guest store and a
 * core in WFI issues none — answering UNKNOWN would be safe but would stop the
 * machine fast-forwarding any idle period at all.
 */
static void test_wake_sources_declare_lines_nine_and_ten(void) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, 0u, 1u << 16), "machine init failed");

    const s5l_wake_source_t *src = NULL;
    unsigned n = s5l8900_wake_sources(&src);
    const s5l_wake_source_t *spi0 = NULL, *spi1 = NULL;
    for (unsigned i = 0; i < n; i++) {
        if (src[i].line == S5L8900_IRQ_SPI0) spi0 = &src[i];
        if (src[i].line == S5L8900_IRQ_SPI1) spi1 = &src[i];
    }
    CHECK(spi0 && spi1,
          "the wake table declares no source for line %u / %u",
          S5L8900_IRQ_SPI0, S5L8900_IRQ_SPI1);
    if (!spi0 || !spi1) { s5l8900_free(&m); return; }
    CHECK(spi0->next_edge && spi1->next_edge && spi0->name && spi1->name,
          "an SPI wake source is malformed");
    CHECK(strcmp(spi0->name, "spi0") == 0 && strcmp(spi1->name, "spi1") == 0,
          "SPI wake sources are named %s / %s", spi0->name, spi1->name);

    uint32_t at = 0xdeadbeefu;
    CHECK(spi0->next_edge(&m, &at) == S5L_WAKE_NEVER &&
          spi1->next_edge(&m, &at) == S5L_WAKE_NEVER,
          "an SPI source claimed a future edge it cannot have");

    /* And they must not degrade the reduction: with both lines enabled and a
     * transfer pending, the timer's distance still wins unchanged. */
    m.vic[0].enable = (1u << S5L8900_IRQ_TIMER) | (1u << S5L8900_IRQ_SPI0) |
                      (1u << S5L8900_IRQ_SPI1);
    m.timer.t4_state = TIMER4_STATE_START;
    m.timer.t4_count = m.timer.t4_value = 40u;
    at = 0xdeadbeefu;
    CHECK(s5l8900_next_wake(&m, src, n, &at) == S5L_WAKE_AT && at == 40u,
          "the SPI sources changed the reduction: kind/ticks=%u", at);

    s5l8900_free(&m);
}

/* A transfer can be in flight across a checkpoint — the guest is asleep inside
 * one — so the FIFOs are state, and a restore must land in the destination's
 * own device rather than the source's. */
static void test_snapshot_carries_an_in_flight_transfer(void) {
    s5l8900_t src, dst;
    CHECK(s5l8900_init(&src, 0u, 1u << 16), "source init failed");
    CHECK(s5l8900_init(&dst, 0u, 1u << 16), "destination init failed");

    src.bus.write32(src.bus.ctx, S5L8900_SPI1_BASE + SPI_CNT, 16u);
    src.bus.write32(src.bus.ctx, S5L8900_SPI1_BASE + SPI_CONTROL,
                    SPI_CONTROL_START);
    for (unsigned i = 0; i < 12u; i++)
        src.bus.write32(src.bus.ctx, S5L8900_SPI1_BASE + SPI_TXDATA, 0x60u + i);
    CHECK(src.spi[1].rx_level == S5L_SPI_FIFO_DEPTH &&
          src.spi[1].tx_level == 4u,
          "the source is not mid-transfer: rx=%u tx=%u",
          src.spi[1].rx_level, src.spi[1].tx_level);

    /* Stop an LCD register read between its selector and dummy word, after the
     * ignored first response has already been drained exactly as it is in the
     * guest. The explicit pending bit travels, while the callback context must
     * still point at the destination controller after restore. */
    src.bus.write32(src.bus.ctx, S5L8900_SPI0_BASE + SPI_CNT, 2u);
    src.bus.write32(src.bus.ctx, S5L8900_SPI0_BASE + SPI_TXDATA, 0x95u);
    CHECK(src.bus.read32(src.bus.ctx,
                         S5L8900_SPI0_BASE + SPI_RXDATA) == 0u &&
          src.spi[0].words_left == 1u && src.spi[0].rx_level == 0u &&
          (src.spi[0].cs & S5L_SPI_LCD_READ_PENDING) != 0u,
          "the source is not between the two LCD register-read words");

    uint8_t *blob = NULL;
    size_t blob_len = 0;
    CHECK(snapshot_save_mem(&src, &blob, &blob_len) == SNAP_OK,
          "could not save a mid-transfer snapshot");
    CHECK(snapshot_load_mem(&dst, blob, blob_len) == SNAP_OK,
          "could not restore a mid-transfer snapshot");
    CHECK(memcmp(&dst.spi[1].tx, &src.spi[1].tx, S5L_SPI_FIFO_DEPTH) == 0 &&
          dst.spi[1].tx_level == src.spi[1].tx_level &&
          dst.spi[1].rx_level == src.spi[1].rx_level &&
          dst.spi[1].words == src.spi[1].words &&
          dst.spi[1].words_left == src.spi[1].words_left &&
          dst.spi[1].status == src.spi[1].status,
          "the in-flight transfer did not survive the round trip");
    CHECK(dst.spi[1].slaves[0].transfer != NULL,
          "the restored controller lost its device");
    CHECK((dst.spi[0].cs & S5L_SPI_CS_ROUTE_MASK) == S5L_SPI0_LCD_CS &&
          (dst.spi[0].cs & S5L_SPI_LCD_READ_PENDING) != 0u &&
          dst.spi[0].slaves[S5L_SPI0_LCD_CS].transfer != NULL &&
          dst.spi[0].slaves[S5L_SPI0_LCD_CS].ctx == &dst.spi[0],
          "the restored machine lost lcd0's board route");

    dst.bus.write32(dst.bus.ctx, S5L8900_SPI0_BASE + SPI_TXDATA, 0xffu);
    CHECK(dst.bus.read32(dst.bus.ctx, S5L8900_SPI0_BASE + SPI_RXDATA) ==
              0x01u &&
          (dst.spi[0].cs & S5L_SPI_LCD_READ_PENDING) == 0u,
          "the restored LCD register read did not resume with ready status");
    CHECK(src.spi[0].words_left == 1u && src.spi[0].rx_level == 0u &&
          (src.spi[0].cs & S5L_SPI_LCD_READ_PENDING) != 0u,
          "the restored LCD read mutated the source controller");

    /* The restored transfer resumes into the destination and asserts the
     * destination's own line. */
    uint64_t src_words = src.spi[1].words;
    for (unsigned i = 0; i < S5L_SPI_FIFO_DEPTH; i++)
        (void)dst.bus.read32(dst.bus.ctx, S5L8900_SPI1_BASE + SPI_RXDATA);
    CHECK(dst.spi[1].words == src_words + 4u && dst.spi[1].tx_level == 0u,
          "the restored backlog did not flush: words=%llu tx=%u",
          (unsigned long long)dst.spi[1].words, dst.spi[1].tx_level);
    CHECK(src.spi[1].words == src_words,
          "the restore mutated the source controller");

    free(blob);
    s5l8900_free(&src);
    s5l8900_free(&dst);
}

/* States the model cannot produce must not survive a round trip either. */
static void test_snapshot_rejects_impossible_spi_state(void) {
    s5l8900_t m;
    CHECK(s5l8900_init(&m, 0u, 1u << 16), "machine init failed");
    uint8_t *out = NULL;
    size_t out_len = 0;

    m.spi[1].rx_level = S5L_SPI_FIFO_DEPTH + 1u;
    CHECK(snapshot_save_mem(&m, &out, &out_len) == SNAP_ERR_CORRUPT,
          "an over-deep receive FIFO was snapshotted");
    m.spi[1].rx_level = 0u;

    m.spi[0].tx_level = S5L_SPI_FIFO_DEPTH + 1u;
    CHECK(snapshot_save_mem(&m, &out, &out_len) == SNAP_ERR_CORRUPT,
          "an over-deep transmit FIFO was snapshotted");
    m.spi[0].tx_level = 0u;

    m.spi[1].cs = S5L_SPI_SLAVES;
    CHECK(snapshot_save_mem(&m, &out, &out_len) == SNAP_ERR_CORRUPT,
          "an out-of-range chip select was snapshotted");
    m.spi[1].cs = 0u;

    m.spi[0].cs |= S5L_SPI_LCD_READ_PENDING;
    CHECK(snapshot_save_mem(&m, &out, &out_len) == SNAP_ERR_CORRUPT,
          "an LCD pending bit outside a two-word read was snapshotted");
    m.spi[0].cs &= (uint8_t)~S5L_SPI_LCD_READ_PENDING;

    m.spi[1].cnt = 2u;
    m.spi[1].words_left = 1u;
    m.spi[1].cs |= S5L_SPI_LCD_READ_PENDING;
    CHECK(snapshot_save_mem(&m, &out, &out_len) == SNAP_ERR_CORRUPT,
          "the LCD pending bit was accepted on the touch controller");
    m.spi[1].cs = 0u;
    m.spi[1].cnt = m.spi[1].words_left = 0u;

    /* The level fields are computed on read and never stored, so a status word
     * carrying one is a state the model cannot have produced. */
    m.spi[1].status = 1u << SPI_STATUS_RX_SHIFT;
    CHECK(snapshot_save_mem(&m, &out, &out_len) == SNAP_ERR_CORRUPT,
          "a stored FIFO level in the status word was snapshotted");
    m.spi[1].status = 0u;

    m.spi[0].unknown_off_count = S5L_SPI_UNKNOWN_OFF + 1u;
    CHECK(snapshot_save_mem(&m, &out, &out_len) == SNAP_ERR_CORRUPT,
          "an overflowed unknown-offset count was snapshotted");
    m.spi[0].unknown_off_count = 0u;

    /* Board wiring is host state the file never carries, so it has to match on
     * both sides: a receive FIFO restored behind a controller with no device
     * would resume a transfer that can never finish. */
    s5l_spi_slave_t lcd = m.spi[0].slaves[S5L_SPI0_LCD_CS];
    memset(&m.spi[0].slaves[S5L_SPI0_LCD_CS], 0,
           sizeof m.spi[0].slaves[S5L_SPI0_LCD_CS]);
    CHECK(snapshot_save_mem(&m, &out, &out_len) == SNAP_ERR_CORRUPT,
          "a machine with no lcd0 endpoint was snapshotted");
    m.spi[0].slaves[S5L_SPI0_LCD_CS] = lcd;

    memset(&m.spi[1].slaves[0], 0, sizeof m.spi[1].slaves[0]);
    CHECK(snapshot_save_mem(&m, &out, &out_len) == SNAP_ERR_CORRUPT,
          "a machine with no device on spi1 chip select 0 was snapshotted");
    CHECK(out == NULL && out_len == 0u,
          "a failed snapshot returned an allocation");

    s5l8900_free(&m);
}

int main(void) {
    printf("S5LBox S5L8900 SPI controller tests\n");
    test_reset_and_attachment_are_bounded();
    test_status_level_fields_sit_where_the_driver_looks();
    test_fifo_depth_boundary_is_eight_and_bounded();
    test_status_writeback_is_write_one_to_clear();
    test_register_file_and_word_count();
    test_interrupt_requires_a_byte_the_handler_can_drain();
    test_interrupt_enable_holds_the_line_through_the_prefill();
    test_a_dma_burst_is_not_stalled_by_a_full_receive_fifo();
    test_a_new_transfer_does_not_inherit_the_last_ones_answers();
    test_a_bus_with_no_device_shifts_nothing();
    test_null_device_answers_zero();
    test_lcd_transport_clocks_receive_bytes();
    test_machine_routes_spi_windows_and_irq_lines();
    test_the_stock_filter_algorithm_terminates();
    test_wake_sources_declare_lines_nine_and_ten();
    test_snapshot_carries_an_in_flight_transfer();
    test_snapshot_rejects_impossible_spi_state();
    printf("  %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
