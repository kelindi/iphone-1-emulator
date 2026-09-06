# The first Wi-Fi helper frame completes through Apple's driver

## Tweet idea

The original iPhone's Wi-Fi driver has now completed its first helper-frame transfer in our isolated SDIO model. Every received byte matches the original helper, and Apple's real interrupt handler acknowledges the result. One transfer works; loaded firmware and networking are still ahead.

## What you're seeing

This is a nonvisual device/driver milestone. The [fresh cold-boot trace](../docs/traces/sdio-data/candidate-mmio.log) records the actual sequence:

1. The first CMD53 request receives its real modeled response. The driver then writes **DCTRL=0x10** at guest tick **5,314,820**, starting the data engine. Whole-range lookup validates its physical **64-byte** source buffer; the endpoint has received zero bytes at this point.
2. A separate deadline is due at tick **5,315,420**. The probe services it at **5,315,426**, six device ticks later, atomically receiving all 64 bytes and asserting the original **IRQ42** route. This is guest-time sequencing, not a browser speed measurement.
3. Apple's actual IRQ handler at **`0xc0432108`** reads and acknowledges IRQ bit 0. It calls the genuine DMA completion method, which reads data status **0x21**, acknowledges it, and restores interrupt mask 3. At instruction **352,602,757**, the original caller **`0xc0379684`** receives **R0=1**. The probe stops after that return.

The [run report](../docs/traces/sdio-data/candidate-report.json), [data report](../docs/traces/sdio-data/data-report.json) and [independent verifier](../docs/traces/sdio-data/verification.json) require **38 command responses, exactly one source lookup/start/copy, 64 received bytes, acknowledged IRQ/status, and zero card, transport, MMIO or data errors**. The complete source and received frames both match the original helper frame SHA-256 **`90f8e97b50ecbbc45c68f4fa07379ea46d02e157aa2bd0ba86aa5f3d0a72057c`**. [Original helper provenance](../docs/traces/sdio-data/helper-provenance.json).

After independent review, **168,062 contracts pass**, retaining the prior **114,312** and adding **53,750** checks. [Contract result](../docs/traces/sdio-data/contracts.log). This includes invalid mapping/length/count/state, no early or duplicate copy, reset and rollback cancellation, finite capacity and independent acknowledgement.

Independent review found that cancelling an in-flight transfer also erased an already completed controller event. Cancellation now preserves the latched status/IRQ across reader rebinding, card power/disable/reset and time rollback; explicit controller/FIFO reset and the guest's separate acknowledgements can clear them. The [old candidate fails 30 new assertions](../docs/traces/sdio-data/latch-review-negative.log); the corrected candidate passes all earlier 167,415 checks plus 647 new checks. A fresh corrected cold boot produces a **byte-identical full MMIO trace, run report and data report**. The [first-run source audit](../docs/traces/sdio-data/before-latch-review-verification.json) remains preserved alongside the corrected source hashes.

## ELI5 technical explanation

Preparing a packet, sending it, and notifying the driver are different hardware steps. The previous milestone stopped after preparation. The new opt-in model now answers the narrowly validated command, waits for Apple's actual data-start write, checks the entire source range, and stages the bytes. It copies into the card's real bounded storage only after a separate deadline and a second state/capacity check.

The notification follows [Apple's original IRQ method](../docs/traces/sdio-data/original-irq-dis.txt) and [completion call path](../docs/traces/sdio-data/original-completion-dis.txt). IRQ bit 0 leads to command completion and the genuine DMA method before waking its waiter. The data-status encoding also agrees with the independent [openiBoot controller implementation](https://raw.githubusercontent.com/planetbeing/iphonelinux/master/openiboot/sdio.c). No CPU jump, fabricated helper execution or firmware-ready flag substitutes for that path.

The endpoint is now full with the first frame. It neither drains bytes nor declares the helper loaded. The private device-tree policy remains the previously proven missing-calibration branch and emulated local MAC **02:00:00:00:01:01**; original kernel, DT, work disk and native archive hashes are unchanged. All implementation remains isolated, with production machine and snapshot layouts untouched.

## Why it matters

We can now prove a complete first-frame round trip through the genuine driver: source bytes, received bytes, interrupt, acknowledgement and completion return. The remaining helper stream, termination/execution handshake, main firmware, operational network interface and packets need separate gates. Production networking remains unavailable. [Networking plan](../plans/networking.md#isolated-first-helper-frame-completion).
