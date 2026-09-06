# The original Wi-Fi driver sends its first main-program payload

## Tweet idea

The original iPhone Wi-Fi driver now follows a modeled version of its actual helper protocol: a 16-byte header, a 512-byte payload, then another header request. We verified every transported byte and all 508 bytes copied into card memory. No pretend firmware-ready flag—and no claim that Wi-Fi works yet.

## What you're seeing

This is a nonvisual original-driver milestone. The [positive cold-boot trace](../docs/traces/sdio-main/positive-mmio.log) records:

1. `modeled helper request published`: the complete, genuinely received 2,140-byte helper selects its explicitly supported semantic model. Its modeled initialization requests **16 bytes**. This is compatibility selection, not evidence that a helper CPU executed.
2. `genuine initial main request consumed`: Apple's unmodified main loader consumes that request. It sends a **32-byte physical transfer** containing the original 16-byte header and 16 preserved zero padding bytes.
3. The modeled helper parses the actual header and requests **512 bytes**. The original driver sends them, and the semantic callback copies the first **508 bytes** into modeled card memory, leaving the final four payload bytes uncopied.
4. `genuine next main request pair consumed`: after both original transfer calls return successfully, the host consumes the next **16-byte** request at **`0xc038a8c8`**, with source offset **528**. The stop is after the register-read results are committed, before the next header transfer. Instruction count: **1,741,652,858**. [Report](../docs/traces/sdio-main/positive-report.json).

The [independent verifier](../docs/traces/sdio-main/verification.json) checks **2,880 total wire bytes**, including the earlier helper stream; **39 original DMA return/IRQ/status acknowledgement chains**; **162 successful command responses**; the exact [544-byte main wire](../docs/traces/sdio-main/actual-main-wire.bin); and all [508 copied bytes plus four untouched memory bytes](../docs/traces/sdio-main/positive-card-ram.bin). Actual main-stage low/high requested-length reads produce **16 → 512 → 16**, with no mixed pair. The earlier pre-upload zero pair is separately checked.

A [separate root-agent audit](../docs/traces/sdio-main/root-byte-audit.json) extracts the expected bytes directly from the original Mach-O segments, independently of the model's fixtures and logic. It verifies the same complete wire, received helper, header padding, body, copied bytes, canaries and skipped `94 6e 44 e6` trailer. [Audit source](../docs/traces/sdio-main/root-byte-audit.py).

The [separate no-model control](../docs/traces/sdio-main/main-negative-mmio.log) still receives the full helper, then reaches the original failure at **`0xc038a788`** after exactly **100 zero requested-length polls**, instruction **1,678,065,205**. [Report](../docs/traces/sdio-main/main-negative-report.json). All **549,969 contracts pass**, preserving 310,197 earlier checks and adding 239,772. [Contract log](../docs/traces/sdio-main/contracts.log). These are correctness probes, not performance measurements.

## ELI5 technical explanation

The phone sends a small helper program to its Wi-Fi card before sending the main program. Receiving the helper bytes alone does not make the card run it. We decoded the original helper's relocation table and ARM/Thumb calls to understand the particular receive protocol it implements. [Original entry](../docs/traces/sdio-main/arm-entry-dis.txt), [receiver and callbacks](../docs/traces/sdio-main/thumb-receive-dis.txt), and [literal/source provenance](../docs/traces/sdio-main/source-provenance.json) preserve that evidence.

The isolated model implements that documented behavior using actual received buffers and bounded card memory. Header command 1 requests a body and copies its length minus four bytes. The reached SDIO callback ignores the header's fourth word and the body's last four bytes and returns zero; we do **not** invent a CRC algorithm from those fields. An unknown command produces requested-length error indication 17, which this Apple caller treats as an error and aborts. Command 4 would branch to downloaded code; that handoff is explicitly unsupported here.

Physical transfer completion and later header/body processing remain separate. Completed card bytes move into separate storage at the public endpoint-receive API, so a host reset or callback change cannot erase them—even when that API is called outside the DMA wrapper. The DMA path enqueues each frame once. Unfinished transfers cancel; true card reset clears card state. Those retention and processing-delay choices are declared emulation policy. The requested-length bytes are independent samples, with no invented atomic latch: deadline-boundary tests expose mixed values, and the bounded transfer checks prevent them from silently advancing the upload. That rejection is a limit of this narrow model, not a claim about how real hardware handles malformed host transfers. The actual positive trace must—and does—show coherent reads.

The model remains isolated. Original kernel, device-tree file, work disk and native archive hashes are unchanged; the previously reviewed private missing-calibration/local-MAC boot-copy policy remains explicit. No Apple executable, production machine layout, snapshot or browser package changes accompany this result. [Durable source and evidence](../docs/traces/sdio-main/README.md).

## Why it matters

We have crossed the first real main-program transfer boundary using behavior derived from the original helper, with the unchanged Apple driver acknowledging it. The remaining work includes more memory destinations, complete download and handoff, main firmware behavior, an actual network interface and a browser network backend. This milestone proves none of those later stages, and it does not execute the helper CPU. [Networking plan](../plans/networking.md#isolated-first-main-header-and-payload).
