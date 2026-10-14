# Original iPhone audio bringup

> Historical recordings and publishing artifacts have been removed from Git history. The summaries below describe the original runs; remaining test reports and screenshots are retained. Historical hash receipts may predate this archival cleanup.

**2026-09-06 — original guest consumes its first PCM buffer (083):** the actual app returns successfully from initialization, explicitly starts AMC, receives the original driver notification, reads every one of4,096 PCM bytes and returns ownership through a verified retired original store. All18 actual artifact negatives reject; root independently confirms the read coverage, IPC and exact instruction receipt. The same bounded engine still has a partial fifth input and two published buffers. [Milestone083](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/amc-first-consumption/README.md) and [complete evidence](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/amc-first-consumption/README.md). Continuous timing, I2S/codec output and audible playback remain open. The GPL decoder/model remain isolated; no production/snapshot/browser-audio integration.

**2026-09-06 — first guest PCM buffer verified:** the original Apple driver now starts the isolated functional engine, accepts its two distinct 8,192-byte buffers, supplies actual encoded input and writes the supported session settings. Two raw frames consume 348 genuine input bytes and publish the first 4,096 PCM bytes, exactly matching the independently packetized reference. The original-kernel, input, SRAM-mask and actual artifact-corruption audits pass, as does root's independent review. Execution stops before the next guest instruction, output-IRQ refresh or host consumption. [Milestone 075](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/amc-first-pcm/README.md) and [durable evidence](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/amc-first-pcm/README.md) retain the one-run boundary. Production, browser audio and speaker output remain unchanged; ownership return and continuous delivery are later gates.

**Consumption follow-up:** one fresh continuation reproduces075 exactly, handles a real input-completion notification, and stops before a second16KiB input transfer at `c0278130`. It records zero PCM reads/releases. The requested physical bytes were not captured, so their media offset is unproven. [Preserved trace, checks and limits](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/amc-consumption-probe/README.md) distinguish this earlier boundary from a successful consumption test. A capture-only source review precedes further admission.

**2026-09-06 decoder feasibility:** a standalone pinned decoder now discovers all 1,069 original raw AAC frame boundaries without access to packet sizes. Raw and partial-input continuation output exactly matches the independently packetized control: 4,374,528 PCM bytes. At the captured 16 KiB cutoff, the incomplete 50th frame remains uncommitted until the test supplies its missing 52 original bytes; all 371 truncated prefixes reject. This uses the original file's configuration as an explicit offline fixture, with no guest/device integration or playback claim. [Milestone 072](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/aac-framing/README.md) and [complete evidence](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/aac-framing/README.md) retain the decoder's startup behavior, version/license, resource caps and next device contract.

The M68 board now has its original WM8758 control endpoint at I2C0 address `0x1a`. The genuine iPhone OS 1.0 `1A543a` driver completes the observed codec initialization writes successfully. This is a control-protocol foundation: the emulator does not yet produce audible audio or demonstrate a working PCM stream.

## Evidence from the original firmware

The supplied device tree distinguishes three paths that must not be conflated:

| Device-tree node | Observed wiring | Role to investigate |
|---|---|---|
| `/arm-io/i2c0/audio0` | `audio-control,wm8758`, address `0x1a` | Codec configuration |
| `/arm-io/i2s0/audio0` | `audio-data,wm8758`, controller `0x3ca00000`, DMA parent dmac0 (`0x38200000`) | Codec sample transport |
| `/arm-io/i2s1/audio1` | `audio-data,baseband`, controller `0x3cd00000`, DMA parent dmac1 (`0x39900000`) | Separate baseband audio path |

The I2S nodes refer to GPIO interrupt-controller events `0x86` and `0xaa`; those are not VIC interrupt numbers. The DMA controller VIC sources are 16 and 17. The complete DMA channel properties still need decoding against the original driver before implementing sample requests.

Disassembly of the supplied kernel's prelinked `AppleWM8758Audio` establishes the control protocol directly:

- Vtable `0xc045d344`; register writer `0xc0459a8c` through `0xc0459b4c`; transfer return at `0xc0459af0`.
- Each write contains exactly two bytes: `byte0 = (register << 1) | ((value >> 8) & 1)`, `byte1 = value & 255`. Register indexes are seven bits; values are nine bits.
- The driver updates its own register shadow before the I2C transfer. Its read/modify/write helper at `0xc0459b58` reads this shadow. No codec I2C read transaction appeared in the observed initialization prefix.
- Initialization sets a 62-register table at `0xc045d8cc` and a default sample-rate field of 44100. The table marks reserved entries with `0xffff`. A field's default is not proof that a sample stream has started or that every later stream uses that rate.
- The observed software reset writes register zero with value zero.

A native cold-boot probe traced writer entry arguments, the actual I2C bytes, and the driver's transfer result without modifying Apple code or guest memory. With no codec attached, all 51 sampled writer returns were `0xe00002e9`. A diagnostic acknowledging endpoint captured 56 two-byte commands with successful returns and no reads. The production endpoint then accepted 56 writes with no refused commands, and all 56 sampled driver returns were zero. The probe stops at a batch boundary after reaching a write-count threshold, so the different sample counts are not a claimed change in guest behavior.

