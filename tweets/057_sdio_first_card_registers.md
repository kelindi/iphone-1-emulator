# The original Wi-Fi driver reaches its first card registers

## Tweet idea

The original iPhone's Wi-Fi driver used to ask its missing controller “ready?” 100,001 times and give up. An isolated SDIO model now completes the real startup exchange and returns the first two card-register reads to Apple's unchanged driver. Wi-Fi itself is still ahead.

## What you're seeing

This is a backend command-trace milestone, with no visible UI change or screen-recording claim. The [negative cold-boot trace](../docs/traces/sdio-first/baseline-mmio.log) ends in repeated zero readiness, with the complete poll count in its [report](../docs/traces/sdio-first/baseline-report.json). The [successful cold-boot trace](../docs/traces/sdio-first/candidate-mmio.log) contains these exact sections:

- Instructions 304,815,847–304,857,028: CMD5 query, explicit execution, delayed completion, acknowledgement, and operating-condition response.
- Instructions 304,857,062–304,980,837: voltage negotiation, RCA assignment and card selection through CMD5/CMD3/CMD7.
- Instructions 304,980,889–305,063,372: CMD52 reads of function-0 addresses 0 and 8, including all four response words for each command.
- Final marker at PC `0xc03797dc`: the original CCCR initializer has regained control after the address-8 read returns; R0 is 1, indicating success. This stop occurs after the call returns, outside the MMIO callback.

[Positive report](../docs/traces/sdio-first/candidate-report.json) · [verified commands and unchanged input hashes](../docs/traces/sdio-first/verification.json). Repeated identical status reads are compressed in the text trace; report counters retain every read.

## ELI5 technical explanation

The Wi-Fi chip talks to the processor through a small controller. Before downloading Wi-Fi firmware, the operating system must discover a card, agree on voltage, assign it an address, select it and read its basic registers. Previously the controller's entire address range was missing, so the driver never got past its first readiness check.

The isolated model gives the controller and card separate state. Writing a command does not execute it: the genuine driver must set the execute bit. A command takes 600 emulated timebase ticks, or 100 microseconds at the unchanged 6 MHz timebase, before completion becomes visible. That delay is a declared model choice, not a measurement of the original chip. Completion remains latched until acknowledged; busy rewrites, unsupported transfers and invalid registers return errors. Polling alone cannot manufacture progress.

Apple's unmodified code now completes six commands and consumes both configured CCCR bytes. Standalone contracts pass **101,258 checks with zero failures**, including 100,001 untriggered polls and 1,000 busy polls at stationary guest time. The repeated original baseline still produces exactly **100,011 accesses and 100,001 zero status reads**. The positive boot has six responses, zero rejected writes or transport errors, and no fabricated interrupt.

The endpoint declares one I/O function, compatible voltage, RCA 1 and two conservative read-only CCCR bytes. It does not claim a Marvell identity, CIS layout, enabled data function, firmware success or a network interface. All non-SDIO bus accesses retain their original handlers; the kernel, input device tree and work disk hashes remain unchanged. Production machine structures and snapshot formats are untouched. See [the networking plan](../plans/networking.md#isolated-first-card-register-gate-passed) for the exact boundary and later integration requirements.

## Why it matters

This advances networking past a measured hardware failure through the original driver, rather than hiding it with an always-successful response. The next gate is proving card identity and observing the real function-enable and data-transfer requests. Browser networking, Safari HTTP, and old-service compatibility still require later work.
