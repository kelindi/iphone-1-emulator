# A verified restart point for the original Wi-Fi driver

## Tweet idea

Our original iPhone Wi-Fi experiment can now resume from a verified checkpoint instead of repeating its long firmware upload. A fresh reload followed the original phone through nearly 42 million instructions and 1,000 real driver polls. Networking is still unfinished; the next experiment now has a trustworthy starting point.

## What you're seeing

This is a nonvisual device-development milestone. The [genuine cold-boot trace](../docs/traces/sdio-checkpoint/v2/actual/full-mmio.log) ends at `full main container handoff request consumed`: all 504 transfers complete, with the original driver at `0xc0379684`, instruction 17,517,070,017. Both original-file audits verify 128,704 wire bytes,117,932 copied bytes, exact write masks and untouched guards. [Gate and byte evidence](../docs/traces/sdio-checkpoint/v2/actual/README.md).

The original machine stays alive while a fresh M68 loads the private checkpoint. Initial machine and sidecar bytes match exactly. They then execute 41,947,204 instructions and 4,581,748 matching SDIO transactions, reaching the original 1,000-poll failure at `0xc038a8ec`. The [continuation result](../docs/traces/sdio-checkpoint/v2/actual/full-continuation-comparison.json), [independent parser](../docs/traces/sdio-checkpoint/v2/actual/continuation-independent-audit.json) and [separate root review](../docs/traces/sdio-checkpoint/v2/actual/root-review.json) verify the still-unready status 8 rather than manufacturing a successful firmware response.

Only then does the manifest-last publisher commit the private package and load it again through actual hash, identity and C canonical validation. [Publication and reload](../docs/traces/sdio-checkpoint/v2/actual/publication-report.json). The 785-second boot stayed below its 900-second cap; this is a bounded correctness result, not a speed comparison or browser recording.

## ELI5 technical explanation

A checkpoint needs more than the phone's RAM. It must retain unfinished device work, pending interrupts and the bytes a controller is still holding. Our first attempt exposed a real omission: standard restore cleared part of the touch controller's receive state and did not save its receive buffer or uptime. That failed pair stays unpublished; we did not guess its missing bytes. [Preserved failure](../docs/traces/sdio-checkpoint/attempt-1/canonical-failure.json).

The new private version explicitly records all 106 SDIO and 118 non-host touch-controller fields, keeps host callbacks separate, validates every field and requires exact full-file roundtrip before admitting a fresh phone. It also preserves native contact padding already present in the standard snapshot without adding padding to the portable device record. 3,987 focused touch-state checks,36,578 lifecycle checks and 65 real-C package checks pass; 1,012,343 unchanged earlier SDIO contracts remain retained evidence. Deliberately omitted receive/uptime state and the older padding decoder fail their negative controls. [Implementation and tests](../docs/traces/sdio-checkpoint/v2/README.md).

During actual continuation, every instruction's PC/time/SDIO IRQ agrees, every SDIO transaction agrees, and complete saved architectural/device state agrees at five prefixes. Reload intentionally rebuilds translation caches, so only three independently checked cache diagnostic counters and their verified checksum may differ after execution. The original running machine and raw files are never normalized. This exception does not cover device, register or memory differences. [Exact five comparison rows](../docs/traces/sdio-checkpoint/v2/actual/full-continuation-cache-counters.jsonl).

## Why it matters

The next bounded Wi-Fi protocol investigation can start after the proven upload instead of repeating 17.5 billion instructions. The package remains a private development tool: no production snapshot format, browser feature or user profile changes. It does not execute the downloaded card program, advertise firmware readiness, create a network interface or provide connectivity. Those remain separate original-driver protocol gates. [Networking plan](../plans/networking.md#verified-private-checkpoint-and-original-driver-continuation).