Representative wire commands, also covered by the maintained test:

| Bytes | Driver register | Driver value |
|---|---|---|
| `00 00` | `0x00` | `0x000` (reset) |
| `7b 01` | `0x3d` | `0x101` |
| `63 86` | `0x31` | `0x186` |
| `69 f9` | `0x34` | `0x1f9` |

Local investigation artifacts are in `.context/audio/`: `devicetree.txt`, `register-path.txt`, `wm8758-dis.txt`, `boot-nak.log`, `boot-ack.log`, and `production-boot.log`. These are working evidence, not shipped firmware assets. The complete observed command prefix is retained in [test_wm8758.c](../core/tests/test_wm8758.c).

## Implemented scope and snapshot compatibility

[wm8758.c](../core/src/soc/wm8758.c) provides separate WM8758 callbacks and nine-bit storage semantics. It accepts the measured packed write form, restores the driver's reset-table values after reset, and refuses unsupported reads without inventing an ID. Truncated and overlong transfers cannot partly update a register. Reserved registers and unobserved reset forms do not mutate state. The model does not attempt analog routing, power sequencing delays, microphone capture, or playback.

The existing codec storage layout is reused, with separate callbacks and validation. This avoids a snapshot schema change: version 39 and serialized field order remain unchanged. Formerly unused, all-zero M68 codec storage represents unwritten reset state; reset values are derived when inspecting it rather than inserted into old files. PMU remains I2C slot zero, and the codec is appended at slot one. Host callback wiring is not serialized, so old PMU selections keep their meaning. Snapshot validation selects the codec protocol from host wiring; the N82 board retains its WM8991 endpoint at `0x1b`, identity behavior, and stricter WM8991 discriminator checks.

Three actual older v39 files loaded successfully in the new native build: the shipped Home snapshot, the Settings persistence export, and the Calendar workflow export. Their unused codec bytes remained unchanged. Saving, loading, and saving again produced identical bytes for each restored state. This comparison is after the initial load; it does not claim a raw historical file remains byte-identical across every existing loader normalization.

An old running snapshot has another limitation: the guest driver may already have cached writes that failed before this device existed. Restoring it does not replay those writes. Codec initialization was therefore verified by a genuine cold boot. Later playback testing must start from cold boot or prove that an authentic guest reconfiguration path programs the missing state; it must not silently copy the guest shadow into hardware registers.

## Validation

- All 24 maintained native test suites pass, including 460 checks in `test_wm8758`.
- The codec suite replays the 56 observed commands through actual I2C MMIO and completion events, checks malformed transfers and nine-bit values, restores a snapshot halfway through a two-byte transaction, and checks reset-state restoration and unchanged N82 identity constraints.
- The WASM build and initialization smoke check pass. Chrome restored the old Home snapshot and completed Settings → Home → Safari → Home with no reported failures. Screenshots were inspected for app identity; this is a navigation regression check, not audio evidence or a latency benchmark. The local report is `.context/audio/browser/report.json`.
- Production cold-boot tracing proves successful control transfers. At the bounded stopping point both I2S controllers had zero writes; this is not a test of the later audio streaming path.

## Measured playback blocker: ringtone preview (2026-09-05)

A genuine cold boot of the existing 185 MiB work image reached Home, programmed the codec 63 times without refusal, and opened Settings → Sounds → Ringtone. Selecting **Alarm** requested playback. No Apple code or guest-memory patch was used. These measurements use that explicitly identified work image; they do not claim the newly packaged compact default was the source. The newer pristine compact Home independently retains the same 63 successful codec writes and is the preferred fixture for further work.

The preview selects **I2S1/baseband**, not I2S0/WM8758. A diagnostic headphone-detect transition on the device-tree pin/interrupt reprogrammed the codec (63 → 76 writes), but the same ringtone request still reached I2S1. Ringtone routing must therefore not be assumed to test the codec sample path.

### Two distinct hardware gates

1. **AppleAMC cannot finish its audio-engine reset.** The original tree gives `/arm-io/amc` a register window at `0x38500000` and SRAM at `0x22000000`. Neither has a functioning device model. The original narrow trace records a write of `3` to `0x38502000`, a read of `0x38500010`, then eleven reads of `0x38500b18`, all returning zero. **The write of 3 is divider setup, not the reset request**: the later wider trace below proves separate core/DE reset commands at `0x3c500050`. At `0xc0277ec4`–`0xc0277f04`, the genuine reset routine polls bit 8 of that last register. The guest subsequently reports failure of `ode_resetAudioDE()` and another AppleAMC assertion. Setting this bit arbitrarily would only hide an unimplemented engine.
2. **I2S waits for external frame-clock events before starting DMA.** The OS1 controller's configure routine is `0xc040ba58`, start is `0xc040bb60`, write accessor is `0xc040bea8`, and GPIO callback is `0xc040be44`. These are original OS1 addresses, distinct from the later-firmware addresses in the historical shared-header comments. Configuration bit 20 selects a synchronization path. The driver enables its GPIO source, waits for two callbacks to advance its own state from 1 → 2 → 3, then submits DMA. For this request neither callback arrives; after approximately ten guest seconds start returns `0xe00002d8`, and AppleBasebandOutput reports that the device is not ready. The tree identifies I2S1's interrupt as GPIO-controller line `0xaa` (170), not a VIC line.

