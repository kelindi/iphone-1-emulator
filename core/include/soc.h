/*
 * S5LBox — Samsung S5L8900 system-on-chip model.
 *
 * The S5L8900 is the application processor in the original iPhone, the iPhone
 * 3G, and the iPod touch 1G — the silicon iPhone OS 1–3 ran on. This header
 * declares its memory map and the device models the CPU talks to over the bus.
 *
 * The peripheral base addresses below started as public reverse-engineering of
 * the S5L8900 and are now derived from the shipped firmware itself — see the
 * memory-map block below for the device-tree ranges every one of them is
 * resolved through, and for the two addresses that are ours rather than the
 * SoC's (the NOR window) or the SoC's but unmodelled (edram, vrom, SRAM).
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#ifndef S5LBOX_SOC_H
#define S5LBOX_SOC_H

#include <stddef.h>
#include <stdint.h>
#include "arm.h"

/* ------------------------------------------------------------ memory map
 *
 * CONFIRMED from the shipped device tree (firmware/devicetree.bin, iPhone1,2
 * 7E18). /arm-io carries two ranges triples — (child, parent, size):
 *
 *     00000000 38000000 08000000     child 0x00000000.. -> phys 0x38000000..
 *     10000000 18000000 10000000     child 0x10000000.. -> phys 0x18000000..
 *
 * so a peripheral's `reg` child address becomes a physical address by adding
 * 0x38000000 in the first window and 0x08000000 in the second. Two independent
 * anchors pin the second window, which is the one that matters here:
 *
 *   /arm-io/vrom  reg {0x18000000,0x10000}  -> phys 0x20000000  (the boot ROM,
 *                 the publicly known S5L8900 secure-ROM address)
 *   /arm-io/amc   reg[1] {0x1a000000,0x2c000} -> phys 0x22000000, and
 *                 firmware/llb.bin carries 0x22000000 at file+0x310 as its own
 *                 link address — the bootrom loads LLB into SRAM there.
 *
 * The resulting physical map, with what is and is not modelled:
 *
 *   0x08000000  DRAM base. 128 MB fitted on an iPhone1,2 (ends 0x10000000).
 *   0x18000000  edram, 0x140000                    NOT MODELLED
 *   0x20000000  vrom (boot ROM), 0x10000           NOT MODELLED
 *   0x22000000  SRAM / AMC window, 0x2c000         NOT MODELLED
 *   0x28000000  <- first physical byte the device tree assigns to NOTHING
 *   0x38000000  arm-io, 0x08000000 — every modelled peripheral lives here
 *
 * The NOR is NOT in this map. The device tree describes it as an SPI slave —
 * /arm-io/spi0/nor-flash, compatible "nor-flash,spi", with a flash-RELATIVE
 * address space (ranges {0,0,0x100000}, which is where S5L8900_NOR_SIZE's 1 MiB
 * comes from) and partitions at flash offsets (nvram @0xfc000, raw-device
 * @0x8000). See S5L8900_NOR_BASE for what our memory window therefore is.
 *
 * The DRAM aperture ceiling: DRAM starts at 0x08000000 and the next thing the
 * SoC decodes is edram at 0x18000000, so no DRAM configuration above 256 MB can
 * be physically real on this part. We allow larger anyway (a RAM-disk root does
 * not fit otherwise); s5l8900_ram_conflict() is what keeps that fiction from
 * becoming a silent alias.
 */
#define S5L8900_SRAM_BASE   0x22000000u   /* SRAM / AMC window, size 0x2c000 */
#define S5L8900_SRAM_SIZE   0x0002c000u
#define S5L8900_VROM_BASE   0x20000000u   /* boot ROM                        */
#define S5L8900_VROM_SIZE   0x00010000u
#define S5L8900_EDRAM_BASE  0x18000000u
#define S5L8900_EDRAM_SIZE  0x00140000u
#define S5L8900_SDRAM_BASE  0x08000000u
#define S5L8900_ARMIO_BASE  0x38000000u   /* /arm-io ranges parent           */
#define S5L8900_ARMIO_SIZE  0x08000000u
#define S5L8900_UART0_BASE  0x3cc00000u
/*
 * uart4, the fourth of the four serial ports the shipped tree declares, and
 * the one this machine carries guest PPP over. See the UART section below for
 * the whole rationale; the address is derived exactly as every other /arm-io
 * child's is (child offset + 0x38000000), and run59's non-RAM page report
 * independently attributes live traffic at 0x3cc10000 to AppleS5L8900XSerial.
 *
 * The four ports and their children, from the shipped device tree:
 *
 *   uart0  0x3cc00000  VIC 24  `iap`        taken: this is the kprintf console
 *   uart1  0x3cc04000  VIC 25  `umts`       the baseband's line
 *   uart3  0x3cc0c000  VIC 27  `bluetooth`  contended: BTServer ships
 *   uart4  0x3cc10000  VIC 28  `debug`      free: nothing on the image owns it
 *
 * uart2 is genuinely absent from the tree; the numbering is Apple's, not a gap
 * we introduced.
 *
 * VIC line 28 is what the nub's `interrupts` property says. It used to be
 * recorded here in prose and deliberately NOT given a constant, because a
 * transmit-only port never raises a line and a constant that looks wired but is
 * not is the landmine the old S5L8900_GPIO_BASE was. The receive path is the
 * day that comment named: the constant is below, and s5l8900_tick() drives it
 * from uart4's receive FIFO. It still cannot assert on a run with no host peer,
 * because nothing but s5l_uart_rx_push() can put a byte in that FIFO.
 */
#define S5L8900_UART4_BASE  0x3cc10000u
#define S5L8900_IRQ_UART4   28u   /* /arm-io/uart4 `interrupts` = {28} */
#define S5L8900_VIC0_BASE   0x38e00000u
#define S5L8900_TIMER_BASE  0x3e200000u
#define S5L8900_CLOCK_BASE  0x3c500000u
/*
 * GPIO. CONFIRMED against two independent sources: the shipped device tree's
 * /arm-io/gpio has reg {0x6400000,0x1000} and arm-io maps child+0x38000000, and
 * a walk of the guest's live page tables from the VA AppleS5L8900XGPIOIC prints
 * lands on the same page. The value here was previously 0x3cf00000 — the
 * S5L8720-era GPIO address, wrong for this SoC. It was unused, so it was a
 * landmine rather than a live bug; it is corrected rather than left to be
 * "confirmed" by whoever wired it up next.
 */
#define S5L8900_GPIO_BASE   0x3e400000u
#define S5L8900_MIU_BASE    0x38100000u   /* clkrstgen's second reg range */
#define S5L8900_EDGEIC_BASE 0x38e02000u
/*
 * The two I2S controllers. /arm-io/i2s0 carries reg {0x4a00000,0x1000} and
 * /arm-io/i2s1 {0x4d00000,0x1000}, and arm-io maps child+0x38000000 — the same
 * derivation every other peripheral here uses. The guest's own driver prints a
 * mapped VA for each on every boot, so the pair is confirmed twice.
 *
 * As with uart4 there is deliberately NO S5L8900_IRQ_I2S0/1 constant. The
 * `interrupts` properties say 0x86 and 0xaa, but both nodes name
 * `interrupt-parent = /arm-io/gpio` — they are GPIO-IC lines, not VIC lines —
 * and nothing in this model raises either. A constant that looks wired but is
 * not is the landmine the old S5L8900_GPIO_BASE was; it joins the header the
 * day something can actually assert it.
 */
#define S5L8900_I2S0_BASE   0x3ca00000u
#define S5L8900_I2S1_BASE   0x3cd00000u
#define S5L8900_I2S_COUNT   2u
#define S5L8900_DEV_SIZE    0x00001000u   /* per-peripheral window */

/*
 * The three SPI controllers, confirmed the same two ways GPIO was.
 * /arm-io/spi0, /arm-io/spi1 and /arm-io/spi2 carry reg {0x4300000,0x1000},
 * {0x4e00000,0x1000} and {0x5200000,0x1000}, and arm-io maps child+0x38000000;
 * run23's non-RAM page report independently attributes live guest traffic at
 * each resulting physical page to AppleS5L8900XSPIController (spi0/spi1) and
 * com.apple.driver.BasebandSPI (spi2).
 *
 * spi2 is the baseband transport: compatible "spi,s5l8900x,baseband", with the
 * SRDY and MRDY handshake lines exposed as GPIO platform functions and DMA
 * channel descriptors pointing at 0x3d200010/0x3d200020.
 *
 * Their device-tree interrupt numbers are NOT three numbers of the same kind,
 * and writing them as "9/10/7" conflated two different interrupt controllers.
 * From the shipped tree:
 *
 *   /arm-io/spi0  interrupts {0x9}      interrupt-parent 0x00b04cc0 = /arm-io/vic
 *   /arm-io/spi1  interrupts {0xa}      interrupt-parent 0x00b04cc0 = /arm-io/vic
 *   /arm-io/spi2  interrupts {0x7,0x2}  interrupt-parent 0x00b05320 = /arm-io/gpio
 *
 * so spi0 and spi1 really are VIC vectors 9 and 10, while spi2's 7 is a GPIO
 * interrupt — a two-cell specifier, /arm-io/gpio having #interrupt-cells 2 —
 * on a controller that is itself a VIC child. It is emphatically not VIC
 * vector 7, which on this SoC is the timer (S5L8900_IRQ_TIMER).
 *
 * The three controllers use consecutive direct VIC lines; spi2 is vector 11.
 *
 * spi2 carries both the M68 Z1 touch controller and the baseband transport.
 * The latter still has no protocol model, but routing the window through the
 * controller lets each attached endpoint observe the traffic.
 */
#define S5L8900_SPI0_BASE   0x3c300000u
#define S5L8900_SPI1_BASE   0x3ce00000u
#define S5L8900_SPI2_BASE   0x3d200000u
#define S5L8900_IRQ_SPI0    9u
#define S5L8900_IRQ_SPI1    10u
#define S5L8900_IRQ_SPI2    11u
#define S5L8900_SPI_COUNT   3u

/*
 * The Synopsys DesignWare USB 2.0 OTG (DWC2) controller. CONFIRMED from the
 * shipped device tree the same way the SPI trio above was: /arm-io/usb-otg has
 * reg {0x00400000,0x1000} and compatible "usb-otg,s5l8900x", and /arm-io maps
 * child + 0x38000000.
 *
 * Only the hardware-configuration registers are modelled. See the DWC2 section
 * further down for exactly which four, why those values, and what this
 * deliberately does not claim to be.
 */
#define S5L8900_USB_OTG_BASE 0x38400000u

/*
 * There are TWO PL192 VICs, not one. The device tree gives the block as
 * reg {0xe00000, 0x2000} with vic-stride 0x1000, and AppleARMPL192VIC maps both
 * pages. The interrupt numbering is flat across the pair: /arm-io/gpio lists
 * lines 0x20 and 0x21, /arm-io/wdt line 0x33, /arm-io/sdio line 0x2a — all past
 * VIC0's 32 lines, so they can only be VIC1 lines 0, 1, 19 and 10.
 */
#define S5L8900_VIC1_BASE   0x38e01000u
#define S5L8900_VIC_COUNT   2u

/* --------------------------------------------------------------- UART ---
 * Samsung-style UART (as on S3C-family parts). Writing a byte to UTXH
 * transmits it; UTRSTAT reports the transmitter permanently ready, so guest
 * "wait until TX empty" spin loops make progress immediately.
 *
 * TWO INSTANCES, ONE MODEL. uart0 is the kprintf console. uart4 is the port
 * the guest's own /usr/sbin/pppd is pointed at (see S5L8900_UART4_BASE above
 * and docs/networking.md). Transmit is captured on both. Receive exists on
 * both — one model, one FIFO — but only uart4's is wired to a VIC line, and
 * only a host that calls s5l_uart_rx_push() can put a byte in either.
 *
 * REGISTER SEMANTICS, read out of AppleS5L8900XSerial rather than guessed
 * (docs/derivations.md §23.5.1):
 *
 *   UFSTAT (+0x18)  bits[3:0] receive count, bit 8 receive full,
 *                   bits[7:4] transmit count, bit 9 transmit full.
 *                   FIFO depth is 16.
 *   UTRSTAT (+0x10) is NOT a read-only status word. The interrupt filter at
 *                   0xc065eed8 reads it, masks, and writes the result back,
 *                   so the hardware register is write-one-to-clear. Apple's
 *                   own apple_uart_ack_irq() acknowledges the same way: build
 *                   a zero word, set one status bit, store it.
 *
 * THE RECEIVE PATH, and what is modelled versus what is deliberately not.
 *
 * s5l_uart_rx_push() is the ONLY way a byte enters the receive FIFO. Nothing
 * inside core/ ever calls it: a host that has attached a peer calls it between
 * run slices, which is the handoff docs/derivations.md §23.5.1 requires
 * (never from a socket callback — core/ has no threading vocabulary). So on
 * every run without a peer the FIFO is empty at every instant, and the three
 * registers below answer exactly what they answered when this model was
 * transmit-only. That is not a coincidence to be preserved by luck; it is
 * asserted in core/tests/test_uart4.c.
 *
 *   UFSTAT   bits[3:0] = rx_count, bit 8 = rx_count == UART_RX_FIFO. The count
 *            field is four bits and the depth is sixteen, so a full FIFO shows
 *            0 in the field and 1 in the full bit — the standard Samsung
 *            encoding, and the reason the full bit exists at all.
 *   UTRSTAT  bit 0 = "receive data ready" = rx_count != 0. A LEVEL derived from
 *            the FIFO, not a latch: it clears when the guest drains URXH.
 *            bit 4 = the LATCHED receive interrupt, set by an arriving byte and
 *            cleared only by the driver's own store. The two halves of this
 *            register are the subject of the block below.
 *   URXH     dequeues one byte, or answers 0 when empty. Answering anything
 *            else for an empty FIFO would be inventing a byte the host never
 *            sent, which is the one thing these models may not do.
 *
 * ---------------------------------------------------------------------------
 * UTRSTAT IS TWO REGISTERS IN ONE, and which half a bit is in decides this
 * port's whole interrupt behaviour. Apple names every bit in current XNU
 * (pexpert/pexpert/arm/apple_uart_regs.h); Linux names four of the same ones
 * (APPLE_S5L_UTRSTAT_RXTHRESH = BIT(4), RXTO_LEGACY = BIT(3)) and m1n1 agrees:
 *
 *   0x001 receive_buffer_data_ready     0x010 receive_interrupt_status
 *   0x002 transmit_buffer_empty         0x020 transmit_interrupt_status
 *   0x004 transmitter_empty             0x040 error_interrupt_status
 *   0x008 receive_time_out_int_status   0x100 auto_baud_interrupt_status
 *                                       0x200 new_receive_time_out_int_status
 *
 * The first three are LEVELS with a live source behind them — status, which is
 * what a polled console loop reads and what the three lines above describe. The
 * rest are LATCHED interrupt causes: "internal interrupt sources are treated as
 * edge-triggered, even though the IRQ output is level-triggered" (the upstream
 * Linux commit for this variant). They are also exactly the set the driver's
 * own enable mask can hold, which is the second, independent way the two halves
 * were told apart here — read out of the binary, not inferred:
 *
 *   0xc065eecc  the filter, registered as the IOFilterInterruptEventSource
 *               filter action by AppleS5L8900XSerial::start at 0xc065e734.
 *               It computes  r5 = UTRSTAT(+0x10) & this->0x9c , writes r5 back
 *               to +0x10 — THAT WRITE IS THE ACKNOWLEDGE — and then tests ONLY
 *               bits 0x100, 0x40, 0x8, 0x10 and 0x20. Its return value is r6,
 *               and the sole `mov r6, #1` is on the 0x8 branch, so r5 == 0
 *               returns 0.
 *   0xc065f0e4  the only writer of this->0x9c in the whole kext (two stores,
 *               no branch between them). Three boolean arguments contribute
 *               0x18, 0x20 and 0x140 respectively, and each also sets UCON
 *               (+0x04) bits 0x1880, 0x2000 and 0x14000. So the mask can hold
 *               ONLY {0x8,0x10,0x20,0x40,0x100} — bit 0x1 can never enter it —
 *               and every enable sits eight bits above the status bit it gates:
 *               0x1000 enables 0x10, 0x800 enables 0x8, 0x2000 enables 0x20,
 *               0x4000 enables 0x40, 0x10000 enables 0x100. (UCON 0x80 is
 *               rx_time_out_enable, and 0x200's enable is UCON bit 9, outside
 *               the rule — neither matters while neither status bit is ever
 *               asserted.)
 *   0xc018b70a  IOFilterInterruptEventSource::disableInterruptOccurred returns
 *               HERE when the filter returns 0 — before the
 *               provider->disableInterrupt(source) at 0xc018b718. A level that
 *               the filter rejects is therefore never masked.
 *
 * A sweep of 0xc065d000..0xc0662000 in both ARM and Thumb finds exactly two
 * instructions that touch +0x10 at all (the read and the write-back above).
 * NOTHING in this driver tests UTRSTAT bit 0.
 *
 * WHAT THAT COST. This model used to assert bit 0 as the interrupt, i.e. a bit
 * that can never enter the driver's mask: the filter therefore returned 0, the
 * level was never masked, and the VIC re-dispatched it forever. run94: 6,393,888
 * IRQ entries, 44.5% of the run in IRQ mode, disableInterruptOccurred hot in
 * the profile, and one octet of PPP transmitted instead of forty-seven. run80,
 * before the receive path existed: 2,614 and 0.2%. Withholding the line
 * (--no-uart4-rx-irq) restored run80's behaviour with byte delivery unchanged,
 * which is what turned the explanation into a demonstrated cause.
 *
 * SO: 0x10 IS LATCHED, AND NOTHING ELSE IS.
 *
 *   - A byte arriving in the receive FIFO sets pending bit 0x10. Every arrival,
 *     not just the empty->non-empty one: a driver that acknowledges, drains
 *     part of the FIFO and stops would otherwise never be told about the rest.
 *   - Bits 0, 1 and 2 stay LEVELS and are never latched. They are status, the
 *     polled console path depends on them, and bit 0 is what a drain loop tests
 *     to know when to stop.
 *   - A store to UTRSTAT clears exactly the pending bits set in the value. It
 *     cannot touch a level, because no level is stored in the latch.
 *   - The VIC line is (pending & the UCON enables), so a driver that has not
 *     enabled the receive interrupt never sees it. The gate this model once
 *     declined to guess is legible now: 0xc065f0e4 sets the enable and the mask
 *     in the same straight-line instruction stream.
 *   - 0x8, 0x40, 0x100 and 0x200 are NEVER asserted. 0x8's handler reports an
 *     overrun that did not happen; 0x100 runs an auto-baud calculation over
 *     +0x2c, which this model answers 0 from and which is only safe while that
 *     branch is never taken; 0x200 is newer than this kext, which neither tests
 *     it nor enables it.
 *
 * THE ACKNOWLEDGE CLEARS 0x10 EVEN WITH THE FIFO STILL FULL, and that is the
 * load-bearing half of the repair rather than a corner case. The filter's
 * write-back is the only thing that lowers this line: it returns 0 for a receive
 * interrupt (the sole `mov r6, #1` is on the 0x8 branch), so
 * disableInterruptOccurred returns before disableInterrupt() and NOTHING MASKS
 * the line. Re-arming the latch because the FIFO still held data would put the
 * level straight back up against a driver that has already declined to mask it,
 * which is run94 again with a different bit number. The byte is not stranded by
 * this: bit 0 is still a live level, so the driver's drain loop keeps reading
 * until the FIFO is empty, and the next arrival latches a fresh edge.
 * ---------------------------------------------------------------------------
 *
 * FLOW CONTROL is not modelled and does not need to be: uart1 and uart4 carry
 * `no-flow-control`, so AppleS5L8900XSerial's getFlowStatus short-circuits to
 * "asserted" at 0xc065e0bc without ever reading UMSTAT. uart3 is the only port
 * that would have needed it, which is one of the two reasons uart4 was chosen
 * over uart3 — the other being that BTServer ships and contends for uart3.
 *
 * THE CAPTURE IS A FIRST-N CAP, NOT A RING. tx_len stops growing at
 * UART_TX_BUFFER - 1, so a long run keeps the FIRST 8191 bytes and discards
 * the tail. For uart0 that is a known defect (§23.2: roughly half of every
 * boot's console is lost). For uart4 it is the policy we want: the observable
 * is pppd's FIRST LCP Configure-Request, so keeping the head is keeping the
 * evidence. Do not "fix" this into a ring without re-reading both call sites.
 */
#define UART_ULCON   0x00u
#define UART_UCON    0x04u
#define UART_UFCON   0x08u
#define UART_UMCON   0x0cu
#define UART_UTRSTAT 0x10u
#define UART_UERSTAT 0x14u
#define UART_UFSTAT  0x18u
#define UART_UMSTAT  0x1cu
#define UART_UTXH    0x20u
#define UART_URXH    0x24u
#define UART_UBRDIV  0x28u

/*
 * UTRSTAT's latched receive cause, Apple's own receive_interrupt_status. It is
 * named in the header rather than kept private to core/src/soc/uart.c for one
 * reason: core/src/snapshot.c re-derives it on restore, and a bare 0x10 there
 * would be a magic number in the one file where a wrong bit is silent. Every
 * other bit of the register stays private, because uart.c is the only place
 * entitled to decide that one of them is set.
 */
#define UTRSTAT_RX_INT 0x010u

#define UART_TX_BUFFER 8192

/* AppleS5L8900XSerial's own depth, read out of UFSTAT's field widths: a
 * four-bit count plus a separate full bit is exactly sixteen entries. */
#define UART_RX_FIFO 16u

typedef struct {
    uint32_t ulcon, ucon, ufcon, umcon, ubrdiv;
    char     tx[UART_TX_BUFFER];   /* everything the guest has printed */
    size_t   tx_len;

    /*
     * The receive FIFO, and four counters that exist because "the guest never
     * read a byte" and "the host never sent one" are different failures with
     * different next steps, and nothing else in the machine can tell them
     * apart. rx_dropped in particular is the back-pressure signal: the host
     * pushed into a full FIFO and the byte is GONE, which for a PPP stream
     * means a frame the peer will have to retransmit.
     */
    uint64_t rx_pushed;            /* bytes the host handed to this port    */
    uint64_t rx_dropped;           /* pushes refused: the FIFO was full     */
    uint64_t rx_reads;             /* URXH reads that returned a real byte  */
    uint64_t rx_underruns;         /* URXH reads of an empty FIFO           */
    uint8_t  rx[UART_RX_FIFO];
    uint8_t  rx_head;              /* index of the next byte out            */
    uint8_t  rx_count;             /* 0 .. UART_RX_FIFO                     */

    /*
     * HARNESS POLICY, NOT DEVICE STATE — see s5l_uart_set_rx_irq(). Zero is the
     * hardware-faithful behaviour, so a memset()-initialised port asserts and a
     * run that never names the flag is byte for byte the run it was before this
     * field existed. s5l_uart_reset() clears it like everything else, which is
     * why the setter must be called AFTER s5l8900_init(); see its contract.
     *
     * It is deliberately NOT in snap_uart(): a snapshot carries what the guest
     * did, and this is what the operator asked for, so it is re-applied from the
     * restoring run's command line instead of travelling in the stream. It fits
     * in the tail padding this struct already had, so SNAP_SIZE_GUARD's 8280
     * still holds and no SNAPSHOT_VERSION bump is owed — which is the only
     * reason a new field here is not a snapshot change. Check that again before
     * adding a second one.
     */
    bool     rx_irq_suppressed;

    /*
     * UTRSTAT's LATCHED half — today only 0x10, receive_interrupt_status. Set
     * by an arriving byte, cleared by the driver's write-one-to-clear store,
     * and ANDed with the UCON enables to produce the VIC line. The levels (bits
     * 0, 1, 2) are deliberately not here: they are derived from the FIFO on
     * every read, which is what makes "a W1C store cannot clear a level" a
     * property of the layout rather than a rule someone has to remember.
     *
     * Placed last on purpose. It lands in the tail padding this struct already
     * had, so sizeof stays 8280 and core/src/snapshot.c's guard — and the
     * machine's, which contains two of these — do not move.
     *
     * NOT serialised, for the reason `level_dirty` is not: snap_uart() DERIVES
     * it on restore, from the FIFO that does travel, in the only direction that
     * cannot lose an interrupt. See the note there before changing that.
     */
    uint32_t utrstat_pending;
} s5l_uart_t;

void     s5l_uart_reset(s5l_uart_t *u);
uint32_t s5l_uart_read(s5l_uart_t *u, uint32_t off);
void     s5l_uart_write(s5l_uart_t *u, uint32_t off, uint32_t val);

/*
 * Hand one byte to the port as if it had arrived on the wire. Returns false and
 * counts a drop when the FIFO is full — it never blocks and never grows, so the
 * CPU thread cannot be stalled by a host that is producing faster than the
 * guest consumes (docs/networking.md §6).
 *
 * MUST be called between run slices, on the CPU thread. See the receive-path
 * block above.
 */
bool     s5l_uart_rx_push(s5l_uart_t *u, uint8_t byte);
/* How many more bytes s5l_uart_rx_push() would accept right now. */
unsigned s5l_uart_rx_space(const s5l_uart_t *u);
/*
 * The port's interrupt line: (UTRSTAT's latched half & the UCON enables) != 0.
 *
 * NOT "the FIFO is non-empty". The latch is set by an arriving byte and cleared
 * by the driver's acknowledge, so this can be false with bytes still queued —
 * which is the point, because the driver's filter declines to mask the line and
 * only the acknowledge can lower it. The UART block above has the evidence and
 * the run94 measurement that forced it.
 *
 * The line is still a LEVEL as far as core/src/soc/vic.c is concerned, exactly
 * as the hardware's IRQ output is: it stays asserted until the acknowledge,
 * so a handler cannot miss an edge that arrived while it was masked.
 */
bool     s5l_uart_rx_irq(const s5l_uart_t *u);
/*
 * Suppress that line without touching anything else: bytes still arrive, the
 * FIFO still fills, UTRSTAT reports both its levels and its latch, UFSTAT still
 * counts, URXH still dequeues, a W1C store still acknowledges — only
 * s5l_uart_rx_irq() is forced false, so the VIC line never asserts. `enabled` is
 * the interrupt, so s5l_uart_set_rx_irq(u, false) is the suppression; true
 * restores the default.
 *
 * This exists for ONE experiment, and it is a control rather than a fix. run94
 * (with this path enabled, asserting the wrong bit) took 6,393,888 IRQ entries
 * and spent 44.5% of the run in IRQ mode, against run80's 2,614 and 0.2% before
 * the receive path existed; suppressing the line reproduced run80's interrupt
 * behaviour while leaving byte delivery exactly as it was, which is the only
 * way to separate "the interrupt broke the guest" from "the byte did". It said
 * the interrupt, and the repair went where that answer pointed — into the bits
 * this model reports, not into this flag. The flag stays because that control
 * is the one to re-run the day this line misbehaves again.
 *
 * MUST be called AFTER s5l8900_init(), which resets every port and would clear
 * it. There is no second reset path today; if one appears, it will clear this
 * too, and a control run would stop being a control halfway through without
 * saying so. core/tests/test_uart4.c pins both halves of that.
 */
void     s5l_uart_set_rx_irq(s5l_uart_t *u, bool enabled);

/* ---------------------------------------------------------------- VIC ---
 * PL190-style vectored interrupt controller. Devices assert lines into `raw`;
 * the controller ORs in software interrupts, masks by `enable`, and routes each
 * line to IRQ or FIQ according to `select`.
 */
#define VIC_IRQSTATUS    0x00u
#define VIC_FIQSTATUS    0x04u
#define VIC_RAWINTR      0x08u
#define VIC_INTSELECT    0x0cu
#define VIC_INTENABLE    0x10u
#define VIC_INTENCLEAR   0x14u
#define VIC_SOFTINT      0x18u
#define VIC_SOFTINTCLEAR 0x1cu
#define VIC_VECTADDR0    0x100u  /* per-source ISR address bank, 32 entries    */
#define VIC_VECTADDR     0xf00u  /* PL192 vectored dispatch: read = source|bit31, write = EOI */

typedef struct { uint32_t raw, enable, select, soft; } s5l_vic_t;

void     s5l_vic_reset(s5l_vic_t *v);
uint32_t s5l_vic_read(s5l_vic_t *v, uint32_t off);
void     s5l_vic_write(s5l_vic_t *v, uint32_t off, uint32_t val);
void     s5l_vic_set_line(s5l_vic_t *v, unsigned line, bool level);
bool     s5l_vic_irq(const s5l_vic_t *v);
bool     s5l_vic_fiq(const s5l_vic_t *v);
/* PL192 VICADDRESS: highest-priority pending source tagged with bit 31, or 0.
 * base_source positions this VIC in the daisy chain (0 for VIC0, 32 for VIC1). */
uint32_t s5l_vic_vectaddr(const s5l_vic_t *v, unsigned base_source);

/* -------------------------------------------------------------- timer ---
 * The real S5L8900 timer block, as used by XNU rather than as invented by us.
 *
 * The layout below is not guesswork: instrumenting the bus and correlating each
 * access against the kernel's own symbols shows exactly which registers matter
 * and what it expects of them.
 *
 *   _s5l8900x_get_timebase    reads 0x080/0x084 as a free-running 64-bit
 *                             counter. This is mach_absolute_time(). It must
 *                             count whether or not any timer is "enabled", or
 *                             every delay loop in the kernel spins forever.
 *   _pe_arm_init_interrupts   programs timer 4 at 0x0A0-0x0AF and then routes
 *                             VIC line 7 to *FIQ*, not IRQ.
 *   _s5l8900x_set_decrementer writes the next deadline to 0x0A8.
 *   _fleh_fiq_s5l8900x        acknowledges by writing 0x00030000 to the latch.
 *
 * That acknowledge mask is load-bearing. Latching any other bit pattern leaves
 * the line asserted after the handler returns, which produces an interrupt
 * storm rather than a scheduler tick.
 *
 * The status alias at 0x10000 sits outside the usual 4 KB peripheral window,
 * which is why the timer's window is widened (see S5L8900_TIMER_SIZE).
 */
#define TIMER_TICKSHIGH  0x0080u   /* free-running counter, high word */
#define TIMER_TICKSLOW   0x0084u   /* free-running counter, low word  */
#define TIMER_CONFIG     0x0088u
#define TIMER4_CONFIG    0x00a0u
#define TIMER4_STATE     0x00a4u   /* bit0 start, bit1 update-from-buffer */
#define TIMER4_COUNTBUF  0x00a8u   /* deadline written by set_decrementer  */
#define TIMER4_COUNTBUF2 0x00acu
#define TIMER4_VALUE     0x00b4u   /* live down-count */
#define TIMER_IRQACK     0x00f4u   /* write-1-to-clear */
#define TIMER_IRQLATCH   0x00f8u
#define TIMER_IRQSTATUS  0x10000u  /* alias, outside the 4 KB window */

#define TIMER4_STATE_START  (1u << 0)
#define TIMER4_STATE_UPDATE (1u << 1)
#define TIMER4_IRQ_BITS  0x00030000u  /* what _fleh_fiq_s5l8900x acks */

/* Widened so TIMER_IRQSTATUS at 0x10000 falls inside the timer's window. */
#define S5L8900_TIMER_SIZE 0x00011000u

#define S5L8900_IRQ_TIMER 7u     /* VIC line the timer drives (routed to FIQ) */

/*
 * The guest's clocks, as we advertise them to it in the device tree.
 *
 * This ratio is a real design parameter, not bookkeeping. On the hardware the
 * CPU runs at 412 MHz while the timebase counts at 6 MHz, so one timebase tick
 * costs about 68 CPU cycles. Advancing the timebase once per retired
 * instruction instead makes guest time run ~68x fast relative to guest work,
 * and the kernel then cannot finish servicing one timer deadline before the
 * next one is already in the past: it clamps the decrementer to its minimum,
 * re-enters immediately, and livelocks. That is not a hypothetical -- it burned
 * 66% of a 200M-instruction boot in the FIQ handler.
 *
 * During active execution one retired instruction is treated as one CPU
 * cycle, which is optimistic for an ARM1176 but keeps the ratio honest in the
 * direction that matters.  WFI can advance the same clock without retiring
 * instructions; cpu.cycles remains a retired-instruction counter.
 */
#define S5L8900_CPU_HZ 412000000u
#define S5L8900_TB_HZ    6000000u

