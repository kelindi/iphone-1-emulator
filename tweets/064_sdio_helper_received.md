# The complete Wi-Fi helper arrives, with the next boundary intact

## Tweet idea

The original iPhone Wi-Fi driver now transfers its entire 2,140-byte helper into bounded emulated download memory. Every byte and all 37 transfer completions are checked. Then the genuine main loader reaches the expected next failure: the received helper has not executed or requested firmware yet.

## What you're seeing

This is a nonvisual device/driver milestone with three separate genuine cold boots:

1. [No-consume baseline](../docs/traces/sdio-helper/no-consume-mmio.log): the previous endpoint holds its first 64 bytes, then the driver requests the correct next frame. It consumes the truthful no-space R5 response **0x1800** at **`0xc0431598`**. The first frame remains unchanged. [Report](../docs/traces/sdio-helper/no-consume-report.json). Apple's wrapper ignores card-error flags, so this is not labelled a driver-level failure return.
2. [Full receive](../docs/traces/sdio-helper/receive-mmio.log): **36×64-byte data transfers** carry 35 payloads of 60 bytes and one of 40 bytes, followed by a **32-byte zero terminator**. All **2,336 wire bytes** match the original-code prediction, and all **2,140 stored payload bytes** match the original helper. The driver acknowledges each IRQ/status and receives **37 successful DMA returns**, ending at **`0xc0379684`**, instruction **1,239,997,594**. [Report](../docs/traces/sdio-helper/receive-report.json).
3. [Separate main-loader negative](../docs/traces/sdio-helper/main-negative-mmio.log): after the full stream and terminator, the unchanged main loader polls requested-length registers 0x10/0x11 **100 times**, receiving zero. It reaches its actual failure path **`0xc038a788`**, instruction **1,678,065,205**, before retry. The received bytes remain intact. [Report](../docs/traces/sdio-helper/main-negative-report.json).

The [combined verifier](../docs/traces/sdio-helper/verification.json) checks the actual wire, independently reconstructed payload, retained memory, completion ordering, negative boundaries and original input hashes. A [separate root-agent byte audit](../docs/traces/sdio-helper/root-independent-byte-audit.json) confirms the same result. Wire SHA-256: `2580e629b83e3c101f0826dacfb2182db01e6d36a547bfa4433da5c0a110c33e`; stored helper SHA-256: `f1404eaae9b12a4858ac7440567de187de78dc0b8704bd693ee7de94dec74211`.

All **310,197 contracts pass**, retaining all 168,062 earlier checks and adding 142,135. [Contract log](../docs/traces/sdio-helper/contracts.log). The first full attempt stopped at its inherited 600-million-instruction limit after 20 correct frames/1,200 payload bytes; its [report](../docs/traces/sdio-helper/budget-attempt-report.json) and [receiver state](../docs/traces/sdio-helper/budget-attempt-helper.json) remain preserved. The successful bounded retries used limits of 1.5 billion and 2 billion instructions respectively; these are correctness probes, not speed benchmarks.

## ELI5 technical explanation

A one-frame buffer cannot receive an entire program unless something actually stores its contents and makes room. The new opt-in receiver copies each declared payload into a separate bounded download-memory buffer. It frees the transfer FIFO only after that real append. It never replaces the data with a known image or reports success just because a counter reaches 2,140.

[Apple's original uploader](../docs/traces/sdio-helper/original-helper-uploader-dis.txt) reuses its 64-byte buffer. The last short payload leaves 20 bytes from the previous frame in the padding; those transported bytes are verified but excluded from the stored program. The uploader also **ignores the terminator's return value**, which is why the probe requires its actual DMA completion rather than trusting the routine's final success return.

Independent review caught a possible discard of earlier FIFO contents. Helper mode now requires an empty FIFO before admitting another frame, even if a larger buffer is configured; negative tests preserve preexisting bytes. Malformed framing holds the actual received bytes and records a separate receiver diagnostic. It does not invent a physical SDIO CRC failure. Completed controller events retain the acknowledgement/reset rules from milestone 062.

The receiver's memory and reset-retention rules are explicit emulation policy. The same reviewed private missing-calibration/local-MAC device-tree policy applies. Original kernel, DT, work disk and native archive hashes remain unchanged; no production machine, snapshot or Apple executable changes accompany this experiment.

## Why it matters

We now have a verified full-helper transport, with the original driver's next unmet dependency exposed honestly. The next source investigation can model the helper's actual requested-length and receive protocol from its code, with real memory and byte accounting. This milestone does not execute the helper CPU, load main firmware, create a working network interface or provide connectivity. [Networking plan](../plans/networking.md#isolated-full-helper-receive-and-next-failure).