The actual I2S1 configuration writes were:

| Register offset | Value |
|---|---|
| `0x00` | `0x00000001` |
| `0x40` | `0x00000000` |
| `0x04` | `0x01100301` |
| `0x30` | `0x00000000` |
| `0x08`, `0x34` | `0x00000000` |
| `0x3c` | `0x00000001` |

### Diagnostic edge experiment: DMA moves silence

A separate, explicitly diagnostic run pulsed GPIO line 170 after 50,000-instruction host batches. This was a causality probe with artificial timing, not a frame-clock implementation and not production behavior. It did not patch guest code or force a function return. The genuine callback ran twice, and the guest proceeded to program PL080 dmac1 channel 5:

| Field | Observed value |
|---|---|
| Source | `0x14bcf000` |
| Destination | `0x3cd00010` (I2S1 transmit FIFO) |
| Linked item | zero for the sampled first buffer |
| Control | `0x84249800` (2,048 transfers, 16-bit source/destination width) |
| Configuration | `0x00008885` |

The guest start routine then returned success and wrote `6` to I2S1 offset `0x08`. The model's DMA delivered the first whole buffer at tick `1757265043`, before that enable write at tick `1757265063`. It subsequently completed and rearmed transfers without sample-clock pacing. This confirms a missing FIFO/cadence model; it does not establish the physical FIFO depth, clock rate or correct interrupt intervals.

The capture contained **482,217,984 bytes / 241,108,992 signed 16-bit values, all exactly zero**. I2S0 received no sample bytes. There is no nonzero waveform, no WAV presented as working sound, and no audio support claim. AppleAMC's reset assertions remained present. Its failure is an observed upstream problem; proving exactly which engine operation should fill the buffers is still required.

The following evidence is local under `.context/audio/`: `stream-trace.c` (native probe), `ringtone-alarm.log` (unaltered device behavior), `alarm-pulse.log` (clearly separated artificial-edge experiment), `alarm-headphone.log`, and `amc-reset.log` (the exact reset poll). `playback-findings.json` retains the zero-buffer scan and important addresses. The probe captures actual bus traffic; it never generates replacement PCM. Large temporary raw captures may be reused by subsequent probes; the recorded scan counts and trace identify this experiment. No production core, timing, snapshot layout or browser audio code changed in this stage.