typedef struct {
    uint64_t ticks;              /* free-running; never gated by any enable */
    uint32_t config;
    uint32_t t4_config, t4_state;
    uint32_t t4_count, t4_count2, t4_value;
    uint32_t irqlatch;
} s5l_timer_t;

void s5l_timer_reset(s5l_timer_t *t);
uint32_t s5l_timer_read(s5l_timer_t *t, uint32_t off);
void s5l_timer_write(s5l_timer_t *t, uint32_t off, uint32_t val);
/* Advance by `ticks`; returns true while an interrupt is pending. */
bool s5l_timer_tick(s5l_timer_t *t, uint32_t ticks);

/* ----------------------------------------------------------- NOR flash ---
 * On the S5L8900 the low-level boot images (LLB, iBoot, the device tree, the
 * boot logo) live in a small NOR flash reached over SPI. We model it as a
 * read-only memory-mapped region plus a directory built by *scanning* for IMG3
 * containers.
 *
 * Scanning is deliberate: Apple's exact NOR image-table layout for this SoC
 * could not be verified from a primary source, and guessing a structure would
 * be a silent source of wrong behaviour. Scanning for the IMG3 magic works
 * whatever the surrounding directory format turns out to be, and can be
 * replaced with a real table reader once the layout is confirmed against a
 * genuine dump.
 */
/*
 * WHERE THE NOR WINDOW IS, AND WHY IT MOVED.
 *
 * This aperture is OURS, not the SoC's. The shipped device tree does not map
 * the NOR into the physical address space at all: it is an SPI slave under
 * /arm-io/spi0 with a flash-relative 1 MiB space (see the memory-map block at
 * the top of this header). We expose it as memory because that is the cheapest
 * honest way to give a guest payload the read/program path an untethered
 * jailbreak needs, without a full SPI protocol model.
 *
 * Because the address is ours, it has to be chosen so that it cannot collide
 * with anything — and 0x24000000, the value this had until now, failed that.
 * It sat inside the DRAM window we actually boot with (-R 512 gives DRAM
 * 0x08000000..0x28000000), and since bus_read tested RAM first, every NOR read
 * silently returned RAM-disk bytes. Nothing faulted and nothing logged.
 * 0x24000000 also had no source: it appears in no shipped artifact — not in the
 * device tree, not in firmware/llb.bin — and the commit that introduced it
 * cited none.
 *
 * 0x28000000 is picked from evidence, and two independent lines land on it:
 *
 *   - it is the first physical byte the shipped device tree assigns to nothing:
 *     /arm-io's second range covers phys 0x18000000..0x28000000 and its first
 *     covers 0x38000000..0x40000000;
 *   - it is the first byte above the largest DRAM the kernel can use. xnu-1357
 *     arm_vm_init fixes virtual_avail at 0xe0000000, so with gVirtBase
 *     0xc0000000 mem_size cannot exceed 512 MB, and DRAM cannot reach past
 *     0x08000000 + 512 MB = 0x28000000.
 *
 * So no RAM aperture this machine will accept can reach it, and no device tree
 * region claims it. Should either of those stop being true, s5l8900_init()
 * refuses to build the machine rather than aliasing anything — see
 * s5l8900_ram_conflict().
 */
#define S5L8900_NOR_BASE 0x28000000u
#define S5L8900_NOR_SIZE 0x00100000u   /* 1 MiB — /arm-io/spi0/nor-flash ranges */
/*
 * M68 (iPhone1,1) is different: its 1 MiB NOR is a parallel CFI part on the
 * memory bus. The 1A543a device tree has /arm-io/nor-flash reg {0x1c000000,
 * 0x100000} under arm-io's second range {0x10000000 -> 0x18000000, size
 * 0x10000000}, i.e. phys 0x24000000, and the 1.0 kernel's nor-flash,cfi and
 * nvram,chrp drivers read exactly that window (offsets 0x0/0x5000/0xa000 for
 * CFI, 0xfc000..0xfffff for the two NVRAM copies).
 */
#define S5L8900_M68_NOR_BASE  0x24000000u
#define S5L8900_M68_NVRAM_OFF 0x000fc000u  /* /arm-io/nor-flash/nvram reg */
#define S5L8900_M68_NVRAM_LEN 0x00002000u  /* each of the two copies */
#define S5L_NOR_MAX_IMAGES 16

typedef struct {
    uint32_t ident;     /* IMG3 ident, e.g. 'illb', 'ibot', 'dtre' */
    uint32_t offset;    /* byte offset within the NOR              */
    uint32_t size;      /* container size (fullSize)               */
} s5l_nor_entry_t;

typedef struct {
    uint8_t        *data;
    uint32_t        size;
    s5l_nor_entry_t images[S5L_NOR_MAX_IMAGES];
    unsigned        image_count;
} s5l_nor_t;

/* NOR erase granularity. Programming can only clear bits (as on real flash);
 * an erase restores a whole sector to 0xFF. */
#define S5L8900_NOR_SECTOR 0x1000u

bool     s5l_nor_init(s5l_nor_t *n, uint32_t size);
void     s5l_nor_free(s5l_nor_t *n);
uint32_t s5l_nor_read(const s5l_nor_t *n, uint32_t off, unsigned bytes);
/* Copy `len` bytes into the NOR at `off` (as a factory flasher would). This is
 * an unconditional overwrite, used to lay down an initial image. */
void     s5l_nor_program(s5l_nor_t *n, uint32_t off, const void *src, size_t len);

/*
 * Flash-accurate programming: bits can only go 1 -> 0. Returns false if the
 * write would need to set a bit back to 1 (erase the sector first) or is out of
 * range. This is the path guest writes take, so a guest payload can persist
 * itself into NOR — which is exactly what an untethered jailbreak such as
 * 24kpwn does on this SoC.
 */
bool     s5l_nor_write(s5l_nor_t *n, uint32_t off, uint32_t val, unsigned bytes);

/* Erase one sector back to all-ones. */
bool     s5l_nor_erase_sector(s5l_nor_t *n, uint32_t off);
/* Rebuild the image directory by scanning for IMG3 containers. */
unsigned s5l_nor_scan(s5l_nor_t *n);
/* Find a scanned image by ident; returns NULL if absent. */
const s5l_nor_entry_t *s5l_nor_find(const s5l_nor_t *n, uint32_t ident);

#define S5L_UNMAPPED_LOG 32
#define S5L_DEVLOG        256

/* --------------------------------------------------------- stub windows ---
 * A named, register-backed MMIO window for a peripheral we have identified but
 * not yet modelled.
 *
 * This is a deliberate, bounded exception to this core's "trap what you don't
 * implement" rule, and it is worth being precise about why. For an MMIO window
 * there is no option that avoids making a claim: returning 0 for an unmapped
 * read is *already* a guess, and a demonstrably dangerous one — a driver
 * polling a status bit that reads 0 forever spun on one such window for
 * 3.9 million reads, about 2% of an entire boot.
 *
 * A stub is therefore honest storage rather than invented behaviour: reads
 * return what was last written, writes are recorded, and every window is named
 * and counted so it appears in the report instead of hiding. What a stub must
 * never do is fabricate a value the guest is waiting for. When a driver needs a
 * bit to change on its own, that is a real device model, not a stub, and it
 * belongs in its own file.
 */
/* ------------------------------------------------------ power controller ---
 * The power-gate block. See core/src/soc/power.c for why this is a real model
 * rather than a stub: the guest never writes STATE, so read-back storage would
 * leave it reading zero forever — which is exactly what wedged the boot.
 *
 * Note the page is SHARED. Only 0x00-0x7F belongs to the power controller;
 * 0x80 and above is the GPIO interrupt controller, a different block with a
 * different driver.
 */
#define S5L8900_POWER_BASE   0x39a00000u
#define S5L8900_POWER_SIZE   0x00000080u   /* the rest of the page is GPIOIC */
#define S5L8900_GPIOIC_BASE  0x39a00080u
#define S5L8900_GPIOIC_SIZE  0x00000f80u

#define POWER_CONFIG0   0x00u
#define POWER_CONFIG1   0x04u
#define POWER_SETSTATE  0x08u
#define POWER_ONCTRL    0x0cu   /* write 1 to ungate: clears the STATE bit */
#define POWER_OFFCTRL   0x10u   /* write 1 to gate:   sets the STATE bit   */
#define POWER_STATE     0x14u   /* bit n set == domain n is gated OFF      */
#define POWER_SRAM      0x20u
#define POWER_CFG24     0x24u
#define POWER_CFG28     0x28u

/* 14 domains; the driver masks with 0x3fff and the device tree lists 14. */
#define S5L_POWER_DOMAIN_MASK    0x00003fffu
/* The device tree's power-gate-defaults. See s5l_power_reset. */
#define S5L_POWER_GATE_DEFAULTS  0x000012fcu

typedef struct {
    uint32_t state, cfg0, cfg1, sram, cfg24, cfg28;
} s5l_power_t;

void     s5l_power_reset(s5l_power_t *p);
uint32_t s5l_power_read(s5l_power_t *p, uint32_t off);
void     s5l_power_write(s5l_power_t *p, uint32_t off, uint32_t val);

/* ------------------------------------------------------ PowerVR MBX block ---
 *
 * /arm-io/mbx, which this VM un-matched by default because
 * com.apple.driver.AppleMBX starts, requests a reset, and then spins forever
 * waiting for an acknowledgement no register could give it. r226 counted the
 * spin exactly: 82,521,473 reads of one offset, against a single write of 1.
 *
 * It matters for frame rate because QuartzCore's MBXServer DEFAULTS to the
 * MBX2D hardware path and only falls back to compositing every pixel on the
 * CPU -- 40.2% of a swipe's instructions -- when this kext failed to give it
 * an IOKit connection.
 *
 * TWO PAGES, and both are real: r226 saw traffic on 0x3b000000 (offsets 0x080,
 * 0x12c, 0x134) and on 0x3b001000 (0x000-0x01c taking DRAM addresses, and
 * 0x020 taking the reset). One window covers both.
 */
#define S5L8900_MBX_BASE     0x3b000000u
#define S5L_MBX_SIZE         0x00002000u   /* the two pages r226 observed */
/* Offset of the reset register within the block, i.e. 0x3b001020. Read out of
 * the driver's own literal pool at 0xc078348c, and independently confirmed by
 * r226 finding the spin at page 0x3b001000 offset 0x020. */
#define S5L_MBX_RESET        0x00001020u
/* The bit AppleMBX+0xb478 tests with `tst r0, #0x10000`, looping while clear. */
#define S5L_MBX_RESET_DONE   (1u << 16)

/*
 * The second handshake, from AppleMBX+0x68a0. Having cleared reset the driver
 * programs three registers and then waits on a status bit:
 *
 *     write(0x838, 1);            enable
 *     write(0x83c, 0x00100000);   a size -- 1 MiB
 *     write(0x6d8, 0x09000000);   a DRAM address, and the last one written
 *   loop:
 *     r3 = read(0x12c);
 *     tst r3, #0x40;  beq loop    spin while bit 6 is CLEAR
 *     write(0x134, 0x40);         then acknowledge exactly that bit
 *
 * So 0x12c is a status word and 0x134 is its write-one-to-clear
 * acknowledgement -- which is corroborated by the driver writing 0x7ff to
 * 0x134 during init, i.e. clearing every pending bit before it starts.
 *
 * It is also write-one-to-SET. AppleMBX's recovery routine at
 * 0xc077f3e4..0xc077f428 writes missing ISP, render-complete and EVM-deallocate
 * bits directly to 0x12c, then reads the accumulated word and acknowledges it
 * through 0x134. The three writes are consecutive, so replacing the word on
 * each write would lose two of the events; they must OR into pending status.
 * r370 caught the old model accepting those writes into reg[] while reads of
 * 0x12c still returned the separate, unchanged status field.
 */
#define S5L_MBX_STATUS       0x0000012cu
#define S5L_MBX_STATUS_ACK   0x00000134u
/* Names and bit positions from the PowerVR MBX register definitions used by
 * the same hardware family. They also match the branches in AppleMBX's ISR at
 * 0xc07804dc. Keep the names specific: these are independent events, not one
 * generic completion flag. */
#define S5L_MBX_STATUS_CMDPROC          (1u << 0)
#define S5L_MBX_STATUS_RENDER_COMPLETE  (1u << 2)
#define S5L_MBX_STATUS_ISP              (1u << 3)
#define S5L_MBX_STATUS_TA_COMPLETE      (1u << 4)
#define S5L_MBX_STATUS_TA_OVERFLOW      (1u << 5)
#define S5L_MBX_STATUS_EVM_DALLOC       (1u << 6)
#define S5L_MBX_STATUS_TA_TIMEOUT       (1u << 7)
#define S5L_MBX_STATUS_TA_CONTEXT       (1u << 8)
#define S5L_MBX_STATUS_TA_STREAM_ERROR  (1u << 9)
#define S5L_MBX_STATUS_2D_SYNC          (1u << 10)
/*
 * THE 2D COMPLETION, and it is the bit the driver's own interrupt handler
 * tests. AppleMBX+0x804d4 is the ISR, and it reads as:
 *
 *     ldr  r3, [r2, #0x12c]     ; status
 *     ldr  r2, [r0, #0x148]     ; the driver's shadow of the enable mask
 *     and  r5, r3, r2           ; pending = status & mask
 *     bl   write(this, 0x134, pending)   ; acknowledge exactly those
 *     ands r2, r5, #0x400
 *     ...  strb r6, [r4, #0x120]         ; 2DIdle = 1
 *
 * So 2DIdle is a DRIVER FIELD at this+0x120, not a register -- which is why
 * the histogram could see only three offsets ever read and still have the
 * driver believe the 2D core was wedged. It is set from bit 10 and from
 * nowhere else, and the recovery diagnostic writing 0x400 to 0x12c is the
 * same bit being cleared.
 *
 * Bit 6 is a different signal and both are real: the startup path at
 * AppleMBX+0xe854 polls 0x12c for bit 6 SYNCHRONOUSLY and acknowledges 0x40.
 * r365 disproved the old shortcut that raised both on BACKGROUND_TAG: that
 * startup write raises EVM_DALLOC, while a decoded 2D packet raises 2D_SYNC
 * only after its pixels have actually moved.
 */
/* 0x130 is the interrupt ENABLE. The driver writes its this+0x148 shadow
 * there, and its ISR masks the status with that same shadow, so the line must
 * be gated on it rather than on any pending bit. */
#define S5L_MBX_INTMASK      0x00000130u
/* The public register map names 0x6d8 BACKGROUND_TAG. AppleMBX's measured
 * startup helper writes it last, waits synchronously for EVM_DALLOC, and then
 * acknowledges that bit. The model preserves that measured coupling without
 * pretending this register is a generic 2D/3D submission doorbell. */
#define S5L_MBX_BACKGROUND_TAG 0x000006d8u

/*
 * THE TA CONTEXT-RESET HANDSHAKE. The public MBX1 register map names 0x81c
 * TAGLOBREG_CONTEXT_RESET and its companion interrupt definitions name status
 * bit 8 TA_CONTEXT. The shipped AppleMBX path at 0xc077ed04 proves how this
 * particular driver uses that pair:
 *
 *     write(0x81c, 1);
 *   loop:
 *     status = read(0x12c);
 *     if (!(status & 0x100)) goto loop;
 *     write(0x134, 0x100);
 *
 * This is synchronous setup, not a render completion. Only the one observed
 * request value is accepted below; an unmeasured value must remain ordinary
 * register storage rather than manufacturing a completion.
 */
#define S5L_MBX_TA_CONTEXT_RESET 0x0000081cu

/* The companion context-store handshake reached after TA_COMPLETE. AppleMBX
 * at 0xc077d570..0xc077d5bc writes one to 0x818, polls status bit 8, panics
 * with `ta store timeout` if it stays clear, and acknowledges that same bit.
 * Store and reset are different requests with the shared TA_CONTEXT event. */
#define S5L_MBX_TA_CONTEXT_STORE 0x00000818u

/* The load half follows the store. A register capture at AppleMBX 0xc077d6f4
 * has r1=0x814 and r2=1; the following loop polls the same bit 8 and panics
 * with `ta load timeout` when it is absent. The adjacent 0x810 setup write is
 * not this doorbell. */
#define S5L_MBX_TA_CONTEXT_LOAD  0x00000814u

/* The TA-side scene identity and output buffers. The public MBX1 map names
 * these registers, and the retained Voice Memos transition ties them to the
 * following render independently: TA render-id 1 programmed object database
 * 0x00100000 and region base 0x00040000; STARTRENDER then consumed OBJBASE
 * 0x00100000 and RGNBASE 0x00040000. The raw PIO stream is preserved in that
 * already-snapshotted output allocation until the software TA consumer has
 * converted or rendered it. */
#define S5L_MBX_TA_RENDER_ID       0x00000810u
#define S5L_MBX_TA_OBJECT_DATABASE 0x0000083cu
#define S5L_MBX_TA_TAILPTR_BASE    0x00000840u
#define S5L_MBX_TA_REGION_BASE     0x00000844u

/*
 * THE TA SUBMISSION DOORBELL. The iPhone OS 3 AppleMBX path at
 * 0xc077ed54..0xc077ed84 sets its software TA state to 1 and then writes the
 * literal one to 0x800. Its recovery path at 0xc077f3a4..0xc077f3c8 treats a
 * non-zero TA state as a missing bit-4 completion: it changes the software
 * state to 8 and injects exactly 0x10 into INTSTATUS.
 *
 * A captured Voice Memos transition made that sequence concrete: 0x800 was
 * written while status stayed zero, and the watchdog later printed
 * `TAStatus=1`, injected 0x10, and discarded the scene. Complete only the
 * measured request value; other values remain ordinary register storage.
 * Completion is not immediate: that submission then wrote 2,570 words
 * through the single 3DDATA FIFO port and ended in 0xf0000000; the next wrote
 * 2,430 words and ended identically. START arms the transaction and the
 * measured FIFO terminator completes it. Completing at START races the
 * driver's own stream producer.
 */
#define S5L_MBX_TA_START       0x00000800u

/* The first measured tiled-render register set. The names come from the
 * PowerVR MBX register definitions and match AppleMBX's stores immediately
 * before it writes STARTRENDER. Values remain ordinary register storage;
 * STARTRENDER is only completed when the captured object family validates and
 * commits pixels through s5l_mbx_process_3d(). */
#define S5L_MBX_RGNBASE        0x00000608u
#define S5L_MBX_OBJBASE        0x0000060cu
#define S5L_MBX_3DPIXSAMP      0x0000061cu
#define S5L_MBX_FBCTL          0x00000650u
#define S5L_MBX_FBXCLIP        0x00000654u
#define S5L_MBX_FBYCLIP        0x00000658u
#define S5L_MBX_FBSTART        0x0000065cu
#define S5L_MBX_FBLINESTRIDE   0x00000660u
#define S5L_MBX_STARTRENDER    0x00000680u

/*
 * THE CORE REVISION REGISTER, and the reason AppleMBXDevice::start gave up
 * even after both handshakes were modelled. From AppleMBX+0x8eb8:
 *
 *     ldr r3, [r5, #0xd8]      ; the register base
 *     ldr r3, [r3, #0xf00]     ; read offset 0xf00
 *     lsr r4, r3, #0x18
 *     cmp r4, #1               ; byte 3 must be 0x01
 *     bne fail
 *     and r3, r3, #0xff0000
 *     cmp r3, #0x20000         ; byte 2 must be 0x02
 *     bne fail                 ; -> cleanup, start() returns false
 *
 * So the driver identifies its silicon and declines anything it does not
 * recognise. Reporting zero is what made it decline, silently -- there is no
 * console message on that path, which is why the failure looked like a
 * mystery teardown rather than a rejection.
 *
 * The value is therefore constrained by the driver's own test and by nothing
 * else: bits 31-24 = 0x01, bits 23-16 = 0x02. The low half is unconstrained
 * by anything observed, so it is zero rather than invented -- if some later
 * code reads a minor revision out of it, that will surface as a NEW failure
 * at a new site, which is the same discipline the rest of this block follows.
 *
 * This is a statement of identity, not of behaviour: the device tree already
 * declares `compatible = "mbx,s5l8900x"`, and this is the register through
 * which that same claim is made to the driver.
 */
#define S5L_MBX_REVISION     0x00000f00u
#define S5L_MBX_REVISION_ID  0x01020000u

/*
 * THE REST OF THE APERTURE IS MEMORY, and leaving it unmodelled is why MBX2D
 * has nothing to composite from.
 *
 * S5L_MBX_SIZE is 8 KB because that is what r226 saw ACCESSED, but the device
 * tree declares /arm-io/mbx with `reg = {0x03000000, 0x01000000}` -- sixteen
 * megabytes -- and the boot log says what the rest of it is: "IOSurfaceDevice-
 * MemoryRegion: edram was powered on at boot". It is the GPU's local video
 * memory, and MBX2D allocates its surfaces there.
 *
 * The evidence that this is the gap, from runs that had already been taken:
 * r265 followed the blit's source surface down to an IOGeneralMemoryDescriptor
 * whose range is {0x03a8a000, 0x97000}, which is ~10 MB into this aperture and
 * far past the 8 KB modelled; and every MBX run's own report lists 0x3ba00000
 * among the pages "touched outside the memory map", against 10,030 unmapped
 * reads. Those reads return zero, so a surface read yields nothing -- which is
 * exactly what a compositor with no pixels looks like.
 *
 * So the register block keeps the low 8 KB and everything above it is plain
 * storage. That invents no behaviour: memory that returns what was written to
 * it is the least a RAM aperture can do, and anything the device is supposed to
 * DO with those bytes still has to be modelled separately and still fails
 * loudly if it is not.
 */
#define S5L_MBX_APERTURE     0x01000000u   /* /arm-io/mbx reg size, 16 MB */
#define S5L_MBX_EDRAM_SIZE   (S5L_MBX_APERTURE - S5L_MBX_SIZE)
/* AppleMBX+0x1188 copies submitted 2D words into this aperture-relative
 * 64 KiB ring. These are public because the machine bus must preserve the old
 * ring word across a store without adding a read to unrelated EDRAM traffic. */
#define S5L_MBX_2D_RING_BASE 0x00a00000u
#define S5L_MBX_2D_RING_SIZE 0x00010000u
#define S5L_MBX_2D_COMMAND_HEADER 0xa0060500u
#define S5L_MBX_2D_SUBMIT 0xf0000000u
/* AppleMBX+0x41b8 writes every submitted TA word to this one FIFO port.
 * The public MBX1 register map calls the same aperture offset 3DDATA. */
#define S5L_MBX_3D_DATA_FIFO 0x00800000u
#define S5L_MBX_3D_SUBMIT    0xf0000000u

typedef struct {
    uint32_t reg[S5L_MBX_SIZE / 4u];
    uint32_t status;          /* 0x12c W1S; write-one-to-clear via 0x134   */
    bool     reset_done;      /* set by a reset REQUEST, never self-asserted */
    /*
     * Owned by the machine, exactly as m->ram is, and NOT part of this struct's
     * bytes -- 16 MB inside a snapshot-guarded struct would be absurd. Survives
     * s5l_mbx_reset(), which zeroes the contents rather than dropping them.
     */
    uint8_t *edram;
} s5l_mbx_t;

#define S5L_MBX_3D_REJECTION_HISTORY 4u
#define S5L_MBX_3D_REJECTION_RECORD_WORDS 44u
#define S5L_MBX_3D_ACCEPT_HISTORY 16u
#define S5L_MBX_3D_ACCEPT_RECORD_WORDS 8u
#define S5L_MBX_3D_TARGET_LEDGER 8u

#define S5L_MBX_3D_ACCEPT_TILED  1u
#define S5L_MBX_3D_ACCEPT_STATUS 2u
#define S5L_MBX_3D_ACCEPT_SPRITE 3u
#define S5L_MBX_3D_ACCEPT_SOLID  4u
#define S5L_MBX_3D_ACCEPT_TA_STREAM 5u

/* Host-only evidence for one fail-closed STARTRENDER. Four records cover the
 * complete burst observed during the SpringBoard-to-Settings transition.
 * The selected object record is captured at rejection time because later
 * accepted renders may reuse the same guest allocation before a checkpoint
 * can be taken. Nothing here is guest-visible or part of snap_mbx(). */
typedef struct {
    uint64_t sequence;
    uint64_t tiled_reason_hash;
    uint64_t status_reason_hash;
    uint64_t sprite_reason_hash;
    uint64_t solid_reason_hash;
    uint32_t region;
    uint32_t object;
    uint32_t target;
    uint32_t xclip;
    uint32_t yclip;
    uint32_t pixel_sample;
    uint32_t framebuffer_control;
    uint32_t framebuffer_stride;
    uint32_t list_valid_mask;
    uint32_t list_words[4];
    uint32_t record_base;
    uint32_t record_valid_words;
    uint32_t record_words[S5L_MBX_3D_REJECTION_RECORD_WORDS];
} s5l_mbx_3d_rejection_witness_t;

/* Bounded evidence for completed STARTRENDERs. A completion counter alone
 * cannot distinguish a full transition from a decoder that accepted a small
 * status sprite, so retain the decoder family, committed pixel count and the
 * selected object identity. The first physical backing and contiguous mapping
 * span let host diagnostics correlate the GPU target with the active scanout.
 * The full selected record is hashed before later renders can reuse its guest
 * allocation; its first eight words retain the control and address fields
 * needed to group matching forms. */
typedef struct {
    uint64_t sequence;
    uint64_t record_hash;
    uint32_t kind;
    uint32_t pixels;
    uint32_t region;
    uint32_t object;
    uint32_t target;
    uint32_t target_physical;
    uint32_t target_mapping_span;
    uint32_t xclip;
    uint32_t yclip;
    uint32_t pixel_sample;
    uint32_t framebuffer_control;
    uint32_t framebuffer_stride;
    uint32_t list_valid_mask;
    uint32_t list_words[4];
    uint32_t record_base;
    uint32_t record_valid_words;
    uint32_t record_words[S5L_MBX_3D_ACCEPT_RECORD_WORDS];
} s5l_mbx_3d_accept_witness_t;

/* Bounded host-only aggregate for completed work by framebuffer mapping.
 * The accepted-render ring is intentionally short and can roll over during a
 * single transition. This ledger preserves the last eight distinct
 * GPU/physical target pairs so a frontend can still determine whether the CLCD
 * scanout received completed work. Least-recently-used entries are replaced;
 * no field is guest-visible or part of snap_mbx(). */
typedef struct {
    uint64_t last_sequence;
    uint64_t completed;
    uint64_t pixels;
    uint32_t target;
    uint32_t target_physical;
    uint32_t target_mapping_span;
    uint32_t last_kind;
} s5l_mbx_3d_target_ledger_t;

/*
 * Host-only work ledger for one machine's synchronous MBX submissions.
 *
 * These totals deliberately live outside s5l_mbx_t: that struct is guest
 * device state and has an independently guarded snapshot layout. The ledger
 * answers a different question -- how much native renderer work happened
 * between two host observations -- so snapshot restore preserves the live
 * totals instead of rewinding them with the guest. Updating it costs only a
 * handful of integer additions at a real submit boundary; ordinary register
 * and EDRAM traffic does not touch it.
 */
typedef struct {
    uint64_t candidates_2d;
    uint64_t completed_2d;
    uint64_t rejected_2d;
    uint64_t bytes_2d;
    /* Raw identity of the most recent failed atomic 2D submission. These are
     * endpoints, not monotonic counters: a frontend must pair them with an
     * increase in rejected_2d before attributing them to its observation.
     * count is zero only for the explicit >=256/legacy-unknown marker. */
    uint64_t last_rejected_2d_ring_offset;
    uint64_t last_rejected_2d_count;
    uint64_t last_rejected_2d_reason_hash;
    uint64_t candidates_3d;
    uint64_t completed_3d;
    uint64_t rejected_3d;
    uint64_t pixels_3d;
    s5l_mbx_3d_rejection_witness_t rejected_3d_history[
        S5L_MBX_3D_REJECTION_HISTORY];
    s5l_mbx_3d_accept_witness_t accepted_3d_history[
        S5L_MBX_3D_ACCEPT_HISTORY];
    s5l_mbx_3d_target_ledger_t target_3d_ledger[
        S5L_MBX_3D_TARGET_LEDGER];
} s5l_mbx_telemetry_t;

void     s5l_mbx_reset(s5l_mbx_t *m);
uint32_t s5l_mbx_read(s5l_mbx_t *m, uint32_t off);
void     s5l_mbx_write(s5l_mbx_t *m, uint32_t off, uint32_t val);
/* Called immediately before s5l_mbx_write() for the same store. TA command
 * words enter through a write-only FIFO, while their measured output buffer
 * is GART-backed guest RAM. This bridge retains the raw stream there so a
 * checkpoint taken mid-submission carries both the words already accepted and
 * the word cursor in snap_mbx()'s existing register file. */
bool     s5l_mbx_stage_ta_write(s5l_mbx_t *m, const arm_bus_t *bus,
                                uint32_t off, uint32_t val);

/* Observe one CPU write to the MBX2D command ring. AppleMBX+0x1188 copies a
 * command beginning with 0xa0060500; AppleMBX+0x1f58 then submits it with the
 * separate, fixed write 0xf0000000 to ring+0. The machine supplies its bus
 * because the MBX struct deliberately owns no host pointers except EDRAM, and
 * destination writes must remain visible to a frontend bus observer. Returns
 * true only after a decoded copy, measured premultiplied/global-alpha blend,
 * or captured lower-screen black fill committed pixels and raised 2D_SYNC. */
bool     s5l_mbx_process_2d(s5l_mbx_t *m, const arm_bus_t *bus,
                            uint32_t written_off, uint32_t value,
                            s5l_mbx_telemetry_t *telemetry);

/* Validate one retained 2D ring packet without committing its staged pixels
 * or raising completion. This is for offline snapshot diagnosis: the live
 * submit path above remains the only path that can change guest-visible state.
 * packet_words receives the decoded packet length when validation succeeds. */
bool     s5l_mbx_probe_2d_packet(s5l_mbx_t *m, const arm_bus_t *bus,
                                 uint32_t packet_off,
                                 uint32_t *packet_words,
                                 const char **why);

/* Validate one exact retained atomic 2D submission without committing pixels
 * or raising completion. packet_off is the absolute MBX aperture offset;
 * command_count is the exact pending-head count captured at the live submit.
 * A rejection returns this build's exact reason text and the same FNV-1a
 * witness recorded in s5l_mbx_telemetry_t, allowing a frontend endpoint to
 * select one retained circular-ring batch without persisting host diagnostic
 * strings. */
bool     s5l_mbx_probe_2d_submit(s5l_mbx_t *m, const arm_bus_t *bus,
                                 uint32_t packet_off,
                                 uint32_t command_count,
                                 uint64_t *reason_hash,
                                 const char **why);

/* Attempt an exactly decoded tiled 3D object after a STARTRENDER write. The
 * accepted forms are the captured 320x96 premultiplied source-over rectangle
 * plus the padlock, `Searching...`, battery, unlock-slider label, status-bar
 * time, and transparent clipped surface-transfer forms, including their tile
 * lists, object words, formats, coordinates and GART resources.
 * Returns true only after staged pixels commit and the three events AppleMBX
 * requires for 3DIdle (ISP, render complete, EVM deallocate) rise. */
bool     s5l_mbx_process_3d(s5l_mbx_t *m, const arm_bus_t *bus,
                            uint32_t written_off, uint32_t value,
                            s5l_mbx_telemetry_t *telemetry);

/*
 * THE LINE THE DEVICE TREE SAYS THIS DEVICE HAS. /arm-io/mbx carries
 * `interrupts = {0x0c}`, and nothing in this machine was driving it -- the
 * model raised its status bit on a kick and then told no one.
 *
 * That is the shape of the r246 failure. The driver reaches exactly ONE
 * Graphics Recovery Event and reports `2DIdle=0, 3DIdle=1`, and the histogram
 * says only 0x012c, 0x1020 and 0xf00 are ever READ in the whole run -- far too
 * few reads for a driver polling for completion. So 2DIdle is the driver's own
 * bookkeeping: marked busy when work is submitted, cleared when the completion
 * INTERRUPT arrives. No interrupt, no clear, and the watchdog concludes the 2D
 * core is wedged.
 *
 * Asserted while a status bit is pending and dropped when the driver clears it
 * through the write-one-to-clear at S5L_MBX_STATUS_ACK. That is a level, and it
 * is deliberately the same rule i2c and spi already follow here rather than a
 * new one invented for this device: the status word and its acknowledge were
 * already modelled, and this only connects them to the controller.
 */
