# All-fallback PC census — verified measurement, no speed change

The [retained warm Settings profiles](../warm-settings-reassessment/README.md) identify interpreter execution as the strongest measured remaining CPU target. Prior generated-exit counts cannot quantify instructions in wholly unseeded stretches. This isolated observer is intended to close that gap. The final observer and browser census now pass the gates below. No speed gain or production CPU optimization is established.

## Frozen questions and partitions

Count every actual arm_step call, its original status, and whether the original interpreter fetched an instruction. ARM_OK includes interrupt/prefetch/data-abort handling, so it is called a completed interpreter step, not necessarily a successfully committed guest instruction. Fetched ARM/Thumb opcodes come from the existing local variables after the original fetch. No observer MMU walk, bus read, RAM lookup, register write or guest binary patch is allowed.

Each row contains original PC, mode, actual fetched opcode, original status, event flags, seed admission and actual attempt provenance. Different opcodes at one PC remain distinct, preserving code replacement/aliases.

Independent admission categories are:

1. The 1 KiB base is absent from both original ARM and Thumb seed-region maps.
2. The base exists only in the other mode.
3. This mode's original region exists but its supported-position bit is absent.
4. The original bit exists, but the runtime actually invalidated/disabled its generated function.
5. This position remains admitted.

Orthogonal provenance records no attempt, disabled execution, actual region/mask rejection, actual runtime guard rejection with a field bitmask, generated function returning zero, actual stale callback followed by zero, or the mandatory interpreter step after a positive generated prefix. An attempt/step PC-mode mismatch is explicit and must be investigated. A generated zero return without a stale callback is **unknown reason**; it is not guessed to be a memory fault or unsupported opcode. The unchanged seed does not export its local exit reason. Interpreter FIQ/IRQ/prefetch/data-abort events and original status remain available separately.

## Isolation and bounds

[prepare.py](source/prepare.py) produces isolated copies of arm_interp.c and jit-runtime.c. Reversing its exact substitutions restores both original sources byte-for-byte; no original decoder/helper/return is replaced. The wrapper calls the entire original arm_step once and returns its exact result. Every successful original install updates an independent seed map; original lookup state, masks, table entries and last_region are never queried by calling region_find again. Actual original stale events update the shadow active bit.

The collector has a fixed 2,048-slot admission map and 524,288-row table (20 MiB), with at most 64 row probes per observed step and an odd second-hash stride (a full permutation of the power-of-two table). Counts are uint64_t. Every unrecorded row increments overflow; any overflow, map overflow, unexpected install/reset during a capture, or provenance mismatch rejects a coverage conclusion. It does not drop events silently or extend the run. This observer intentionally adds host work and memory; it is unsuitable for speed measurements.

No arm_cpu_t field, seed ABI, generated body, seed admission policy, device budget, interrupt policy, MMU/TLB accounting, CPU-cycle count or snapshot format changes. The existing original seed and supported masks must be byte-identical. Disabled observation still adds wrapper/call overhead; that is host instrumentation, not an optimization.

## Required preflight before one browser diagnostic

- Run isolated collector contracts for every admission/provenance class, distinct same-PC opcodes, negative status, IRQ/prefetch/data abort, disabled/reset/start/stale behavior, and exact row/count conservation. Tiny-capacity saturation must positively produce overflow and reject acceptance.
- Run deliberate observer mutations that omit a fetched event, misclassify missing admission or hide overflow; independent expected counts must reject them.
- Build an isolated observer core and a no-observer control from the same pinned production inputs. The latter must match the actual release WASM. Retain compiler/link/archive/source hashes and source projection receipts.
- Exercise the actual original interpreter and generated runtime with representative ARM/Thumb conditional, load/store, MMU fault, physical alias, pending IRQ/FIQ, stale current code, missing mask, zero-return, positive-prefix and exact-budget paths. Require identical CPU/RAM/device/snapshot and TLB/read/JIT policy counters against the uninstrumented engine. Only host observer memory may differ.
- Run one bounded deterministic 100M replay from the same original MBX fixture on both cores, requiring full raw snapshot identity and all counters. This is functional proof, not a timing comparison. No observer data is serialized into guest state.

