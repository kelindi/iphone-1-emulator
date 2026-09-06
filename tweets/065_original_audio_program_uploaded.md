# The original audio driver uploads its program

## Tweet idea

The original iPhone audio driver now uploads its own program and data into emulated SRAM in an isolated test. We checked every byte against Apple's original firmware tables. The next missing piece is executing that program—this is not working audio yet.

## What you're seeing

This is a **nonvisual hardware trace**, with [the original guest upload](../docs/traces/amc-upload/upload.log), [independent original-file verification](../docs/traces/amc-upload/kernel-oracle.json), and [source/fixture hashes](../docs/traces/amc-upload/verification.json). No screen or audio recording is claimed.

The driver selects descriptor index 1, whose original table is named `Espico_aac`, then performs **17,687 SRAM writes** covering **59,622 program/data bytes**. It reaches its upload-marker write request at `0xc0277998`. The entire 180,224-byte SRAM image matches the reconstruction from the original kernel file, including untouched bytes. The no-model control still reaches the original reset failure without writing SRAM.

## ELI5 technical explanation

The audio engine has its own small memory. Before it can do useful work, the real driver resets it and loads its program. Our isolated model now connects the original clock controller's core/engine reset requests to a completed idle reset, provided the relevant clocks and power domain are enabled. Completion is **synchronous as an explicit functional approximation**, not measured physical timing. It produces only reset-complete bit 8; the later start-enable bit 9 remains clear.

The guest does all the copying. Some program words are interleaved across eight lanes; another bank packs two values into each word, so the driver reads a word, replaces one half, then writes it back. A separate oracle reads the original kernel file and checks the resulting layout, exact write coverage and the six neighboring bytes retained by those partial updates. **20,265 reset contracts, 30 write-boundary contracts and eight oracle negative controls pass**, in addition to the earlier storage checks.

The disposable probe stops inside the upload-marker MMIO handler after checking all committed SRAM writes. The marker instruction has not retired, and this is not a resumable saved phone. Unknown requests stop before device mutation; no decoder execution, successful transfer or PCM is fabricated.

## Why it matters

We have crossed from a reset assertion to the original program bytes reaching the right memory. The remaining work now begins at the engine's real execution/start boundary. Production remains unchanged, and decoding, sample transport, audible playback and microphone support remain unimplemented. [The full evidence and limits](../docs/traces/amc-upload/README.md) explain exactly what this gate proves.