#define S5L8900_IRQ_MBX   12u   /* /arm-io/mbx `interrupts` = {0x0c} */
bool     s5l_mbx_irq(const s5l_mbx_t *m);

/* --------------------------------------------- GPIO interrupt controller ---
 * The upper part of the 0x39a00000 page: AppleS5L8900XGPIOIC, a seven-group
 * cascade in front of both VICs. See core/src/soc/gpioic.c for the model.
 *
 * REGISTER OFFSETS ARE RELATIVE TO THE PAGE, NOT TO THE DECODED WINDOW.
 * The window this machine decodes starts at S5L8900_GPIOIC_BASE (0x39a00080),
 * because power.c owns 0x00-0x7F of the same physical page. The driver,
 * however, programs against the page base — its four accessors take an offset
 * from object+0x6c, which the pin-accessor decoding in docs/derivations.md §13.0d
 * resolves to 0x39a00000 — so the constants below are the driver's own and
 * s5l_gpioic_read()/write() take an offset from S5L8900_GPIOIC_PAGE. Rebasing
 * them onto the window would make every one of them differ from the
 * disassembly by 0x80, which is exactly how a register map goes wrong.
 *
 * CONFIRMED from AppleS5L8900XGPIOIC itself, ARM, in this kernelcache:
 *
 *   enable, 0xc05a5678-0xc05a56dc
 *     asr r6, r3, #5        group = line >> 5
 *     and r3, r5, #0x1f     bit   = line & 0x1f
 *     add r1, r8, #0xa0     write 0xA0 + 4*group  <- 1 << bit   (clear stale)
 *     ldr r3, [r1, r6, lsl #2] / orr r2, r3, r5 / str            (shadow |=)
 *     add r1, r8, #0xc0     write 0xC0 + 4*group  <- shadow[group]
 *
 *   disable, 0xc05a5748-0xc05a5788: shadow &= ~(1<<bit), then 0xC0 + 4*group.
 *
 *   init, 0xc05a51a8: shadow[g] = 0 and 0xC0 + 4*g <- 0 for g < [r5+0x78],
 *   which is /arm-io/gpio's `#interrupt-groups` = 7.
 *
 *   initVector, 0xc05a54b8-0xc05a562c (the "configure" this file used to call
 *   it): reads 0xE0 + 4*group and 0x80 + 4*group and writes each back with one
 *   bit replaced — a read-modify-write of two configuration registers, from
 *   bit 0 and bit 1 of the device tree's second interrupt cell respectively.
 *   Bit 2, and bit 0 a second time, also go to software shadows. See below.
 *
 * THE SECOND INTERRUPT CELL, and why this file no longer ignores it.
 *
 * An earlier revision of this header said "every `interrupts` property's second
 * cell is zero, so both registers stay zero for every line this machine can
 * drive". That was only ever true of the lines that had been modelled. The
 * shipped tree uses cells 0, 1, 2, 3, 5 and 7 across eleven nodes, and
 * /device-tree/buttons uses 7 and 5 for all five of its lines.
 *
 *   cell bit 0  -> INTTYPE bit (0xE0 + 4*group), 0xc05a55c8-0xc05a55e4,
 *                  AND a software shadow at this+0x8c.
 *   cell bit 1  -> INTLEVEL bit (0x80 + 4*group), 0xc05a55e8-0xc05a5608.
 *   cell bit 2  -> a software shadow at this+0x84, and nothing else.
 *   bits 3..31  -> dead. The only masks applied to the cell in the whole
 *                  function are #5, #1, >>1 &1 and >>2 &1.
 *
 * What those two bits MEAN is settled by the driver itself, twice:
 *
 *   - getInterruptType, 0xc05a429c-0xc05a42b0, stores `cell & 1` as IOKit's
 *     own type value, and kIOInterruptTypeLevel is 1 and kIOInterruptTypeEdge
 *     is 0. So INTTYPE bit 1 == LEVEL-SENSITIVE.
 *   - 0xc05a54fc `and r3, r8, #5` / `cmp r3, #4` calls a panic whose string at
 *     0xc05ac940 is "auto-flip GPIO interrupts must be level-triggered". So
 *     cell bit 2 == AUTO-FLIP, and it is illegal without bit 0. Corroboration:
 *     the shipped tree contains cells 0,1,2,3,5,7 and never 4 or 6 — exactly
 *     the two values that trip that assertion.
 *
 * AUTO-FLIP is how a GPIO button reports both press and release with one line.
 * handleInterrupt at 0xc05a4358-0xc05a4390 reads the this+0x84 shadow and, for
 * an auto-flip line, does `eor r2, r0, r6` on the INTLEVEL word and writes it
 * back — inverting that line's polarity bit — BEFORE dispatching the child
 * handler and BEFORE acknowledging. So the next interrupt is the opposite
 * transition. That shadow is read nowhere else in the class.
 *
 * ACKNOWLEDGE ORDER IS NOT UNIFORM, and it is decided by the this+0x8c shadow,
 * i.e. by cell bit 0: an EDGE line is acknowledged at 0xc05a4354, before the
 * child handler; a LEVEL line at 0xc05a4418, after it returns. handleInterrupt
 * then re-reads 0xA0 + 4*group and loops until it reads zero (0xc05a42fc-
 * 0xc05a4424), highest set bit first, and it never masks with INTEN.
 *
 * These were measured as well as read. run86's group-1 registers settle at
 * INTTYPE 0x00002f00 and INTLEVEL 0x00002900 — five lines configured, and the
 * three whose cell is 7 have their INTLEVEL bit set while the two whose cell is
 * 5 do not — and group 5 settles at INTTYPE 0x00000000, INTLEVEL 0x00000004,
 * which is /arm-io/spi0/lcd0's `interrupts {162, 2}`: bit 0 clear, bit 1 set.
 * Four groups, six distinct cell values, no disagreement.
 *
 * The seven cascade lines are /arm-io/gpio's own `interrupts`, read out of the
 * shipped tree: {0x21,0x20,0x1f,0x03,0x02,0x01,0x00} = {33,32,31,3,2,1,0},
 * one per group in array order. GROUP 4 IS THEREFORE VIC LINE 2, and the
 * multi-touch ATN — /arm-io/spi1/multi-touch `interrupts {0x9b,0}`, i.e. line
 * 155 — is group 4 bit 27, which is precisely the 0x08000000 run59 measured in
 * this controller's group-4 enable register. "GPIO 155" is a line index on
 * this controller with a stride of 32, NOT a pin number: do not compute it as
 * group*8+bit, and do not put 155 in a wake-source entry, because
 * wake_line_enabled() rejects anything at or above 32*S5L8900_VIC_COUNT and
 * would silently answer "cannot wake".
 */
#define S5L8900_GPIOIC_PAGE  0x39a00000u   /* what the driver programs against */
#define S5L_GPIOIC_GROUPS    7u            /* #interrupt-groups                */
#define S5L_GPIOIC_LINES     (S5L_GPIOIC_GROUPS * 32u)

#define GPIOIC_INTLEVEL 0x80u   /* + 4*group, read-modify-write configuration */
#define GPIOIC_INTSTAT  0xa0u   /* + 4*group, pending latch, WRITE-1-TO-CLEAR */
#define GPIOIC_INTEN    0xc0u   /* + 4*group, enable mask                     */
#define GPIOIC_INTTYPE  0xe0u   /* + 4*group, read-modify-write configuration */

/* Device-tree interrupt lines, as flat indices on this controller. */
#define S5L_GPIOIC_LINE_PMU         85u
#define S5L_GPIOIC_LINE_MULTITOUCH 155u
/* M68 `/arm-io/spi2/multi-touch` declares `interrupts {0xa3, 0}`. */
#define S5L_GPIOIC_LINE_MULTITOUCH_M68 163u

/* Number of distinct unknown offsets remembered, as on I2C and SPI. */
#define S5L_GPIOIC_UNKNOWN_OFF 8u

typedef struct {
    uint32_t level[S5L_GPIOIC_GROUPS];
    uint32_t stat [S5L_GPIOIC_GROUPS];
    uint32_t en   [S5L_GPIOIC_GROUPS];
    uint32_t type [S5L_GPIOIC_GROUPS];
    /*
     * What the board is driving into each group right now, which is NOT a
     * register: no guest access can reach it and it is not part of the page.
     * It exists because the pending latch is EDGE-triggered for an EDGE line
     * (see gpioic.c), so setting a line has to know whether it was already
     * high — a device that holds its line up until it is serviced must produce
     * one interrupt and not one per refresh. Getting that backwards cost run71
     * a 1,193,122-iteration livelock in which the guest acknowledged the touch
     * line half a billion instructions' worth of times and never read the
     * report it was being told about.
     *
     * It is load-bearing a second way now: an enabled LEVEL line's pending
     * condition is `raw bit == INTLEVEL bit`, so `raw` is re-consulted every
     * time the guest writes INTSTAT, INTLEVEL, INTTYPE or INTEN, not only when
     * a device moves a line. Masking a line lets its deferred handler clear the
     * source without handleInterrupt immediately reading the same status again.
     */
    uint32_t raw  [S5L_GPIOIC_GROUPS];
    /*
     * WHICH LINES HAVE A DEVICE ON THE END OF THEM. Set the first time anything
     * calls s5l_gpioic_set_line() for that line, for a FALSE level as well as a
     * true one, and never cleared except by reset.
     *
     * This is not the same fact as `raw`, and conflating the two cost run87 its
     * entire budget. Eleven nodes hang off /arm-io/gpio. The PMU is now one of
     * the five this machine models; /arm-io/i2c0/als remains among the six it
     * does not. Both declare interrupt cell 1 — LEVEL, asserting while LOW.
     * Before PMU wiring existed, `raw` at its initial zero and no way to say
     * "nothing drives this" made both look permanently asserted, and the guest
     * read and acknowledged group 2's pending word 668,039 times without ever
     * getting past instruction ~96 million.
     *
     * The edge path never needed it, because an undriven line has no rising
     * edge and silently never latched. A level line has no such luck.
     */
    uint32_t driven[S5L_GPIOIC_GROUPS];

    uint64_t unknown_reads, unknown_writes;
    uint32_t unknown_off[S5L_GPIOIC_UNKNOWN_OFF];
    unsigned unknown_off_count;
} s5l_gpioic_t;

void     s5l_gpioic_reset(s5l_gpioic_t *g);
/* `off` is relative to S5L8900_GPIOIC_PAGE — see the note above. */
uint32_t s5l_gpioic_read(s5l_gpioic_t *g, uint32_t off);
void     s5l_gpioic_write(s5l_gpioic_t *g, uint32_t off, uint32_t val);

/*
 * Drive one of the 224 input lines. Level, not edge: the caller states what
 * the wire is doing and the controller decides what to latch. Out-of-range
 * lines are refused rather than wrapped — a device wired to a line this
 * controller does not have is a bug that must not silently drive a real one.
 *
 * `level` is the electrical level on the wire, NOT "please interrupt". For an
 * EDGE line the controller latches a rising edge of it; for a LEVEL line it
 * latches while it equals that line's INTLEVEL bit. Which of the two applies
 * is the guest's choice, recorded in INTTYPE, and a caller must not try to
 * anticipate it: a device that inverted its own level to "make the interrupt
 * happen" would break the moment the guest flipped the polarity, which for an
 * auto-flip line is after every single interrupt.
 */
void     s5l_gpioic_set_line(s5l_gpioic_t *g, unsigned line, bool level);
/*
 * Consume one already-reported transition on an AUTO-FLIP level line while
 * retaining its electrical level. The PMU Power-wake path uses this after it
 * reports the same physical edge through STAT: no duplicate GPIO interrupt is
 * left pending, and the opposite level is selected so the later release is
 * still observable. Refuses an invalid or non-level line.
 */
bool     s5l_gpioic_consume_autoflip_level(s5l_gpioic_t *g, unsigned line,
                                           bool level);
/* Is that line currently being driven? Mirrors set_line for tests. */
bool     s5l_gpioic_line(const s5l_gpioic_t *g, unsigned line);
/* Is that line's pending bit latched? Mirrors the INTSTAT read for tests and
 * for the button model's "the guest has not serviced the last edge" refusal. */
bool     s5l_gpioic_pending(const s5l_gpioic_t *g, unsigned line);

/* This group's output: OR(STAT & EN). */
bool     s5l_gpioic_group_irq(const s5l_gpioic_t *g, unsigned group);
/*
 * The VIC line `group` cascades to. One definition, used by the bus routing,
 * by the wake-source table and by the tests, so the {33,32,31,3,2,1,0} order
 * cannot be transcribed differently in two places. Returns a value at or above
 * 32*S5L8900_VIC_COUNT for a group that does not exist, which every caller
 * already treats as unroutable.
 */
unsigned s5l_gpioic_cascade(unsigned group);

/* ------------------------------------------------------- GPIO pin block ---
 * The OTHER page /arm-io/gpio names: reg {0x06400000,0x1000, 0x01a00000,0x1000}
 * maps to 0x3e400000 and 0x39a00000, and only the second is the interrupt
 * controller above. This one carries the pin state.
 *
 * A pin id from a device tree `function-*` property splits as
 * group = id >> 8, bit = id & 0xff (AppleS5L8900X's accessor at 0xc05a4494:
 * `lsr r1,r5,#8 / lsl r1,r1,#5 / add r1,r1,#4` then `and r1,r5,#0xff /
 * lsr r0,r0,r1 / and r0,r0,#1`), and the level READS BACK from
 * S5L8900_GPIO_BASE + group*32 + 4. docs/derivations.md §13.0d establishes that the
 * accessor used is the object+0x68 one, i.e. this block and not the gpioic
 * page — a distinction that matters because lcd0's pins are in groups 0 and 3,
 * which would otherwise land inside power.c's claim.
 *
 * READS AND WRITES USE DIFFERENT REGISTERS, and getting that wrong produces a
 * model in which nothing ever happens and nothing ever complains. The per-group
 * level register is READ-ONLY. A pin is DRIVEN by a single 32-bit store to the
 * function-select register at `fsel-offset` 0x320, whose word is
 *
 *     [23:16] group   [15:8] bit   [3:0] function, 0xE|level for a driven output
 *
 * i.e. `write32(0x3e400320, (pin << 8) | 0xE | (level & 1))`. There is no
 * read-modify-write and no per-group write register. This is independently
 * corroborated by run59's measurement of the storage stub that used to cover
 * this page: offset 0x320 was the ONLY offset the guest ever touched on it. A
 * model that watched group*32+4 for stores would therefore never have fired
 * once in an entire boot.
 *
 * The multi-touch controller's `function-reset` (0x0606, group 6 bit 6,
 * active low) and /arm-io/spi1's `function-spi_cs0` (0x1800, group 24 bit 0)
 * are the two pins wired here, and a device watches them through
 * s5l_gpio_watch() so that it sees the EDGE: a reset is a pulse, and polling
 * the level afterwards finds the pin already back where it started.
 *
 * No direction, pull or drive-strength behaviour is modelled, and a function
 * nibble that is not a driven output changes no pin — it is stored in the fsel
 * register and read back, exactly as the storage stub this replaces did.
 */
#define S5L_GPIO_PORTS      25u    /* #gpio-ports, groups 0..24 */
#define S5L_GPIO_WORDS      (S5L8900_DEV_SIZE / 4u)
#define S5L_GPIO_WATCHERS   4u

/* The function-select register: `fsel-offset {0x00000320}` in the device tree,
 * and the only offset on this page run59 saw the guest touch. */
#define S5L_GPIO_FSEL       0x320u
/* Bits[3:1] of an fsel word. 0x7 — i.e. a nibble of 0xE or 0xF — is the driven
 * output the platform expert uses; bit 0 is then the level. */
#define S5L_GPIO_FUNC_OUT   0x7u

/* Byte offset of group `g`'s read-only pin-level register. */
#define S5L_GPIO_PIN_REG(g) ((g) * 32u + 4u)

typedef struct {
    void    *ctx;
    uint16_t pin;                          /* group << 8 | bit */
    bool     armed;
    void   (*changed)(void *ctx, bool level);
} s5l_gpio_watch_t;

typedef struct {
    uint32_t regs[S5L_GPIO_WORDS];
    /* Host wiring. Snapshot code serializes `regs`, never these callbacks. */
    s5l_gpio_watch_t watch[S5L_GPIO_WATCHERS];
} s5l_gpio_t;

void     s5l_gpio_reset(s5l_gpio_t *g);
uint32_t s5l_gpio_read(const s5l_gpio_t *g, uint32_t off, unsigned bytes);
void     s5l_gpio_write(s5l_gpio_t *g, uint32_t off, uint32_t val,
                        unsigned bytes);
/* The level of one device-tree pin id (group << 8 | bit). */
bool     s5l_gpio_pin(const s5l_gpio_t *g, uint16_t pin);
/*
 * THE OTHER END OF THE WIRE. The level register is read-only to the GUEST —
 * s5l_gpio_write() drops stores to it, and the fsel path is the only way the
 * guest moves a pin — but a pin the BOARD drives is an input, and something
 * has to put its level there. This is that, and it is deliberately a separate
 * entry point from s5l_gpio_write(): a model in which a guest store and a
 * board level reached the same code could not answer "which one moved this
 * pin", which is the question the read-only level register exists to settle.
 *
 * Watchers fire on a real change, exactly as they do for a guest store, because
 * a watcher is a subscription to the PIN and the pin moved.
 *
 * Out-of-range groups and bits are dropped rather than wrapped.
 */
void     s5l_gpio_drive(s5l_gpio_t *g, uint16_t pin, bool level);
/*
 * Call `changed` whenever a guest store alters that pin's level. Refuses a
 * pin outside groups 0..24, a NULL callback, a full table, and a pin that is
 * already watched — silently shadowing a subscription would hide exactly the
 * edge the subscriber exists to see.
 */
bool     s5l_gpio_watch(s5l_gpio_t *g, uint16_t pin, void *ctx,
                        void (*changed)(void *ctx, bool level));

/* ------------------------------------------------- the five physical buttons ---
 *
 * There is no button chip. The iPhone1,2 wires five switches to five GPIO pins
 * on port 22 and routes five lines of the GPIO interrupt controller's group 1,
 * and com.apple.driver.AppleM68Buttons — "M68" is this board — is the driver.
 * So this is not a device model in the sense mtz2.c is; it is the BOARD, and
 * everything below is a statement about wiring rather than about registers.
 *
 * /device-tree/buttons, read out of the shipped 40,544-byte tree:
 *
 *   device_type      'buttons'          compatible 'buttons'
 *   interrupt-parent  -> /arm-io/gpio   (the interrupt controller above)
 *   button-names     'hold\0menu\0volup\0voldown\0ringerab\0'
 *   interrupts       {45,7} {40,7} {41,5} {42,5} {43,7}
 *   function-button_hold     {gpio, 'GPIO', 0x1605, 0x00000100}
 *   function-button_menu     {gpio, 'GPIO', 0x1600, 0x00000100}
 *   function-button_volup    {gpio, 'GPIO', 0x1601, 0x00000000}
 *   function-button_voldown  {gpio, 'GPIO', 0x1602, 0x00000000}
 *   function-button_ringerab {gpio, 'GPIO', 0x1603, 0x00010000}
 *   function-wake_button_hold{pmu,  'STAT', 0x00000100}
 *
 * `button-names` and `interrupts` are parallel arrays — the driver walks the
 * names in order and asks its provider for interrupt index i (0xc065a6b0,
 * `interruptEventSource(this, action, provider, i)`) — so BUTTON_HOLD is index
 * 0 and takes line 45, and this file's order is the tree's order for exactly
 * that reason. A table in a different order would still compile.
 *
 * ALL FIVE ARE SoC GPIOs, INCLUDING POWER/HOLD AND THE VOLUME KEYS. The PMU is
 * involved only through `function-wake_button_hold`, a 'STAT' function on
 * /arm-io/i2c0/pmu that AppleM68Buttons resolves once at start (0xc065a6a4,
 * stored at this+0x90+4i) and reads only in its power-state path (0xc065a0c8)
 * to answer "did this button wake us". It is NOT the ordinary press/release
 * path. The machine-level s5l8900_set_button() wrapper models it only while
 * the PMU has put the application processor into standby.
 *
 * WHICH LEVEL IS "PRESSED". This is the number that had to be right, because a
 * wrong one reads as a button held down forever. Two independently decoded
 * fields agree, and a third pin whose polarity was already known corroborates.
 *
 *  1. The fourth word of a `function-*` property is a platform-function
 *     argument block. AppleS5L8900X's apply routine reads BYTE 1 of it and
 *     uses it, and only it, as a polarity: the read path at 0xc05a45d8 is
 *          ldrb r3,[r3,#0xd] / eor r3,r3,#1 / eor r3,r3,r0
 *     over the raw level from getPinLevel (0xc05a4474, which is soc.h's
 *     documented `(reg >> bit) & 1` accessor on the PIN page), and the write
 *     path at 0xc05a459c is the identical pair before setPinOutput. So byte 1
 *     == 1 means ACTIVE HIGH and 0 means ACTIVE LOW, and what a child sees is
 *     already normalised to "1 == asserted".
 *     CORROBORATION: /arm-io/spi1/multi-touch's `function-reset` is
 *     0x00010001, byte 1 == 0 — and that pin was independently established as
 *     ACTIVE LOW from the driver's own reset behaviour long before this byte
 *     was decoded. /arm-io/spi0/lcd0's `function-reset` 0x00000001 agrees too.
 *  2. Every one of these five lines is configured LEVEL-sensitive with
 *     AUTO-FLIP (cell 7 and cell 5 both have bits 0 and 2 set). A level line
 *     that is asserted at rest is an interrupt storm at boot, so the INTLEVEL
 *     bit the guest programs must be the OPPOSITE of the resting level. Guest
 *     programs INTLEVEL 1 for hold and menu, 0 for volup and voldown
 *     (measured: group 1 settles at 0x00002900) — which says hold and menu
 *     rest LOW and the volume keys rest HIGH.
 *
 * (1) and (2) are derived from different words of a different property by
 * different code and they give the same answer for all four momentary keys.
 *
 *              pin      line  cell  INTTYPE  INTLEVEL  polarity  PRESSED  rest
 *   hold      0x1605     45     7    level      1      high        high    low
 *   menu      0x1600     40     7    level      1      high        high    low
 *   volup     0x1601     41     5    level      0      low          low   high
 *   voldown   0x1602     42     5    level      0      low          low   high
 *   ringerab  0x1603     43     7    level      1      low           —      —
 *
 * WHAT THE GUEST DOES WITH THEM. AppleM68Buttons never polls. An interrupt on
 * any of the five arms a single 14 ms one-shot timer (0xc065a31c, the literal
 * is 0x36b0 = 14000 us); when it expires the driver samples ALL FIVE pins
 * (0xc065a2a8) through their platform functions and reports every one whose
 * value differs from its shadow bitmap at this+0xbc. So a press shorter than
 * one debounce interval that has already been undone when the timer fires is
 * genuinely not seen, and this model does not pretend otherwise.
 *
 * It dispatches (0xc065aad0):
 *   hold      HID usage page 0x0C (Consumer), usage 0x30  Power
 *   menu                     0x0C,                  0x40  Menu
 *   volup                    0x0C,                  0xE9  Volume Increment
 *   voldown                  0x0C,                  0xEA  Volume Decrement
 *   ringerab  HID usage page 0x0B (Telephony), usage 0x2E Phone Mute
 *
 * THE RINGER IS THE ONE THE DRIVER INVERTS. At 0xc065ab10 it compares the
 * button's name symbol against the one built from "ringerab" and does
 * `eoreq r6, r6, #1` on the value before anything else. Composed with the
 * active-low platform polarity above, the Phone Mute value it dispatches is
 * the RAW pin level: pin high == muted. That composition is established; which
 * physical position of the slider drives the pin high is NOT, and this model
 * does not claim it. The host API therefore names the two positions after what
 * the guest is told (S5L_BUTTONS_RINGER_MUTED), not after a silkscreen.
 *
 * The rest position chosen for the ringer here is the one that asserts NOTHING
 * at boot, i.e. pin low, unmuted. A rest position that asserted its level line
 * would hand the guest an interrupt it did not ask for before its driver has
 * finished starting, and "the machine boots quietly" is worth more than a
 * guess about a slider.
 */
#define S5L_BUTTON_HOLD     0u   /* Power/Hold  */
#define S5L_BUTTON_MENU     1u   /* Home        */
#define S5L_BUTTON_VOLUP    2u
#define S5L_BUTTON_VOLDOWN  3u
#define S5L_BUTTON_RINGERAB 4u   /* the ringer/silent slider */
#define S5L_BUTTON_COUNT    5u

/*
 * `pressed` in this API always means WHAT THE GUEST WILL REPORT, for all five,
 * and the wiring table absorbs however many inversions lie between that and
 * the wire. For the four momentary keys that is one inversion or none — the
 * platform-function polarity byte. For the slider it is two, because
 * AppleM68Buttons inverts the ringer a second time at 0xc065ab14, and the two
 * compose to "dispatched Phone Mute value == the raw pin level".
 *
 * Defining it the other way round — `pressed` meaning "the pin is asserted" —
 * would have made s5l_buttons_set(RINGERAB, true) silence the phone by putting
 * the slider in the position the guest calls unmuted, which is precisely the
 * class of quiet inversion this project keeps paying for.
 */
#define S5L_BUTTONS_RINGER_MUTED true

typedef struct {
    /*
     * One bit per S5L_BUTTON_*, as the HOST has set it. This is board state,
     * not a register: it is what the switches are doing, and the guest can
     * only see it through the pin levels and the interrupt lines it produces.
     * It travels in a snapshot for the same reason s5l_gpioic_t.raw does — a
     * restore that dropped it would release every held button silently.
     */
    uint8_t  pressed;
    /*
     * Bounded diagnostics, and two of these are load-bearing rather than
     * decoration. `refused` non-zero means the HOST asked and the board said
     * no, and s5l_buttons_set() told it so; `edges` counts transitions this
     * model actually drove onto a pin. `edges` without a guest interrupt means
     * the line is not armed; `refused` alone means the host is pressing faster
     * than the guest is servicing.
     */
    uint64_t sets, refused, edges;
} s5l_buttons_t;

/* Reset releases everything. The rest LEVELS are not zero — see the table
 * above — so a machine must call s5l_buttons_apply() after resetting the pin
 * block and the interrupt controller, and s5l8900_init() does. */
void     s5l_buttons_reset(s5l_buttons_t *b);

/* The wiring, one accessor each, so the device tree's numbers exist once. Out
 * of range answers with a pin/line no board carries, which every caller drops,
 * and with "no name". */
const char *s5l_button_name(unsigned which);
uint16_t s5l_button_pin(unsigned which);
unsigned s5l_button_line(unsigned which);
/*
 * Byte 1 of this button's `function-*` fourth word: the pin level at which the
 * GUEST'S OWN PLATFORM FUNCTION reports the line asserted (0xc05a45dc). Pure
 * hardware, and NOT the same question as s5l_button_level() for the ringer.
 */
bool     s5l_button_active_high(unsigned which);
/*
 * Whether AppleM68Buttons inverts this button a second time before dispatching
 * it (0xc065ab14). True for the ringer and nothing else.
 */
bool     s5l_button_driver_inverts(unsigned which);
/*
 * The pin level that makes the guest report this button as `pressed`, i.e.
 * both inversions composed. This is the one the model drives.
 */
bool     s5l_button_level(unsigned which, bool pressed);

/* Whether the host currently holds that button. */
bool     s5l_buttons_held(const s5l_buttons_t *b, unsigned which);

/*
 * Drive the current board state onto the pin block and the interrupt
 * controller. Idempotent: re-driving an unchanged level latches nothing, which
 * is what lets s5l8900_tick() call it every tick without inventing interrupts.
 * Called once from s5l8900_init() so the rest levels exist before the guest
 * configures anything, and after a snapshot restore for the same reason.
 */
void     s5l_buttons_apply(const s5l_buttons_t *b, s5l_gpio_t *gpio,
                           s5l_gpioic_t *ic);

/* -------------------------------------------------- the host injection API ---
 *
 * THE LINE THIS API DRAWS is the one s5l_mtz2_set_contacts() draws. The host
 * sets the position of a physical switch. The board then presents that through
 * a pin level and an interrupt line, and the guest's own AppleM68Buttons reads
 * it when and if it chooses to. Nothing here dispatches a HID event, posts a
 * GSEvent or calls UIKit: a Home press that does not reach SpringBoard must
 * LOOK like it failed, because that is the only way the thing in between gets
 * found.
 *
 * Returns FALSE — and changes nothing — when the board is in no state to
 * report the change:
 *
 *   - `which` is not one of the five: a malformed request.
 *   - the guest has not armed that line (its INTEN bit is clear): the driver
 *     never polls, so a press it cannot be interrupted for is a press it can
 *     never see. This is the button equivalent of the Z2's `!hbpp_answered`.
 *   - that line's pending bit is still latched: the guest has not serviced the
 *     PREVIOUS transition. Accepting now would collapse a press and a release
 *     into a single pending bit and the guest would observe neither, which is
 *     strictly worse than being told no.
 *
 * Setting a button to the state it is already in is NOT a refusal and NOT an
 * edge. It is a host that is holding a button.
 *
 * Every refusal bumps `refused`, so a caller that ignores the return value
 * still leaves evidence.
 */
bool     s5l_buttons_set(s5l_buttons_t *b, s5l_gpio_t *gpio, s5l_gpioic_t *ic,
                         unsigned which, bool pressed);

/* ---------------------------------------------------- CLCD display controller ---
 * The path to pixels. See core/src/soc/clcd.c for the evidence behind every
 * register below; the short version is that AppleH1CLCD's own code was read out
 * of the shipped kernelcache and each offset here is a line of that code.
 *
 * Base and interrupt line are CONFIRMED from the shipped device tree:
 *   /arm-io/clcd  reg {0x900000, 0x1000}  interrupts {0xd}
 * and /arm-io ranges maps child + 0x38000000.
 */
#define S5L8900_CLCD_BASE 0x38900000u
#define S5L8900_IRQ_CLCD  13u

#define CLCD_ENABLE      0x000u  /* write 1: start scanout (written last)      */
#define CLCD_DISABLE     0x004u  /* write 1: stop scanout                      */
#define CLCD_CTRL        0x008u  /* display + per-window enables               */
#define CLCD_FIFO        0x00cu  /* per-window FIFO thresholds                 */
#define CLCD_INTMASK     0x014u  /* interrupt enable mask                      */
#define CLCD_INTSTATUS   0x018u  /* interrupt status, WRITE-1-TO-CLEAR         */
#define CLCD_REG1C       0x01cu  /* cleared to 0 on start and on stop          */
#define CLCD_PREENABLE   0x020u  /* write 1 just before CLCD_ENABLE            */
#define CLCD_BACKDROP    0x024u  /* backdrop colour, ARGB                      */
#define CLCD_VIDEO_FIRST 0x028u  /* 11 video/YUV overlay regs, 0x028..0x054    */
#define CLCD_VIDEO_LAST  0x054u
#define CLCD_WIN_FIRST   0x058u  /* RGB window k at CLCD_WIN_FIRST + k*0x18    */
#define CLCD_WIN_STRIDE  0x018u
#define CLCD_WIN_COUNT   4u
#define CLCD_UPDATE      0x0d4u  /* write 2 at the head of every window update */
/*
 * Per-window auxiliary configuration pairs. openiBoot's S5L8900 LCD code
 * writes window k at 0x0d8 + k*8 and clears the following word. 0x0e8 is also
 * AppleH1CLCD's update word, so it is represented separately as CLCD_UPDATE2.
 * These were once mislabeled as panel timings; the actual timing registers are
 * VIDTCON0..3 at 0x20c..0x218.
 */
