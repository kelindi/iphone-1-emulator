# Bounded raw AAC framing and partial-input proof

This is a standalone decoder-feasibility result, not a guest audio-engine implementation or playback demonstration. The original media file and its AudioSpecificConfig are explicit **offline fixtures**. Neither the private driver packet table nor the media file is a proposed hardware input source.

The pinned FAAD2 decoder finds all **1,069** raw access-unit boundaries without receiving their sizes. A separate packetized decoder receives independently parsed original MP4 sample sizes as its control. Both produce exactly the same **4,374,528 PCM bytes**. A third persistent decoder reaches the captured 16 KiB cutoff, leaves the partial50th frame uncommitted, receives the actual missing 52 fixture bytes, then produces that same complete PCM output. All 371 incomplete prefixes of the 371-byte50th packet are rejected by disposable probes; the full packet succeeds.

| Result | Evidence |
|---|---|
| All raw offsets and sizes match independent packetized control | [report](report.json), [raw boundaries](raw.csv), [control boundaries](packetized.csv) |
| At 16,384 bytes: 49 complete;319/371 bytes of frame 50 unresolved | [cutoff log](cut.log) |
| Appending 52 original fixture bytes reaches frame 50 boundary 16,436 | [continuation log](cut.log), [full continuation boundaries](cut.csv) |
| Every incomplete prefix rejected; full 371 bytes accepted | [372 disposable probes](truncate.csv) |
| Exact source, upstream version/license and build settings | [provenance](provenance.json), [upstream COPYING](upstream-COPYING.txt) |
| Root's separate original MP4 parser, all three actual CSVs and full nonzero PCM equality | [independent audit](root-audit.json), [audit source](root-audit.py) |

PCM SHA256 for all three paths is `44feeb01ec9697c2f620d2262d606a8c3cf0bd657ed746ad8c0eacee8d3c077f`. FAAD suppresses its first decoded frame's output: the retained per-frame records show zero samples for frame 1, followed by 1,068 frames of 2,048 interleaved16-bit sample units. This yields 1,093,632 stereo PCM frames. No silence or synthetic samples were inserted to conceal the startup behavior. This is equivalence against the same pinned decoder's packetized control, not a claim of bit-identical output from Apple's original DSP or proof of audible fidelity.

The original media SHA256 is `56dece4e074bf622e0eb815331c65fb6c4e653e33b827e9a1ef30c4e0063136e`. [The independent preparation script](source/prepare.py) walks bounded MP4 boxes, chunk mappings and sample sizes, extracts the actual ASC `1190`, and verifies that the stream's first 16 KiB exactly matches the accepted 070 guest input capture. The raw and cutoff modes in [probe.c](source/probe.c) have no packet-size argument; only the separate control mode reads `sizes.oracle`. [run.py](source/run.py) compares all offsets, formats and complete PCM files. It preserves each mode's original output before reporting failure.

FAAD error paths can modify decoder history. The experiment uses a fresh disposable decoder for each discovery attempt and calls the persistent decoder only after a successful bounded consumption result. At the cutoff, error 15 means **unresolved input in this experiment**, not a general assertion that error 15 always means incomplete AAC. The persistent decoder receives no failed input call. Exact full-run PCM equality demonstrates that this continuation preserves history for this corpus. Public decoder format/consumption checks and these truncation tests do not establish a universal hostile-stream validator or support for arbitrary AAC profiles/configuration changes.

Every child process limits address space to 256 MiB, CPU to 120 seconds and wall time to 180 seconds. A discovery call receives at most 64 KiB, streaming attempts are capped at 2,200, and accepted frames at 1,069. The original streaming input is 364,719 bytes. These are explicit test limits, not claimed silicon capacities or timings. The decoder was built locally with GCC 11.5.0, static FAAD2 2.11.3 commit `3fbf62c5823d51b933d5127e8ea3b607d86f8f95`; the source header is GPL-2.0-or-later with upstream notices retained. This does not adopt a production dependency.

Reproduction used `.context/amc-first/stage-e/framing/`: `prepare.py`, a static `faad` CMake target, the exact compile command in provenance, then `python3 run.py run-1`. That directory retains the original downloaded source, build, binary, input fixtures and all three full PCM files. This durable evidence directory contains our test source, logs, per-frame records and hashes; it does not copy the original media or decoded audio into the repository.

No AMC register, bit 9, DMA progress, IRQ, guest memory, snapshot or production file changed. Guest-configured decoding, ownership/event/reset handling and I2S sample consumption remain the integration gates. [The source-derived next device contract](next-device-contract.md) distinguishes those gates from this successful offline test. No screen recording is appropriate: this milestone is a nonvisual byte/decoder check.