The isolated collector contracts pass ASan/UBSan, including all attempt labels, distinct same-PC opcodes, disabled/reset/stale lifecycle, mid-capture installation detection, map overflow, and tiny-row-table overflow conservation. Three deliberate mutations—missing fetched flag, wrong admission and hidden overflow—are rejected by independent assertions. [Logs, negative results and source pins](source/pins.json) are retained. Actual interpreter/runtime boundary oracles, the WASM build and deterministic/browser guest gates have **not run** at this checkpoint. Root's final release smoke has ended; further guest work still requires review of this frozen contract.

## One browser capture contract, pending final pins/review

Use the just-verified production runtime/loader/UI, original d9c661 seed and identical normal-import MBX fixture, copied into an isolated site. The seed defines 512 regions; ordinary startup currently installs 511, whereas normal import/reset can install 512. Freeze the expected active count from the exact import fixture and path, and require that observed count before execution; do not impose 512 on ordinary startup. Perform a first Settings launch and Home return to warm the genuine phone, then collect exactly one warm Settings action and one Home control. Preserve ordinary input pulses, foreground clock, Save cadence and original readiness detector. No profiler is needed: this census measures guest step counts, not sampled wall residency.

Start/stop the collector on the emulator worker, recording retired/generated counters in the same worker commands. Stop after the existing readiness observer confirms the action; record the exact inclusion of its confirmation interval rather than calling all collected instructions release-to-ready work. Cap each action at the existing 15-second readiness limit and the entire session at 240 seconds. Stop on first error or any overflow; no retry or cap extension. Capture the complete session and retain independent baseline Settings/Home references, settled raw pairs and the existing RGB/status-bar crop threshold. Final reference/pin files must be frozen before START.

Require complete row conservation: calls = ARM_OK + other statuses = fetched + no-fetch = sum(rows) + overflow. For the exact observer bracket, completed interpreter steps plus generated retirements must equal the machine's retired counter delta; explicitly investigate exceptional accounting if this does not hold. Keep generated calls/retirements, seed installations/retirements and original device/graphics errors. Screenshot agreement and deterministic preflight state equality are separate gates; no claim of byte-exact browser states from wall-timed input is permitted.

Offline analysis may run the existing emitter's exact admission logic on observed opcodes and pinned page bytes. It must not call a PC "supported" from an instruction mnemonic alone, ignore changed bytes, or infer dynamic benefit from call counts. Rank wholly absent pages and missing positions by actual completed steps, with exceptions and unknown exits separate. Only a material, measured population can justify a later seed-selection proposal. Parked continuation, lazy groups and micro-caches remain parked.


## Preserved observer failures and bounded repair

The first 262,144-row linear-probing version stopped on two overflowed observations. Its raw table and exact stopping instruction count were not persisted; the failure log and complete source/binary are preserved, and no coverage claim is made. A final avalanche fixed the demonstrated high-PC-bit clustering, but the separately authorized successor still stopped at **41M steps, 158,397 rows (60.4% load), one overflow**. Its exact aggregate counters survive: 12,796,608 completed interpreter steps plus 28,203,392 generated instructions equals 41M, with 2,081 no-fetch events. The triggering table was again lost when the assertion terminated the process; this is explicitly not a replay of its actual keys.

Root authorized one final observer repair: **524,288 rows / 20 MiB, odd-stride double hashing, unchanged 64 probes and guest/time caps**. Extrapolating 158,397/41M linearly gives roughly 386k rows at100M; that is a sizing heuristic, not measured final cardinality. Retained 226 actual historical PC/opcode/mode examples and synthetic high-bit patterns test hash behavior, but are not substituted for the lost table. Focused tests also exercise 386k distinct synthetic rows, an adversarial same-slot/same-stride 65-key set (64 admitted, one explicit overflow), a full tiny table, and exact conservation. Future captures persist raw table bytes and counters at checkpoints and **before** failure assertions. No further automatic table change or run is authorized if this successor loses observations.


## Completed real-runtime and browser gates