#define CLCD_WINCFG0     0x0d8u
#define CLCD_UPDATE2     0x0e8u  /* 0x50001000 on AppleH1CLCD window updates  */
#define CLCD_WINCFG2_AUX 0x0ecu
#define CLCD_CSC_FIRST   0x1c8u  /* 8 YUV->RGB matrix regs, 0x1c8..0x1e8       */
#define CLCD_CSC_LAST    0x1e8u
#define CLCD_GATE        0x200u  /* VIDCON0 clock/divisor + scanout bit 0      */
#define CLCD_STATUS      0x204u  /* VIDCON1 polarity; bits[7:6] are live state */
#define CLCD_UNKNOWN208  0x208u
#define CLCD_VIDTCON0    0x20cu  /* vertical porch/sync timing                 */
#define CLCD_VIDTCON1    0x210u  /* horizontal porch/sync timing               */
#define CLCD_VIDTCON2    0x214u  /* active width/height minus one              */
#define CLCD_VIDTCON3    0x218u  /* openiBoot writes 1                         */
#define CLCD_OPAQUE_FIRST CLCD_UNKNOWN208
#define CLCD_OPAQUE_LAST  CLCD_VIDTCON3
#define CLCD_GAMMA0      0x400u  /* three 256-entry u32 LUTs, 0x400/0x800/0xc00 */
#define CLCD_GAMMA_SIZE  0x400u

/* openiBoot's N82 optC selects the 54 MHz display clock, divides it by five
 * for a 10.8 MHz pixel clock, inverts VCLK, and later sets ENVID_F. */
#define CLCD_N82_VIDCON0 0x00000441u
#define CLCD_N82_VIDCON1 0x00000008u

/*
 * Window register sub-offsets, from CLCD_WIN_FIRST + k*CLCD_WIN_STRIDE.
 *
 * CONFIRMED, not inferred, since 0xc0705f00 — AppleH1CLCD's "adopt whatever
 * iBoot left running" routine, the vtable slot IOMobileFramebuffer::start calls
 * immediately after start_hardware:
 *
 *     r2 = mapped register base
 *     if      (read32(r2+8) & 0x40) { sl=[r2+0x58] fp=[r2+0x5c] fb=[r2+0x60] r8=[r2+0x64] }
 *     else if (              & 0x20) { sl=[r2+0x70] fp=[r2+0x74] fb=[r2+0x78] r8=[r2+0x7c] }
 *     else if (              & 0x10) { sl=[r2+0x88] fp=[r2+0x8c] fb=[r2+0x90] r8=[r2+0x94] }
 *     else if (              & 0x08) { sl=[r2+0xa0] fp=[r2+0xa4] fb=[r2+0xa8] r8=[r2+0xac] }
 *
 * which pins the four window bases at 0x58/0x70/0x88/0xa0 (stride 0x18), pins
 * the CLCD_CTRL enable bits to 0x40/0x20/0x10/0x08 in that priority order, and
 * pins the field order. The driver then (0xc0706040..0xc0706068) does
 *
 *     width  = (r8 << 5) >> 21;      // geometry bits[26:16]
 *     height = uxth(r8 &~ 0xfc00);   // geometry bits[9:0]
 *     size   = round_up(sl * height, 0x1000);   // sl is bytes per row
 *
 * and wraps `fb` with IOMemoryDescriptor::withPhysicalAddress, so FBADDR is a
 * PHYSICAL address. Nothing here is a guess any more.
 */
#define CLCD_WIN_PITCH     0x00u  /* stride in BYTES                          */
#define CLCD_WIN_CONTROL   0x04u  /* bits[10:8] pixel format, [17:16] order   */
#define CLCD_WIN_FBADDR    0x08u  /* framebuffer PHYSICAL base                */
#define CLCD_WIN_GEOMETRY  0x0cu  /* (width << 16) | height                   */
#define CLCD_WIN_LINEWORDS 0x10u  /* line length in 32-bit words              */
#define CLCD_WIN_POSITION  0x14u  /* (dstX << 16) | dstY                      */

/* CLCD_CTRL window-enable bits, and the order the driver tests them in. */
#define CLCD_CTRL_WIN0   0x40u
#define CLCD_CTRL_WIN1   0x20u
#define CLCD_CTRL_WIN2   0x10u
#define CLCD_CTRL_WIN3   0x08u
#define CLCD_CTRL_VIDEO  0x80u
#define CLCD_CTRL_ENABLE 0x01u    /* start_hardware ORs this in */

/* CLCD_INTSTATUS / CLCD_INTMASK bits. */
#define CLCD_INT_FRAME    0x0001u /* frame (VBL) done — the swap completion   */
#define CLCD_INT_UNDERRUN 0x3f00u /* per-window FIFO underrun; we never set it */

/*
 * Pixel formats, from window control bits[10:8].
 *
 * The DEPTH is confirmed; the fine-grained names are not, and the difference
 * matters. At 0xc0705ff8 the driver switches on exactly this field to pick the
 * IOSurface pixel format it will publish, through a six-entry jump table:
 *
 *     f = (control >> 8) & 7;
 *     switch (f - 2) { case 0..3: fourcc = '565L'; case 4,5: fourcc = 'ARGB'; }
 *     default (f == 0 or 1):      fourcc = '565L';
 *
 * so the driver itself declares 6 and 7 to be 32 bits per pixel and everything
 * else to be 16-bit 5-6-5. It never distinguishes 2 from 3 from 4 from 5, so
 * neither do we: CLCD_FMT_IS_32BPP() is the only classification the binary
 * supports, and s5l_clcd_scanout() decodes on that and nothing finer.
 */
#define CLCD_FMT_SHIFT     8u
#define CLCD_FMT_MASK      0x7u
#define CLCD_FMT_32BPP     7u
#define CLCD_FMT_RGB565    3u
#define CLCD_FMT_RGB555    2u
#define CLCD_FMT_ARGB4444  5u
#define CLCD_FMT_IS_32BPP(f) ((f) == 6u || (f) == 7u)
/* Component order, from window control bits[17:16]; only meaningful at 32bpp. */
#define CLCD_ORDER_SHIFT   16u
#define CLCD_ORDER_MASK    0x3u
#define CLCD_ORDER_BGRA    0u
#define CLCD_ORDER_ARGB    3u

/* Refresh rate we present to the guest, in GUEST time. */
#define S5L_CLCD_REFRESH_HZ 60u

typedef struct {
    uint32_t stride, control, fbaddr, geometry, linewords, position;
} s5l_clcd_window_t;

typedef struct {
    uint32_t enable, disable, ctrl, fifo;
    uint32_t intmask, intstatus, reg1c, preenable, backdrop;
    uint32_t video[(CLCD_VIDEO_LAST - CLCD_VIDEO_FIRST) / 4u + 1u];
    s5l_clcd_window_t win[CLCD_WIN_COUNT];
    uint32_t update, update2;
    uint32_t wincfg_aux[5];                  /* 0x0d8,0x0dc,0x0e0,0x0e4,0x0ec */
    uint32_t csc[(CLCD_CSC_LAST - CLCD_CSC_FIRST) / 4u + 1u];
    uint32_t gate;
    uint32_t opaque[(CLCD_OPAQUE_LAST - CLCD_OPAQUE_FIRST) / 4u + 1u];
    uint32_t gamma[3][256];

    bool     scanning;        /* CLCD_ENABLE has been written 1, no stop since */
    uint32_t frame_ticks;     /* timebase ticks per frame; 0 disables the VBL  */
    uint32_t frame_accum;
    uint64_t frames;          /* elapsed VBL boundaries (host visibility)      */
    /*
     * Window updates the GUEST submitted, as distinct from `frames`, which is
     * VBL boundaries the model manufactured. run76/83 measured 289 VBLANKs
     * against 150 swaps, so the two are not the same number and only this one
     * says how often the guest actually redrew.
     *
     * It exists to make instructions-per-frame measurable. Every claim in this
     * project about reachable frame rate -- with a JIT or without -- rests on
     * that number, and nothing has ever measured it.
     */
    uint64_t updates;
} s5l_clcd_t;

void     s5l_clcd_reset(s5l_clcd_t *c);
uint32_t s5l_clcd_read(s5l_clcd_t *c, uint32_t off);
void     s5l_clcd_write(s5l_clcd_t *c, uint32_t off, uint32_t val);
/* Advance by `ticks` timebase ticks; returns true while the controller's
 * interrupt output is asserted (status AND mask, as the hardware ANDs them). */
bool     s5l_clcd_tick(s5l_clcd_t *c, uint32_t ticks);

/* True only while pixels can actually be scanned: start has been written,
 * CLCD_CTRL's global enable is set, and VIDCON0/gate bit 0 is set. Window
 * enable bits are intentionally separate because callers may need to diagnose
 * a running controller with no adoptable RGB window. */
bool     s5l_clcd_running(const s5l_clcd_t *c);

/*
 * Pre-seed the controller with an iBoot-compatible N82 display handoff:
 * window 0 programmed and enabled over an already-running scanout, plus the
 * clock and 320x480 porch/sync registers that openiBoot records for this panel.
 * IOMobileFramebuffer::start then adopts the framebuffer verbatim instead of
 * having to invent one.
 *
 * `format` is a CLCD_FMT_* value and `order` a CLCD_ORDER_* value; `stride` is
 * in bytes. This is a host-side call, not a guest-visible register: a boot stub
 * standing in for iBoot calls it before the guest runs. Returns false without
 * changing the controller if the geometry cannot be represented safely or if
 * AppleH1CLCD's page-rounded `stride * height` physical mapping would overflow
 * its 32-bit size/address space.
 */
bool     s5l_clcd_seed_window0(s5l_clcd_t *c, uint32_t fb_phys,
                               uint32_t width, uint32_t height,
                               uint32_t stride, uint32_t format, uint32_t order);

/* Read back a window's programming. Returns false if `k` is out of range or the
 * window is not enabled in CLCD_CTRL. */
bool     s5l_clcd_window(const s5l_clcd_t *c, unsigned k,
                         uint32_t *fb_phys, uint32_t *width, uint32_t *height,
                         uint32_t *stride, uint32_t *format, uint32_t *order);

/*
 * Which window the DRIVER would scan out, tested in the driver's own priority
 * order (window 0, then 1, then 2, then 3 — 0xc0705f10..0xc0705f94). Returns
 * CLCD_WIN_NONE when CLCD_CTRL enables no window at all, which is not a
 * cosmetic state: with no bit set the driver leaves its four locals holding
 * whatever the caller's frame did and builds an IOSurface out of that garbage,
 * so "no window enabled" is a bug in whatever stood in for iBoot, not a blank
 * screen. Callers must treat CLCD_WIN_NONE as an error.
 */
#define CLCD_WIN_NONE 0xffffffffu
uint32_t s5l_clcd_active_window(const s5l_clcd_t *c);

/*
 * Scan out an enabled window into 24-bit RGB, one byte per channel, top row
 * first — the seam clcd.c's header has always named, now that the window layout
 * is confirmed rather than inferred.
 *
 * `ram`/`ram_base`/`ram_len` describe the guest DRAM the window's PHYSICAL
 * framebuffer address is resolved against; a window pointing outside it is an
 * error, not a black rectangle. `rgb` must hold at least width*height*3 bytes.
 *
 * Returns false — and writes NOTHING — if the window is disabled, the geometry
 * is empty, the stride cannot hold a row, the source does not lie wholly inside
 * guest DRAM, or `rgb` is too small. It never invents a pixel: every returned
 * byte comes from guest memory.
 */
bool     s5l_clcd_scanout(const s5l_clcd_t *c, unsigned k,
                          const uint8_t *ram, uint32_t ram_base, size_t ram_len,
                          uint8_t *rgb, size_t rgb_len,
                          uint32_t *out_w, uint32_t *out_h);

/* ---------------------------------------------------------- TV-out path ---
 *
 * The shipped iPhone1,2 7E18 device tree names one /arm-io/tv-out service with
 * three 4 KiB register ranges.  Resolving those child addresses through the
 * arm-io ranges gives, in the order AppleH1DisplayDrivers maps them:
 *
 *   0x39100000  TV control / coefficient-scaler block
 *   0x39200000  mixer
 *   0x39300000  SDO (standard-definition output)
 *
 * The same node gives interrupt lines {30,38}.  AppleH1DisplayDrivers uses
 * line 30 for SDO VSYNC/swap completion: its filter tests SDO_IRQ bit 0 and
 * its action writes that bit back (W1C) before dequeuing the swap.  Line 38 is
 * a separate mixer status source; this minimal model preserves its W1C status
 * register but does not invent a cable/hotplug event.
 *
 * Unknown registers remain byte-addressable backing storage.  That is
 * intentional: the model supplies only the side effects established by the
 * shipped driver while retaining every other guest write for later evidence.
 */
#define S5L8900_TVOUT_CTRL_BASE  0x39100000u
#define S5L8900_TVOUT_MIXER_BASE 0x39200000u
#define S5L8900_TVOUT_SDO_BASE   0x39300000u
#define S5L8900_IRQ_TVOUT        30u

#define S5L_TVOUT_BANK_SIZE      0x1000u
#define S5L_TVOUT_BANK_WORDS     (S5L_TVOUT_BANK_SIZE / 4u)
#define S5L_TVOUT_REFRESH_HZ     60u

#define TVOUT_CTRL_ENABLE        0x000u
#define TVOUT_MIXER_STATUS       0x04cu
#define TVOUT_SDO_CLKCON         0x000u
#define TVOUT_SDO_IRQ            0x280u
#define TVOUT_SDO_IRQMASK        0x284u

#define TVOUT_RUN                0x00000001u
#define TVOUT_READY              0x00000002u
#define TVOUT_SDO_VSYNC          0x00000001u
#define TVOUT_SDO_MASK_VSYNC     0x00000001u

typedef enum {
    S5L_TVOUT_BANK_CTRL = 0,
    S5L_TVOUT_BANK_MIXER,
    S5L_TVOUT_BANK_SDO,
    S5L_TVOUT_BANK_COUNT
} s5l_tvout_bank_t;

typedef struct {
    uint32_t regs[S5L_TVOUT_BANK_COUNT][S5L_TVOUT_BANK_WORDS];
    uint32_t frame_ticks;      /* guest timebase ticks per generated VSYNC */
    uint32_t frame_accum;
    uint64_t frames;           /* elapsed boundaries, even while pending */
} s5l_tvout_t;

void     s5l_tvout_reset(s5l_tvout_t *t, uint32_t tb_hz);
uint32_t s5l_tvout_read(const s5l_tvout_t *t, s5l_tvout_bank_t bank,
                        uint32_t off, unsigned bytes);
void     s5l_tvout_write(s5l_tvout_t *t, s5l_tvout_bank_t bank,
                         uint32_t off, uint32_t val, unsigned bytes);
/* True while the persistent mixer and SDO timing engines are live.  The
 * control/scaler block has an independent handshake and is not a VSYNC gate. */
bool     s5l_tvout_running(const s5l_tvout_t *t);
bool     s5l_tvout_irq(const s5l_tvout_t *t);
/* Advance guest time and return the current level of interrupt line 30. */
bool     s5l_tvout_tick(s5l_tvout_t *t, uint32_t ticks);
/* Distance to the next deliverable VSYNC, or zero if none can wake WFI. */
uint32_t s5l_tvout_ticks_to_vsync(const s5l_tvout_t *t);

/* ----------------------------------------------------------------- I2C ---
 * The two S5L8900 I2C controllers.  The physical windows and interrupt lines
 * come from the shipped iPhone1,2 device tree:
 *
 *   /arm-io/i2c0  reg {0x04600000,0x1000}  interrupts {0x15}
 *   /arm-io/i2c1  reg {0x04900000,0x1000}  interrupts {0x16}
 *
 * /arm-io maps those child addresses at physical +0x38000000.  Register
 * offsets and bits below are from AppleS5L8900XI2CController's 32-bit MMIO
 * accessors and transfer state machine in the shipped kernelcache.
 */
#define S5L8900_I2C0_BASE 0x3c600000u
#define S5L8900_I2C1_BASE 0x3c900000u
#define S5L8900_IRQ_I2C0  21u
#define S5L8900_IRQ_I2C1  22u
#define S5L8900_I2C_COUNT 2u

#define I2C_CON     0x00u
#define I2C_STAT    0x04u
#define I2C_ADD     0x08u
#define I2C_DS      0x0cu
#define I2C_BUSY    0x10u  /* read zero: register writes complete immediately */
#define I2C_ENABLE  0x14u
#define I2C_INT     0x20u  /* interrupt status, write-one-to-clear             */

#define I2C_CON_ACKEN     0x80u
#define I2C_CON_RESUME    0x10u
#define I2C_STAT_MODE     0xc0u
#define I2C_STAT_MODE_MRX 0x80u
#define I2C_STAT_MODE_MTX 0xc0u
#define I2C_STAT_START    0x20u
#define I2C_STAT_ENABLE   0x10u
#define I2C_STAT_NAK      0x01u  /* read-only: last address/byte was not ACKed */

#define I2C_INT_BYTE 0x0100u
#define I2C_INT_STOP 0x2000u
#define I2C_INT_ALL  0x3f00u  /* mask used by the stock driver's clear-all */

typedef struct {
    uint8_t  addr;                         /* seven-bit bus address */
    void    *ctx;
    bool   (*start)(void *ctx, bool read);
    bool   (*write)(void *ctx, uint8_t byte);
    uint8_t (*read)(void *ctx);
    void   (*stop)(void *ctx);
} s5l_i2c_slave_t;

#define S5L_I2C_SLAVES      4u
#define S5L_I2C_UNKNOWN_OFF 8u

typedef struct {
    uint32_t con, stat, add, ds, enable, intstat;
    bool     nak;
    bool     active;
    bool     reading;
    int32_t  sel;             /* selected slave index, or -1 */

    /* Bounded diagnostics: unknown traffic must be visible without allowing
     * a guest to grow host allocations. */
    uint64_t starts, bytes_tx, bytes_rx, naks;
    uint64_t unknown_reads, unknown_writes;
    uint32_t unknown_off[S5L_I2C_UNKNOWN_OFF];
    unsigned unknown_off_count;

    /* Host wiring. Snapshot code serializes `sel`, never these callbacks. */
    s5l_i2c_slave_t slaves[S5L_I2C_SLAVES];
    unsigned        slave_count;
} s5l_i2c_t;

/* Reset is total: it is valid on an uninitialized/poisoned object and removes
 * attached slaves. Callers attach board wiring after reset. */
void     s5l_i2c_reset(s5l_i2c_t *bus);
bool     s5l_i2c_attach(s5l_i2c_t *bus, const s5l_i2c_slave_t *slave);
uint32_t s5l_i2c_read(s5l_i2c_t *bus, uint32_t off);
void     s5l_i2c_write(s5l_i2c_t *bus, uint32_t off, uint32_t val);
bool     s5l_i2c_irq(const s5l_i2c_t *bus);

/* ------------------------------------------------- PCF50635 PMU / RTC ---
 * The device tree names `pmu,pcf50635` at seven-bit address 0x73 on i2c0.
 * Its register pointer is one byte and auto-increments.  Written registers are
 * persistent storage; reads of other unmodelled registers return zero but are
 * counted.  RTC registers 0x59..0x5f are fully defined by the stock driver's
 * decoder: BCD except for the binary weekday at 0x5c.
 */
#define PCF50635_I2C_ADDR 0x73u
#define PCF50635_NREG     0x100u
#define PCF50635_INT1     0x02u
#define PCF50635_INT2     0x03u
#define PCF50635_INT3     0x04u
#define PCF50635_INT4     0x05u
#define PCF50635_INT5     0x06u
#define PCF50635_INT1MASK 0x07u
#define PCF50635_INT2MASK 0x08u
#define PCF50635_INT3MASK 0x09u
#define PCF50635_INT4MASK 0x0au
#define PCF50635_INT5MASK 0x0bu
#define PCF50635_OOCSHDWN 0x0cu
#define PCF50635_INT2_ONKEYR 0x01u
#define PCF50635_OOCSHDWN_GO_STANDBY 0x01u
#define PCF50635_RTCSC    0x59u
#define PCF50635_RTCWD    0x5cu
#define PCF50635_RTCYR    0x5fu
#define PCF50635_MIN_TIME 946684800ull  /* 2000-01-01 00:00:00 UTC */
#define PCF50635_MAX_TIME 4102444799ull /* 2099-12-31 23:59:59 UTC */
#define PCF50635_DEFAULT_TIME 1262304000ull /* 2010-01-01 00:00:00 UTC */
#define PCF50635_UNKNOWN_REGS 16u

typedef struct {
    uint8_t  regs[PCF50635_NREG];
    uint8_t  written[PCF50635_NREG];

    uint64_t seconds;
    uint32_t tick_hz;
    uint64_t tick_accum;

    uint8_t  ptr;
    bool     have_ptr;
    bool     reading;

    uint64_t reg_reads, reg_writes, unknown_reads, unknown_writes;
    uint8_t  unknown_reg[PCF50635_UNKNOWN_REGS];
    unsigned unknown_reg_count;
} s5l_pcf50635_t;

void s5l_pcf50635_reset(s5l_pcf50635_t *pmu, uint32_t tick_hz);
void s5l_pcf50635_set_time(s5l_pcf50635_t *pmu, uint64_t unix_seconds);
void s5l_pcf50635_tick(s5l_pcf50635_t *pmu, uint32_t ticks);
void s5l_pcf50635_bind(s5l_pcf50635_t *pmu, s5l_i2c_slave_t *slave);
bool s5l_pcf50635_irq(const s5l_pcf50635_t *pmu);
bool s5l_pcf50635_in_standby(const s5l_pcf50635_t *pmu);
void s5l_pcf50635_wake_onkey(s5l_pcf50635_t *pmu);
void s5l_pcf50635_civil(uint64_t unix_seconds, int *year, int *month, int *day,
                        int *hour, int *minute, int *second, int *weekday);

/* ------------------------------------------- WM8991 audio codec (i2c0) ---
 * The Wolfson codec `AppleWM8991Audio` drives. Everything below is read out of
 * the shipped device tree and the shipped kext; nothing is taken from a
 * datasheet, because we have none for this part and a guessed register map is
 * indistinguishable from a correct one until the boot diverges.
 *
 * WHERE IT IS. `/arm-io/i2c0/audio0`, device_type `audio-control`, compatible
 * `audio-control,wm8991`, `reg = {0x1b, 0x9c4, 0, 0}` — seven-bit address
 * 0x1B on i2c0, exactly the shape the PMU node uses for 0x73. Its sibling
 * `/arm-io/i2s0/audio0` is `audio-data,wm8991`, which is what puts this codec's
 * sample path on i2s0 rather than i2s1 (i2s1's child is `audio-data,baseband`).
 *
 * THE WIRE PROTOCOL, from AppleEmbeddedAudio's own transfer helpers:
 *
 *   read  (0xc053ff94): one index byte out, then TWO bytes in, and the value is
 *         assembled MSB-first — `ldrb r3,[sp,#0xe]; ldrb r0,[sp,#0xf];
 *         orr r0,r0,r3,lsl #8` at 0xc0540030..0xc0540038. The first byte on
 *         the wire is bits [15:8]. On failure this is the helper that prints
 *         `%s: I2C register read failed (%#x): %s` (0xc0548048) — the exact
 *         format string behind today's `(0): device error`, with `%#x` of zero
 *         rendering as a bare `0`.
 *   write, wide form (0xc0540050): index byte, then the 16-bit value MSB-first
 *         — `lsr r3,r5,#8; strb r3,[sp,#0xe]; strb r5,[sp,#0xf]`. Three bytes
 *         after the address.
 *   write, packed form (0xc0540108): the classic Wolfson two-wire encoding,
 *         seven-bit register and nine-bit datum in two bytes —
 *         `and r1,r1,#0x7f; and r3,r2,#0x100; lsl r1,r1,#1;
 *          orr r1,r1,r3,lsr #8` gives byte0 = (reg << 1) | value[8], and
 *         byte1 = value[7:0]. Two bytes after the address.
 *
 * THIS codec uses the WIDE form: `AppleWM8991Audio::writeCodecRegister`
 * (0xc068b168) dispatches to the wide helper and nothing in the kext reaches
 * the packed one, which belongs to the WM8758 sibling driver. Both are
 * implemented here anyway, because the BYTE COUNT distinguishes them for free
 * and a model that silently misreads a two-byte transfer as a truncated
 * three-byte one would be wrong in a way no test would catch: one byte after
 * the address is a pointer-only write — the stock controller's read setup,
 * which must not be mistaken for a register store — two bytes is the packed
 * form, three the wide form. The counts are distinct, so nothing here has to
 * guess which encoding it is looking at, and any other length is refused and
 * counted rather than interpreted.
 *
 * WHICH REGISTERS EVER REACH THIS MODEL AT ALL. Only six.
 * `AppleWM8991Audio::readCodecRegister` (0xc068b1b4) gates every read on the
 * bitmap 0x0084000f held at 0xc068b21c — `ands r3, r2, r3, lsl r1` over
 * `1 << reg` — and serves everything else out of a RAM shadow the driver seeds
 * from its own default table at 0xc0691030. Set bits {0,1,2,3,18,23} means
 * registers 0x00, 0x01, 0x02, 0x03, 0x12 and 0x17 are the complete set whose
 * readback this model can affect. That default table is NOT reproduced here:
 * its 63 entries are the driver's belief about an untouched part, the driver
 * never needs the device to supply them, and copying them in would be the full
 * register map this model deliberately does not invent. Its entry 0 is 0x8990,
 * which is a second independent confirmation of the identity literal.
 *
 * WHAT IS ACTUALLY VALIDATED. The identify method at 0xc068b078 (codec vtable
 * slot +0x178, which calls its own superclass at the same slot first):
 *
 *   - reads register 0 and compares it against the literal at 0xc068b124.
 *     That word is `0x00008990`, confirmed by reading the file at the byte
 *     level, and 0xc068b0ac is the ONLY instruction anywhere in the kernelcache
 *     that loads it. A mismatch takes `movne r5,#0` and the probe fails. This
 *     is the one hard gate.
 *   - then writes register 1 bit 5, reads register 1 back, and tests bit 5.
 *     This is NOT a second gate: both branches return the same success value.
 *     It selects a NAME. `strbeq` stores 0 when the bit reads back clear and
 *     the getter at 0xc068b044 turns 0 into "WM8991" (0xc0690158) and 1 into
 *     "WM1817" (0xc0690150). So a part whose R1 bit 5 sticks is reported as a
 *     WM1817. The shipped device tree says `wm8991` in both of its audio nodes
 *     and contains no occurrence of "1817" at all, so on THIS board bit 5 must
 *     read back clear, and this model leaves it unimplemented: writes to it are
 *     discarded and reads return zero. The flag it sets is loaded from `this`
 *     at ten further sites in the kext, so this is not a cosmetic choice.
 *
 * THE ONE PLACE THIS MODEL INFERS RATHER THAN OBSERVES, stated plainly because
 * it is the only one. The codec's GPIO configure path contains a poll at
 * 0xc068d4ac..0xc068d514 with NO timeout, NO iteration cap and no IODelay or
 * IOSleep in its body:
 *
 *     rmw(0x17, 0x1000, level << 12);            // 0xc068d44c, before the loop
 *     do {  write(0x17, computed);               // 0xc068d4cc
 *           write(0x12, last & 0x0000efff);      // 0xc068d4e8 — clears bit 12
 *           v = read(0x12);                      // 0xc068d4fc
 *     } while ((v & ~arg & 0x1000) != (level << 12));   // 0xc068d50c
 *
 * The driver forces bit 12 of register 0x12 CLEAR every time it writes that
 * register, and then waits for that same bit to read back as `level`. So it
 * cannot be waiting for storage: on a part where 0x12 bit 12 held what was last
 * written, `level == 1` would spin forever. The only self-consistent reading is
 * that 0x12 bit 12 is a read-only status reflecting the request made through
 * 0x17 bit 12 — a pin commanded in one register and observed in another. This
 * model implements exactly that and nothing more: bit 12 of register 0x12 is
 * not storage, it mirrors bit 12 of register 0x17.
 *
 * That is an inference from the poll's own structure, not a measurement, and it
 * is deliberately the narrowest one that makes a loop whose only exit condition
 * is the device terminate. It is counted, so a boot can say how often it
 * mattered. Every other bit of both registers is ordinary storage.
 *
 * WHAT IS NOT MODELLED. Everything else. There is no volume, routing, clocking,
 * power-sequencing or mute behaviour here, and no register has a reset value
 * other than zero — because none was established. Unwritten registers read zero
 * and are recorded, exactly as the PMU does, so the next reader learns which
 * registers the driver actually wanted from a boot rather than from a guess.
 */
#define WM8991_I2C_ADDR   0x1bu
#define WM8991_NREG       0x80u   /* seven-bit register index space */
#define WM8991_REG_ID     0x00u
#define WM8991_ID_VALUE   0x8990u /* literal at 0xc068b124; the sole hard gate */
#define WM8991_REG_PWR1   0x01u
#define WM8991_PWR1_PROBE 0x0020u /* bit 5: the WM8991/WM1817 discriminator   */
#define WM8991_REG_GPSTAT 0x12u   /* polled at 0xc068d4fc; bit 12 read-only   */
#define WM8991_REG_GPCTRL 0x17u   /* commanded at 0xc068d44c; drives the above */
#define WM8991_GP_BIT     0x1000u
#define WM8991_UNKNOWN_REGS 16u
#define WM8991_MAX_WRITE  3u      /* the longest form: index + two data bytes */

typedef struct {
    uint16_t regs[WM8991_NREG];
    uint8_t  written[WM8991_NREG];

    /* Transfer position. `ptr` survives STOP for the same reason the PMU's
     * does: the stock controller sets the pointer in one transaction and reads
     * in the next, so forgetting it between them would break every read. */
    uint8_t  ptr;
    bool     reading;
    /* Which half of the 16-bit value the next read byte is. False means the
     * MSB, which is what a fresh read must always start with. */
    bool     second_byte;
    /* The register sampled at the first byte and held for the second. Real
     * in-flight state: a checkpoint taken between the two halves has already
     * put the MSB on the wire, and the LSB must come from the same sample. */
    uint16_t latch;
    /* Bytes of the current write, address byte excluded. wbuf[0] is the index
     * or the packed first byte; the rest are data. */
    uint8_t  wbuf[WM8991_MAX_WRITE];
    unsigned wlen;

    uint64_t reg_reads, reg_writes;
    uint64_t wide_writes, packed_writes;  /* which encoding the driver used  */
    uint64_t refused_writes;              /* a length this model cannot read */
    uint64_t id_reads;                    /* how often register 0 was asked  */
    uint64_t probe_bit_writes;            /* attempts to set R1 bit 5        */
    uint64_t status_mirror_reads;         /* reads of 0x12 that took bit 12  */
                                          /* from 0x17 rather than storage   */
    uint64_t unknown_reads;
    uint8_t  unknown_reg[WM8991_UNKNOWN_REGS];
    unsigned unknown_reg_count;
} s5l_wm8991_t;

void s5l_wm8991_reset(s5l_wm8991_t *codec);
void s5l_wm8991_bind(s5l_wm8991_t *codec, s5l_i2c_slave_t *slave);
/* The value a guest read of `reg` would return, without disturbing transfer
 * state. Exposed so the tests can assert the register file directly. */
uint16_t s5l_wm8991_peek(const s5l_wm8991_t *codec, uint8_t reg);

/* M68's WM8758 uses the same bounded register/transaction storage layout,
 * but has a separate 7-bit-index/9-bit-data protocol and no WM8991 ID probe.
 * Reusing storage preserves v39 checkpoints; callbacks and validation remain
 * codec-specific. Unwritten registers are reset state, not stored defaults. */
typedef s5l_wm8991_t s5l_wm8758_t;
#define WM8758_I2C_ADDR 0x1au
#define WM8758_NREG 62u
void s5l_wm8758_reset(s5l_wm8758_t *codec);
void s5l_wm8758_bind(s5l_wm8758_t *codec, s5l_i2c_slave_t *slave);
bool s5l_wm8758_register_valid(unsigned reg);
uint16_t s5l_wm8758_peek(const s5l_wm8758_t *codec, unsigned reg);