The [Wolfson WM8758B datasheet, revision 4.4](https://www.mouser.com/datasheet/2/76/WM8758B_v4_4-1132068.pdf), pages 64–69, is a useful primary reference for codec word length, master/slave mode and PLL scaling. It is a later part revision and does not define the S5L8900 I2S FIFO or establish the clock of this observed baseband route. Register defaults and protocol in production remain grounded in the supplied OS1 driver.

## I2S frame clock, transmit FIFO and paced DMA (2026-10-14)

The I2S1 start gate is implemented. `configure()` sets bit 20 of `+0x04` (`0x01100301`); with it set, `start()` (`0xc040bb60`) unmasks GPIOIC line 170, waits for two callbacks at `0xc040be44` (state 1 -> 2 -> 3), then submits DMA and writes 6 to `+0x08`. The machine now drives a frame clock onto line 134/170 while its port is configured that way, accepts `+0x10` stores into a bounded transmit FIFO, gates PL080 destination readiness on FIFO space, and shifts bytes out at the frame rate in guest timebase time. WFI wake sources name the frame-clock edge and the paced item's retirement, so idle fast-forward cannot skip either.

Approximations, labelled in `soc.h`: 44.1 kHz frame clock, 32-byte FIFO, 4 bytes per frame. None is read off silicon. Stream state is host-only; snapshots keep format v39 and a restore starts with empty FIFOs.

Native result from the shipped home snapshot, pressing Sleep (`tools/iphone1 ... --at 9800000000 press hold --steps 11500000000 --audio-wav lock.wav`): no `could not start DMA` message, 43,311 frame-clock edges, 169,998 bytes shifted, no underrun or overflow. The 42,499-frame capture contains Apple's `lock.caf` exactly: correlation 1.00000 and gain 0.9998 against the CAF's 17,600 frames (44.1 kHz stereo 16-bit). `core/tests/test_i2s.c` covers the clock, backpressure, pacing, drain and long-refresh behavior.

The browser receives the same PCM via `emu_audio_read()`; the worker posts it and the page schedules it on WebAudio after the first gesture, and Pause suspends the AudioContext. In Chrome, Sleep scheduled 208 buffers whose PCM correlates 0.9999997 with `lock.caf`; unlock also sounds, and paused output falls silent within ~15 ms. Playback stutters when the guest runs slower than real time. Still unverified: I2S0/WM8758 media playback (iPod), YouTube startup, recording/input, and exact silicon timing.

## Next gates

1. Resolve the measured AppleAMC reset gate: trace the producer of the reset request, expected status transitions, and any subsequent SRAM upload/engine commands before modeling readiness. In parallel, find a genuine media-category workflow that selects WM8758/I2S0 and compare it with the measured baseband ringtone path. Require nonzero guest-produced samples and a proven format before defining playback semantics.
2. Implement the measured I2S transmit FIFO, sample-clock consumption and DMA request/backpressure behavior. The current I2S register model drops unknown FIFO writes, and the machine's DMA readiness gate does not yet model I2S capacity. A DMA transfer completing into discarded writes cannot establish audio support.
3. Preserve FIFO contents and clock phase across checkpoints with an explicit compatibility design. Test pause, underrun, stop, power changes and save/load boundaries against real driver behavior.
4. Expose verified guest-generated PCM to the browser and deliver it with an AudioWorklet after a user gesture. Measure buffering, continuity and end-to-end latency. Apply the real guest's volume/ringer and route behavior; do not replace missing hardware with host-generated sound effects.
5. Verify an audible original-app workflow and record it with captured audio. Baseband calls and microphone input require additional devices and routing beyond codec playback.

A separate warm-snapshot sleep probe printed `AppleBasebandOutput: could not start DMA: device is not ready` while writing I2S1 registers. That observation concerns the baseband path and must not be used to claim success or failure of an I2S0 codec stream.


## Actual AMC reset request identified; readiness remains unimplemented

A bounded read-only replay of the same genuine ringtone-page checkpoint now traces the clock/reset controller and the original Apple platform functions as well as AMC. It executes **55,300,000 instructions**, ending just after the reset timeout. No register response, guest instruction, external GPIO edge or sample buffer is changed. Durable evidence is [the bus/caller trace](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/amc-first/baseline.log) with [fixture/kernel/probe/archive hashes](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/amc-first/provenance.json).

The ordering is now concrete:

| Observed operation | Original producer and interpretation |
|---|---|
| Enable AMC clocks 3/4 and power domain 4 | CLOCKGEN `0x3c500048` clears bits 3/4; power controller enables domain 4 before the reset sequence. |
| Write `3` to AMC `0x38502000` | Helper `0xc0277e84` stores `r0 - 1`. Actual caller `0xc0274754` supplies 4, calculated by `0xc027130c` from the driver's clock/performance fields. The observed rate field is 103,000,000, factor is 1, and the helper computes a ceiling against 34,000,000. Six callers include performance/power paths. This is divider setup, **not** an audio reset command; it does not establish the physical sample clock. |
| Write `0xa000` to CLOCKGEN `0x3c500050` | `function-core_reset`, corresponding to the original device-tree mask, through platform function method `0xc0410ac4`. |
| Write `0x0a00` to CLOCKGEN `0x3c500050` | `function-de_reset`, corresponding to the device-tree mask, through the same method. `0xc041025c` forwards the mask directly to offset `0x50`; the original code issues one command, without a subsequent software clear. |
| Read AMC `0x38500010`, then poll `0x38500b18` bit 8 | Named `ode_resetAudioDE` helper `0xc0277ea0`. If the first register equals 1, it writes 1 to offset `0x0c`; then it samples bit 8 once and retries ten times with the original sleep helper. The baseline first register and all eleven status reads are zero; the driver reports its reset assertion. |

Driver setup at `0xc02756b4..0xc02756e4` resolves the actual `function-core_reset` and `function-de_reset` objects into fields `0x164` and `0x168`. The reset wrapper at `0xc0271870` calls those functions before polling. This establishes the causal request source missing from the earlier AMC-only log. Current CLOCKGEN storage records the request but has no AMC device reset consumer.

The real firmware path is also located, without pretending it executed successfully. Loader `0xc0277844` selects original descriptors named `pmem`, `ram_mc1` and `ram_mc2`, writes 0 to AMC offset `0x340`, copies the described segments through original routines, then writes 1 to that register only after loading succeeds. Descriptor setup `0xc0276e20` maps program memory to SRAM+`0x1c000`, `ram_mc1` to +`0x14000`, and `ram_mc2` to +`0x28000`. The copy paths include interleaved 32-bit program/data writes and packed halfword read/modify/write for `ram_mc2`. **The bounded failing replay makes zero SRAM accesses and reaches no successful upload.** These static paths are next probes, not firmware execution or PCM evidence.

The smallest plausible next hardware boundary is a genuine CLOCKGEN reset request causing an internal AMC reset transition whose completion produces the status observed by the driver. However, this evidence does not establish reset latency, the individual meanings/dependencies of mask bits, status clearing on execution/start, or which engine resources must be reset. A neighboring routine separately polls status bit 9; it must not be satisfied by an all-ready mask. SRAM storage and the engine executing its uploaded program also remain absent. No candidate completion bit, arbitrary delay, replacement PCM, production source or snapshot change is introduced at this gate.

Working disassemblies and the immutable probe are under `.context/amc-first/`; compile the observer with `cc -O2 -Iweb/tests/include -Icore/include .context/amc-first/baseline-probe.c .context/build-native/core/libemucore.a -lm -o .context/amc-first/baseline-probe`, then pass the hashed ringtone-page snapshot. [Milestone 061](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/amc-first/baseline.log) is a nonvisual investigation update, not an audio demonstration.

### Isolated SRAM boundary verified

Stage A adds independent byte storage for physical `[0x22000000, 0x2202c000)` in a disposable probe, with **279 storage checks and 14 actual machine-bus checks passing**. Byte/halfword/word readback, the original packed-halfword read/modify/write pattern, complete boundary validation before writes, and RAM/MMIO separation are covered. Initial zero contents are an explicit emulator policy, not measured hardware defaults. The current machine allocation guard intentionally excludes this unmodeled SRAM window, so isolated attachment rejects overlapping RAM separately; production integration would require deliberate address-map and snapshot migration work.

Two genuine 55.3M-instruction replays, with the new storage disabled and enabled, retain the same original reset assertion. Their 54-line traces are identical except the declared SRAM mode, with identical final instruction/tick counts and RAM FNV64 `f312103de8cc00b0`. Both make 12 AMC reads, one divider write, and **zero SRAM accesses**. This proves the storage boundary does not manufacture readiness; it does not prove a working upload or audio. [Storage contracts](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/amc-first/stage-a/contracts.log), [bus contracts](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/amc-first/stage-a/bus-contracts.log), [disabled trace](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/amc-first/stage-a/reset-0.log), [enabled trace](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/amc-first/stage-a/reset-1.log), and [source/fixture hashes](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/amc-first/stage-a/verification.json) preserve the evidence. The first test's incorrect assumption about the allocation guard is retained in [its failure note](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/amc-first/stage-a/first-contract-failure.txt).

The next isolated gate models a synchronous idle reset as a labeled functional approximation, derived from the original core/DE requests with clocks 3/4 and power domain 4 enabled. Exact silicon latency alone does not block this bounded model. Bit 8 may follow completed reset state; bit 9 remains clear. The required positive boundary is the original descriptor-verified SRAM upload, stopping before execution. Production source, firmware and snapshot state remain unchanged by Stage A.


## Isolated original program upload verified (065)

The reviewed Stage B model now crosses the reset gate and reaches an original descriptor-verified upload. It accepts only the observed core→DE requests with clocks 3/4 and power domain 4 enabled, clears its modeled idle setup, and derives bit 8 from completion. Reset is **synchronous as a functional approximation**, not measured silicon timing. SRAM/divider are retained; gating invalidates completion; bit 9 always remains zero. Exact pre-upload configuration is limited to observed idle setup, with no inferred engine/IRQ/FIFO effect. Unsupported requests stop before mutation.

The actual original driver selects index 1, mapped by original setup/getters to `Espico_aac`, and writes 40,272 program bytes, 11,126 packed data bytes and 8,224 ordinary data bytes. An independent oracle reads descriptor/source bytes from the original Mach-O file, validates all lane lengths/spans and compares the entire **180,224-byte SRAM**, not just hashes or nonzero areas. All **59,622 descriptor bytes have actual write coverage**. The exact 59,628-byte store footprint includes six retained neighboring RMW bytes, independently explained by the original packed-halfword routine. Actual and reconstructed SRAM SHA-256 is `011452fdff9699442c49115354c9b0bced1b98265ba64ee5b95c1c48bbb01583`.

**20,265 idle-reset contracts, 30 pre-mutation write-footprint contracts and eight oracle negatives pass**, in addition to Stage A's 293 checks. The tightened guest replay is byte-identical to the preserved first successful trace; a fresh no-model control still reaches the original reset assertion with zero SRAM accesses. The guest upload uses 48,013,483 bounded instructions, 17,687 SRAM writes and 5,563 RMW reads. It stops when the original STR at `0xc0277998` requests `(0x38500340,1)`, after all SRAM writes are committed and verified. The marker instruction itself has **not retired**, and the interrupted probe is never resumed or saved. No post-upload device completion, bit-9 readiness, firmware execution, PCM or audio output is claimed.

[Milestone 065](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/amc-upload/README.md) links the [durable logs, independent original-file oracle, source and hashes](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/amc-upload/README.md). No production machine, firmware, snapshot or browser default changed. The next gate is source analysis of the actual start/command/ISA boundary; SRAM/device production integration still needs explicit address-map and snapshot migration tests.


## Original idle configuration and engine-command request verified (067)

Two fresh isolated prefixes now extend the verified upload, preserving065's original sources and capture. The first returns the upload-marker callback as an observation-only boundary, then stops exactly at the next `0x3850101c=1` write,58 instructions later, before mutation. Original interrupt-handler use supports an input-pending acknowledgement; the real replay's latch is empty and no event producer is added. The seven captured codec words are `[2,2,48000,128000,0,0,0]`; only the first is independently named as codec type2.

After that witness, the second frozen candidate accepts six exact idle input-clear writes and thirteen original task-manager configuration writes. Actual task object`0xcc999ee4`, vtable`0xc029c844`, method`0xc02782fc`, argument0 match the static prediction. It then stops at the original `0x38500000=1` request at`0xc0277fa0`,159 instructions after the upload marker, **before accepting the command or retiring its instruction**. SRAM values and exact write footprint remain byte-identical to065; the separate original-kernel oracle still passes all180,224 bytes and coverage. Status is`0x100`, input-pending is false, and no IRQ, transfer, execution or PCM is synthesized.

**20,265 retained reset checks,418 idle-prefix checks,13 upload-observation/coverage checks and30 write-footprint checks pass**, plus eight original-file and five protocol negative controls. The callback validates bytes/coverage from guest descriptors; the independent original-Mach-O oracle checks the captured artifacts afterward, not inside the callback. Unknown or out-of-order requests reject atomically; no interrupted machine is resumed or saved. [Milestone067](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/amc-run-request/README.md) links [durable sources, both witnesses, contract logs and hashes](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/amc-run-request/README.md).

The remaining boundary is the command's actual effect and the producer of later bit9 readiness. A grounded functional model consuming genuine guest input may be appropriate; full ISA emulation is not assumed mandatory. A codec-table label alone does not justify a host decoder substitution or fabricated results. Production source, firmware, snapshots and browser defaults remain unchanged by this isolated work.


## Original encoded input request verified (070,2026-09-06)

The next isolated observational prefix now reaches the original input-transfer request at`c02780e4`, before accepting its final`AMC+100c=1` write or retiring that instruction. Four post-command idle records and six input-setup records were admitted only after separate original witnesses. The +0 request records armed/unserviced state; it does not execute the uploaded program. Full SRAM and guest write coverage remain unchanged; bit9/+10/input-pending remain zero and no IRQ or PCM is created.

The actual index0 IOBufferMemoryDescriptor supplies physical`14a8b000`,16,384 bytes/4,096 words. Two separate original-file/sample-table oracles confirm every byte matches Alarm.m4a at offset51,240. Shared metadata describes50 packets totaling16,436 bytes, but this request supplies only49 complete packets plus319 of packet50's371 bytes. Its missing52 bytes are absent from the full64KiB input buffer; the remainder is zero. Packet continuation is therefore an explicit next dependency, not an excuse to pad or declare a complete frame.

Final checks pass20,265 reset,41,560 armed-configuration,57,927 input-configuration,44 capture-guard,13 upload-observation and30 footprint assertions (shared helper counts overlap), plus eight original-kernel and13 input-oracle corruption controls. The final tightened replay is trace-identical to the earlier accepted observation and passes root's independent media audit. [Milestone070](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/amc-input/README.md) links [all evidence, source and limits](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/amc-input/README.md). These are untimed isolated tests; production remains unchanged.

A future high-level engine may initialize valid buffers and consume genuine compressed input without full DSP ISA emulation, but must ground SRAM allocation bounds, packet continuation, PCM format, ownership, completion/IRQ behavior and sample-clock delivery. Readiness alone is not a working engine. The existing AAC output header at SRAM+f200 remains zero, so a ready response now would produce coincident output addresses with zero capacity. No later command admission has been implemented.


**Second input identified:** the isolated capture-only follow-up preserves the entire prior consumption-probe trace and state while proving the original driver requests exactly the next 16 KiB of the AAC stream. Original-file audits and an actual corrupted-capture rejection pass. The transfer is still stopped before admission/retirement; no PCM read or ownership return is claimed. See [durable evidence](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/amc-consumption-probe/second-input/README.md).


**Second input admitted, consumption pending:** the next isolated run copied the genuine second transfer and delivered its actual completion notification. The driver requested a third transfer before enabling output processing, so the guard stopped it. Both input captures and the complete 32,420-byte owned queue match original media; SRAM/PCM remain unchanged and no output read is recorded. Next: original service/interrupt ordering, without more input admission. [Preserved evidence](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/amc-consumption-probe/second-admitted/README.md).


**Original audio ordering observed:** an unchanged-state probe verifies successful session return and actual input refill indices 1 and 2 before the same third-request stop. The output-start call has not been observed by that boundary. Indirect caller versus interruption remains unresolved; no device timing or admission changed. [Evidence](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/amc-consumption-probe/ordering/README.md).


**Audio caller resolved:** the actual saved LR and retired Thumb return identify original generic scalar I/O configuration dispatch. Output start is a separate original UserClient method; no DMA timing change was needed. Full previous trace/state match. A post-run negative-launcher error is preserved and recovered using saved artifacts only, with no guest repeat. [Evidence](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/amc-consumption-probe/caller-return/README.md).


**Third audio input verified:** the unchanged isolated engine copies the next genuine 16 KiB; all 49,152 copied bytes and the 48,804-byte owned queue match original media. The driver wraps its input index to 0 and requests a fourth transfer before explicit output start or PCM reads. The fixed cap stops before acceptance; no further admission. [Evidence](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/amc-consumption-probe/third-admitted/README.md).


**Actual audio startup source resolved:** original captured client type 1 is MeCCA. Its original framework initializes via selector 2, obtains output information, then sends explicit selector 3; input refills run separately. The IOAudio2 selector-1 path is a different interface. A source-only observation contract targets real initialization/start return values without another input admission. [Original-source evidence](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/amc-consumption-probe/third-admitted/startup-source/README.md).


**Kernel audio configuration returns successfully:** one unchanged-state observer replay records real kernel return R0=0. Selected MeCCA userspace returns were not observed, so their actual mapping/execution remains unresolved; no user error or startup is inferred. All previous input/PCM state remains exact and four real artifact mutations reject. [Evidence](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/amc-consumption-probe/mecca-return/README.md).


**Audio input backpressure verified:** the unchanged64KiB queue owns the full fourth transfer and only348bytes of the fifth. Completion remains clear, allowing execution to reach the expected userspace return PC. The observer then rejects before validating/retiring it; likely user/kernel inspection-permission issue, not a demonstrated engine failure. All actual-input/queue/prefix proofs and eight artifact negatives pass. [Preserved evidence](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/amc-consumption-probe/input-backpressure/README.md). No PCM consumption or playback claim.


**Original userspace audio startup and first PCM reads verified:** correcting only observer permissions reveals successful actual scalar/init/output-info returns, explicit method3 startup and all4,096 first-output bytes read by the original user process. The run stops at the existing owner-release retirement counter assumption; full handoff remains unaccepted pending instruction-start/successor validation. Exact earlier prefix, real input/queue/both PCM and nine artifact negatives pass. [Preserved evidence](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/amc-consumption-probe/user-start-reads/README.md). No audible playback or production change.


**Next source gate after083:** original libSystem reader PCs belong to memcpy/memmove, and MeCCA exposes a real decoded-buffer/process-buffer queue. Actual copy destination and downstream DMA remain unobserved. Current I2S stores configuration and counts FIFO accesses without sample timing; PL080 gates SPI capacity but treats I2S as ready. [Bounded transport plan and original disassembly](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/amc-first-consumption/downstream/next-transport-contract.md) require genuine destination/format/clock evidence before FIFO pacing or host audio.


**First PCM software destination verified:** an unchanged083 observer replay proves the original MeCCA writeSamples call copies the exact4,096 bytes to user00387000/physical160e4000. All23 artifact negatives reject; original caller/frame code is independently matched. [Evidence and corrected TTBR interpretation](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/amc-first-consumption/copy-destination/README.md). The software output manager is not yet a demonstrated DMA ring, and actual output format/clock and audible playback remain open.


**Actual downstream state captured:** the unchanged083 boundary now shows MeCCA allocation00383000..003c3000, actual first write00387000 and software-format48000. Both I2S models and DMAC0 remain untouched through that stop; DMAC1 retains unrelated prior transfers, while WM8758 retains63 earlier register writes. [Transport census and27 actual corruption rejections](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/amc-first-consumption/transport-census/README.md). The next gate is original output-start/CoreAudio/DMA setup, not a fabricated sample clock.


**Continuous ownership next:** an exact fresh083/S prefix followed by12 actual instructions observes the real next+4 service request on a genuinely returned buffer. The diagnostic two-publication cap stops before more work; all28 artifact negatives pass. [Evidence](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/amc-first-consumption/next-service/README.md). Current engine.service was0 before that new request; the old summary service flag was historical. A bounded stream extension is now justified, without synthetic ownership or sound shortcuts.


**Bounded stream feasibility:** standalone unchanged-engine tests pass64 actual-FAAD publications, exact independent PCM/queue, owned-slot no-overwrite and partial-DMA backpressure (714,197 assertions, largely SRAM bytes). The proposed64-publication/eight-input guest gate is tied to the captured256KiB ring and64/224KiB thresholds, not repeated one-buffer retries. [Plan and tests](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/amc-first-consumption/next-service/stream-plan/stream-contract.md). Test-driven release calls are not guest-consumption evidence; no new stream guest has run.


**Audio stream follow-up (Stage U):** one isolated guest now decodes and reads53 consecutive4KiB PCM buffers with51 verified ownership returns. All53 match the independent original-media decode; actual transferred bytes reconstruct the full65,534-byte queue. Fresh083/R/S/T proofs and28 prior+10 new artifact negatives pass. An initial auditor-format assumption about the pre083 generation2 IPC is preserved and corrected using the same artifacts, without a guest retry. Actual software-ring fill reaches228KiB, but I2S remains untouched: the earliest stop is the original BSU+0x400 lock request in the audio state-saving path. No production/sound claim. [Evidence and limits](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/amc-first-consumption/continuous-stream/README.md).


**Audio083 follow-up — BSU lock and partial DMA pause:** the one Stage V guest preserves all53 PCM generations/51 ownership returns, then retires the original BSU-lock and two transport-pause stores. The original driver saves actual current14ae1568 and3,750 remaining words while65,534 owned queue bytes stay exact. Stops before returning unmodeled BSU+410; all17 native targets,483 new focused checks and40 actual artifact negatives pass. No sound, saved-context or production claim. [Evidence](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/amc-first-consumption/bsu-lock-pause/README.md).

**BSU source follow-up:** original loops put the first effective FIFO word at saved+1c, correcting the earlier raw-operand overlap interpretation. The seven words are opaque in reviewed consumers; separate48KiB SRAM saves and actual power/clock mappings are identified. Resume resets/restarts before restoring, so a saved decoder generation needs a separate lifetime from active hardware. [Source evidence and proposed save-only gate](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/amc-first-consumption/bsu-lock-pause/context-source/README.md). No new register, run or production change.

**Audio083 follow-up — save boundary with preserved observer failure:** one Stage W guest performs all seven resource-derived BSU reads and reads every byte of the original48KiB SRAM save sources, then stops before forwarding the power selector request. All V prefixes/resources and40 prior artifact negatives pass. The final destination observer exceeded its4KiB helper limit, so full save acceptance fails; unavailable zeros are not guest bytes. A separate source-only artifact audit validates12,288 actual reads and two corruption negatives, without a guest replay. [Evidence, first failure and limits](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/amc-first-consumption/bsu-save-boundary/README.md). No restore/power/sound/production claim.

**Audio083 follow-up — actual saved context verified:** the observer-only Stage X successor passes the complete seven-word context, actual current/remaining input fields, and49,152-byte destination comparison plus source-read coverage. Its full guest trace and every unchanged W artifact are exact; all20 native targets and47 actual artifact negatives pass. The first W capture failure remains preserved. The pending39a0000c=3 is ONCTRL for already-ungated domains0/1, correcting the earlier “selector” label; no write is forwarded or power transition claimed. [Evidence and limits](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/amc-first-consumption/bsu-saved-context/README.md). Reset/restore, sound and production integration remain open.


**Audio083 follow-up — real suspension:** one Stage Y guest retires all four original power/clock writes and the driver's actual state3→5 store. One retained decoder generation owns the unchanged180,224 SRAM bytes and65,534-byte queue; active allocations/service/DMA/IRQ are cleared. All21 native targets and57 actual artifact negatives pass across the original audits and a separately preserved artifact-only correction. The first auditor's synthetic service1 assumption and final-USR field assumption remain documented; no guest retry. Execution stops at an unaccepted observer binding at31236650, so no HAL/sound claim. [Actual transitions, limits and failed/corrected evidence](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/amc-first-consumption/power-suspension/README.md). Reset/restore and production integration remain open.


**Audio083 follow-up — observer stopped before an unmapped page:** the single Stage Z diagnostic preserves every Stage Y guest/resource byte and stop cycle. Actual L1`15c8ac48=08511001` and L2`085110d8=0` explain all four cloned READ/FETCH fault7 results; no actual guest fetch or HAL call occurred. All22 native targets and65 actual artifact negatives pass across the original proofs and an artifact-only correction using actual snapshot RAM geometry. The original auditor's synthetic-aperture failure is retained; no guest retry. [Evidence and limits](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/amc-first-consumption/hal-binding-diagnostic/README.md). Next is the original kernel's normal fetch-fault path, not a fabricated mapping or audio event.


**Audio083 follow-up — original prefetch abort and mapped retry:** the single Stage AA run lets the unmodified CPU take the actual missing-code fault, then returns 1,636 core steps later to the same full user context with original MeCCA bytes mapped. It stops before the original StartIO entry instruction at `0x31236650`, not HALOutputProc. Exact fresh Z/083 prefixes, all retained SRAM/queue/ownership and suspension proofs pass; 23 native targets and 70 actual artifact negatives pass. No HAL, reset/restore or sound claim. [Evidence and bounds](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/amc-first-consumption/original-prefetch-retry/README.md).

## YouTube silent-clip startup diagnosis (2026-09-10)

Qualified original media-process observation proves `AudioDeviceAddIOProc` succeeds, then `AudioDeviceStart` returns `0xe00002d8`. AppleBasebandOutput reports DMA not ready immediately before that return. The mixer maps the failure to MeCCA result 1 and the generic movie error. All 252,624,866 saved bytes match the same-schedule control. A second qualified replay directly confirms the original I2S synchronization timeout: matching configuration writes, no callback hits, state 4 at return and r6=`e00002d8`. A real external-clock/FIFO/DMA model remains next; clock rate/gating and FIFO behavior are still unproven. Silent video does not bypass audio startup. [Evidence](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/youtube-audio-start/README.md) · [milestone 137](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/youtube-audio-start/README.md).
