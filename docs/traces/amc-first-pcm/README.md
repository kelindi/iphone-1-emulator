# Original guest reaches its first decoded audio buffer

The one reviewed native experiment passed: the original Apple driver initialized the isolated functional engine, accepted its two distinct output buffers, supplied the real encoded input, and wrote the supported session settings. The engine decoded two raw frames and published the first4,096 bytes of PCM. Execution stopped immediately before another guest instruction, applying the new output interrupt, or consuming that buffer.

This is a nonvisual hardware trace milestone. It does not demonstrate speaker playback, output sample timing, continuous consumption, browser audio, or a resumable decoder checkpoint. Production remains unchanged.

## Actual boundary

| Observation | Actual result |
|---|---|
| Original `AMC+0=1` command | cycles9,873,013,642; resources initialized at the real write |
| Original first input enable | cycles9,873,233,416, PC`c02780e4`; real physical`14a8b000`,16,384 bytes |
| Original returned buffer layout | offsets`f220`/`11220`, capacities8,192/8,192; helper returned0 |
| Original session writes | SRAM`2a000/2a002/2a004/2a00a`:16-bit`1b/2/0/3` |
| First PCM publication | cycles9,873,234,585; timer ticks1,756,770,461 |
| Decoding | 2 raw frames,348 input bytes consumed; first frame has zero output during real decoder startup |
| Owned input remaining | 16,036 bytes, exactly the actual supplied bytes after the consumed prefix |
| Output | 2,048 signed16-bit sample units =1,024 stereo frames =4,096 bytes |
| Output identity | SHA256`2ce90c8f574188134ae90dc6a79e045c8e75c99c05ed28a466669d381f7ec372`;178 nonzero bytes |
| Events at stop | raw`600`, mask`2000`, input pending1; no output-IRQ refresh after publication |
| Boundary |48,234,585 total retired instructions; no interrupted instruction at the positive stop |

The original start/input requests occur at the same retired counts as the retained observational067/070 prefixes, despite using one-instruction service boundaries throughout. This was observed, never enforced by a fabricated continuation. The initial fixture, program, APIs, libraries, source and headers are pinned in [build.json](build.json) and [run.json](run.json).

## Evidence and actual rejection controls

- [Actual full trace](trace.log): `VERIFY original run request`, `ACCEPT engine initialization`, `ACCEPT input request`, `HOST layout`, the four `ENGINE SRAM W` records, and the final `PASS first PCM publication`/`ENGINE state`/`BOUNDARY` records.
- [Original-kernel reconstruction](upload/kernel-oracle.json): all180,224 pre-start SRAM bytes,59,622 declared bytes and59,628 actual store/RMW footprint bytes pass the independent original-Mach-O oracle. This runs outside the emulator callback.
- [Input/PCM/SRAM audit](functional-audit.json): root's independently written MP4 sample parser verifies all16KiB input and all50 metadata records, with the missing52 continuation bytes still absent. The complete64KiB capacity is checked. The actual output is byte-identical to the first4,096 bytes of the separately packetized full-media decode from [072](../aac-framing/README.md).
- Intentional write masks contain12 initialization bytes,4,100 PCM/ownership/count bytes, and8 actual host session bytes. All176,108 SRAM bytes outside their union remain identical to the original upload. The original host received the genuine layout values; the adapter did not inject helper results.
- The same frozen auditor runs on two separate artifact sets. A one-byte input mutation fails at the intended [input equality check](negative-input.log); a one-byte PCM mutation fails at the intended [independent PCM comparison](negative-pcm.log). The positive original artifacts remain intact.
- [37 wrapper checks](hook-test.log) verify atomic unknown/active lifecycle rejection, canonical owner release, and the publication stop without CPU advance/output-IRQ refresh. Their synthetic two-sample backend exists only in `test_hook.c`; the real guest run uses the pinned FAAD adapter.

Root independently rechecked actual PCM, the original-file input span, all six output-header halfwords and every byte outside the explicit writable ranges: [independent review](root-review.json).

The reviewed [contract](contract.md), [launcher](launch.py), [external auditor](audit.py), [engine](source/amc_engine.c), [actual guest hook](source/probe.c) and [source/hash inventory](provenance.json) are retained. Both auditors and the launcher were copied to the run directory and hash-recorded before execution. Existing frozen-core LTO warnings remain visible in the build logs. No original media, uploaded program image or decoded PCM file is included in this documentation bundle.

## Limits and next gate

The run took5.38s wall while other functional work was allowed; this is a test duration, not a speed benchmark or playback latency. Fixed limits were240s wall/180s CPU/1GiB process address space/8MiB per file,155.3M total retirement and5M before/after input. It was run once, with no retry. The decoder's source/version/license and standalone framing behavior remain documented in [072](../aac-framing/README.md).

The next source-reviewed experiment must let the original guest react to the real pending events, consume the actual first buffer and return ownership. The first publication remains a retained boundary and exact prefix. No new guest run, I2S transfer, speaker-clock model or audio output follows automatically from this result.