/* ----------------------------------------------------------------- I2S ---
 * The two S5L8900 I2S controllers. Their windows are derived at the top of this
 * header; this is the register model, and it is deliberately the smallest one
 * in this file because the driver is the smallest consumer in the kernelcache.
 *
 * WHY IT IS THIS SMALL. AppleS5L8900XI2SController funnels every window access
 * through two three-instruction accessors, and only one of them is ever
 * reached:
 *
 *     readRegister   0xc05a3c84   ldr r3,[r0,#0x78]; ldr r0,[r1,r3]; bx lr
 *     writeRegister  0xc05a3c90   ldr r3,[r0,#0x78]; str r2,[r1,r3]; bx lr
 *
 * `readRegister` has NO caller anywhere in the kernelcache. Four independent
 * checks agree: its address occurs as an aligned word exactly once in the whole
 * file (its own vtable slot 0xc05ad888); no ARM or Thumb BL/BLX targets it; no
 * `ldr pc,[Rn,#0x3c0]` dispatch exists with any base but PC; and slot +0x3c0 is
 * a virtual this class INTRODUCES — its parent AppleARMIISController's vtable
 * ends at +0x3ac — so no other kext can reach it through a base pointer even in
 * principle. The class therefore contains exactly one MMIO load instruction in
 * the entire image, and that instruction is dead code.
 *
 * That is what makes an unread window safe rather than merely convenient: there
 * is no status bit, no reset acknowledge, no FIFO level and no revision check
 * for a fabricated zero to answer wrongly, because nothing asks. It also
 * independently explains the zero MMIO traffic run62's census recorded on
 * 0x3ca00000 and 0x3cd00000 while both controllers started and published.
 *
 * THE SEVEN OFFSETS, enumerated from every writeRegister dispatch site:
 *
 *   configure()     0xc05a3820  +0x00 (cfg|1), +0x40, +0x04 (cfg|1), +0x30,
 *                               +0x08 (0), +0x34 (0), +0x3c (1, if TX)
 *   startTransfer() 0xc05a3928  +0x08 (6), +0x34 (6)
 *   stop()          0xc05a3ad0  +0x08 (0), +0x34 (0)
 *
 * The values at +0x00/+0x04/+0x30/+0x40 come from a caller-supplied
 * AppleARMIISCommand, never from a register read. NO SEMANTICS ARE CLAIMED for
 * any of the seven. The pattern at +0x08/+0x34 (0 configured, 6 running, 0
 * stopped) is consistent with a per-direction enable, and bit 0 is forced set
 * at +0x00/+0x04, but "consistent with" is not "established", and this model
 * stores rather than interprets. Anything outside the seven is counted and its
 * offset recorded, exactly as on I2C and SPI, so a driver that grows a new
 * register names it in a census instead of vanishing into a stub.
 *
 * WHAT THIS IS NOT. Not a sample path. The PCM FIFOs live at +0x10 and +0x38
 * and the CPU never touches them: the device tree hands their PHYSICAL
 * addresses (0x3ca00010/0x3ca00038, 0x3cd00010/0x3cd00038 — already absolute in
 * the `dma-channels` blob, not arm-io relative) straight to the PL080, which is
 * not modelled. No clock, no frame timing, no interrupt. i2s0 carries the
 * WM8991 (`/arm-io/i2s0/audio0`, `audio-data,wm8991`); i2s1 carries the
 * baseband voice path (`audio-data,baseband`), which is why both windows exist
 * here even though only one of them belongs to the codec.
 */
#define S5L_I2S_REGS         7u
#define S5L_I2S_UNKNOWN_OFF  8u

typedef struct {
    /* Storage for the seven offsets above, in the order they are listed by
     * s5l_i2s_offset(). Indexing by a small map rather than by offset/4 keeps
     * this 28 bytes instead of a 4 KiB page nothing would ever read back. */
    uint32_t regs[S5L_I2S_REGS];
    uint64_t reads, writes;
    uint64_t unknown_reads, unknown_writes;
    uint32_t unknown_off[S5L_I2S_UNKNOWN_OFF];
    unsigned unknown_off_count;
} s5l_i2s_t;

void     s5l_i2s_reset(s5l_i2s_t *i2s);
uint32_t s5l_i2s_read(s5l_i2s_t *i2s, uint32_t off);
void     s5l_i2s_write(s5l_i2s_t *i2s, uint32_t off, uint32_t val);
/* The byte offset backing slot `index`, or UINT32_MAX past the end. The map is
 * exposed so the tests pin the exact seven the driver writes rather than
 * re-deriving them from this model's own storage order. */
uint32_t s5l_i2s_offset(unsigned index);

/* --- the two FIFOs, named rather than merely counted --------------------
 * The paragraph above says the FIFOs live at +0x10 and +0x38 and that the CPU
 * never touches them. Both halves of that remain true and neither is weakened
 * here: nothing below stores anything, nothing below changes what a guest
 * access does, and a CPU access to either offset is still counted as the
 * off-map anomaly it is. What is added is a NAME for the two offsets and the
 * evidence for which of them carries samples OUT, so a capture that taps them
 * cites the device tree instead of a constant somebody typed.
 *
 * WHERE THE TWO OFFSETS COME FROM. /arm-io/i2s0's own `dma-channels`, 64 bytes,
 * read as two 32-byte entries of little-endian words:
 *
 *   [0] 0x00000800 0x00249000 0x00000000 0x3ca00010 0 0 0 0
 *   [1] 0x00001042 0x00249000 0x3ca00038 0x00000000 0 0 0 0
 *
 * and /arm-io/i2s1's is the same shape over its own window:
 *
 *   [0] 0x00000884 0x00249000 0x00000000 0x3cd00010 1 0 0 0
 *   [1] 0x000010c6 0x00249000 0x3cd00038 0x00000000 1 0 0 0
 *
 * Words 2 and 3 are a pair with exactly one side filled in; the zero side is
 * the memory end, which only exists once a transfer is set up. In all four
 * entries +0x10 occupies the LATER of the two slots and +0x38 the EARLIER, so
 * the two channels are opposite directions -- that much is established, twice,
 * independently per controller.
 *
 * WHICH ONE IS TRANSMIT IS AN INFERENCE, not an established fact, and the
 * distinction matters enough to spell out. It rests on reading the pair as
 * (source, destination), which makes +0x10 a destination and therefore the
 * transmit side. Word 0 is consistent with that -- the +0x10 channel carries
 * 0x0800 and the +0x38 one 0x1000, a single differing bit, while their low
 * bits are peripheral identifiers that shift by the same +0x84 from i2s0 to
 * i2s1 -- but "consistent with" is not "established", exactly as for the seven
 * registers above. Nothing here depends on the inference being right: a capture
 * records CPU STORES, and a store is data leaving the CPU whichever FIFO it
 * lands in, so a reversed reading would mislabel a stream rather than lose one.
 */
typedef enum {
    S5L_I2S_FIFO_NONE = 0,   /* not a FIFO offset                            */
    S5L_I2S_FIFO_TX,         /* +0x10: the destination side of channel 0     */
    S5L_I2S_FIFO_RX          /* +0x38: the source side of channel 1          */
} s5l_i2s_fifo_t;

#define S5L_I2S_TX_FIFO_OFF 0x10u
#define S5L_I2S_RX_FIFO_OFF 0x38u

/* Which FIFO, if either, `off` names. Pure; takes no device. */
s5l_i2s_fifo_t s5l_i2s_fifo_role(uint32_t off);
/* The absolute physical address the `dma-channels` blob hands the PL080 for
 * controller `index` (0 or 1). UINT32_MAX for a bad index or S5L_I2S_FIFO_NONE.
 * Exposed so a host tap derives the address the same way the tree states it. */
uint32_t s5l_i2s_fifo_pa(unsigned index, s5l_i2s_fifo_t which);

/* ----------------------------------------------------------------- SPI ---
 * The two S5L8900 SPI controllers AppleS5L8900XSPIController drives. Their
 * windows and VIC lines are derived at the top of this header; what follows is
 * the register model, and why it is a model rather than the stub it replaces.
 *
 * WHY THIS EXISTS. AppleMultitouchZ2SPI::finishStarting() @0xc0442670 probes
 * the touch controller through isInHBPP() @0xc0441008, which issues one 16-byte
 * full-duplex transfer on spi1 — `provider->vtbl[0x368](tx, 16, rx, 16, 0)` at
 * 0xc04410a4 — and then blocks at 0xc05a6d80 in IOCommandGate::commandSleep(
 * event, 0): the two-argument form, so no deadline. It loops there while its
 * done flag is clear (0xc05a6d8c) until the SPI completion interrupt sets the
 * flag and calls commandWakeup. Against a storage stub that interrupt can never
 * arrive, so the kext has been asleep since the first time it ran. run59
 * measured the shape of the stall exactly: spi1 `r=0 w=19`, which is the
 * driver's eleven configuration writes plus the eight bytes it pushes into an
 * eight-deep transmit FIFO before it starts and sleeps. `r=0` is the proof that
 * the interrupt handler never ran even once.
 *
 * THE ONE RULE THAT MATTERS. The interrupt routine is the FILTER half of an
 * IOFilterInterruptEventSource (registered at 0xc05a7150; body at 0xc05a6688,
 * action finishTransfer at 0xc05a6840). It reads STATUS and, when the
 * receive-FIFO level field is zero, returns false at 0xc05a66e4 having
 * acknowledged nothing — and a filter returning false does not schedule its
 * action, so finishTransfer never runs and nothing calls commandWakeup.
 * Raising the line with an empty receive FIFO therefore reproduces the
 * unbounded sleep exactly rather than ending it, which is why s5l_spi_irq()
 * requires a byte the filter can actually drain.
 *
 * WHAT THIS IS NOT. No clock rate, DMA channel, chip-select edge or bit-order
 * behaviour is emulated. The dividers at 0x30/0x38 are stored and never turned
 * into time: a word is shifted the instant the guest stores it, exactly as an
 * I2C transfer completes inside its command store.
 */
#define SPI_CONTROL 0x00u   /* run/mode; 0x0d starts, 0 powers the block down */
#define SPI_SETUP   0x04u
#define SPI_STATUS  0x08u   /* event latches + both FIFO levels               */
#define SPI_PIN     0x0cu   /* internal chip select in bit 1                  */
#define SPI_TXDATA  0x10u   /* transmit FIFO, write-only                      */
#define SPI_RXDATA  0x20u   /* receive FIFO, read-only and destructive        */
#define SPI_CLKDIV  0x30u
#define SPI_CNT     0x34u   /* word count = max(txLen, rxLen)                 */
#define SPI_IDD     0x38u   /* second divider                                 */

/*
 * CONTROL. The stock driver writes 0x0d to start a transfer and 0 to power the
 * block down; run23 caught BasebandSPI writing 0x0c to spi2 while configuring
 * it and never starting anything, so bit 0 is the run bit and the 0x0c above it
 * is a mode field both drivers share.
 */
#define SPI_CONTROL_RUN   0x0001u
#define SPI_CONTROL_START 0x000du

/*
 * SETUP. The driver builds a base of 0x1000 (0xc05a6fdc, the spi-version 0 arm
 * of start()) ORed with 0x18 (0xc05a64b4), ORs 0x20 to arm the transfer
 * (0xc05a6cb8), and then ORs 0x180 to go (0xc05a6d3c). 0x40 selects DMA
 * (0xc05a6c40), which this model does not implement.
 *
 * Of that 0x180, 0x100 is the completion-interrupt enable, and it is the one
 * SETUP bit s5l_spi_irq() consults. Two independent sites pin it: the filter
 * clears exactly 0x100 and nothing else when the transfer's counts run out
 * (`bic r3, r3, #0x100` at 0xc05a6808), and finishTransfer writes the bare base
 * word back (0xc05a685c), dropping it again.
 *
 * Gating on it is not caution, it is required. The driver's order is fill the
 * transmit FIFO, arm, THEN enable — precisely so the completion interrupt
 * cannot arrive while it is still pushing bytes. A line that ignored this bit
 * would fire on the driver's FIRST prefill store, and the filter would run
 * against half-initialised transfer counts.
 */
#define SPI_SETUP_BASE 0x1018u
#define SPI_SETUP_ARM  0x0020u
#define SPI_SETUP_RUN  0x0080u
#define SPI_SETUP_IRQ  0x0100u
#define SPI_SETUP_GO   (SPI_SETUP_RUN | SPI_SETUP_IRQ)
#define SPI_SETUP_DMA  0x0040u

/*
 * STATUS, for `spi-version 0` — which is what all three controllers are. The
 * shipped tree gives every one of them `spi-version {0}`, and the guest's own
 * start message agrees: `_spiVersion = 0 _spiInternalCS = 0`.
 *
 * The low four bits are event latches and are write-one-to-clear. Both FIFO
 * levels sit above them and are OCCUPANCY, not free space: the driver's
 * decoders at 0xc05a6904 and 0xc05a6928 read `(s >> 4) & 0xF` and
 * `(s >> 8) & 0xF` and the transmit one then subtracts from the depth itself
 * (`rsb r0, r0, #8`). Publishing free space in the transmit field instead would
 * make a drained FIFO look full and the filter would never refill it.
 *
 * The levels are computed from the FIFOs on every read rather than stored,
 * which is what makes the filter's acknowledge safe: it writes back the WHOLE
 * raw status word it read (0xc05a67d0, `mov r2, r8`), not the event mask, so a
 * model that stored the levels would zero them on the first acknowledge.
 *
 * Version 1 parts move both fields (`(s >> 6) & 0x1F` and `(s >> 11) & 0x1F`),
 * use a depth of 16, a SETUP base of 0x4000 and an event mask of 0x0040000F,
 * and add a register at 0x4c the driver writes only when _spiVersion is
 * non-zero. None of that is defined here: this SoC has no such controller, and
 * a constant that looks wired but is not is a landmine.
 */
#define SPI_STATUS_EVENTS   0x000fu
#define SPI_STATUS_TX_SHIFT 4u
#define SPI_STATUS_RX_SHIFT 8u
#define SPI_STATUS_LEVEL    0x000fu

/* Eight, from start()'s spi-version 0 arm at 0xc05a6fec — and the same 8 the
 * driver uses as its prefill limit, which is why run59's 19 writes decompose
 * as eleven configuration stores plus exactly eight FIFO bytes. */
#define S5L_SPI_FIFO_DEPTH  8u
/* Four chip selects, because the driver masks the select out of its per-device
 * configuration word with `and r3, r3, #3` (0xc05a64c0) and indexes a table at
 * this+0x84 by it. spi0 really does carry two devices, nor-flash and lcd0. */
#define S5L_SPI_SLAVES      4u
/* reg[0] of /arm-io/spi0/lcd0. The NOR is reg[0] == 0 but is exposed through
 * its dedicated memory window; controller traffic on the modelled board is the
 * Merlot panel path. */
#define S5L_SPI0_LCD_CS      1u
/* The low bits select one of the four routes. Bit 7 is endpoint-private,
 * serialized state for a Merlot register read whose second clock has not yet
 * happened. It lives in `cs` because that byte already travels across a
 * checkpoint and the route itself occupies only two bits; it is never exposed
 * through an SPI register. */
#define S5L_SPI_CS_ROUTE_MASK       (S5L_SPI_SLAVES - 1u)
#define S5L_SPI_LCD_READ_PENDING    0x80u
#define S5L_SPI_UNKNOWN_OFF 8u

/*
 * A device on the bus. SPI is full duplex: one byte leaves the master and one
 * arrives in the same word, so a slave is a function from the outgoing byte to
 * the incoming one and there is no separate read entry point as there is on
 * I2C.
 *
 * There is deliberately no chip-select callback. On this board the select lines
 * are GPIO platform functions (`function-spi_cs0`) and neither controller sets
 * `internal-cs`, so the controller cannot observe a select edge and a callback
 * for one could never fire. A device model that needs packet framing is what
 * makes the GPIO block a model rather than a stub; it is not something this
 * interface can fake.
 */
typedef struct {
    void   *ctx;
    uint8_t (*transfer)(void *ctx, uint8_t out);
} s5l_spi_slave_t;

typedef struct {
    uint32_t control, setup, pin, clkdiv, cnt, idd;
    uint32_t status;        /* event latches only; levels are computed        */
    uint32_t words_left;    /* latched from CNT. Visibility, not a gate — see
                             * s5l_spi_write() for why it must not be one.    */

    uint8_t  tx[S5L_SPI_FIFO_DEPTH];
    uint8_t  rx[S5L_SPI_FIFO_DEPTH];
    uint8_t  tx_level, rx_level;
    /*
     * Which chip select words are routed to, in the low two bits. The real
     * select lines are GPIO platform functions and the controller cannot
     * observe them yet. Board construction therefore selects the only
     * controller-backed device on each modelled bus: spi0 lcd0 at select 1 and
     * spi1 multi-touch at select 0. The NOR at spi0 select 0 is already exposed
     * through its dedicated memory window. Bit 7 is the explicitly named LCD
     * transaction flag above, not part of the route. When GPIO chip-select
     * routing is modelled, it drives only the low route bits.
     */
    uint8_t  cs;

    /* Bounded diagnostics, as on I2C: unknown or refused traffic must be
     * visible without letting a guest grow host allocations. */
    uint64_t words;         /* completed shifts                               */
    uint64_t tx_drops;      /* TXDATA stores into a full transmit FIFO        */
    uint64_t rx_underruns;  /* RXDATA loads from an empty receive FIFO        */
    /*
     * Bytes shifted OUT whose answer had nowhere to go: the receive FIFO was
     * full and nothing was draining it. Only reachable in DMA mode -- see
     * spi_shift() -- and it is the honest name for what silicon does there,
     * which is overrun rather than stall. run104 is why it exists: a
     * transmit-only DMA burst of 812,340 octets delivered sixteen and counted
     * the rest in tx_drops, because the shifter was waiting for a reader that
     * a DMA transfer does not have.
     */
    uint64_t rx_overruns;
    /*
     * WHO, IF ANYONE, IS EMPTYING THE RECEIVE FIFO -- and whether this model
     * ever gives them the chance.
     *
     * run156 fixed the DMA burst so it queues nothing, and the bus deadlocked
     * anyway: `tx/rx level 2/8`, unchanged. The eight octets are not the DMA's
     * at all. The transaction ledger reads `... 16 0 16 8 54148 0`, and that
     * 8-octet PIO transaction fills the FIFO before the burst even starts.
     * Nothing drains it, the following PIO ATN_ACK cannot shift while it is
     * full, and the bootload stops there.
     *
     * Two hypotheses fit that equally well and need different fixes:
     *
     *   - the guest never reads RXDATA, so silicon must be discarding or
     *     flushing these somewhere this model keeps them; or
     *   - the guest WOULD read them from its interrupt filter, and this model
     *     never raises the line, so it is never asked to.
     *
     * The end-state registers cannot separate those -- `status` is cleared by
     * the guest's own acknowledge, so a line that fired and one that never did
     * look identical afterwards. These count the events instead of the state.
     */
    uint64_t rx_reads;      /* RXDATA loads that returned a real octet         */
    uint64_t irq_rises;     /* transitions of s5l_spi_irq() from false to true */
    bool     irq_last;      /* edge detector for the above; not guest-visible  */
    /*
     * Rising edges on SPI_SETUP_DMA, as distinct from the bit's VALUE.
     *
     * "SPI_SETUP bit 0x40 is never set, so SPI_SETUP_DMA -- the flag the whole
     * fix is gated on -- is never raised by the driver" was recorded as a
     * measurement and was not one: it came from printing the register at the
     * END of a run, where a driver that arms DMA, transfers and disarms is
     * indistinguishable from one that never armed it at all. Same error as the
     * PL080's config report, found by auditing for it after that one.
     */
    uint64_t dma_arms;
    uint64_t unknown_reads, unknown_writes;
    uint32_t unknown_off[S5L_SPI_UNKNOWN_OFF];
    unsigned unknown_off_count;

    /* Host wiring. Snapshot code serializes `cs`, never these callbacks. */
    s5l_spi_slave_t slaves[S5L_SPI_SLAVES];
} s5l_spi_t;

/* Reset is total: valid on an uninitialized/poisoned object, and it removes
 * attached devices. Callers attach board wiring after reset. */
void     s5l_spi_reset(s5l_spi_t *bus);
/* Attach at a chip select. Refuses an out-of-range select, a slave with no
 * transfer callback, and any select that is already taken — silently shadowing
 * a device would be worse than refusing. */
bool     s5l_spi_attach(s5l_spi_t *bus, unsigned cs,
                        const s5l_spi_slave_t *slave);
/* Not const: reading SPI_RXDATA pops the receive FIFO, which is what makes room
 * for the rest of a backed-up transfer. */
uint32_t s5l_spi_read(s5l_spi_t *bus, uint32_t off);
void     s5l_spi_write(s5l_spi_t *bus, uint32_t off, uint32_t val);
/* A level, held until the guest clears the event latches through the W1C
 * register — and only while the completion interrupt is enabled in SETUP and
 * the receive FIFO holds a byte. See "THE ONE RULE THAT MATTERS" and the SETUP
 * note above; neither term is a decoration. */
bool     s5l_spi_irq(const s5l_spi_t *bus);
/* Sample the line and count its rising edges into irq_rises. Call once per
 * tick, from one place; see the note beside irq_rises for why the level alone
 * cannot answer the question this exists for. */
void     s5l_spi_irq_note(s5l_spi_t *bus);
/*
 * Run the shifter, from the tick rather than from a register access.
 *
 * The shifter used to run only inside s5l_spi_write() and s5l_spi_read(),
 * which is enough while the CPU is the only thing feeding the port: every
 * byte arrives as a store, and the store shifts it. It is NOT enough once
 * the DMA controller waits for transmit-FIFO space, because then nobody is
 * writing -- the controller is waiting for the port and the port is waiting
 * to be written. run119 deadlocked exactly there: `ch5 ... runs 0 bytes 16
 * ENABLED` with 1020 transfers left, against `spi1 tx/rx level 8/8`.
 *
 * Silicon does not work that way: a shifter with data and a clock shifts.
 */
void     s5l_spi_step(s5l_spi_t *bus);

/*
 * The null device: it answers every word with 0x00.
 *
 * This is what proves the controller without claiming a touch controller.
 * isInHBPP() accepts the probe only if the two big-endian halfwords rx[0..1]
 * and rx[2..3] both pass its test at 0xc0440658, so an all-zero response is a
 * definite rejection: the driver returns false from finishStarting() and says
 * so, instead of sleeping with no deadline. A real device model attaches here
 * later.
 */
void     s5l_spi_null_bind(s5l_spi_slave_t *slave);

/*
 * The deliberately small endpoint for /arm-io/spi0/lcd0 (lcd,merlot). It
 * supplies the full-duplex receive clocks required to finish transmit-only
 * requests and the one status fact AppleMerlotLCD has been measured polling:
 * register 0x15 bit 0 says that the attached panel is ready. Every other
 * semantic reply remains zero. This is not a command-level panel model and it
 * does not change CLCD scanout state.
 *
 * The controller is the context because S5L_SPI_LCD_READ_PENDING in its
 * serialized `cs` byte carries the read phase between the two words, including
 * across a checkpoint. It must be the controller the slave is attached to.
 */
void     s5l_spi_lcd_bind(s5l_spi_slave_t *slave, s5l_spi_t *bus);

/* M68's separate display-link transmitter: /arm-io/spi0/lcd0,
 * compatible mpl-tx,lm2512, chip select 0. This transport clocks the measured
 * two-byte configuration writes; it supplies no register-read semantics and
 * does not generate pixels or change CLCD window state. */
void     s5l_spi_mpl_bind(s5l_spi_slave_t *slave, s5l_spi_t *bus);

/* ---------------------------------------- AppleMultitouchZ2SPI's device ---
 * The touch controller on /arm-io/spi1 chip select 0. See core/src/soc/mtz2.c
 * for the protocol; what follows is the shape of the state and the one thing
 * that must not be got backwards.
 *
 * THE HBPP ANSWER IS DIRECTION-SENSITIVE. isInHBPP() (0xc0441008, vtable slot
 * 0x4d0 off base 0xc0449f40) issues one 16-byte full-duplex transfer of
 * `1A A1 18 E1 18 E1 18 E1 18 E1 18 E1 18 E1 18 E1` and requires BOTH
 * BE16(rx[0..1]) AND BE16(rx[2..3]) to be in the set
 * {0x1AA1,0x18E1,0x1F01,0x4879,0x4969,0x4BC1,0x4AD1} (the test at 0xc0440658,
 * whose literal pool holds 0x18E1, 0x1AA1 and 0x4879 and derives the other
 * four by the add/sub chain at 0xc0440674-0xc044069c). The first two words
 * transmitted are 0x1AA1 and 0x18E1, so a device that simply echoes passes and
 * one that answers zeros fails. And the two callers want OPPOSITE answers:
 *
 *   finishStarting 0xc0442670  -> beq 0xc0442714 on FALSE, which prints
 *                                 "Could not detect HBPP. Returning false from
 *                                 finishStarting()" and detaches the driver.
 *                                 It needs TRUE.
 *   attemptToBootloadDevice 0xc04414c4 -> its first act is the same probe, and
 *                                 a TRUE sends it into the real HBPP firmware
 *                                 download. It needs FALSE.
 *
 * So the device answers the probe once and then stops. That is a deliberate
 * choice, not a simulation: it costs three cosmetic "Bootload attempt N of 3
 * failed" lines and it drops a 54,156-byte firmware push and the whole
 * unmodelled AppleARMPL080DMAC out of scope. mtz2.c states it again where it
 * is implemented.
 */
#define MTZ2_OP_CMD_STATUS   0xe1u
#define MTZ2_OP_DEVICE_INFO  0xe2u
#define MTZ2_OP_REPORT_INFO  0xe3u
#define MTZ2_OP_WRITE_SHORT  0xe4u
#define MTZ2_OP_WRITE_LONG   0xe5u
#define MTZ2_OP_READ_SHORT   0xe6u
#define MTZ2_OP_READ_LONG    0xe7u
#define MTZ2_OP_FRAME_Z1     0xeau
#define MTZ2_OP_FRAME_Z2     0xebu   /* this part uses 0xEB                  */
#define MTZ2_OP_WAKE         0xeeu
#define MTZ2_OP_REQ_WAKEUP   0x19u   /* two bytes, {0x19, 0xC1}              */
#define MTZ2_OP_HBPP         0x1au   /* first byte of the HBPP loopback probe */

/* ================================================== the HBPP bootloader ===
 *
 * A Z2 has no flash. iOS downloads its firmware ON EVERY BOOT, which is why
 * finishStarting() (0xc0442670) detaches unless isInHBPP() says yes: at that
 * moment the part really is an unprogrammed bootloader. The full derivation,
 * with every address, is docs/multitouch.md §6.
 *
 * These are the five packet types the bootloader speaks, in addition to the
 * probe above and MTZ2_OP_WAKE.
 */
#define MTZ2_OP_HBPP_RDREG   0x1cu   /* 1C 73 <addr:4> <sum:2>,       8 bytes */
#define MTZ2_OP_HBPP_EXEC    0x1du   /* 1D 53 …,                     12 bytes */
#define MTZ2_OP_HBPP_WRREG   0x1eu   /* 1E 33 <addr><mask><val><sum>,16 bytes */
#define MTZ2_OP_HBPP_CALIB   0x1fu   /* 1F 01,                        2 bytes */
/*
 * THE IDLE ATTENTION WORD, 0x18E1, framed as a two-octet packet.
 *
 * run144 dumped the stream the bootloader actually receives and found this
 * sitting between the probe and the firmware:
 *
 *   18 e1 | 30 01 34 df 00 00 00 00 01 13 ...
 *
 * The right-hand side is a perfectly-formed HBPP DATA header -- opcode, M2,
 * 13,535 words big-endian, address zero, and a header checksum of 0x0113 that
 * verifies exactly against sum16 of the six bytes before it. 13,535 words is
 * 54,140 octets, and with 14 of framing that is the whole 54,156-octet image
 * in ONE packet.
 *
 * So the firmware was never raw ARM code. The framer simply never saw its
 * header, because 0x18 is not an opcode it knows: it ate the 0x18 as unknown
 * and then read the 0xE1 as GET_CMD_STATUS, a sixteen-octet frame that
 * swallowed the DATA header whole. Everything after that was ARM instructions
 * being read as commands -- which is why e5/ea/e2 (LDR/STR, B, data-processing)
 * appeared as WRITE_LONG/FRAME_Z1/DEVICE_INFO.
 *
 * 0x18E1 is one of the seven words isInHBPP's own accept-set contains
 * (0xc0440658), so a device seeing it between packets is seeing an attention
 * word and not a command. Two octets, and it loops back like the probe does.
 */
#define MTZ2_OP_HBPP_IDLE    0x18u   /* 18 E1, two octets, loops back        */
#define MTZ2_OP_HBPP_DATA    0x30u   /* 30 01 <count:2> <addr:4> …,14+4W bytes */

/* The second octet of each, which the driver hard-codes and this model checks
 * rather than ignores: a packet whose marker is wrong is not one of ours. */
#define MTZ2_HBPP_RDREG_M2   0x73u
#define MTZ2_HBPP_EXEC_M2    0x53u
#define MTZ2_HBPP_WRREG_M2   0x33u
#define MTZ2_HBPP_CALIB_M2   0x01u
#define MTZ2_HBPP_DATA_M2    0x01u

/*
 * What an ATN_ACK must answer, compared at 0xc0445284 against the literal
 * 0x00004bc1 and retried five times before the send is abandoned. Assembled
 * big-endian: rx[0] = 0x4B, rx[1] = 0xC1.
 */
#define MTZ2_HBPP_ATN_OK     0x4bc1u
/*
 * ...AND A REGISTER WRITE EXPECTS A DIFFERENT NUMBER. Measured 2026-07-30 by
 * disassembling the two senders, after run161 changed the write's
 * acknowledgement from a sixteen-octet probe to a correct two-octet 0x4BC1 and
 * the driver behaved identically.
 *
 * They are different functions with different literals:
 *
 *   the DATA sender, 0xc0445144, compares at 0xc0445284 against 0x00004BC1
 *     (the literal sits at 0xc04452c4)
 *   the register-WRITE helper, 0xc0440e4c, compares at 0xc0440f94 against
 *     0x00004AD1 (the literal sits at 0xc0440ffc)
 *
 * Answering 0x4BC1 to a write's acknowledgement therefore fails its compare,
 * 0xc0440f98's `addeq` never runs, the helper returns 0, and performCalibSeq
 * bails at 0xc0445810 having made ONE of its four writes -- which is precisely
 * what run160 and run161 recorded, six identical cycles of
 * `RD 10008ffc / WR 10001c04` and then the whole bootload again.
 *
 * The read helper is not affected: its long MemRead acknowledgement carries
 * the register value and the driver takes it, which is why reads have worked
 * throughout.
 */
#define MTZ2_HBPP_ATN_WROK   0x4ad1u

/*
 * The register performCalibSeq reads at 0xc044562c, and the value it must NOT
 * see. 0x5A020028 makes the driver SKIP the four register writes and set
 * bootloader->0x7e4, which attemptToBootloadDevice then reads at 0xc0441568 as
 * a reason to take the "*** ERROR: Disabling touch ***" path. So the
 * convenient-looking answer is the one that switches the digitizer off.
 */
#define MTZ2_HBPP_VERSION_REG 0x10008ffcu
#define MTZ2_HBPP_VERSION_BAD 0x5a020028u
/*
 * What this part reports instead. The low half matches the bad revision's
 * (0x0028) because that is a stepping field and nothing reads it; the high
 * half is deliberately NOT 0x5a02, and any value that differs would do — this
 * one is chosen so a log line reading "Detected Z2 Version: 0x5A030028" is
 * recognisable as one revision along rather than as a number from nowhere.
 */
#define MTZ2_HBPP_VERSION     0x5a030028u

/*
 * The ATN_ACK's length is not decidable from its bytes. isInHBPP's probe is
 * `1A A1` + `18 E1` x7; the MemRead acknowledgement is the same bytes cut to
 * eight; the short acknowledgement is the same bytes cut to two. Two of the
 * three are PREFIXES of one another, so only the command that came before can
 * separate them — on real silicon the chip select does it, and s5l_spi_slave_t
 * records why this controller cannot see one.
 */
#define MTZ2_ATN_PROBE       16u
#define MTZ2_ATN_MEMREAD     8u
#define MTZ2_ATN_SHORT       2u

/* Every 16-byte command frame: opcode at [0], parameters at [1..4], LE16 sum16
 * of [0..13] at [14..15]. Confirmed at 0xc0443288-0xc044329c, which writes
 * tx[0]=0xE3, tx[1]=id and tx[14..15] = LE16(0xE3 + id) — the plain byte sum,
 * with no seed and no complement. */
#define MTZ2_FRAME_LEN       16u
/* A control read's payload begins at rx[3]: three prologue bytes, then the
 * body, then the two checksum bytes. 16 - 3 - 2 = 11, which is exactly the
 * `length <= 11 -> short form` cut the driver applies at 0xc0442e10. */
