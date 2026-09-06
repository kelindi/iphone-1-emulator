> Archived pre-run review contract. Its preparation status describes the point before075; the completed one-run result is in [README.md](README.md).

# First guest-driven functional AMC publication — review contract

Prepared and reviewed source; **no Stage G guest run has occurred**. The final native build and37 wrapper contracts passed after the CPU continuation browser quiet window. Final source includes layout-result observation, explicit boundary/counter reporting, and strict hook installation. `build.json` pins every source/header and both executable hashes; `hook-test.log` retains the37-check result. Existing frozen-core LTO maybe-uninitialized warnings remain in build logs, not changed by this isolated hook. The copied Stage F engine/FAAD implementation is byte-identical to accepted run-4. No production files, snapshot format, saved phone, or speaker output change.

Parent has accepted one bounded run after its next first-use timing window ends. Until explicit END, all AMC heavy work is held. Both external auditors and the launcher are copied into the new run directory and their hashes recorded before execution; positive and mutated-artifact checks execute that frozen copied auditor.

## Real start and scheduling

`source/probe.c` continues the fresh historical `.context/audio/ringtone-page.snap` through the existing original upload descriptor reconstruction and actual-byte/write-coverage checks. At the actual `+0=1` request, it retains the entire **pre-initialization** SRAM, expected-byte map and real write mask under `upload/`. Then it invokes `amc_engine_start` at that write. Policy bytes/mask are separately hash-pinned to the original-kernel-audited070 artifacts; they are not generated from current private guest RAM. The current guest descriptor prediction remains independent diagnostic evidence.

There is no delayed start, synthetic pre-armed admission, replayed host command, or forced070 post-start prefix. Initialization allocates the real bounded decoder/queue, initializes the checked header and latches200 at the genuine command. Later driver behavior follows that actual state. Unknown behavior remains an earlier preserved stop.

The disposable probe uses `emu_run(1)` from the initial fixture, with the same genuine touch location held for5M retired instructions. After each retired instruction, it performs at most one reviewed engine service call. Hardware timer ticks are never equated with retirement or manually advanced. This is a declared functional scheduling policy, not measured DSP latency and not the historical50k batching schedule. A zero/multiple-retirement return is a diagnostic stop. Initialization and transfer stores are logged while not yet retired; the publication boundary is explicitly outside the CPU call.

IRQ18 is driven only from actual pending events AND their masks. Bus-side level changes dirty the existing device graph; service-side changes use the existing zero-tick refresh to derive the CPU IRQ/FIQ through the original VICs. Neither path fabricates interrupt entry or elapsed time. On first publication the probe stops immediately **before** the next guest instruction and before applying the newly produced output IRQ level. Existing input IRQ delivery before that boundary remains genuine modeled delivery.

## Input, actual host layout, and positive stop

The original caller/vtable/physical descriptor capture remains observational. `+1004/+1008/+100c` provide the engine's real physical DMA address/count; only those DMA-owned bytes feed FAAD. Guest packet metadata and original media are external test oracles, never engine inputs. Configuring FAAD uses only the accepted program policy and the exact original device-visible session writes.

The hook now observes original `c0276010` get-layout arguments and `c0276014` results. It requires the original helper to return0 and the actual outputs to be offsetsf220/11220 with capacities8192/8192. This source computes the original layout in `stage-d/apple-amc-all-dis.txt:c0271680..16d8`; the probe does not substitute those outputs. The session write sequence remains the reviewed engine's exact16-bit1b,2,0,3 acceptance. First publication can occur before the session helper returns, so no claim requires or invents that later return.

Positive stop: first nonempty decoded PCM buffer, count/owner/event committed, before guest consumption. Actual queued bytes, SRAM, the three disjoint-purpose write masks, first PCM, counters, CPU/timer context and trace are retained. One publication is the stop; two is an additional hard guard. The context is not a portable snapshot because isolated decoder/device state is not serialized. Unsupported MMIO stops inside an unretired instruction; service/budget stops are labeled between instructions. An admitted quiesce that precedes the target is explicitly labeled and preserves its actual effect.

## Fixed limits and first unsupported witness

`launch.py --build` only compiles the probe and runs synthetic wrapper contracts. `launch.py --run NEW_DIRECTORY` is the separately reviewed guest experiment, never the default. Every output directory must be new. Source/header/API/archive/policy identities and build commands are captured; the run rejects stale rebuilt source or unexpected binaries.

The native process is limited to240s wall,180s CPU,1GiB address space,8MiB per artifact/log file, no core dump,155.3M total retired instructions/calls,5M instructions between start and first input, and5M after first input. A timeout, cap or unknown access preserves its prefix and cannot produce a success report. The1GiB process limit includes the full256MiB phone and snapshot-loading copies; the earlier256MiB standalone decoder-process bound is not incorrectly applied to the full guest.

Unknown AMC/SRAM writes reject before mutation. All active clock/power/reset writes are conservatively stopped before physical mutation; no unreviewed lifecycle admission is added. Valid masks/acknowledgements use the reviewed engine; repeated input, altered physical spans, a missing original caller or incompatible configuration stop. The wrapper tests separately exercise active lifecycle/staging rejection, canonical owner release, and stopping at publication without retirement or output IRQ refresh. Their two synthetic PCM samples are a unit-test-only sentinel, never part of the real guest probe.

## External positive and negative audits

After a run, the unchanged `stage-d/source/kernel-oracle.py` reconstructs the **original Mach-O** program and compares every pre-start SRAM byte, declared byte and RMW write footprint under `upload/`. Its existing source/coverage corruption checks remain. This audit runs outside the emulator; it is not falsely described as part of the start callback.

`audit.py` then uses root's independent072 original-MP4 box/sample parser. It verifies the actual three16KiB captures, all64KiB capacity including absent continuation, all50 metadata offset/size records, exact first-two raw frame consumption, remaining owned queue, and first4096 PCM bytes against the independently packetized full-media decode. The full PCM oracle hash is pinned. Original private media never supplies bytes to the device implementation.

It checks actual host layout, initialization/session/ownership values, exact init/PCM masks and bounded host mask, then every SRAM byte outside the masks against the original upload. It requires first publication after a retired boundary, no STOP line, and no saved-state/playback claim.

Finally the launcher invokes the **same auditor** twice on separate artifact sets: only one input byte, then only one PCM byte is corrupted. Each must exit nonzero at its intended equality check; logs are retained. These are actual rejection controls, not merely local unequal-byte assertions. Positive evidence and all first failed prefixes remain immutable. An independent parent audit remains welcome before recording any milestone.

## Review/execution commands

Completed build/wrapper gate: `python3 .context/amc-first/stage-g/launch.py --build` (37 checks pass).

Reviewed, but held until root first-use timing END: `python3 .context/amc-first/stage-g/launch.py --run .context/amc-first/stage-g/run-1`.

No speaker, audio sample clock, host consumption, decoder snapshot, or production compatibility claim follows from this first buffer. Those are later gates.
