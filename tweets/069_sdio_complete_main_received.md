# The complete original Wi-Fi main program reaches card memory

## Tweet idea

The original iPhone Wi-Fi driver now sends its entire main-program container into our isolated card model. All 504 transfers are acknowledged, every copied byte is verified, and the final handoff request is recorded. The next boundary stays honest: no downloaded code runs and no firmware-ready flag is fabricated.

## What you're seeing

This is a nonvisual original-driver milestone. The [full cold-boot trace](../docs/traces/sdio-full-main/full-mmio.log) follows the received helper's supported semantic protocol through **234 headers and 233 bodies**, ending with command 4 and entry address zero. The last `genuine completeDMA consumed return` records successful return **504**, with IRQ/status acknowledged. The next trace section, `full main container handoff request consumed`, stops at **`0xc0379684`**, instruction **17,517,070,017**. [Report](../docs/traces/sdio-full-main/full-report.json).

The [combined audit](../docs/traces/sdio-full-main/verification.json) and [separate root-agent audit](../docs/traces/sdio-full-main/root-byte-audit.json) verify:

- **128,704 actual wire bytes**, including the earlier helper and **126,368 main-transfer bytes**. Actual [mapped source](../docs/traces/sdio-full-main/full-source-wire.bin) and [request buffers](../docs/traces/sdio-full-main/full-request-wire.bin) match the original Mach-O-derived prediction, including reused padding.
- **117,932 copied bytes** in two memory banks, exact byte-written coverage, **852 untouched mapped bytes**, and **64 outside canary bytes**. Both audits check the entire banks, including zero-valued payload bytes.
- **3,414 command responses**, **504 sequential IRQ/status/DMA acknowledgements**, and **467 coherent host requested-length pairs**. The [final physical handoff frame](../docs/traces/sdio-full-main/full-handoff-frame.bin) is retained. The host has not yet consumed a zero request or returned from its overall loader.

All **1,012,343 contracts pass**, retaining 549,969 earlier checks and adding 462,374. [Contract log](../docs/traces/sdio-full-main/contracts.log). The new build also reproduces milestone 066's [first-payload stop](../docs/traces/sdio-full-main/positive-report.json) and the disabled-model [100-zero-poll failure](../docs/traces/sdio-full-main/main-negative-report.json) at their exact original instruction counts.

The run finished in **780 wall seconds**, below its fixed 22-billion-instruction/900-second cap. Untimed functional browser checks ran concurrently. This is a bounded correctness probe, not a speed result. [Budget](../docs/traces/sdio-full-main/full-budget.json).

## ELI5 technical explanation

The main file contains instructions about where each piece belongs. The original helper requests a header, then the body length written in that header, and copies the body minus four trailing bytes into the named memory address. Our high-level model follows that reached original behavior using actual received bytes; it does not execute a helper CPU or inject a known firmware image.

The data spans two bounded memory banks. Their ranges exclude the relocated helper's code, callback tables and receive buffer. The old received-helper image remains a compatibility/provenance record, not a claim that those bytes still occupy address zero after the main image overwrites low memory.

The host rounds transfers to 32-byte blocks while overwriting only the logical request length. Most bodies are 512 bytes; one is 504 and another is 88, producing 512- and 96-byte physical transfers with eight old buffer bytes left in their padding. Header padding also retains earlier body bytes. Independent oracles reconstruct that reuse rather than assuming padding is zero. No CRC algorithm is inferred from the header's fourth word or the four body bytes skipped by the reached software callback.

Completed card bytes remain separate from unfinished host DMA, preserving the reviewed reset, cancellation and acknowledgement rules. Requested-length bytes remain independent samples; actual full-run pairs must match the original container sequence. The initial verifier incorrectly assumed every completion would first be observed at exactly its 600-tick deadline. **3,413 were observed at 600 ticks; one at 616.** The [failed assumption and exact event](../docs/traces/sdio-full-main/completion-observation-review.json) are preserved. The corrected audit rejects early observations and retains exact response/acknowledgement ordering; it does not claim a proven cause for the 16-tick delay. Device code and deadline tests were unchanged.

Command 4 records a handoff request and closes reception. The model neither branches to the downloaded program nor advertises firmware readiness to make the original loader succeed. All work remains isolated; no SDIO changes to production, Apple executables, firmware input files or snapshot layouts accompany this result. [Durable evidence and source](../docs/traces/sdio-full-main/README.md).

## Why it matters

The complete original program has now crossed the real host driver's transfer path, with exact memory and completion evidence. The next step can investigate a grounded model of the original host's firmware command and network protocol. Full card-CPU emulation is not assumed mandatory, but readiness must come from initialized, verified protocol state. This milestone does not create a working Wi-Fi interface or connectivity. [Networking plan](../plans/networking.md#isolated-complete-main-container-and-handoff-request).
