# Original ringtone input reaches the audio-engine transfer request

2026-09-06. **An isolated original-driver replay reaches its first16KiB input-transfer request. Every supplied byte matches the retained original Alarm ringtone file. The transfer itself is not accepted; sound is still unimplemented.** Production core, firmware, snapshot ABI and browser defaults are unchanged.

[Final trace](trace.log), [verification and source hashes](verification.json), [original-kernel SRAM oracle](kernel-oracle.json), [input/file/descriptor oracle](input-oracle.json), and [root's independent media audit](root-input-audit.json) contain the evidence. Raw guest/firmware bytes remain in `.context/amc-first/stage-d/verified/`; they are not duplicated into this documentation.

## What the original driver did

The probe reruns065's verified synchronous idle reset and original program upload, then067's19 observed configuration writes. It permits the already witnessed +0=1 request to return only as an **armed, unserviced observation**. This does not execute the uploaded program, change engine status, initialize output buffers or generate an event.

Four fresh prefixes individually capture +204=1, +184=7, +b0c=2000 and +1018=1. Later candidates retain only those witnessed request values, with no inferred active readback or interrupt behavior. The driver then reaches its original input helper at`c0278084`, before its bit9 poll. Actual index0 selects descriptor`ccaa8b80`; its original kernel vtable identifies `IOBufferMemoryDescriptor`. The original `IOMemoryDescriptor::getPhysicalAddress` dispatches to `IOGeneralMemoryDescriptor::getPhysicalSegment`, yielding physical`14a8b000` and4,096 words for16,384 bytes. The raw descriptor, vtable, driver fields and per-page virtual/physical mappings are captured and independently checked.

Six more individually witnessed setup writes stage the input request:

`101c=1, 100c=0, 1000=1, 1004=14a8b000, 1008=1000, a00=1`.

The final original `100c=1` reaches the MMIO handler at **PC`c02780e4`, instruction counter9,873,233,416, timer tick1,756,770,444**. The handler stops before accepting the transfer or retiring that STR. This is219,774 instructions after067, within the5M follow-on budget. The longjmp stop is disposable, not a resumable or saved phone.

The earlier unsupported requests are retained in [prefix logs](prefixes/). The last tightened replay adds checks for the observed driver object, codec type and exact descriptor class/method; its final trace is byte-identical to the preceding input-6 run.

## What the bytes prove—and what is missing

The exact16,384-byte request begins at file offset51,240, the first media-data byte of the retained original `Alarm.m4a`. Payload SHA256 is`49aea839363fa9d716f314882fbf4bd466f27a0d6affe8e7339bd59a15fe51a9`; full source-file SHA256 is`56dece4e074bf622e0eb815331c65fb6c4e653e33b827e9a1ef30c4e0063136e`. The file was extracted read-only from the retained HFS image; that image and extractor inputs are identified in the manifest. No decoder or replacement media was used.

The shared packet descriptor contains count50 followed by50 eight-byte offset/size records. The original driver's consumer names the count `numFrames`, advances past a four-byte header, then reads offset and size while advancing eight bytes. This source evidence and the independent original media tables agree. The differently structured packet records used by another input callback must not be substituted for this shared-buffer layout.

The50 sizes total16,436 bytes. **Only49 full packets and319 of packet50's371 bytes are supplied.** The remaining52 bytes are not present: all49,152 bytes after the transfer span in the full64KiB input buffer are zero, while51 of the52 original continuation bytes are nonzero. Packet metadata therefore describes more data than this first transfer contains. Nothing in this experiment consumes or fills the missing tail.

Both independent media parsers reconstruct original sample locations from bounded file tables. Apple documents that [sample sizes](https://developer.apple.com/documentation/quicktime-file-format/sample_size_atom) describe individual media samples and [sample-to-chunk records](https://developer.apple.com/documentation/quicktime-file-format/sample-to-chunk_atom) locate them within chunks. The oracles compare actual bytes and metadata using those original tables; a codec label or guessed sync word is not their acceptance criterion.

## Validation and limits

The isolated suites pass20,265 idle-reset checks,41,560 armed-configuration checks,57,927 input-configuration checks,44 diagnostic capture checks,13 upload-observation checks and30 SRAM write-footprint checks. Counts include shared helpers and should not be summed as independent coverage. Original-kernel reconstruction includes eight deliberate corruptions; the input oracle rejects13 byte, metadata, mapping and status corruptions. Root's separately written media parser independently confirms the entire request, packet sizes and absent continuation.

All180,224 SRAM bytes and the exact59,628-byte guest write footprint remain unchanged from065. Every one of the59,622 descriptor-declared bytes has real write coverage. Status remains`0x100` (bit9 clear), +10 remains0, input-pending stays false. There is no progress-address advance, input IRQ, output-buffer allocation, decoder execution, PCM, sample clock or audible output.

Unknown/order/value/width requests reject before device mutation. The active prefix rejects divider changes and unproven+184 reads; lifecycle changes stop the probe. Descriptor capture checks the original caller/class, encrypted flag, index uniqueness, length/capacity, checked address arithmetic and every requested RAM mapping. Input metadata/file equality is additionally verified by external oracles afterward, not claimed to run inside the device callback.

Two harness setup errors are retained in the working record: the initial idle-test link omitted `amc_sram.c`, and the first input oracle manually mistyped the original getCapacity opcode (`c86e` instead of actual`c06e`). Both were corrected; neither was a guest failure or required changing a capture. Final native/oracle checks pass. These untimed functional runs overlapped other correctness work; no speed conclusion is drawn.

## Reproduction and next boundary

The final isolated [source](source/) is compiled with `AMC_INPUT_PREFIX=6`; earlier admission counts are recorded beside their frozen working sources. In this retained workspace:

```sh
cc -O2 -DAMC_INPUT_PREFIX=6 -Iweb/tests/include -Icore/include \
  docs/traces/amc-input/source/probe.c \
  docs/traces/amc-input/source/amc_idle.c \
  docs/traces/amc-input/source/amc_sram.c \
  .context/build-native/core/libemucore.a -lm -o .context/amc-first/stage-d/probe
# Supply a fresh existing artifact directory; never resume a stopped probe.
.context/amc-first/stage-d/probe .context/audio/ringtone-page.snap 1 OUTPUT_DIRECTORY
python3 docs/traces/amc-input/source/kernel-oracle.py OUTPUT_DIRECTORY
python3 docs/traces/amc-input/source/input-oracle.py OUTPUT_DIRECTORY .context/amc-first/stage-d/Alarm.original.m4a
```

The baseline archive hash is pinned in the manifest. The root oracle's retained script names the final verified directory explicitly.

A later functional engine needs bounded genuine input consumption and partial-packet continuation, valid output SRAM allocation, PCM format, buffer ownership, completion/IRQ behavior and sample-clock delivery. Full DSP ISA emulation is not assumed necessary. A supported high-level decoder must consume this real protocol and data; setting readiness alone or decoding a file merely because its program label says AAC would not establish that model. No further command admission is part of070.