The no-observer relink is byte-identical to the released RTC core **7a72add6…9088**. The final **c8d213d0…d89f** observer preserves every original CPU/runtime statement under inverse source projection and uses the original **d9c661…3881** seed. All **404 actual-emitter scenarios / 1,212 separate-module engine cases** match architecture, RAM, TLB/read counts, generated counters and applicable timer snapshots. The final 100M replay passes all four complete raw snapshot checkpoints and exact final RGBA; 26,906,160 interpreter steps plus 73,093,840 generated instructions equals 100M. Its 285,763 rows have zero overflow. The [parent's independent full-state review](root-100m-review.json) and [concise result](correctness-100m.json) are retained. The original failed observers are preserved in source/v1-overflow and source/v2-overflow.

The single actual browser session passes **five settled full-RGB/alpha frame comparisons and manual reviews**, all counter conservation gates, exact served core/seed bodies and closure of all eleven scoped Chrome process identities. Normal paused import installs 512 regions. Ordinary startup also happened to report 512 in this particular observed session; startup was recorded, not forced to a universal 511/512 value. The UI is the frozen verified release before the independent visible-recovery UI change. Root's permitted native Wi-Fi coldboot overlap is recorded, and the observer itself adds substantial CPU/memory/transport work. **Its elapsed times are not navigation performance results.**

[Full uncut 56.40-second recording](../../../tweets/videos/warm_fallback_census_diagnostic.mp4) · [actual 49-second thumbnail](../../../tweets/thumbnails/warm_fallback_census_diagnostic.jpg) · [browser report](browser/report.json) · [five-screen manual review](browser/manual-review.json). The recording includes diagnostic normal-import setup, the preparation Settings/Home pair, and the collected warm Settings/Home pair. The readiness confirmation interval is included in the counted worker brackets, as predeclared. Source and encoded video duration are identical.

| Actual completed steps | Warm Settings | Home control |
| --- | ---: | ---: |
| Total worker retirements | 111,871,289 | 22,261,486 |
| Generated | 81,660,737 | 14,919,424 |
| Interpreter | 30,210,552 | 7,342,062 |
| Outside original seeded bases | 13,189,241 | 3,742,734 |
| Other mode at seeded base | 76,942 | 16,975 |
| Missing supported-position bit | 9,514,709 | 1,961,117 |
| Invalidated function, original bit present | 12,799 | 2,835 |
| Admitted position | 7,416,861 | 1,618,401 |
| No-fetch interpreter events (included above) | 3,815 | 1,136 |
| Distinct rows | 286,966 | 145,032 |
| Dropped observations | **0** | **0** |

The [independent admission audit](root-admission-audit.json) reproduces every static page/mode/mask category in all 431,998 rows and rejects ten altered-category controls. It combines active/inactive when testing static definitions; it does not pretend to reconstruct the runtime invalidation history independently.

## Actual-emitter ranking and concrete next candidate

[rank.mjs](rank.mjs) uses the unchanged compiler's actual compileRegion masks on observed opcode values. Across both windows, 1,621 PCs have multiple observed opcodes; those positions are excluded. No-fetch and data-abort events are separate. Of the 13.189M outside-base Settings steps, 12.042M are syntactically emittable with stable observed bytes. This is an upper bound: original raw-code, fetch, data-cache, alias and budget guards can still force fallback. The raw samples and script are pinned; unobserved bytes are not invented.

[cohorts.json](cohorts.json) additionally excludes mandatory fallback after a positive generated prefix when ranking absent pages. The best 8/16/32/64 absent pages then cover **1.623/2.207/3.064/4.300M** direct rejected attempts, respectively. No observed opcode changes occur anywhere in these top 64 pages. The largest is Thumb page **0x3001d800**; the next is ARM page **0x30731400**. The opportunity is spread across many pages, not one dominant removable function.

The comparison with the parked 959-position candidate is explicit. Only **20,574** direct missing-bit attempts in this Settings window were already admitted by that old candidate; **3,862,929** direct attempts occur at newly measured positions. Its other **699,015** already-covered syntactic observations follow positive prefixes and receive no automatic credit. The hottest new position is **0x3059211c, ADD r3,r3,#1**, with 212,480 direct attempts: it follows the LDR at 0x30592118 that the old exit-only census already added. Counting every interpreted step exposes the following sequence that an exit-only census missed.

The proposed isolated mixed candidate adds the strongest **64 absent pages plus 2,048 new positions across 71 existing regions**, preserving every original one of the 512 regions and all original admission bits. The directly observed syntactic upper bound is **7,857,848 Settings steps (7.02% of this bracket's total)**; this is neither predicted retirement gain nor predicted elapsed saving. Root authorized implementation with an explicit 576-region cap and **16 MiB maximum seed**, unchanged compiler/guard semantics and ABI offsets. Exact body-size/compilation budgets, raw-byte provenance and lifecycle/correctness gates must be frozen before any timing. No random original-page eviction, parked continuation or guest-binary patch is part of this proposal. The two-second navigation goal remains open.