#define MTZ2_PAYLOAD_AT      3u
#define MTZ2_PAYLOAD_MAX     (MTZ2_FRAME_LEN - MTZ2_PAYLOAD_AT - 2u)

/* ==================================================== the long control read ===
 *
 * 0xE7 is 0xE6 with the frame stretched, and that is not a simplification —
 * the short form IS this form evaluated at length 11.
 *
 * WHICH FORM, decided at 0xc0442e0c against the length GET_REPORT_INFO just
 * announced, with the same halfword already bounded at 0xc0442dc4:
 *
 *     ldrh r1, [sp, #0x36]        <- the announced report length
 *     cmp  r1, #0x200             (0xc0442dc4) above 512 the read is refused
 *     ldrh r1, [sp, #0x36]
 *     cmp  r1, #0xb               (0xc0442e10)
 *     bhi  #0xc0442e3c            <- 12 and up: vtable slot 0x4c0
 *     ...  ldr pc, [ip, #0x4bc]   <- 11 and under: slot 0x4bc, the 0xE6 form
 *     ...  ldr pc, [ip, #0x4c0]
 *
 * Slot 0x4c0 is 0xc0441ba0, and it opens `mvn r3, #0x18 / strb r3, [r2]` —
 * 0xE7. (Slot 0x4bc at 0xc0441ea4 is `mvn r3, #0x19` = 0xE6, and the two
 * control WRITES beside them are 0x4c4 -> 0xE4 and 0x4c8 -> 0xE5. Slot 0x4d0
 * in the same block is 0xc0441008, isInHBPP, which is what fixes the vtable
 * base at 0xc0449f40.)
 *
 * BOTH STAGES, from 0xc0441ba0. `length` is the announced length, in r1:
 *
 *   STAGE 1, sixteen bytes (`mov r5,#0x10` at 0xc0441be8, handed to the SPI
 *   entry at 0xc0441c70):
 *       tx[0]      = 0xE7
 *       tx[1]      = report id
 *       tx[2]      = 0
 *       tx[3..4]   = LE16 length          <- the host states the frame size
 *       tx[14..15] = LE16 sum16(tx, 5)    <- FIVE bytes, `mov r1,#5` 0xc0441bf0
 *   The answer is not examined: 0xc0441cb4 tests only the SPI call's return.
 *
 *   STAGE 2, length + 5 bytes (`add r5, r8, #5` at 0xc0441d18):
 *       tx[2]              = 1
 *       tx[14..15]         = 0            (0xc0441d10/0xc0441d1c)
 *       tx[length+3..+4]   = the STAGE 1 checksum, moved rather than recomputed
 *   which is the same stale-checksum quirk the short form has at 0xc0441ff8 —
 *   there it needs no move because length 11 already puts it at [14..15].
 *
 * WHAT THE ANSWER MUST SATISFY, at 0xc0441da0-0xc0441e08, and it is two things:
 *       rx[0] == tx[0]                                   (0xc0441db0)
 *       LE16(rx[length+3..length+4]) == sum16(rx, length+3)
 * and then `memcpy(desc + 1, rx + 3, length)` at 0xc0441e24 with
 * `desc->length = length` at 0xc0441e2c. So the payload is rx[3 .. 3+length),
 * the checksum is the two bytes after it, and at length 11 every one of those
 * positions is the short form's. One rule serves both, which is why mtz2.c has
 * no second composer.
 */

/*
 * The longest report body this device publishes, and the size of the buffer
 * s5l_mtz2_report() writes into. It is the Sensor Region Descriptor's two
 * seven-byte records: the driver's own ceiling is 512 (0xc0442dc4) and this
 * device is nowhere near it, but the number must be the DEVICE's longest
 * report rather than a round one, because a report longer than the reply
 * buffer is a frame the model announces and then cannot drive.
 */
#define MTZ2_REGION_RECORD   7u
#define MTZ2_REGION_RECORDS  2u
#define MTZ2_REPORT_MAX      (MTZ2_REGION_RECORD * MTZ2_REGION_RECORDS)

/* Longest COMMAND packet this model frames, and the size of the received-byte
 * buffer. Nothing the driver sends exceeds a 16-byte frame — a frame read's
 * transmit side is 16 or L+5 bytes but only its first three carry information
 * (opcode, toggle, phase), so the tail is dropped rather than stored. */
#define S5L_MTZ2_BUF 40u

/* =========================================================== frame reads ===
 *
 * A report is read in TWO transactions, both with opcode MTZ2_OP_FRAME_Z2 and
 * distinguished by tx[2]. Both were read out of the kext, not inferred:
 *
 * LENGTH READ, tx[2] == 0, always 16 bytes (0xc0442440, `mov r4,#0x10`):
 *   tx[0]   = this+0x7e5, the frame opcode        (0xEB; 0xEA before the HBPP
 *             answer sets this+0x1bc — see 0xc0440560)
 *   tx[1]   = this+0x7e6, the toggle              (1 initially, flipped 1<->2
 *             after every successful data read at 0xc0443100)
 *   tx[2]   = 0
 *   tx[14..15] = LE16 sum16(tx, 14)
 * and the answer must satisfy, at 0xc0442554-0xc04425c0:
 *   (rx[0] & 0xF0) == 0xE0
 *   LE16(rx[14..15]) == sum16(rx, 14)
 *   L = rx[1] | rx[2] << 8            <- the caller's out-parameter
 * L == 0 is NOT an error. The caller tests it at 0xc043bf90 / 0xc04430bc and
 * simply stops, so an idle device answers one 16-byte transfer and is done.
 *
 * DATA READ, tx[2] == 1, L + 5 bytes (0xc04430c4, `add r1,r1,#5`), same
 * tx[0..1], and the transmit checksum moves to tx[len-2..len-1] because it is
 * stored relative to the transfer length (0xc0441254/0xc044126c) — it is still
 * sum16 of only the first FOURTEEN bytes. The answer is checked at
 * 0xc044130c-0xc04413b8:
 *   rx[0]            == 0xEB (or 0xEA), and 0xEB additionally requires the
 *                       driver's this+0x1bc to be non-zero
 *   (rx[0]+rx[1]+rx[2]+rx[3]+rx[4]) & 0xFF == 0
 *   L2 = rx[2] | rx[3] << 8, and L2 of 0 or 2 means "no payload", checked
 *        BEFORE the payload checksum
 *   payload          = rx[5 .. 5 + L2 - 3], i.e. L2 - 2 bytes
 *   LE16(rx[len-2..len-1]) == sum16(payload, L2 - 2)
 * The payload checksum's position comes from the TRANSFER length and the
 * payload's own length comes from the EMBEDDED L2, so the two agree only when
 * L2 == L: payload occupies [5, L+3) and the checksum [L+3, L+5).
 *
 * Then `this->v[0x414](payload, L-2)` at 0xc0441400 -> 0xc04389fc, which reads
 * ONE byte of the payload — `payload[0] == 0x50` is diverted to v[0x418]
 * (0xc043d1ac, a separate 8-byte status record) — and otherwise tail-calls
 * v[0x3c4] = 0xc043c31c, which fans the buffer out to up to 32 subscribed
 * clients through 0xc043d684: `IODataQueue::enqueue(payload, (len+3) & ~3)`.
 * So a touch frame's first payload byte must not be 0x50.
 */
/* The largest payload this device will report. Five contacts is the panel's
 * own limit and the encoding is 10 bytes of header plus 32 per contact, so 170
 * is the real maximum; the buffer is rounded up to 176 and the transfer that
 * carries it is payload + 7 = 183 bytes, which keeps every length in the
 * uint8_t the framer uses. */
#define MTZ2_PAYLOAD_LIMIT   176u
/* Longest reply this device drives: 5 prologue + payload + 2 checksum. */
#define S5L_MTZ2_RSP         (MTZ2_PAYLOAD_LIMIT + 7u)

/* The reports the driver interrogates, in the order it asks for them. */
#define MTZ2_REPORT_FAMILY_ID   0xd1u
#define MTZ2_REPORT_GEOMETRY    0xd3u   /* also the isBootloaded() probe */
#define MTZ2_REPORT_BUTTONS     0xd7u
#define MTZ2_REPORT_SURFACE     0xd9u
#define MTZ2_REPORT_REGION_DESC 0xd0u
#define MTZ2_REPORT_REGION_PARAM 0xa1u

typedef struct {
    /*
     * Protocol position. `len` is zero when the device is between packets;
     * `pos` is the byte index inside the current one. There is no chip-select
     * callback on this bus (see s5l_spi_slave_t), so a packet is framed by the
     * length its own opcode implies — which is enough, because every command
     * this device answers has a length fixed by its first byte.
     */
    /*
     * Held in the reset pin. A part whose reset line is asserted drives
     * nothing, and modelling that is what separates the two wire-identical
     * exchanges each probe site makes: `resetDevice` clocks a 16-byte DUMMY
     * transfer while the line is down and discards the answer, then releases
     * the line and probes. Both transfers send `1A A1 18 E1…`; only the
     * second one's answer is ever read. See s5l_mtz2_reset_pin().
     */
    bool     in_reset;
    /*
     * IS THIS PART RUNNING ITS BOOTLOADER, and it is a device state now rather
     * than a one-time claim.
     *
     * It used to be `hbpp_answered`: TRUE once, FALSE forever after. That
     * satisfied finishStarting() — which detaches on FALSE — and then starved
     * attemptToBootloadDevice(), which needs TRUE to push firmware. run100
     * measured the cost: the driver never issued a single control read of any
     * report in two billion instructions, so no property ever reached
     * userspace, so the surface bounds went negative, so every touch landed in
     * the same place. The comment that called it "a bounded, named, single-bit
     * lie that costs three cosmetic log lines" was wrong about the cost.
     *
     * A real Z2 has no flash and is bootloaded on every boot, so TRUE is
     * simply what it says until it has been programmed. `hbpp_exec` is the
     * event that ends it: the twelve-octet 1D 53 packet, whose answer the
     * driver never even looks at.
     */
    bool     hbpp_mode;
    /*
     * The length the NEXT `1A A1` carries, because its bytes cannot say. Set
     * by whichever command precedes it; MTZ2_ATN_PROBE when nothing has.
     */
    uint8_t  atn_len;
    /* What that acknowledgement must ANSWER, which is not one number: see
     * MTZ2_HBPP_ATN_WROK. Set beside atn_len by the same switch, because the
     * command that came before is the only thing that decides either. */
    uint16_t atn_val;
    /* Where a MemRead acknowledgement's answer comes from: the address the
     * preceding 1C 73 named, resolved once so the reply is pure arithmetic. */
    uint32_t rdreg_addr;
    /*
     * THE REGISTER CONVERSATION, bounded.
     *
     * run158 got the driver as far as §6.5's primitives for the first time --
     * `rd 6  wr 6`, where every previous run had 0/0 -- and it then re-ran the
     * whole bootload instead of sending EXEC. The packet list shows the shape
     * (`... 30:270 1a:2 1c:8 1a:8 1e:16` and then `1a:16 1a:16 18:2 30:54154`
     * all over again) but not the CONTENT, and the content is what decides
     * whether the driver dislikes an answer this model invented.
     *
     * `rd 6 wr 6` cannot distinguish "asked for six registers and got six
     * sensible answers" from "asked for the same one six times because the
     * answer was wrong". These record which address, which direction, and the
     * value that crossed -- for a read, what this model ANSWERED, which is the
     * half a bus trace of the guest could never show.
     */
    uint32_t reg_log_addr[16];
    uint32_t reg_log_val[16];
    uint8_t  reg_log_write[16];   /* 1 = WRREG, 0 = RDREG                   */
    uint8_t  reg_log_n;
    /*
     * WIDER THAN A BYTE, because an HBPP DATA packet is 14 + 4W octets and the
     * driver only reaches for DMA above 255 (0xc04451bc) -- so a PIO packet is
     * already allowed to be longer than a uint8_t can frame, and the firmware
     * arrives in packets far larger than that. The receive buffer stays small:
     * nothing past the header of a DATA packet is worth keeping, and both
     * req[] and rsp[] are bounds-guarded at their own sizes.
     */
    uint32_t pos, len;
    uint8_t  op;
    /*
     * `phase` is the frame read's tx[2], latched when it arrives so the framer
     * can lengthen the packet from 16 to L+5 in the middle of it. 0xFF means
     * "not a frame read", which keeps a stale 1 from a previous data read out
     * of the next command's framing decision.
     */
    uint8_t  frame_phase;
    uint8_t  req[S5L_MTZ2_BUF];
    uint8_t  rsp[S5L_MTZ2_RSP];

    /*
     * What the device says it is. All of it — every dimension the driver
     * publishes — comes from the device and none of it from the device tree,
     * so these are OUR choice and the reason for each is in mtz2.c.
     */
    uint8_t  rows, columns, endianness, family_id, buttons;
    uint16_t bcd_version;
    uint32_t surface_width, surface_height;

    /*
     * The pending report and the attention line that announces it.
     *
     * `atn` drives GPIO interrupt line 155 -> group 4 bit 27 -> VIC line 2, the
     * cascade the guest armed with `INTEN group 4 = 0x08000000` in run59. It is
     * a LEVEL: it goes up when a report is queued and comes down when the host
     * has clocked the data read out, which is exactly the condition the GPIO
     * interrupt controller's level latch (see gpioic.c) was built for.
     *
     * `frame_len` is the PAYLOAD length in bytes — the L-2 the driver hands to
     * IODataQueue — so the wire length L is `frame_len + 2` and the data-read
     * transfer is `L + 5`. Zero means no report, which is the answer a length
     * read gets when nothing is pending and is not an error.
     */
    bool     atn;
    uint8_t  contacts;
    uint8_t  frame_len;
    uint8_t  frame[MTZ2_PAYLOAD_LIMIT];
    /*
     * The frame counter the payload header carries, incremented once per
     * queued report. Wraps at 8 bits with the header field, so nothing here
     * has to decide what a wrap means — and the parser's own field is a u8
     * that "wraps at 256" too.
     */
    uint8_t  frame_seq;
    /* The frame timestamp, in the milliseconds the parser expects. Advanced by
     * MTZ2_FRAME_PERIOD_MS per queued report; see that constant. */
    uint32_t frame_ms;
    /*
     * The power LDO, /arm-io/spi1/multi-touch's `function-power_ldo`
     * {phandle, 'GPIO', 0x0701, 0x00000101}. Byte 1 of that platform-function
     * argument is the polarity, decoded at 0xc05a459c/0xc05a45d8: this line is
     * ACTIVE HIGH. Each real edge clears the volatile protocol/application
     * state and returns this flashless part to HBPP for the next powered
     * session; `power_edges` also proves the GPIO subscription is live.
     */
    bool     power_level;
    uint64_t power_edges;
    /*
     * SELECT EDGES, and whether this device has any packet framing at all.
     *
     * s5l_mtz2_select_pin() has resynchronised the framer since it was
     * written, on the stated assumption that "the framer already knows every
     * packet's length from its opcode, [so] a driver that never drives this
     * pin is not broken by its absence". run138 showed that assumption is
     * false for the one transfer that matters: the Z2 bootload streams a raw
     * ARM image, and no octet in it carries a length. The framer read that
     * image as commands -- e5, ea, e2 are LDR/STR, B and data-processing, and
     * they collide exactly with WRITE_LONG, FRAME_Z1 and DEVICE_INFO because
     * ARM's AL condition nibble IS 0xe.
     *
     * Framing the bootload therefore needs the select, and the select is only
     * usable if the driver drives it. Nothing counted that, so it was never
     * knowable. This counts it: zero means CS framing is not available on this
     * guest and the boundary rule has to come from somewhere else.
     */
    uint64_t select_edges;
    /*
     * HOW LONG EACH TRANSACTION IS, which is the fact the bootload fix needs
     * and the one nobody has.
     *
     * run138 proved the firmware cannot be framed by opcode -- ARM's AL
     * nibble is 0xe and the command opcodes are 0xe1..0xee. run141 proved the
     * chip select is driven, 14 times. What is still unknown is the SHAPE:
     * whether a transaction is one command, one DMA chunk, or the whole image,
     * and no rule for telling firmware from commands can be written without
     * it. Guessing that shape is exactly the mistake that has been retracted
     * four times in this log.
     *
     * `txn_mark` is `octets` at the last select edge; each entry is the octet
     * count of one completed transaction. A run of {16, 16, 3868, 3868, ...}
     * makes the rule obvious; {16, 16, 54156} makes it a different rule; and
     * anything interleaved says the select alone is not enough.
     */
    uint64_t txn_mark;
    uint32_t txn_octets[24];
    unsigned txn_n;
    /*
     * THE FIRST OCTETS OF THE STREAM ITSELF, which is the one thing about this
     * bootload nobody has actually looked at.
     *
     * Three ways of DELIMITING the image have now been eliminated by
     * measurement -- opcode framing (run138: ARM's AL nibble is 0xe and the
     * commands are 0xe1..0xee), the chip select (run142: it brackets the
     * 16-octet probes, not the image), and SPI_CNT/SPI_PIN (the DMA path
     * writes CNT zero and PIN sees only a power-on zero). Nothing on the wire
     * carries the length.
     *
     * Which means a real Z2 cannot be told where the image ends either, so it
     * must parse the stream -- so the image IS structured and our reading of
     * that structure is what is wrong. This records the first 64 octets after
     * the probes, because every theory so far has been about boundaries and
     * none has been about content.
     */
    uint8_t  head[64];
    unsigned head_n;

    /*
     * Bounded diagnostics, as on I2C and SPI, and two of these are load-bearing
     * rather than decoration. `resets` zero means the GPIO subscription is not
     * wired at all. `reset_bytes` zero means the reset line is being SEEN but
     * the dummy transfer is not landing inside the asserted window — which is
     * the difference between the driver being kept alive and it pushing 54 KB
     * of firmware at a device that cannot take it.
     *
     * The frame counters answer the two questions a failed tap raises in the
     * right order: `injects_refused` non-zero means the host asked and the
     * DEVICE said no (and `s5l_mtz2_set_contacts` told it so); `frames_queued`
     * without `length_reads` means the guest never looked, i.e. the interrupt
     * did not route; `length_reads` without `data_reads` means it looked and
     * declined, i.e. the length answer was malformed.
     */
    /*
     * The bootload, counted so a run can say which step it stopped on. A
     * download that never starts and one that stops after two packets look
     * identical in the guest's log, which prints nothing per packet.
     */
    uint64_t hbpp_data_packets, hbpp_data_bytes, hbpp_atn_acks;
    uint64_t hbpp_reg_reads, hbpp_reg_writes, hbpp_calibs, hbpp_execs;
    uint64_t packets, hbpp_probes, unknown_opcodes, resets, reset_bytes;
    /*
     * THE LEDGER THAT HAS TO BALANCE.
     *
     * `octets` counts every call into s5l_mtz2_transfer, and `packet_octets`
     * counts the ones consumed inside a packet. Every octet takes exactly one
     * of three paths -- held in reset, not an opcode, or inside a packet -- so
     *
     *     octets == reset_bytes + unknown_opcodes + packet_octets
     *
     * is an identity, and `octets` against spi1's `words` says whether the
     * controller and the device even agree on how much crossed the wire.
     *
     * This exists because the counters that were here could not close the
     * books, and I mis-derived where 35,000 octets went TWICE from them --
     * first as a framer desync (refuted by unknown_opcodes 192), then as an
     * unfinished packet (refuted by the framer being idle at exit). Both
     * derivations were arithmetic on numbers that never had to add up to
     * anything. These do.
     */
    uint64_t octets, packet_octets;
    /*
     * THE FIRST 24 PACKETS, opcode and length, because three rounds of
     * reasoning from aggregate counters produced three wrong answers.
     *
     * run137's ledger balances -- 54,236 octets in, 53,996 of them inside
     * packets -- so nothing is lost. But only 15 packets consumed those
     * 53,996, and the two counted as HBPP DATA account for 18,436 of them.
     * Nothing in frame_len() returns more than 16, and both lengthening paths
     * are bounded (wire_len is frame_len+2; the READ_LONG path clamps to
     * S5L_MTZ2_RSP), so on paper no packet can be thousands of octets long.
     * One of those statements is false and aggregates cannot say which.
     *
     * Fifteen packets is a list, not a statistic. This records it.
     */
    /*
     * TWENTY-FOUR WAS EXACTLY THE BOOTLOAD, which stopped being the
     * interesting part on 2026-07-30.
     *
     * run164 framed 103 packets and this list held the first 24 -- and those
     * 24 are precisely the bootload, ending at the `1d:12` execute. The 79
     * that follow are what the driver does with a part it has just programmed:
     * whether it interrogates the device for `Sensor Rows`/`Sensor Columns`
     * (0xD1/0xD3/0xD9/0xD0/0xA1) and therefore whether the surface bounds are
     * real this time. run96 measured those bounds NEGATIVE -- Xmax -434, Xmin
     * -75 -- because the properties were missing, because the driver never
     * interrogated a part that had never been programmed. That precondition
     * has changed and the evidence was being truncated away.
     *
     * 128 covers run164's 103 with room. Still first-N rather than a ring: the
     * order matters more than recency here, and a ring that wrapped would make
     * "the bootload ran twice" indistinguishable from "the bootload ran once
     * and the driver talked a lot afterwards" -- which is the exact question
     * of run158 versus run163.
     */
    uint8_t  pkt_op[128];
    uint32_t pkt_len[128];
    unsigned pkt_n;
    uint64_t frames_queued, frames_read, length_reads, data_reads;
    uint64_t injects_refused;
    uint8_t  last_unknown_op;
} s5l_mtz2_t;

/* The three pins this device watches, as device-tree platform-function ids.
 * Read straight off /arm-io/spi1 and its multi-touch child:
 *   function-reset     {phandle, 'GPIO', 0x0606, 0x00010001}
 *   function-power_ldo {phandle, 'GPIO', 0x0701, 0x00000101}
 *   function-spi_cs0   {phandle, 'GPIO', 0x1800, 0x00000001}   (on spi1)
 */
#define MTZ2_PIN_RESET  0x0606u  /* multi-touch function-reset, ACTIVE LOW  */
#define MTZ2_PIN_POWER  0x0701u  /* multi-touch function-power_ldo, ACT HIGH */
#define MTZ2_PIN_SELECT 0x1800u  /* /arm-io/spi1 function-spi_cs0           */

/*
 * Reset is total. It leaves the part HELD IN RESET, which is not an arbitrary
 * choice: the pin block powers up all-zero, this line is active low, so a part
 * on this board really is held down until the driver releases it. A unit test
 * that wants a live device calls s5l_mtz2_reset_pin(dev, true) first, exactly
 * as the guest does.
 */
void     s5l_mtz2_reset(s5l_mtz2_t *dev);
/* Attach to an SPI chip select. */
void     s5l_mtz2_bind(s5l_mtz2_t *dev, s5l_spi_slave_t *slave);
/* Is the attention line asserted? Drives GPIO line 155 through the interrupt
 * controller. True exactly while a queued report has not yet been clocked out
 * by a data read. */
bool     s5l_mtz2_irq(const s5l_mtz2_t *dev);
/*
 * The protocol's only checksum: a plain truncating 16-bit sum of bytes, stored
 * little-endian. Exposed because the tests build the driver's own frames with
 * it and a checksum computed twice from the same code proves nothing.
 */
uint16_t s5l_mtz2_sum16(const uint8_t *p, unsigned n);
/*
 * The body of one report, as this device would return it. Returns its length
 * (0 for a report this device does not have) and writes at most
 * MTZ2_REPORT_MAX bytes — which is more than the short control-read form can
 * carry, deliberately: the Sensor Region Descriptor is 14 bytes and the driver
 * fetches anything over 11 with the 0xE7 form instead.
 */
unsigned s5l_mtz2_report(const s5l_mtz2_t *dev, uint8_t id, uint8_t *out);
/*
 * The reset line moved. Wired to GPIO pin MTZ2_PIN_RESET, ACTIVE LOW, so
 * `level == false` means the part is held down. Signature matches
 * s5l_gpio_watch()'s callback.
 *
 * This does NOT touch `hbpp_answered`. That is the fix for the bug run65 found
 * and the single most important line in this file; see s5l_mtz2_reset_pin().
 */
void     s5l_mtz2_reset_pin(void *ctx, bool level);
/*
 * The chip select moved. Wired to GPIO pin 0x1800 (/arm-io/spi1's
 * `function-spi_cs0`). It resynchronises the packet framer and nothing else,
 * so a driver that never drives the select is not broken by its absence.
 */
void     s5l_mtz2_select_pin(void *ctx, bool level);
/*
 * The active-high power LDO moved. A real edge discards the flashless part's
 * downloaded image and volatile protocol state; a repeated same-level write
 * does not. See `power_level` and s5l_mtz2_power_pin().
 */
void     s5l_mtz2_power_pin(void *ctx, bool level);

/* ------------------------------------------------ the host injection API ---
 *
 * THE LINE THIS API EXISTS TO DRAW. The host sets PENDING CONTACT STATE. The
 * emulated Z2 then reports it through its own registers, in its own encoding,
 * when the guest's own driver reads them. Nothing here enqueues into
 * MTIODataQueue, synthesises a GSEvent or calls UIKit; a tap that does not
 * reach SpringBoard because some part of the guest stack is wrong must LOOK
 * broken, because that is the only way it can be found.
 *
 * Coordinates are PANEL PIXELS — x in [0, S5L_MT_PANEL_W), y in
 * [0, S5L_MT_PANEL_H) — and the device converts them into the surface units it
 * published in report 0xD9. Keeping the conversion inside the device is what
 * makes "surface 4800 x 7200" one decision in one place rather than an
 * agreement two files have to keep.
 */
#define S5L_MT_PANEL_W 320u
#define S5L_MT_PANEL_H 480u
/* Five fingers. The panel's own limit, and it bounds the payload: 10 bytes of
 * header plus 32 per contact is 170, which is what MTZ2_PAYLOAD_LIMIT is sized
 * for. It is also inside the 1..11 the parser's path table accepts. */
#define MTZ2_CONTACT_MAX 5u
/*
 * The payload's shape, for frame type 0xCC. The header is a CONSTANT ten bytes
 * for this type — the parser does not read a header-length field, it uses 10 —
 * and the per-contact stride is a fixed 32 (`lsl r2, r3, #5` at 0x33cfbbcc).
 * mtz2.c carries the full field map and the evidence for it.
 */
#define MTZ2_FRAME_TYPE     0xccu
#define MTZ2_FRAME_HEADER   10u
#define MTZ2_CONTACT_STRIDE 32u
#define MTZ1_FRAME_TYPE     0xceu
#define MTZ1_FRAME_HEADER   16u
/*
 * Milliseconds the device advances its frame timestamp by per report. This
 * device has no time base of its own; the timestamp exists because
 * _mt_CheckForTimestampErrors (0x33cfb2b4) logs "timestamp invalid!" on a zero
 * and "time travel, eh?" on a decreasing one and posts notification 0x66 to
 * the driver. Non-zero and non-decreasing are the only properties anything was
 * observed to require, and 16 ms is one frame at the ~60 Hz a part like this
 * scans at.
 */
#define MTZ2_FRAME_PERIOD_MS 16u

/* Contact lifecycle, in the device's own encoding — see mtz2.c for how these
 * map onto the eight path states MultitouchSupport names. */
#define MTZ2_PHASE_NOT_TRACKING 0u
#define MTZ2_PHASE_START        1u   /* in range, not yet touching  */
#define MTZ2_PHASE_HOVER        2u
#define MTZ2_PHASE_MAKE_TOUCH   3u   /* the finger just landed      */
#define MTZ2_PHASE_TOUCHING     4u   /* still down, possibly moving */
#define MTZ2_PHASE_BREAK_TOUCH  5u   /* the finger just lifted      */
#define MTZ2_PHASE_LINGER       6u
#define MTZ2_PHASE_OUT_OF_RANGE 7u

typedef struct {
    /*
     * The path identifier. MUST be 1..11: _mt_getPathLifeCycle at 0x33cfd5f4
     * does `sub r3,r1,#1 / cmp r3,#0xa` and silently aliases anything outside
     * that range onto slot 0, and MultitouchHID's own
     * mthm_ExpandAndFilterPackedContacts memcpys into
     * gMTHMPathStates + id * 0x5c with NO bounds check at all. Zero in
     * particular is not a contact: it is what an unset slot reads as.
     */
    uint8_t  id;
    uint8_t  phase;      /* MTZ2_PHASE_*                                */
    uint16_t x, y;       /* panel pixels, origin TOP-left               */
    /*
     * Contact amplitude, 0..255, mapped linearly onto the dimensionless "Z
     * total" the parser divides by 256. The only property anything downstream
     * was observed to require is that it be greater than zero for a finger
     * that is actually touching, so a lift should carry 0.
     */
    uint8_t  pressure;
    uint8_t  major, minor; /* contact ellipse axes, panel pixels        */
} s5l_mt_contact_t;

typedef struct {
    uint8_t  family_id;
    uint8_t  endianness;
    uint8_t  rows;
    uint8_t  columns;
    uint16_t bcd_version;
    uint8_t  buttons;
    uint32_t surface_width;
    uint32_t surface_height;
} s5l_mt_geom_t;

unsigned s5l_mt_report(const s5l_mt_geom_t *, uint8_t id, uint8_t *out);
unsigned s5l_mt_frame_encode(const s5l_mt_geom_t *,
                             const s5l_mt_contact_t *, unsigned n,
                             uint8_t seq, uint32_t ms, uint8_t *out);
unsigned s5l_mt_frame_encode_ce(const s5l_mt_geom_t *,
                                const s5l_mt_contact_t *, unsigned n,
                                uint8_t seq, uint32_t ms, uint8_t *out);

/*
 * Queue one report. Returns FALSE — and queues nothing — when the device is in
 * no state to report, so a caller can never assume delivery:
 *
 *   - `n` above MTZ2_CONTACT_MAX, a null array with n != 0, an out-of-range
 *     coordinate, or a phase this device does not have: a malformed request.
 *   - held in reset: a part driving nothing cannot raise an attention line.
 *   - `!hbpp_answered`: the driver has not yet been told the part is alive, so
 *     its this+0x1bc is still zero and deviceReadResultData (0xc0441324)
 *     REJECTS a 0xEB frame outright. A report queued now could not be read.
 *   - a report is already pending: this device holds exactly one, and silently
 *     replacing it would drop the finger-down half of a tap.
 *
 * Every refusal bumps `injects_refused`, so a caller that ignores the return
 * value still leaves evidence.
 */
bool     s5l_mtz2_set_contacts(s5l_mtz2_t *dev,
                               const s5l_mt_contact_t *contacts, unsigned n);
/*
 * Build the payload for a set of contacts, without queueing anything. Returns
 * the payload length, or 0 if the request is malformed. `out` must have room
 * for MTZ2_PAYLOAD_LIMIT bytes. Exposed so the tests can check the encoding
 * against a hand-written expectation rather than against the encoder's own
 * output — a format checked against itself checks nothing.
 */
unsigned s5l_mtz2_encode(const s5l_mtz2_t *dev, const s5l_mt_contact_t *c,
                         unsigned n, uint8_t seq, uint32_t ms, uint8_t *out);

/* ------------------------------------------- AppleMultitouchSPI's device ---
 * M68's Z1 controller on /arm-io/spi2 chip select 0 (`function-spi_cs0`
 * GPIO 0x0705, `function-power_ldo` GPIO 0x0804, attention on GPIOIC line
 * 0xa3). See core/src/soc/mtz1.c. */
#define MTZ1_PIN_SELECT 0x0705u
#define MTZ1_PIN_POWER  0x0804u
#define Z1_PARAM_BYTES  49u
#define MTZ1_FRAME_TICKS 60000u
#define MTZ1_FRAME_PERIOD_MS 10u
#define MTZ1_TICKS_PER_MS (MTZ1_FRAME_TICKS / MTZ1_FRAME_PERIOD_MS)

typedef struct {
    bool     present, powered, selected, atn, trace;
    bool     firmware, stage_b, check_seen, atn_pulse, raw_stream;
    bool     aspeed_stream;
    bool     transaction_overflow;
    uint8_t  operating_mode, seq, last_n;
    uint8_t  params[Z1_PARAM_BYTES];
    uint8_t  rx[1024];
    uint32_t pkt_len, rx_len;
    uint16_t last_checksum;
    uint32_t b_checksum;
    uint64_t octets, aspeed_bytes, bspeed_bytes;
    uint64_t transactions, mode_transitions, checks, executes;
    uint64_t unknown_ops, report_unknown, frames_queued, frames_read;
    s5l_mt_geom_t geom;
    uint8_t  pending[MTZ2_PAYLOAD_LIMIT];
    uint8_t  pending_len;
    s5l_mt_contact_t last_contacts[MTZ2_CONTACT_MAX];
    uint8_t  frame_seq;
    uint32_t frame_ms;
    s5l_mt_contact_t held[MTZ2_CONTACT_MAX];
    uint8_t  held_n;
    bool     lift_pending;
    uint32_t tick_acc;
    uint32_t uptime_ms;
    uint32_t uptime_tick_acc;
} s5l_mtz1_t;

void     s5l_mtz1_reset(s5l_mtz1_t *dev);
void     s5l_mtz1_bind(s5l_mtz1_t *dev, s5l_spi_slave_t *slave);
bool     s5l_mtz1_irq(const s5l_mtz1_t *dev);
void     s5l_mtz1_select_pin(void *ctx, bool level);
void     s5l_mtz1_power_pin(void *ctx, bool level);
bool     s5l_mtz1_set_contacts(s5l_mtz1_t *, const s5l_mt_contact_t *, unsigned n);
void     s5l_mtz1_tick(s5l_mtz1_t *, uint32_t tb_ticks);

/* ------------------------------------ Synopsys DWC2 USB OTG (config only) ---
 * The four hardware-configuration registers AppleSynopsysOTGDevice reads out of
 * the block at S5L8900_USB_OTG_BASE, and nothing else. core/src/soc/usbotg.c
 * carries the evidence: which accesses the driver makes, the endpoint
 * derivation it runs on what it reads, and why each value below is the one
 * chosen. The short version is that an unmodelled window answered those reads
 * with zero, the driver believed it, and the boot panicked on the endpoint
 * count it computed — deterministically, at the same instruction every run.
 *
 * These are a legal and sufficient DWC2 configuration. They are NOT values
 * measured from real S5L8900 silicon; we have no dump of this part's
 * configuration registers, and nothing here pretends otherwise.
 *
 * This is a configuration-register model, not a USB controller. No transfer,
 * FIFO, endpoint, DMA, PHY or interrupt behaviour is emulated, and the device
 * tree's interrupt 0x13 is deliberately not defined as a constant: nothing here
 * ever asserts it, and a constant that looks wired but is not is the same
 * landmine the SPI note above describes.
 */
#define USBOTG_GHWCFG1 0x044u   /* per-endpoint direction, 2 bits per endpoint */
#define USBOTG_GHWCFG2 0x048u   /* architecture + counts, incl. NumDevEps      */
#define USBOTG_GHWCFG4 0x050u   /* driver reads it and uses only bit 25        */
#define USBOTG_PCGCCTL 0xe00u   /* power/clock gating; guest read-modify-write */

#define S5L_DWC2_GHWCFG1 0x00000000u   /* every endpoint bidirectional */
#define S5L_DWC2_GHWCFG2 0x228de550u   /* NumDevEps=9 -> 5 IN / 5 OUT  */
#define S5L_DWC2_GHWCFG4 0x00000000u

typedef struct {
    /* The only writable state in this model. Reset 0. */
    uint32_t pcgcctl;
} s5l_usbotg_t;

void     s5l_usbotg_reset(s5l_usbotg_t *u);
/* Unmodelled offsets read 0 and accept-and-discard writes, which is exactly
 * what an unmapped access did before this window existed. */
uint32_t s5l_usbotg_read(const s5l_usbotg_t *u, uint32_t off);
void     s5l_usbotg_write(s5l_usbotg_t *u, uint32_t off, uint32_t val);

/* ------------------------------------------- ARM PrimeCell PL080 DMAC ---
 * The two DMA controllers, /arm-io/dmac0 and /arm-io/dmac1.
 *
 *   /arm-io/dmac0  reg {0x00200000,0x1000}  interrupts {0x10}  compatible
 *                  'dmac,pl080'
 *   /arm-io/dmac1  reg {0x01900000,0x1000}  interrupts {0x11}  same compatible
 *
 * /arm-io's `ranges` is TWO windows — {0, 0x38000000, 0x08000000} and
 * {0x10000000, 0x18000000, 0x10000000} — so "child + 0x38000000" is the rule
 * for the FIRST only. Both dmac nodes are under 0x08000000 and therefore in it,
 * giving the two bases below; a node above 0x10000000 would need the second.
 * Both interrupt-parent to /arm-io/vic, and both numbers are under 32, so both
 * lines are VIC0's — unlike i2s0/i2s1, whose `interrupts` are GPIO-IC numbers
 * and therefore have deliberately no constant here.
 *
 * WHY THIS EXISTS AT ALL. `dma-channels` on /arm-io/i2s0 hands the PL080 the
 * absolute physical addresses 0x3ca00010 and 0x3ca00038 — the I2S transmit and
 * receive FIFOs. The CPU never touches them: the audio path is DMA-driven end
 * to end, so with no controller here not one sample could ever leave the guest,
 * however correct the codec and the I2S window were. The driver allocates ten
 * channels on every boot, printing a physical LLI-buffer address for each.
 *
 * AND WHAT THE FIRST BOOT WITH IT MEASURED, which is narrower than the sentence
 * above deserves. Allocating a channel touches no register: _initDMAChannel
 * (0xc070f01c) only allocates the 4 KiB linked-list buffer and copies the
 * device tree's 32-byte `dma-channels` entry into a software record. The
 * registers are first touched when a COMMAND is queued, and in a boot to
 * userspace at 900M instructions the whole 0x38200000 page saw one read and two
 * writes — byte for byte what run89-base's census recorded with no model here at
 * all, and that run went to 3.55 BILLION instructions. 0x39900000 saw nothing.
 * So this controller is now correct, decoded, and idle: nothing in this boot
 * ever calls startDMACommand, and the blocker for guest audio has moved
 * upstream of the DMA controller.
 *
 * ------------------------------------------------------------------------
 * THE REGISTER MAP, AND WHERE EACH LINE OF IT COMES FROM
 *
 * Everything marked "read" below was read out of AppleARMPL080DMAC in the
 * shipped 7E18 kernelcache (kext __TEXT 0xc070e000..0xc070fe28, ARM mode), not
 * out of the published PrimeCell TRM. The driver reaches the register file
 * through exactly two out-of-line virtuals, which is what makes the census
 * complete rather than merely thorough:
 *
 *   vptr+0x3a4  0xc070ecd0  read32 (this, off)          -> ldr r0,[off, base]
 *   vptr+0x3a8  0xc070ecdc  writeMasked(this, off, val, mask)
 *
 * writeMasked skips the read-modify-write when mask == 0xffffffff, so a full
 * write never reads first. Every call site was enumerated: 10 reads and 17
 * writes, resolving to these offsets and no others.
 *
 *   0x000  read   at 0xc070f16c, the interrupt filter's first act.
 *   0x008  write  at 0xc070f18c, the value just read from 0x000, mask ~0.
 *   0x030  write  1 at 0xc070ed7c and 0 at 0xc070ee90 (power on / off).
 *   0x100 + 0x20*n + 0x00  write  SrcAddr   (0xc070e98c)
 *                    + 0x04  write  DestAddr  (0xc070e9ac)
 *                    + 0x08  read+write  LLI  (0xc070e8f0 / 0xc070e934)
 *                    + 0x0c  read+write  Control (0xc070f9f8 / 0xc070e9cc)
 *                    + 0x10  read+write  Configuration (0xc070e948/0xc070e9f0)
 *
 * The channel stride and count are read, not assumed: the power-on path at
 * 0xc070edb8 clears Configuration and LLI for r5 = 0x130, 0x150 ... 0x1f0 after
 * doing channel 0 explicitly at 0x110/0x108, which is eight channels 0x20
 * apart. startDMACommand refuses an index above 7 (`cmp r1,#7; bhi`, 0xc070e0f8)
 * and the channel-allocator at 0xc070e830 clamps to 7 and walks a 100-byte
 * per-channel record with `ldr r0,[ip],#-0x64`.
 *
 * The bit positions are read the same way:
 *
 *   Configuration bit 0  (E)      0xc070e950 `tst r0,#1` decides whether the
 *                                 channel still needs programming; 0xc070e9d0
 *                                 starts it with template | 0x8001.
 *   Configuration bit 15 (ITC)    the 0x8000 half of that same 0x8001.
 *   Configuration bit 17 (A)      0xc070efc8 `tst r0,#0x20000` spins in an
 *                                 UNBOUNDED loop until it clears — this model
 *                                 must never leave it set, or the guest hangs.
 *   Configuration bit 18 (H)      0xc070ef04 halts with template | 0x40001,
 *                                 the literal at 0xc070ef68 — enable AND halt
 *                                 in one store, so a model that reads bit 0 as
 *                                 "go" would start the transfer this write
 *                                 exists to stop.
 *   Configuration [13:11] flow    0xc070e45c rewrites exactly that field in the
 *                                 software template per direction: 0x800 out,
 *                                 0x1000 in — and 0xc070e444 skips the rewrite
 *                                 entirely for direction 0, leaving whatever
 *                                 the device tree supplied.
 *   Control [11:0] size           0xc070e738 and 0xc070fb7c mask a read-back
 *                                 with 0xfff to get the remaining count.
 *   Control bit 26 (SI)           0xc070e4bc ORs 0x4000000 when direction & 2.
 *   Control bit 27 (DI)           0xc070e4c4 ORs 0x8000000 when direction & 1.
 *   Control bit 31 (I)            0xc070fbfc sets 0x80000000 on the LAST LLI
 *                                 only, and 0xc070fb94 reads it back with
 *                                 `lsr #0x1f`.
 *   Control [20:18]/[23:21] width 0xc070e14c picks >>21 for direction 1 and
 *                                 >>18 otherwise, then &7.
 *
 * The linked-list item is four words {SrcAddr, DestAddr, Next, Control} at
 * +0x0/+4/+8/+0xc: the builder at 0xc070fba0 stores exactly those, and the
 * reader at 0xc070fb3c takes Next from +8 and Control from +0xc. `bic ...,#3`
 * at 0xc070e718, 0xc070e750 and 0xc070f954 strips the AHB master-select field
 * in the low two bits — on the three read-backs whose value is used as an
 * ADDRESS. The fourth, 0xc070e8f0, is left unmasked because 0xc070e8f8 only
 * compares it against zero.
 *
 * WHERE APPLE'S PART MATCHES THE PUBLISHED SPEC. Everywhere the driver goes.
 * Nothing in the disassembly contradicts the PL080 TRM's map, and the DT's own
 * `compatible` is 'dmac,pl080'. The offsets NOT in the list above are the ones
 * this comment cannot vouch for from the binary — see the refusals below.
 *
 * ------------------------------------------------------------------------
 * WHAT THIS MODEL REFUSES, BY NAME
 *
 * Each of these is counted, not silently ignored, so a guest that needs one is
 * visible in the counters instead of mysterious:
 *
 *   refused_flow    FlowCntrl 4-7 — the ones where the PERIPHERAL is the flow
 *                   controller and supplies the transfer size. This model has
 *                   no DMA request lines, so it cannot know when such a
 *                   transfer ends. The shipped tree only ever uses 0, 1, 2 and
 *                   3 (all "DMA controller is the flow controller"), which is
 *                   why those four run and these four do not.
 *   refused_width   a reserved width code (3-7), or a transfer whose total byte
 *                   count is not a whole number of DESTINATION transfers.
 *                   Mismatched source and destination widths themselves DO run
 *                   — /arm-io/spi1 uses them, four bytes in and one byte out —
 *                   but a remainder would be left sitting in the channel FIFO
 *                   this model does not have, and there is no Active bit it
 *                   could honestly set to say so. See run_item() in pl080.c for
 *                   which side Control[11:0] counts and how that was settled.
 *   refused_chain   an LLI chain longer than S5L_PL080_MAX_ITEMS. A list that
 *                   points at itself is a hung host, and real hardware has no
 *                   such limit; this one is ours and it is stated.
 *   refused_softreq a write to SoftBReq/SoftSReq/SoftLBReq/SoftLSReq (0x020,
 *                   0x024, 0x028, 0x02c). Software DMA requests only mean
 *                   something to the burst/single pacing this model does not
 *                   have. The stock driver writes none of the four.
 *   refused_endian  DMACConfiguration M1/M2 (bits 1-2). The whole machine is
 *                   little-endian; a guest asking for big-endian AHB masters
 *                   would get bytes this model does not swap.
 *
 * Also deliberately absent, with no counter because nothing can ask for them:
 * the PrimeCell identification registers at 0xfe0-0xffc. We have no dump of
 * Apple's PeriphID/PCellID values, the driver never reads one, and inventing
 * four constants would be exactly the fabrication this file exists to avoid.
 * They fall through to the unknown-offset log like any other unmodelled word.
 *
 * BURSTS AND REQUEST LINES ARE NOT MODELLED. SBSize/DBSize are stored and
 * ignored, and a channel with E set and H clear transfers as though its
 * peripheral always had a request pending. That is the one place this model is
 * knowingly faster than the part: audio moves at whatever rate the guest can
 * queue it, not at 44.1 kHz. It is what makes the capture record anything at
 * all, and it is not a claim about timing.
 */
#define S5L8900_DMAC0_BASE  0x38200000u
#define S5L8900_DMAC1_BASE  0x39900000u
#define S5L8900_DMAC_COUNT  2u
#define S5L8900_IRQ_DMAC0   0x10u
#define S5L8900_IRQ_DMAC1   0x11u

#define S5L_PL080_CHANNELS      8u
#define S5L_PL080_UNKNOWN_OFF   8u
/* Our cap on how far one enable may follow the guest's linked list. Four times
 * the 256 items its own 4 KiB LLI buffer can hold (0xc070f02c allocates 0x1000
 * and 0xc070fba0 strides 16), so a well-formed chain can never reach it. */
#define S5L_PL080_MAX_ITEMS  1024u

#define PL080_INTSTATUS        0x000u  /* masked TC | masked error, read-only */
#define PL080_INTTCSTATUS      0x004u
#define PL080_INTTCCLEAR       0x008u  /* write-one-to-clear, write-only      */
#define PL080_INTERRSTATUS     0x00cu
#define PL080_INTERRCLEAR      0x010u
#define PL080_RAWINTTCSTATUS   0x014u
#define PL080_RAWINTERRSTATUS  0x018u
#define PL080_ENBLDCHNS        0x01cu
#define PL080_SOFTBREQ         0x020u
#define PL080_SOFTSREQ         0x024u
#define PL080_SOFTLBREQ        0x028u
#define PL080_SOFTLSREQ        0x02cu
#define PL080_CONFIG           0x030u
#define PL080_SYNC             0x034u
#define PL080_CHAN_BASE        0x100u
#define PL080_CHAN_STRIDE      0x020u
#define PL080_CH_SRC           0x00u
#define PL080_CH_DST           0x04u
#define PL080_CH_LLI           0x08u
#define PL080_CH_CTRL          0x0cu
#define PL080_CH_CFG           0x10u

#define PL080_CONFIG_EN        0x00000001u
#define PL080_CONFIG_ENDIAN    0x00000006u  /* M1, M2 */

#define PL080_CFG_EN           0x00000001u
#define PL080_CFG_FLOW_SHIFT   11u
#define PL080_CFG_FLOW_MASK    0x00003800u
#define PL080_CFG_IE           0x00004000u
#define PL080_CFG_ITC          0x00008000u
#define PL080_CFG_LOCK         0x00010000u
#define PL080_CFG_ACTIVE       0x00020000u  /* read-only in this model, always 0 */
#define PL080_CFG_HALT         0x00040000u

#define PL080_CTRL_SIZE_MASK   0x00000fffu
#define PL080_CTRL_SWIDTH_SHIFT 18u
#define PL080_CTRL_DWIDTH_SHIFT 21u
#define PL080_CTRL_WIDTH_MASK  7u
#define PL080_CTRL_SI          0x04000000u
#define PL080_CTRL_DI          0x08000000u
#define PL080_CTRL_I           0x80000000u

/* The five per-channel registers, in the order the driver programs them,
 * plus what this channel actually did.
 *
 * PER-CHANNEL, because the controller-wide `items` and `bytes_moved` were
 * misread once already: run104's `dmac1 items 210 bytes 812340` was taken as
 * evidence that the digitizer's channel was moving 812 KB into SPI1, when the
 * figure covers every channel on the controller. A channel's own registers at
 * rest cannot settle it either -- a PL080 clears the size and the enable bit
 * on terminal count, so a completed transfer and one that never started look
 * identical. These two counters are the difference.
 */
typedef struct {
    uint32_t src, dst, lli, ctrl, cfg;
    uint64_t runs;    /* items this channel completed */
    uint64_t bytes;   /* bytes it delivered to its destination */
} s5l_pl080_chan_t;

typedef struct {
    s5l_pl080_chan_t ch[S5L_PL080_CHANNELS];
    uint32_t config;    /* 0x030 */
    uint32_t sync;      /* 0x034, stored; no request line is gated by it     */
    uint32_t raw_tc;    /* 0x014; 0x004 and 0x000 are derived from it        */
    uint32_t raw_err;   /* 0x018; nothing in this model ever sets a bit here */

    uint64_t reads, writes;
    uint64_t unknown_reads, unknown_writes;
    uint32_t unknown_off[S5L_PL080_UNKNOWN_OFF];
    unsigned unknown_off_count;

    /* What actually moved. bytes_moved is the number the audio question is
     * really asking, so it is counted separately from the transfer count. */
    uint64_t transfers;
    uint64_t bytes_moved;
    uint64_t items;         /* linked-list items retired                     */
    uint64_t completions;   /* terminal-count events that set a status bit   */

    uint64_t refused_flow;
    uint64_t refused_width;
    uint64_t refused_chain;
    uint64_t refused_softreq;
    uint64_t refused_endian;

    /*
     * WRITES to DMACConfiguration, as distinct from its VALUE.
     *
     * The diagnostic used to print "NEVER WRITTEN" whenever `config` was zero,
     * and that is not the same statement: a driver that writes 0, or writes 1
     * and later clears it, is indistinguishable from one that never touched the
     * register. "AppleARMPL080DMAC never writes DMACConfiguration" was repeated
     * for days on the strength of that, and it was an inference dressed as a
     * measurement. This counter is the measurement.
     */
    uint64_t config_writes;
    uint32_t config_first;   /* the first value written, whatever became of it */
} s5l_pl080_t;

void     s5l_pl080_reset(s5l_pl080_t *d);
uint32_t s5l_pl080_read(s5l_pl080_t *d, uint32_t off);
void     s5l_pl080_write(s5l_pl080_t *d, uint32_t off, uint32_t val);
/* The combined interrupt line, which is what 0x000 reads back. */
bool     s5l_pl080_irq(const s5l_pl080_t *d);
/*
 * Run every runnable channel to the end of its chain and return the new line
 * level. Called from s5l8900_tick(), NOT from the register write, so the bus
 * accesses it makes never re-enter bus_write(); `bus` is passed rather than
 * stored so the device holds no pointer into the machine that a snapshot or a
 * copy could invalidate. Passing a null bus does nothing and moves nothing.
 */
/*
 * DESTINATION PACING, which is what a PL080's request lines do.
 *
 * `ready` is asked, before every destination transfer, whether the thing at
 * that address can accept one. NULL means "always", which is the right answer
 * for a memory destination and was this model's only behaviour until run116.
 *
 * It is not a refinement. This controller completes a whole channel inside one
 * s5l8900_tick() -- see the note at the top of pl080.c, which defends that as
 * "the same ORDER the guest observes", and for memory it is. For a PERIPHERAL
 * it is not: the real part moves a burst only when the peripheral asserts
 * DMACBREQ, and even in flow modes 0-3, where the CONTROLLER owns the transfer
 * size, that request still gates each burst.
 *
 * run116 measured what its absence costs. With the bus decode fixed so narrow
 * stores finally reach the port, the controller delivered a 54,156-byte Z2
 * firmware image into an eight-deep FIFO before the driver had armed it:
 * `tx-drops 54140`, `dma-arms 1`. The bytes arrived and were thrown away one
 * FIFO-full at a time.
 *
 * A channel that is refused stops WITHOUT clearing its enable bit and with its
 * remaining count already written back, so the next tick resumes it exactly
 * where it stopped. That is also what the driver's own progress reporting
 * expects to see.
 */
typedef bool (*s5l_pl080_ready_fn)(void *ctx, uint32_t dst, unsigned width);

bool     s5l_pl080_run(s5l_pl080_t *d, const arm_bus_t *bus,
                       s5l_pl080_ready_fn ready, void *ready_ctx);

#define S5L_STUB_MAX      16

/*
 * A bounded host replacement may take over only at one of these exact guest
 * PCs.  This is intentionally a target table rather than a callback on every
 * instruction: the signed engine can stop before a registered entry without
 * paying an indirect host call at every graph node.
 */
#define S5L_PRE_STEP_TARGET_MAX 16u
typedef bool (*s5l_pre_step_fn)(void *ctx);

/*
 * Optional host pacing for an interactive frontend's WFI idle intervals.
 *
 * The machine already knows the exact guest CPU ticks to the next deliverable
 * device edge.  A frontend that supplies this callback asks the host to wait
 * for the corresponding wall-clock interval before those idle ticks are
 * committed.  Returning false means the full wait did not complete; guest
 * time is then left at the last safely paced point and the WFI completes
 * spuriously, which is safer than manufacturing elapsed time.
 *
 * The callback runs synchronously on the machine-owning thread and must not
 * call back into the machine.  Generic frontends leave it NULL and retain the
 * historical deterministic fast-forward exactly.
 */
typedef bool (*s5l_wfi_host_sleep_fn)(void *ctx, uint64_t nanoseconds);

/*
 * Optional monotonic clock for interactive active execution.  The callback
 * writes host nanoseconds and returns true, or returns false without claiming
 * that time advanced.  It runs synchronously on the machine-owning thread and
 * must not call back into the machine.  Generic frontends leave it NULL and
 * retain instruction-paced guest time exactly.
 */
typedef bool (*s5l_active_host_now_fn)(void *ctx, uint64_t *nanoseconds);

/*
 * Optional host peer on uart4, the guest's PPP line.
 *
 * `tx` sees every low byte the guest writes to UTXH, including bytes beyond
 * uart4's bounded diagnostic capture. It runs inside the guest MMIO write and
 * must update host-owned state only: it may not call back into the machine.
 *
 * `service` runs once after each public s5l8900_run() slice and receives the
 * number of guest instructions that slice retired. This is the safe handoff
 * boundary for a non-blocking host peer: it may push bytes into uart4's RX
 * FIFO through s5l_uart_rx_push() and then call s5l8900_tick(machine, 0) to
 * refresh the interrupt line. It must not call s5l8900_run() recursively.
 *
 * Both callbacks run synchronously on the machine-owning thread. Generic
 * frontends leave them NULL, preserving the historical machine exactly.
 */
typedef void (*s5l_uart4_host_tx_fn)(void *ctx, uint8_t byte);
typedef void (*s5l_uart4_host_service_fn)(void *ctx, unsigned retired);

/* Do not hold an interactive execution slice inside WFI for longer than this.
 * Longer guest waits are advanced in real-time-sized pieces, yielding between
 * them so a frontend can drain input, stop requests and scanout. */
#define S5L8900_WFI_PACE_SLICE_NS UINT64_C(8000000)

/* Do not inject more than this much host time that has not already been
 * advanced by the guest. Paced WFI time is subtracted before applying the
 * ceiling, so ordinary host sleep overshoot is preserved while a suspend,
 * debugger stop or starved frontend interval still cannot arrive in one
 * burst. During active execution the clock is sampled much more frequently. */
#define S5L8900_ACTIVE_CLOCK_MAX_STEP_NS UINT64_C(8000000)
/* Keep engine/interpreter retirement groups small for input and MMIO checks,
 * but do not turn every such boundary into an expensive host clock call. At
 * the measured 16--30 Minsn/s, 4096 retirements are about 0.14--0.26 ms. */
#define S5L8900_ACTIVE_CLOCK_BATCH_INSNS  256u
#define S5L8900_ACTIVE_CLOCK_SAMPLE_INSNS 4096u

typedef struct {
    uint32_t    base, size;
    const char *name;
    /* Backing store covers the WHOLE declared window. A fixed-size array was
     * the first design and it was wrong in a way worth remembering: at 64
     * registers it covered offsets 0x000-0x0FC, while the two registers we had
     * actually measured live at 0x320 (GPIO FSEL) and 0x404 (CLOCK0 ADJ2).
     * Both fell past the array and were counted but not stored, so read-back
     * returned 0 — the stub silently failed to be the honest storage it exists
     * to be, for precisely the registers that mattered. Size the backing to
     * the window instead of hoping the window is small. */
    uint32_t   *regs;
    uint32_t    nregs;
    uint64_t    reads, writes;
    uint64_t    oob;                  /* accesses past the backing store */
} s5l_stub_t;

/* ------------------------------------------------------------- machine ---
 * Wires the CPU to RAM and the peripherals through one arm_bus_t.
 */
typedef struct {
    arm_cpu_t  cpu;
    arm_bus_t  bus;
    uint8_t   *ram;
    uint32_t   ram_base;
    uint32_t   ram_size;
    s5l_uart_t  uart0;
    /*
     * uart4, the guest's PPP line. A second instance of the same block rather
     * than a second model, because it IS the same block: one register file,
     * one set of semantics, and two independent transmit captures. Keeping the
     * captures separate is the whole point — the console and the PPP stream
     * must never be spliced into one buffer, because a reader cannot tell
     * which byte came from which port afterwards, and the milestone this port
     * exists for is a six-byte sequence. core/tests/test_uart4.c pins that.
     */
    s5l_uart_t  uart4;
    /*
     * Both PL192 VICs. vic[0] is the historical vic0; vic[1] backs the second
     * page at S5L8900_VIC1_BASE, which AppleARMPL192VIC maps and which carries
     * device-tree interrupt lines 32..63.
     */
    s5l_vic_t   vic[S5L8900_VIC_COUNT];
    s5l_timer_t timer;
    s5l_power_t power;
    s5l_mbx_t   mbx;
    s5l_clcd_t  clcd;
    s5l_tvout_t tvout;
    s5l_i2c_t   i2c[S5L8900_I2C_COUNT];
    s5l_pcf50635_t pmu;
    /* The WM8991 on i2c0 address 0x1B, and the two I2S windows. The codec and
     * the transport are one unit and land together on purpose: a codec that
     * answers its identity with no window behind it turns today's loud, correct
     * and harmless `I2C register read failed` into a boot that proceeds into an
     * audio stack whose transport is undeclared. i2s[0] is i2s0 (the codec's),
     * i2s[1] is i2s1 (the baseband voice path). */
    s5l_wm8991_t codec;
    s5l_i2s_t   i2s[S5L8900_I2S_COUNT];
    /* spi[0] is spi0, spi[1] is spi1 and spi[2] is spi2. M68 attaches its Z1
     * touch controller on spi2; the baseband transport also uses that bus. */
    s5l_spi_t   spi[S5L8900_SPI_COUNT];
    /* The two halves of /arm-io/gpio: the interrupt cascade on the power page
     * and the pin state on its own. See the GPIOIC section above. */
    s5l_gpioic_t gpioic;
    s5l_gpio_t   gpio;
    /* The five switches the board wires to gpio port 22 and gpioic group 1.
     * Not a chip: see the buttons section above. */
    s5l_buttons_t buttons;
    s5l_mtz2_t   mtz2;      /* the touch controller on spi1 chip select 0 */
    s5l_mtz1_t   mtz1;      /* M68: the Z1 touch controller on spi2 cs0    */
    s5l_usbotg_t usbotg;
    /* The two PL080 DMA controllers, /arm-io/dmac0 and /arm-io/dmac1. */
    s5l_pl080_t dmac[S5L8900_DMAC_COUNT];
    s5l_nor_t   nor;
    /* Physical base of the NOR window. Defaults to S5L8900_NOR_BASE (the
     * synthetic aperture N82 needs); M68 overrides it with the genuine
     * memory-mapped CFI NOR at S5L8900_M68_NOR_BASE. */
    uint32_t    nor_base;
    uint64_t   unmapped_reads;   /* visibility: accesses outside the map */
    uint64_t   unmapped_writes;

    /* Diagnostic: the first distinct addresses the guest touched outside the
     * memory map. When real firmware wanders off, these name the peripheral we
     * have not modelled yet — which is the next thing to build. */
    uint32_t   unmapped_addr[S5L_UNMAPPED_LOG];
    unsigned   unmapped_addr_count;

    /* Diagnostic: log accesses to device windows (not RAM). Real firmware
     * polls hardware to decide what to do next, so seeing exactly which
     * registers it reads is how we learn what it is waiting for. */
    bool       trace_devices;
    /* Diagnostic: stream every access to this 4 KiB device page to stderr. */
    uint32_t   mmio_watch_page;
    uint32_t   dev_addr[S5L_DEVLOG];
    uint32_t   dev_value[S5L_DEVLOG];
    bool       dev_is_write[S5L_DEVLOG];
    unsigned   dev_count;

    /*
     * How fast guest time runs relative to guest work. See S5L8900_CPU_HZ.
     * cpu_hz elapsed CPU-clock ticks advance the timebase by tb_hz ticks;
     * tb_accum carries the remainder so the ratio stays exact over time. Active
     * execution supplies one clock tick per retired instruction; WFI can
     * supply an idle interval without changing cpu.cycles.
     */
    uint32_t   cpu_hz, tb_hz;
    uint64_t   tb_accum;

    /*
     * Has anything moved a device level since the last full refresh?
     *
     * THE MEASUREMENT. tools/insnbench prices s5l8900_tick(m, 1) at 543 ns
     * against ~84 ns for an interpreted instruction, so ticking once per
     * retired instruction costs about seven eighths of the interpreter's
     * throughput — 1.58 M/s measured against 6.68 M/s with the tick removed.
     * docs/dynarec.md §1.2's -21% is a reading from when the tick was a
     * divide, one timer and one VIC line; it now sweeps twelve devices, five
     * buttons and seven GPIO cascade groups and recomputes both VICs.
     *
     * WHAT THE FLAG BUYS. Almost all of that work is a LEVEL REFRESH, not a
     * time advance: re-reading the I2C/SPI/UART latches, re-driving the five
     * button pins through s5l_gpio_drive() (which snapshots all 25 pin words
     * per call), recomputing the seven cascade groups and both VIC outputs.
     * Every one of those steps is idempotent — see the "still high sets
     * nothing" note in s5l_gpioic_set_line() and the s5l_gpio_drive() comment
     * that makes s5l_buttons_apply() safe per-tick — so repeating it over
     * unchanged inputs cannot change the machine. The inputs change in exactly
     * three ways: elapsed timebase ticks, a guest access to a device window,
     * and a host reaching in behind the bus. The first is detected by the
     * accumulator; the second sets this flag at the bus interposer; the third
     * is what `s5l8900_tick(m, 0)` is for.
     *
     * WHY THE FAILURE MODE IS BENIGN. The skip only ever applies to a tick
     * that crossed NO timebase edge, and at 412 MHz against 6 MHz that is at
     * most 68 consecutive instructions. So a host that changes a level and
     * forgets to ask for a refresh delivers it up to 68 instructions late,
     * never not at all — unlike a wake-source deadline, where guessing wrong
     * loses the interrupt outright.
     *
     * WHAT IT MEASURED. insnbench's tick=yes rows went 1.58 -> 7.89 M/s with
     * the 4 KiB-page MMU and 1.83 -> 14.18 M/s without it, which leaves the
     * tick costing 3.4% of the MMU row rather than 76% of it. A real 300 M
     * instruction kernel boot through s5l8900_run() went 189.1 s -> 48.8 s,
     * and produced byte-identical guest console, framebuffer and machine
     * snapshot. Part of that is s5l_gpio_drive()'s own early out, which this
     * flag is what made worth having: it cut the remaining full refresh in
     * half, and the refresh is now what a real boot mostly pays for.
     *
     * Deliberately NOT serialised. It is derived state, and snapshot_load()
     * sets it rather than restoring it, so a loaded machine re-derives every
     * level once before it can observe anything. That is why the s5l8900_t
     * size guard in core/src/snapshot.c moves without SNAPSHOT_VERSION moving:
     * the file format is unchanged.
     */
    bool       level_dirty;

    /*
     * Runtime-only retirement count used while an interpreter batch is inside
     * arm_step(). It is not snapshot state and must be zero between run calls.
     */
    unsigned   batch_pending;

    /*
     * What the last full refresh saw on the three inputs a HOST can move
     * WITHOUT going through the bus, packed by ext_inputs() in machine.c.
     *
     * This is the third way in, and it is the one nothing in the machine can
     * observe: s5l_uart_rx_push() into uart4's receive FIFO, the touch
     * controller's attention line after s5l_mtz2_set_contacts(), and the five
     * switches after s5l_buttons_set(). Each takes a sub-struct, not a machine,
     * so none of them can set `level_dirty`. `s5l8900_tick(m, 0)` is the
     * contract and every shipping injector now calls it; this witness is what
     * makes forgetting it a lost 68 instructions rather than a lost interrupt,
     * and it is what lets core/tests/test_uart4.c still pin "a byte pushed
     * between run slices asserts VIC line 28 on the NEXT tick" unchanged.
     *
     * A new host-drivable input belongs in ext_inputs() beside these three.
     * Not serialised, for the same reason `level_dirty` is not.
     */
    uint32_t   ext_seen;

    /* Identified-but-unmodelled peripheral windows. See s5l_stub_t. */
    s5l_stub_t stubs[S5L_STUB_MAX];
    unsigned   stub_count;
    /* Declarations that were refused (table full, overlap, allocation). Zero is
     * the only acceptable value after init; a non-zero count means a window we
     * meant to name is silently unmapped instead. */
    unsigned   stub_declare_failures;

    /* Host-only decode cache for the optional build-time-signed AArch64
     * engine. Never serialised: it contains host pointers and is derivable
     * from guest RAM. The public enable call owns it; s5l8900_free releases
     * it. Keeping one per machine avoids cross-VM code/data aliases. */
    void      *static_a64_state;

    /*
     * Optional host-function interception, also never serialised.  A hook is
     * consulted only when PC equals one of pre_step_target[]; returning true
     * means it completed the whole guest operation and set the next PC.  The
     * machine supplies one device tick, but no retired guest instruction, to
     * match bootkernel's established HLE contract.
     */
    s5l_pre_step_fn pre_step_hook;
    void           *pre_step_ctx;
    uint32_t        pre_step_target[S5L_PRE_STEP_TARGET_MAX];
    unsigned        pre_step_target_count;
    uint64_t        pre_step_filter;
    uint64_t        pre_step_matches;
    uint64_t        pre_step_handled;

    /*
     * Host-only evidence for the interpreter's timebase-bounded User-mode
     * tick batching. A batch invocation may retire only one instruction when
     * that instruction touches MMIO, observes a host input, or enters an
     * exception; recording both totals makes that distinction visible.
     *
     * These are execution diagnostics, not guest state. They are deliberately
     * absent from snap_mach(), just like the pre-step counters above, and a
     * snapshot load preserves the live machine's running totals.
     */
    uint64_t        interpreter_tick_batches;
    uint64_t        interpreter_tick_batched_retired;

    /* Per-machine native renderer work, sampled by the app at its existing
     * scanout boundary. Host diagnostics only: snap_mach() deliberately
     * preserves these running totals across a guest-state restore. */
    s5l_mbx_telemetry_t mbx_telemetry;

    /*
     * Interactive WFI pacing is host policy, never guest state.  The callback,
     * context and evidence counters are absent from snap_mach(), while
     * snapshot_load() clears only the transient run-slice yield.  A NULL
     * callback is the deterministic/default machine used by every existing
     * harness.  See s5l8900_set_wfi_host_pacing().
     */
    s5l_wfi_host_sleep_fn wfi_host_sleep;
    void                 *wfi_host_sleep_ctx;
    uint64_t              wfi_paced_waits;
    uint64_t              wfi_paced_wait_ns;
    uint64_t              wfi_paced_partial_advances;
    uint64_t              wfi_paced_failures;
    bool                  wfi_pace_yield;

    /*
     * Optional active host clock, also host policy and never guest state.
     * The callback/context and evidence counters survive snapshot restore.
     * The anchor, conversion remainder and ticks observed since the anchor are
     * transient: snapshot_load() clears them so a restored guest instant can
     * never be compared with an older host instant.
     *
     * Every s5l8900_tick() while the callback is installed contributes to
     * guest_ticks_since_sync. That includes paced WFI intervals, allowing the
     * active synchronizer to subtract already-modeled time before bounding and
     * adding only the residual real-time deficit.
     */
    s5l_active_host_now_fn active_host_now;
    void                  *active_host_now_ctx;
    uint64_t               active_clock_last_host_ns;
    uint64_t               active_clock_guest_ticks_since_sync;
    uint64_t               active_clock_fraction;
    uint64_t               active_clock_updates;
    uint64_t               active_clock_added_ticks;
    uint64_t               active_clock_clamps;
    uint64_t               active_clock_failures;
    bool                   active_clock_anchor_valid;
    bool                   active_clock_carry; /* Optional foreground policy, never serialized. */
    uint64_t               active_clock_debt_ticks; /* Unpaid CPU-clock ticks, never serialized. */

    /*
     * uart4's optional host peer. Host wiring, never guest state: snapshot
     * visitors deliberately leave all three fields untouched, so restoring a
     * guest underneath a live frontend retains that frontend's peer rather
     * than importing a stale pointer from the snapshot writer.
     */
    s5l_uart4_host_tx_fn      uart4_host_tx;
    s5l_uart4_host_service_fn uart4_host_service;
    void                     *uart4_host_ctx;
    s5l_uart4_host_tx_fn      uart0_host_tx;
    void                     *uart0_host_ctx;
} s5l8900_t;

/*
 * Declare a stub window. `name` must be a string literal or otherwise outlive
 * the machine. Returns false if the table is full, or if the window overlaps a
 * modelled device, another stub, or RAM — silently shadowing, or being silently
 * shadowed by, anything else on the bus would be worse than refusing. Windows
 * larger than S5L_STUB_REGS*4 are accepted; accesses beyond that are counted in
 * `oob` rather than stored, so the shortfall is visible.
 */
bool s5l8900_add_stub(s5l8900_t *m, uint32_t base, uint32_t size,
                      const char *name);

/* ------------------------------------------------------- address routing ---
 *
 * THE ROUTING CONTRACT. There is exactly one rule, and it is enforced at
 * construction rather than at every access:
 *
 *   RAM MAY NOT OVERLAP ANY WINDOW THIS MACHINE DECODES.
 *
 * s5l8900_init() refuses to build a machine whose RAM aperture would cover a
 * device window, and s5l8900_add_stub() refuses a window that would land inside
 * RAM. With that invariant held, "device wins" and "RAM wins" are the same
 * routing, so bus_read()/bus_write() are free to keep RAM on the fast path —
 * and a silent alias is not a bug that can be reintroduced, it is a machine
 * that cannot be constructed.
 *
 * The alternative contracts were considered and rejected:
 *
 *   - Let devices win at access time. The guest is still told through the
 *     device tree that it owns a contiguous DRAM bank, so the kernel would
 *     allocate pages inside the device window and quietly corrupt itself. It
 *     also costs a linear window scan on every RAM access.
 *   - Clamp RAM to the aperture. bootkernel publishes mem_size in boot_args and
 *     in /memory:reg from its OWN variable, so a clamp inside the machine just
 *     moves the lie: the guest would use DRAM the machine does not have.
 *
 * WHAT THIS DOES NOT CLAIM. A DRAM window larger than the SoC's real aperture
 * necessarily covers physical regions the S5L8900 has (edram, vrom, SRAM — see
 * the memory-map block at the top of this header). We do not model those, so we
 * do not decode them and nothing is shadowed. The day one of them becomes a
 * device model it joins this list, and an oversized -R stops constructing.
 */
typedef struct {
    uint32_t    base, size;
    const char *name;                 /* string literal; never owned */
} s5l_window_t;

/* Enough for every fixed device window plus every stub. The 22 is the length of
 * DEVICE_WINDOWS in core/src/soc/machine.c — nor, clcd, the three tv-out banks,
 * i2c0, i2c1, i2s0, i2s1, spi0, spi1, usb-otg, vic0, vic1, power, gpioic, gpio,
 * uart0, uart4, timer, dmac0, dmac1 — and there is no slack left in it, so a
 * new device model has to raise this number too. It was 13 until spi0 and spi1
 * stopped being stubs, 15 until the two halves of /arm-io/gpio did, 17 until
 * uart4 was decoded, 20 until the two I2S windows were, and 22 with the two
 * PL080 DMA controllers. */
#define S5L_WINDOW_MAX (S5L_STUB_MAX + 22u)

/*
 * Every window this machine decodes: the modelled devices first, then the
 * declared stubs. Writes at most `max` entries and returns how many windows
 * exist (which may exceed `max`, so a short buffer is detectable).
 */
unsigned s5l8900_windows(const s5l8900_t *m, s5l_window_t *out, unsigned max);

/*
 * The first device window a RAM aperture of [ram_base, ram_base+ram_size) would
 * shadow, or NULL if there is none. Pure: it depends only on the fixed device
 * map, so a caller can ask BEFORE building a machine (or before choosing -R).
 * The returned pointer is into a static table and outlives any machine.
 *
 * Stub windows are not consulted here because no stub exists before init; stubs
 * are checked against RAM as they are declared, by s5l8900_add_stub().
 */
const s5l_window_t *s5l8900_ram_conflict(uint32_t ram_base, uint32_t ram_size);

/*
 * Physical regions the S5L8900 itself decodes, as confirmed from the shipped
 * device tree — INCLUDING the ones we do not model. Sets `*out` to a static
 * table and returns its length. This exists so "does this RAM size cover
 * something real?" is a question the core can answer with evidence, instead of
 * a warning hardcoded in a tool.
 */
unsigned s5l8900_soc_regions(const s5l_window_t **out);

/* True if [a, a+alen) and [b, b+blen) intersect. Zero-length ranges never do.
 * 64-bit inside, so a range near the top of the space cannot wrap into a
 * false negative. */
bool s5l8900_overlaps(uint32_t a, uint32_t alen, uint32_t b, uint32_t blen);

/*
 * Advance devices by elapsed guest CPU-clock ticks and refresh the CPU's
 * interrupt lines.  This does not retire CPU instructions.
 *
 * `ticks == 0` is not a no-op: it means "re-derive every level NOW, without
 * advancing guest time". That is what machine_wait_for_interrupt() has always
 * used it for, and it is also the call a HOST must make after reaching into
 * the machine's devices behind the bus — s5l_buttons_set(),
 * s5l_mtz2_set_contacts(), s5l_uart_rx_push(). See `level_dirty` in s5l8900_t
 * for why a refresh has to be asked for rather than assumed.
 */
void s5l8900_tick(s5l8900_t *m, uint32_t ticks);

/*
 * Wake a PMU-standby machine through the retained-RAM reset path without
 * manufacturing a host button transition.  Snapshot restore uses this after
 * loading a powered-down guest: the saved PMU state supplies the wake context,
 * while the absence of a physical press means no held GPIO wire or deferred
 * release may be added to the restored machine.  Returns false for NULL or a
 * machine that is not in standby.
 */
bool s5l8900_wake_from_standby(s5l8900_t *m);

/*
 * Inject one host button transition through the complete machine, including
 * the PCF50635's separate Power wake path. Ordinary running transitions use
 * the GPIO button model above and refresh interrupt levels before returning.
 *
 * When the guest has commanded PMU standby, only a Power press is a wake
 * source. It latches the PMU ONKEY rising event and resets the ARM core into
 * the retained-RAM reset vector prepared by XNU. AppleM68Buttons turns that
 * PMU reason into the Power press itself, so the GPIO controller consumes the
 * same wake edge while retaining the held wire and arms its auto-flipped
 * polarity for the later release. Other button transitions are consumed
 * without reaching the powered-down application processor, so one stale Home
 * event cannot permanently block a later Power event in a FIFO.
 */
bool s5l8900_set_button(s5l8900_t *m, unsigned which, bool pressed);
bool s5l8900_set_touch(s5l8900_t *m, const s5l_mt_contact_t *c,
                       unsigned n);

/* ---------------------------------------------------------- wake sources ---
 *
 * How far a WFI may fast-forward, expressed as DATA rather than as a list of
 * devices baked into the wait itself.
 *
 * A core in WFI retires nothing, so the machine skips guest time to the first
 * moment an interrupt can arrive instead of spinning the host. That skip is an
 * optimisation, but it is one that decides what the guest can observe: a
 * source the wait does not know about is a source that cannot wake the CPU,
 * however correctly the device asserts its line, because time steps straight
 * over the tick it fired on. An idle SpringBoard sits in WFI, which is exactly
 * the state a touch, a GPIO edge or a USB event has to interrupt.
 *
 * So every modelled device that can raise an interrupt declares itself in the
 * table in core/src/soc/machine.c, and the wait takes the minimum. Adding a
 * device is a table entry, not an edit to the wait.
 *
 * The three-way answer is deliberately asymmetric, because the two ways of
 * being wrong are not equally bad. Waking too early only costs host time;
 * waking too late is a lost interrupt. S5L_WAKE_NEVER is therefore the ONLY
 * reply that lets guest time be skipped past a source, and it means "this
 * device cannot fire from the state it is in" — stopped, masked, not scanning.
 * A source that merely finds the distance hard to work out must answer
 * S5L_WAKE_UNKNOWN, and the machine then does not fast-forward at all.
 */
typedef enum {
    S5L_WAKE_NEVER = 0,  /* cannot fire from this state; safe to sleep past   */
    S5L_WAKE_AT,         /* fires in *ticks timebase ticks; *ticks is >= 1    */
    S5L_WAKE_UNKNOWN     /* cannot say — the machine must not sleep past it   */
} s5l_wake_kind_t;

typedef struct {
    const char *name;    /* string literal; never owned                       */
    /*
     * The flat device-tree interrupt number this source drives: 0-31 on VIC0,
     * 32-63 on VIC1 (see the VIC1 note above). A source whose line is not
     * enabled in its VIC cannot reach the CPU at all, so the wait skips it
     * without asking — which is why `next_edge` need not re-check the VIC.
     */
    unsigned    line;
    /* Distance to this source's next interrupt edge, in TIMEBASE ticks.
     * Called only when `line` is enabled. Must not mutate the machine. */
    s5l_wake_kind_t (*next_edge)(const s5l8900_t *m, uint32_t *ticks);
} s5l_wake_source_t;

/*
 * The machine's wake sources. Sets `*out` to a static table and returns its
 * length, exactly as s5l8900_soc_regions() does for physical regions.
 */
unsigned s5l8900_wake_sources(const s5l_wake_source_t **out);

/*
 * The earliest edge among `n` sources, in timebase ticks.
 *
 * Returns S5L_WAKE_AT and sets `*ticks` when every enabled source could say
 * where it stands and at least one has a future edge; S5L_WAKE_NEVER when the
 * enabled ones all decline (nothing to wait for, so nothing may be skipped
 * either); S5L_WAKE_UNKNOWN if any enabled source could not answer — including
 * one that answers S5L_WAKE_AT with a distance of zero, which is not a
 * statement about the future. `*ticks` is untouched unless S5L_WAKE_AT.
 *
 * Pure with respect to the machine. Exposed so the reduction can be tested
 * against sources the machine does not (yet) have.
 */
s5l_wake_kind_t s5l8900_next_wake(const s5l8900_t *m,
                                  const s5l_wake_source_t *src, unsigned n,
                                  uint32_t *ticks);

/*
 * Install or clear interactive host pacing for autonomous WFI time. `sleep ==
 * NULL` clears it and requires ctx == NULL. Installing or clearing resets its
 * host-only evidence counters. This does not pace active CPU execution and it
 * does not alter the deterministic fast-forward used when no callback exists.
 */
bool s5l8900_set_wfi_host_pacing(s5l8900_t *m,
                                 s5l_wfi_host_sleep_fn sleep, void *ctx);

/*
 * Install or clear wall-clock timing for active CPU execution. `now == NULL`
 * clears it and requires ctx == NULL. Installing or clearing resets all
 * host-only evidence and transient anchor state. A successful callback makes
 * elapsed monotonic host time, not retired-instruction throughput, drive the
 * guest CPU/timebase ratio. Callback failure falls back to the historical
 * instruction clock for that bounded run call.
 */
bool s5l8900_set_active_host_clock(s5l8900_t *m,
                                   s5l_active_host_now_fn now, void *ctx);

/* Optional foreground policy: retain unpaid host time, paying <=8ms per sync.
 * CPU rates below 125Hz are refused; changing policy clears pending debt.
 * Pause/restore/failure clear debt. Native default remains discard-after-cap. */
bool s5l8900_set_active_clock_carry(s5l8900_t *m, bool enabled);

/*
 * Attach or detach uart4's host peer. Attaching requires both callbacks; the
 * context may be NULL. Passing all three as NULL detaches. A partial request
 * is rejected without changing the live attachment.
 */
bool s5l8900_set_uart4_host(s5l8900_t *m, s5l_uart4_host_tx_fn tx,
                            s5l_uart4_host_service_fn service, void *ctx);
bool s5l8900_set_uart0_host(s5l8900_t *m, s5l_uart4_host_tx_fn tx,
                            void *ctx);

/*
 * ram_base/ram_size define where RAM appears. Returns false on allocation
 * failure, or if the RAM aperture would shadow a device window — see the
 * routing contract above and s5l8900_ram_conflict(), which a caller can use to
 * find out WHICH window before (or instead of) calling this. Call
 * s5l8900_free() when done.
 */
bool s5l8900_init_soc(s5l8900_t *m, uint32_t ram_base, uint32_t ram_size);
bool s5l8900_init(s5l8900_t *m, uint32_t ram_base, uint32_t ram_size);
void s5l8900_free(s5l8900_t *m);

/*
 * Set the average ARM1176 cycles per retired instruction. Guest wall clock
 * (the 6 MHz timebase) is unchanged; only the instructions per guest second
 * change. The default is 1. A changed rate is refused while foreground carry
 * and its host callback are live; Pause/clear the callback before changing it.
 */
bool s5l8900_set_cycles_per_insn(s5l8900_t *m, unsigned cpi);

/* Same-binary control for callback-free plain-RAM CPU stores. Enabling succeeds
 * only while the machine still owns its canonical, uninterposed write bus.
 * Disabling is always safe and invalidates every derived write pointer. */
bool s5l8900_set_direct_ram_writes(s5l8900_t *m, bool enabled);

/*
 * Install or clear an exact-PC host replacement hook.  `fn == NULL` clears
 * the hook and requires targets == NULL/count == 0.  Targets must be distinct
 * halfword-aligned guest addresses.  Changing the table invalidates only
 * derived signed decode/graph state so a previously cached block can never
 * run across a newly registered boundary.
 */
bool s5l8900_set_pre_step_hook(s5l8900_t *m, s5l_pre_step_fn fn, void *ctx,
                               const uint32_t *targets, unsigned count);
bool s5l8900_pre_step_target(const s5l8900_t *m, uint32_t pc);
uint64_t s5l8900_pre_step_matches(const s5l8900_t *m);
uint64_t s5l8900_pre_step_handled(const s5l8900_t *m);

/* Copy a blob into guest RAM at a physical address. */
void s5l8900_load(s5l8900_t *m, uint32_t addr, const void *data, size_t len);

/* Optional signed-static AArch64 engine. It uses ordinary executable text
 * produced at build time: no runtime code generation and no writable/executable
 * pages. The feature is compile-time OFF by default and enabling it can fail
 * when the build or host architecture does not provide the signed handlers.
 * `m` must be an initialised machine. The retired and chained-block counts are
 * host diagnostics, not guest state and not part of a snapshot. */
bool s5l8900_static_a64_available(void);
bool s5l8900_static_a64_set_enabled(s5l8900_t *m, bool enabled);
/* Experimental same-binary control for the persistent native-context path.
 * It can be enabled only after the signed engine itself is live. */
bool s5l8900_static_a64_set_persistent(s5l8900_t *m, bool enabled);
/* Experimental callback-free descriptor lookup; mutually exclusive with the
 * callback scaffold above and still compile-time/app-default off. */
bool s5l8900_static_a64_set_graph(s5l8900_t *m, bool enabled);
/* Default-off compact live-byte mode. It consumes only the current proven
 * 1 KiB fetch window, supports MMU-on code, and admits only instruction shapes
 * whose complete effect is implemented by the build-time-signed runner. It is
 * mutually exclusive with the decoded persistent/graph paths. */
bool s5l8900_static_a64_set_compact_raw(s5l8900_t *m, bool enabled);
/* Same-binary control for native prefixes entered while the ARM core is in an
 * implemented privileged mode. Product default is enabled. Privileged control
 * instructions remain interpreter-only: the resident callback refuses them,
 * commits any already-retired native prefix, and returns to the machine loop
 * before arm_step() executes the boundary. */
bool s5l8900_static_a64_set_compact_raw_privileged(s5l8900_t *m,
                                                   bool enabled);
/* Same-binary control for continuing a resident compact interval at an
 * unchanged PC in another 1 KiB code window. User-mode continuation defaults
 * on and reuses an exact live FETCH witness directly. This switch is also the
 * outer safety gate for the separately controlled privileged continuation.
 * Neither route walks, faults, touches MMIO or retires the unchanged
 * instruction through the callback. */
bool s5l8900_static_a64_set_compact_raw_window_refill(s5l8900_t *m,
                                                       bool enabled);
/* Privileged continuation is retained as a measured efficiency experiment but
 * defaults off: three exact physical-A9 Settings pairs reduced engine entries
 * while regressing displayed cadence. When explicitly enabled, it first
 * accounts the completed prefix through the normal machine/device boundary,
 * rechecks interrupts and translation state, and only then publishes another
 * exact FETCH witness. The generic window-refill switch above must also be on. */
bool s5l8900_static_a64_set_compact_raw_privileged_window_refill(
    s5l8900_t *m, bool enabled);
/* Opt-in same-binary experiment for repeated User-mode window transitions.
 * Up to eight full FETCH witnesses proved inside one compact invocation may
 * be selected directly by the build-time-linked runner. The cache is cleared
 * on every invocation, never crosses privilege or translation generation,
 * and defaults off until physical cadence evidence justifies rollout. */
bool s5l8900_static_a64_set_compact_raw_window_cache(s5l8900_t *m,
                                                      bool enabled);
/* Explicit diagnostic-only sampling for the compact runner. On a supported
 * Apple AArch64 host a marker-created sampler polls only the pthread executing
 * s5l8900_run(), and retains a PC only when that target is running before and
 * after state capture. Ordinary machines pay one disabled gate per public run
 * slice, never per guest instruction; they create no sampler thread. It emits
 * no runtime code and is never guest snapshot state. */
bool s5l8900_static_a64_enable_compact_raw_pc_profile(s5l8900_t *m);

typedef enum {
    S5L_STATIC_A64_COMPACT_PC_ENTRY = 0,
    S5L_STATIC_A64_COMPACT_PC_DP,
    S5L_STATIC_A64_COMPACT_PC_MEMORY,
    S5L_STATIC_A64_COMPACT_PC_BLOCK_CONTROL,
    S5L_STATIC_A64_COMPACT_PC_SYSTEM,
    S5L_STATIC_A64_COMPACT_PC_VFP,
    S5L_STATIC_A64_COMPACT_PC_THUMB_DECODE,
    S5L_STATIC_A64_COMPACT_PC_THUMB_LOW_ALU,
    S5L_STATIC_A64_COMPACT_PC_THUMB_ALU_HIGH,
    S5L_STATIC_A64_COMPACT_PC_THUMB_MEMORY_FORM,
    S5L_STATIC_A64_COMPACT_PC_THUMB_MISC,
    S5L_STATIC_A64_COMPACT_PC_THUMB_BRANCH,
    S5L_STATIC_A64_COMPACT_PC_THUMB_MEMORY_ACCESS,
    S5L_STATIC_A64_COMPACT_PC_THUMB_CONDITION,
    S5L_STATIC_A64_COMPACT_PC_A32_CONDITION,
    S5L_STATIC_A64_COMPACT_PC_RETIRE,
    S5L_STATIC_A64_COMPACT_PC_FALLBACK,
    S5L_STATIC_A64_COMPACT_PC_EXIT,
    S5L_STATIC_A64_COMPACT_PC_REGION_COUNT
} s5l_static_a64_compact_pc_region_t;

#define S5L_STATIC_A64_COMPACT_PC_HOT_COUNT 8u

typedef struct {
    uint64_t pc;
    uint64_t samples;
} s5l_static_a64_compact_pc_hot_t;

typedef struct {
    bool enabled;
    uint64_t polls;
    uint64_t not_running;
    uint64_t state_failures;
    uint64_t target_races;
    uint64_t samples;
    uint64_t outside;
    uint64_t region[S5L_STATIC_A64_COMPACT_PC_REGION_COUNT];
    uint64_t reference_pc;
    uint64_t outside_pc_captured;
    uint64_t outside_pc_dropped;
    s5l_static_a64_compact_pc_hot_t
        outside_hot[S5L_STATIC_A64_COMPACT_PC_HOT_COUNT];
} s5l_static_a64_compact_pc_profile_t;

void s5l8900_static_a64_compact_raw_pc_profile(
    const s5l8900_t *m, s5l_static_a64_compact_pc_profile_t *out);
/* Same-binary rollout/benchmark control for terminal A32/Thumb BX/BLX signed
 * records. It defaults on when the engine is created. Changing it clears only
 * derived decode/graph entries so an off/on comparison cannot reuse a block
 * admitted under the other feature set. */
bool s5l8900_static_a64_set_indirect_branches(s5l8900_t *m, bool enabled);
/* Same-binary rollout/benchmark control for terminal Thumb conditional B.
 * All fourteen valid conditions default on; changing the switch clears only
 * derived decode/graph state and leaves guest state and counters untouched. */
bool s5l8900_static_a64_set_thumb_conditional_branches(s5l8900_t *m,
                                                       bool enabled);
/* Same-binary rollout/benchmark control for signed VSTR S/D records. It
 * defaults on and invalidates only derived decode/graph state when changed. */
bool s5l8900_static_a64_set_vstr(s5l8900_t *m, bool enabled);
/* Same-binary rollout/benchmark control for transactional ordinary A32 STM
 * records. It defaults on and clears only derived decode/graph state. */
bool s5l8900_static_a64_set_stm(s5l8900_t *m, bool enabled);
/* Same-binary rollout/benchmark control for transactional aligned one-block
 * ordinary A32 LDM records without PC or user-bank transfers. */
bool s5l8900_static_a64_set_ldm(s5l8900_t *m, bool enabled);
/* Same-binary rollout/benchmark control for transactional one-block A32 VSTM
 * records. Deprecated FSTMX and cross-block transfers stay literal. */
bool s5l8900_static_a64_set_vstm(s5l8900_t *m, bool enabled);
/* Same-binary rollout/benchmark control for guarded VFPv2 scalar arithmetic.
 * The signed handlers commit only the exact RunFast/simple-value contract and
 * return every other case to arm_step(). */
bool s5l8900_static_a64_set_vfp_arithmetic(s5l8900_t *m, bool enabled);

/* Same-binary benchmark control for lazy host FPCR/FPSR preservation across
 * adjacent guarded arithmetic handlers. Product default is enabled. Disabling
 * it retains exact semantics but restores host FP state after every operation. */
bool s5l8900_static_a64_set_vfp_fp_session(s5l8900_t *m, bool enabled);
/* Same-binary control for rebuilding a missing instruction-fetch host pointer
 * from an exact live software-TLB witness. Product default is enabled with an
 * adaptive admission policy: a call that has never retired more than one
 * instruction is periodically re-probed instead of paying the refill on every
 * visit. The helper never walks, faults or reads MMIO; a refusal leaves
 * arm_step() as the sole owner of the architectural fetch. */
bool s5l8900_static_a64_set_fetch_refill(s5l8900_t *m, bool enabled);
/* Exact execution-policy control for eliding the otherwise immediate second
 * signed-engine probe after a positive chain stops at an unchanged cached
 * unsupported instruction. Product default is enabled. The device tick,
 * machine gate and complete negative byte/fetch witness are rechecked before
 * one ordinary arm_step(); no architectural state or snapshot field is added. */
bool s5l8900_static_a64_set_known_negative_bypass(s5l8900_t *m,
                                                   bool enabled);
/* Host-policy changes such as a new pre-step boundary call this to discard
 * only derivable entries; architectural state and diagnostic totals remain. */
void s5l8900_static_a64_invalidate_derived(s5l8900_t *m);
/* Same-binary benchmark control for the total signed invocation bound. A
 * decoded head remains at most sixteen instructions and the machine still
 * clamps every invocation to the first exact timebase edge. The generic engine
 * initializes at sixteen; the gated iOS product explicitly selects 256.
 * Accepted values are 1..256. */
bool s5l8900_static_a64_set_chain_limit(s5l8900_t *m, unsigned max_insns);
uint64_t s5l8900_static_a64_retired(const s5l8900_t *m);
uint64_t s5l8900_static_a64_chained_blocks(const s5l8900_t *m);
uint64_t s5l8900_static_a64_persistent_chained_blocks(const s5l8900_t *m);
uint64_t s5l8900_static_a64_graph_chained_blocks(const s5l8900_t *m);
uint64_t s5l8900_static_a64_compact_raw_attempts(const s5l8900_t *m);
uint64_t s5l8900_static_a64_compact_raw_calls(const s5l8900_t *m);
uint64_t s5l8900_static_a64_compact_raw_retired(const s5l8900_t *m);
uint64_t s5l8900_static_a64_compact_raw_fallback_retired(
    const s5l8900_t *m);
uint64_t s5l8900_static_a64_compact_raw_privileged_attempts(
    const s5l8900_t *m);
uint64_t s5l8900_static_a64_compact_raw_privileged_calls(
    const s5l8900_t *m);
uint64_t s5l8900_static_a64_compact_raw_privileged_retired(
    const s5l8900_t *m);
uint64_t s5l8900_static_a64_compact_raw_window_crossings(
    const s5l8900_t *m);
uint64_t s5l8900_static_a64_compact_raw_window_reloads(
    const s5l8900_t *m);
uint64_t s5l8900_static_a64_compact_raw_window_stops(
    const s5l8900_t *m);
uint64_t s5l8900_static_a64_compact_raw_window_fast_refills(
    const s5l8900_t *m);
uint64_t s5l8900_static_a64_compact_raw_window_cache_hits(
    const s5l8900_t *m);
uint64_t s5l8900_static_a64_compact_raw_privileged_window_refills(
    const s5l8900_t *m);
uint64_t s5l8900_static_a64_compact_raw_privileged_boundary_retired(
    const s5l8900_t *m);
/* Mutually exclusive reasons why one compact-raw machine-loop attempt did
 * not become a positive invocation.  Together with calls, these counters
 * partition attempts exactly.  They are host diagnostics only and are never
 * part of a guest snapshot. */
typedef struct {
    uint64_t guard;
    uint64_t privileged;
    uint64_t alignment;
    uint64_t fetch_witness;
    uint64_t runner;
    uint64_t zero_retired;
} s5l_static_a64_compact_raw_refusals_t;
void s5l8900_static_a64_compact_raw_refusals(
    const s5l8900_t *m, s5l_static_a64_compact_raw_refusals_t *out);
uint64_t s5l8900_static_a64_fetch_refill_attempts(const s5l8900_t *m);
uint64_t s5l8900_static_a64_fetch_refill_hits(const s5l8900_t *m);
uint64_t s5l8900_static_a64_fetch_refill_skips(const s5l8900_t *m);
uint64_t s5l8900_static_a64_known_negative_bypasses(const s5l8900_t *m);
/* Host diagnostic: byte span protecting the currently cached signed head at
 * `pc`, or zero when no such entry exists. It is derived cache state and is
 * never part of a guest snapshot. */
unsigned s5l8900_static_a64_cached_witness_bytes(const s5l8900_t *m,
                                                 uint32_t pc, bool thumb);

/* Run up to max_steps instructions, stopping early on a non-OK status.
 * Returns the number of instructions retired. */
unsigned s5l8900_run(s5l8900_t *m, unsigned max_steps, arm_status_t *status);
/* Host diagnostics for User-mode interpreter intervals whose per-instruction
 * device ticks were collapsed into one exact timebase-bounded tick. Never part
 * of guest state or a snapshot. */
uint64_t s5l8900_interpreter_tick_batches(const s5l8900_t *m);
uint64_t s5l8900_interpreter_tick_batched_retired(const s5l8900_t *m);

#endif /* S5LBOX_SOC_H */
