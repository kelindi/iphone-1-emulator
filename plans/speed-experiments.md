# Browser execution-engine experiments

> Historical recordings and publishing artifacts have been removed from Git history. The summaries below describe the original runs; remaining test reports and screenshots are retained. Historical hash receipts may predate this archival cleanup.

Measured on 2026-09-05 from baseline `b0a7399`, in the Amazon Linux x86_64 workspace: Intel Xeon @ 2.90 GHz, GCC 11.5.0, Emscripten 4.0.16 and Node 24.14.1. These are execution-throughput experiments on a shared VM, not browser interaction acceptance results. The [product performance gates](web-iphone.md) remain unmet.

`tools/bench/replay.c` measures only execution after restoring the shipped 1A543a home snapshot. Snapshot reads/writes, compilation and profiling are outside the reported execution interval. The initial snapshot is at 9,790,000,000 retired instructions. Native 400-million-instruction runs end at 10,190,000,000; most initial WASM comparisons used a 100-million-instruction prefix.

## Retained improvement

The existing direct RAM-write cache was disabled in the generic frontend. Its public opt-in checks that the machine bus has no write observer. Enabling it improves the measured baseline without changing guest timing.

| Three-run median | Before | Direct writes | Change |
|---|---:|---:|---:|
| Native, 400M instructions | 43.99 MIPS | 46.07 MIPS | +4.7% |
| Node/WASM, 100M instructions | 27.99 MIPS | 29.85 MIPS | +6.7% |

The final snapshots differ only in serialized TLB diagnostic hit/miss counters and the corresponding checksum. CPU architectural state, RAM and devices match. This modest improvement does not establish original-device speed. Future dirty-page checkpoint tracking must account for direct CPU stores, DMA and other memory writers.

## Rejected C-engine experiments

A one-billion-instruction `perf` sample on the native baseline attributed 26.9% to `arm_step`, 20.8% to Thumb execution, 11.2% to the machine run loop, 7.6% to word reads, and 7.2% to ARM data processing. Instruction entry and memory traffic matter alongside decoding.

| Experiment | Native median | Node/WASM median | Decision |
|---|---:|---:|---|
| Combined NZCV flag updates | No improvement | 28.04 vs 27.99 MIPS | Reverted |
| Raw-opcode decoded classes and simple ALU operands | 45.10 vs 46.07 MIPS | 29.22 vs 29.85 MIPS | Reverted |
| Multiple ARM/Thumb register instructions per CPU invocation | 49.87 vs 46.07 MIPS | 28.18 vs 29.85 MIPS | Reverted |
| Same runner with cache-proven ordinary RAM accesses | 52.70 vs 46.07 MIPS | 29.08 vs 29.85 MIPS | Reverted |

The multi-instruction runner used the existing exact timebase retirement limit and stopped before uncached memory, MMIO, WFI, coprocessor operations, exceptions, and unsupported instructions. Hook eligibility and pending-retirement accounting remained in the original machine loop. It reread raw code and required valid fetch/data cache generations. No snapshot fields changed.

All 23 tests available at that stage passed, and final snapshots matched the corresponding interpreter baselines exactly. Link-time optimization and forced helper inlining did not recover the WASM regression. Native-only gains were insufficient for this browser objective; production core/header files were restored.

## Actual runtime-generated WebAssembly

An isolated experiment then emitted real WebAssembly guest instructions instead of looping over C semantic helpers. It lives under `.context/jit-experiment/`, outside the production build.

The experiment:

- Profiles hot guest PCs in an isolated copy of the existing bounded machine loop.
- Imports the same Emscripten `WebAssembly.Memory` into generated modules.
- Installs generated exports into the Emscripten function table, allowing direct C-to-WASM execution without JavaScript calls for each block.
- Emits ARM/Thumb ALU operations, conditions/flags, branches, ordinary cached RAM accesses, and selected LDM/STM/PUSH/POP transfers.
- Validates the current fetch privilege/generation and pending interrupts before entry. Each generated instruction validates its current raw encoding and exact remaining instruction budget. RAM helpers admit only already-authorized cache spans, otherwise returning to `arm_step` before modifying that instruction's state.
- Leaves instruction families outside the prototype on the existing interpreter. No Apple binary or guest clock modification is involved.

The prototype uses [wabt.js](https://github.com/AssemblyScript/wabt.js), pinned to 1.0.39, to assemble generated WAT into WASM. A production compiler could emit binary modules directly; the current WAT assembler and emitter are research tooling.

### Small blocks

2,048 functions, including 1,438 Thumb functions, produced a 1.02 MB WASM module. Intrusive profiling took 5.9 seconds; generation/assembly took 342 ms, WebAssembly compilation 8.6 ms, and linking/table installation 4.4 ms.

Only 23.8% of the 100M guest instructions executed in generated functions. Calls averaged 3.4 retired instructions, including zero-work calls. Three alternating runs measured approximately 22.3 MIPS with generated code versus 31.0 MIPS with the same instrumented build's interpreter. Final snapshots matched exactly.

### Compiled regions

Grouping instructions into internally dispatched regions reduced repeated returns to C. The final prototype compiled 512 regions of 256 guest bytes, including 332 Thumb regions. An entry bitmap avoided calls at statically unsupported instruction positions. Larger 1 KiB regions exceeded the prototype WAT assembler's recursion/stack capacity; the smaller regions are an implementation limit of this experiment, not proof of a browser WebAssembly limit.

The generated module is 3,120,226 bytes. In the final run, intrusive profiling took 6.23 seconds, generation/assembly 1.06 seconds, WebAssembly compilation 5.9 ms, and linking/table installation 2.1 ms. These phases are separate from steady execution and from first-use browser optimization.

| Paired 400M-instruction replay | Interpreter | Generated regions |
|---|---:|---:|
| Execution time | 13.47 s | 14.82 s |
| Throughput | 29.70 MIPS | 26.99 MIPS |
| Instructions executed in generated code | 0 | 160,401,988 (40.1%) |

The generated run made 36,807,957 calls, of which 2,589,830 retired nothing. Positive calls averaged 4.7 instructions. The remaining unsupported operations and cache misses still interrupt execution frequently, and the entry/dispatch/checking costs outweigh the translated work in this prototype. The earlier three-pair 100M comparison reached the same conclusion: approximately 27.0 versus 31.0 MIPS.

### Correctness evidence

- The final 400M generated replay snapshot is byte-identical to its paired interpreter control and to the original uninstrumented Node/WASM replay with direct writes enabled.
- Fifty focused checks passed: ARM and Thumb register/flag/RAM/stack-transfer loops compared with the literal interpreter over several initial flags and retirement budgets; rejection checks covered changed raw code, stale fetch generation, pending IRQ, changed fetch privilege and unaligned entry PC.
- Earlier 100M snapshots also matched the native baseline exactly. At 400M, the original native and original WASM baselines differ in VFP state; that cross-platform difference is not introduced by this JIT and needs a separate investigation before claiming universal native/WASM byte equivalence.
- The generated engine was not promoted into production. Full app workflows, cold boot, Safari browser testing, broader instruction coverage, and production compiler lifecycle checks remain outside this proof.

## What these results establish

Runtime WASM generation with shared guest memory and interpreter fallback works, and compilation itself was inexpensive at this scale. It has not yet improved whole-guest speed. The next engine attempt must increase useful coverage and work per entry, reduce admission/fallback overhead, and retain exact memory/interrupt behavior; it must demonstrate a browser gain before integration.

Separately, reducing a long app-close black interval through a rendering fix may improve the current experience more than another low-coverage engine experiment. That is a different acceptance result from CPU throughput.

Local evidence: `.context/replay-*.log`, `.context/replay.perf.data`, `.context/decoded-cache-experiment.patch`, `.context/fast-block-experiment.patch`, and `.context/jit-experiment/{run.log,regions-filter.log,final.log,final.err}`. The `.context` tooling and binaries are not tracked production artifacts. `INSTRUCTIONS=400000000 TRIALS=1 node .context/jit-experiment/run-regions.cjs` reproduces the final JIT replay in this prepared workspace.

## Connected basic blocks: second WASM prototype (September 5, 2026)

A second isolated experiment in `.context/jit-v2/` produces a real improvement
on a Settings launch replay. It is not integrated into the browser release.
It retains the exact interpreter clock and unchanged guest binaries.

The compiler groups hot code into 512 regions of 1 KiB. General registers and
CPSR stay in WebAssembly locals until a region exits. Nested dispatch selects
a basic block and an instruction suffix, permitting exact retirement-budget
exits without refusing a partially fitting block. Authorized ordinary RAM
cache accesses execute inline. ARM/Thumb arithmetic, selected multiword
transfers, ARM multiply and signed/halfword loads, and raw VFP single/double
loads and stores cover the measured hot paths. VFP arithmetic still falls back.
This avoids the previous experiment's repeated CPU-structure traffic and
short-block entry costs.

The same C machine retirement loop invokes generated exports through the WASM
function table; JavaScript does not mediate each block. Fetch privilege,
cache generations, raw code, IRQ/FIQ, aborts, alignment and instruction budgets
remain checked. Stores that could overlap the active physical code page exit
**before the write**, even through another virtual mapping. Unsupported or
uncached accesses return to the literal interpreter before that instruction
changes architectural state. No snapshot schema changes are needed.

A 600M-instruction replay starts from the pristine compact Home checkpoint,
presses Settings for 5M instructions, releases it, and executes the remaining
595M. Alternating runs in one Node/WASM process measured:

| Run order | Interpreter MIPS | Generated MIPS |
|---|---:|---:|
| Interpreter, generated | 30.75 | 39.45 |
| Generated, interpreter | 30.85 | 42.48 |
| Interpreter, generated | 30.19 | 43.40 |

The median ratio is **1.38×**. The last warmed pair reaches 1.44×; the first pair
is 1.28×, so first-use engine tiering cannot be omitted from user-facing claims.
The final complete snapshots match byte for byte. All generated runs retire
481,010,605 instructions in generated code (80.17% coverage), with 57,720,070
calls, averaging 8.33 useful instructions per call. This falls short of the
experiment's 2× throughput / 16-instruction target.

The module contains 76,964 supported instruction positions and 18,285 basic
blocks, totaling 12,273,829 bytes. WAT assembly takes approximately 4.2 seconds;
WebAssembly compile/link takes 22 ms before later execution-tier optimization.
These are separate costs. The prototype selects regions from a previously
recorded Settings trace; it has not proved a production hot-code collection,
background compilation, code-budget or eviction lifecycle.

160 focused checks now cover ARM/Thumb register/flag/RAM loops against the
literal interpreter, exact small retirement budgets, changed raw code, stale
fetch generation, IRQ/FIQ, pending abort, invalid mode/entry alignment, and
fetch privilege. An actual MMU-alias self-modification case warms the write
cache through one virtual address, attempts to replace a later instruction
through that alias, verifies pre-write exit and stale-code rejection, then
compares the entire resulting snapshot. Four further oracle cases verify
unaligned read/write data aborts, a privileged write-cache entry reused from
user mode against a read-only mapping, and permission changes followed by
real TLB invalidation. Their exception state and snapshots match. Another 42 machine cases compare exact returned instruction counts and complete snapshots across small budgets, timer IRQ/FIQ edges, WFI yields, dirty MMIO, user SVC and pre-step hooks; callback counts are explicitly checked.

Browser tap-to-ready results, more app and cold-boot coverage, production
compiler lifecycle, remain gates before promotion. The earlier section's conclusion that generated
WASM had not improved throughput applies to the first experiment; this second
prototype improves deterministic guest throughput, while full-speed browser
use remains unproved.

Local evidence: `.context/jit-v2/{run.cjs,trial.c,machine.c,long-paired.log,
fault-checked.log}`. Reproduce the extended paired replay with
`REPEAT=1 BLOCKS=512 INSTRUCTIONS=600000000 node .context/jit-v2/run.cjs` in this
prepared workspace. Subsequent runner revisions additionally print and compare
the SHA-256 of every final snapshot, not only the last pair.

### Where Settings spends guest instructions

The fresh 600M Settings census captures 557,414,127 opcode observations; its
bounded profile table drops 7.1% of observations on collisions, so these shares
refer to captured instructions. Mapping actual Mach-O `__TEXT` ranges and
defined symbols places **30.423% in CoreGraphics**, including **27.635% in
`_layer_blit_stack`**, with additional ARGB blending/raster routines. libSystem
accounts for 9.293% and CoreFoundation 5.376%. This is strong evidence that the
software graphics path is a major CPU cost. It does not establish which
fraction the existing MBX model can replace. A separate clean-image probe
with MBX2D enabled is needed; the current work image explicitly disables MBX2D
and OpenGL.

Evidence and image-aware mapper:
`.context/speed-next/{settings-profile.tsv,map-profile.py,settings-symbol-profile.txt}`.

### Paired Chrome interaction probe

The same isolated v2 core was built for Chrome with the copied current worker
clock policy. Both arms import the identical `usb-ready-home.snap` fixture,
receive 30 ms real mouse presses and use the maintained Settings/Home readiness
probe (stable guest layout for 500 ms, reporting the start of that interval).
The generated module uses the fixed recorded Settings-region selection.

| Probe | Settings first / second | Home first / second |
|---|---:|---:|
| Interpreter control | 4.10 / 4.30 s | 1.20 / 1.22 s |
| Generated regions | 3.25 / 2.25 s | 1.00 / 1.15 s |
| Reversed interpreter control | 4.05 / 3.95 s | 1.15 / 1.125 s |

All three runs passed; screenshots show genuine Settings rows and Home. This
is a small comparison, not p95 or proof of original-device speed. The first
generated launch includes execution tiering effects; the second is warmer.
Recordings, screenshots and complete reports are in
`.context/jit-v2/browser-{control,generated,control-repeat}/`. Production worker
changes are deliberately kept out of this frozen pair. A third isolated
experiment in `.context/jit-v3/` now tests selection from live guest fetches
and compilation in a second worker, with bounded function/module retention,
raw-code invalidation and cancellation across restore.

### Live compiler lifecycle and remaining regression

The live-selection v3 experiment remains isolated. It collects executed guest
instruction positions, compiles those regions in a second browser worker,
and installs exports sharing the emulator memory. A small restricted binary
encoder replaces WABT in this candidate; unsupported generated forms fail
closed. Retention is capped at 512 functions and 32 MiB of retained module
binaries. Replaced functions are removed from the function table; raw-code
mismatches retire their region. Restore advances a generation and terminates
the old compiler so delayed results cannot install into a different phone.

Five controlled lifecycle contracts pass: reset during asynchronous
instantiation, stale-generation rejection, table reclamation, byte-cap
admission, and compiler-failure interpreter fallback. These are controller
checks, not a substitute for browser pause/import/restore testing.

The latest completed dynamic Chrome probe does **not** justify promotion:
Settings took 5.85, 5.10 and 3.95 seconds, while Home took 2.10, 1.22 and 1.40
seconds. The controller performed 1,081 compilations before filling 512
retained regions (12.19 MB); measured compiler-worker work totaled 6.09
seconds. Collecting instruction coverage and repeatedly replacing growing
regions impose considerable first-use cost. The fixed-trace v2 gain therefore
cannot yet be claimed for the dynamic browser engine.

A modern USB-enabled snapshot replay initially failed equality. Narrowing
showed the mismatch occurred before any generated instruction executed; an
interpreter-only repeat reproduced it. The isolated harness reset the Z1
uptime milliseconds but omitted its fractional tick accumulator, which v39
does not store. Production `emu_load_snapshot` already reanchors both. The
harness now follows that same rule; a rebuilt replay must pass before using
its new results. No generated ARM correctness conclusion follows from that
harness failure.

Evidence: `.context/jit-v3/browser-cap-stops-trace/report.json`,
`test-lifecycle.cjs`, `modern-narrow1m.log`, `repeat-baseline.cjs`, and the
reproducible `build-node.sh`. Browser pause/import/restore, more applications,
and improved cold-start selection remain required before integration.

### Guarded seed candidate and stronger oracle coverage

A simpler isolated candidate now prefetches the fixed-profile generated module
while firmware loads, then installs its 512 guarded regions. It performs no
live sampling or runtime source compilation. Rejected/stale regions retire and
use the interpreter. This avoids the measured compilation churn without
pretending that the seed covers every app.

The seed contains 12,341,313 bytes. Its metadata binds the exact module and
region definitions by SHA-256. Before installing any export, the controller
also compares a C semantic ABI revision and all 16 C-computed layout offsets
against the seed metadata. A version/layout mismatch disables the seed and
retains interpreter execution. The module is bounded to 32 MiB and 512 region
exports. Restore clears function-table entries and advances a generation;
delayed instantiations cannot install into the restored phone. Six controlled
controller tests cover these lifetime, ABI and capacity rules.

The corrected modern USB-enabled replay now passes 100M instructions with
live compilation. The fixed seed additionally passes six complete snapshot
SHA-256 comparisons at 100M intervals through a 600M Settings/Home workflow,
including a second run in the same WASM instance restored through the actual
browser `emu_load_snapshot` memory API. It executes 496,605,837 instructions in
generated code (82.77% coverage), averaging 8.72 instructions per generated
call. The separate FILE/NODERAWFS replay restore traps inside libc cleanup
after the long control; the browser's memory restore does not reproduce that
trap. That harness I/O discrepancy remains recorded rather than attributed
to guest compilation.

An independent frozen-emitter review passed 64,352 differential comparisons:
ARM/Thumb arithmetic, shifts and predicates; invalid/valid BX exits following
prior work; multiplication; cached memory and permission/alignment cases; and
grouped dispatch plus stale-code exits. Its compiler uses WABT, as does the
prebuilt seed. Those results do not by themselves validate the live
candidate's separate restricted binary encoder.

Browser mode validation verifies the actual worker reports enabled regions
before timing. The first attempted seed comparison mistakenly copied a
production app that did not forward the experimental flag to its worker;
both arms therefore ran the interpreter. Those reports are preserved and
excluded from acceleration claims. A corrected three-arm comparison against
the actual current production build, the enabled seed and its disabled
control is required before promotion.

Evidence: `.context/jit-v3/{modern-fixed100.log,modern-seed600-memory.log,
seed-replay.cjs,test-seed-lifecycle.cjs,seed-web/seed-abi.json,
seed-mode-validation/report.json}` and
`.context/compiler-oracle-review/{summary.json,README.md}`.

The corrected three-arm browser comparison completed, with actual seed
execution asserted in the worker:

| Arm | Settings first / second | Home first / second |
|---|---:|---:|
| Current production build | 4.90 / 4.85 s | 1.41 / 1.30 s |
| Guarded fixed seed | 5.30 / 4.50 s | 1.33 / 1.10 s |
| Same experimental core, seed disabled | 7.35 / 5.25 s | 1.78 / 1.85 s |

These results **do not justify promotion**: first-use Settings is slower than
production and the warmer improvement is small. A brief unrelated compilation
also overlapped the later part of the window, so these remain bounded
observations. The disabled experimental core is substantially slower, making
an improvement against that control insufficient. The source/archive audit
found identical headers and core objects apart from the JIT machine hook and
concurrent MBX work. That hook calls the JIT admission function on every inner
interpreter iteration, even when disabled. A new isolated v4 comparison uses
the exact current production archive/build flags and gates outside the inner
loop so disabled execution uses the literal production loop. Authoritative
same-instruction comparison must precede further browser claims.

Evidence: `.context/jit-v3/verified-{production,seed,disabled}/report.json`
and `.context/jit-v4/{machine-gated.c,machine-hook.c,build.sh,bench.cjs}`.

### Maintained guarded seed candidate (2026-09-05)

The exact-current-archive investigation isolated real disabled-engine overhead: the prototype called its lookup helper inside every interpreter iteration, even with generation disabled. Moving its enabled check outside the complete batch restores the literal interpreter loop. Three alternating 100M-instruction passes produced identical complete snapshots for all 15 comparisons: original median 21.49 MIPS, old disabled hook 20.46, gated disabled 22.02, guarded seed 38.01, and seed with bounded last-region lookup 41.10. The shared V8 module warms across variants, so these are bounded engine diagnostics, not first-launch product figures. Evidence: `.context/jit-v4/paired.log`.

A quiet actual-Chrome comparison against the then-current production build measured Settings at 5.55/5.35 seconds versus the gated seed's 5.10/4.00, and Home at 1.34/1.30 versus 1.14/1.25. The seed handled approximately 78–80% of instructions during measured intervals, retaining 509 of 512 regions after three stale-code retirements. These two-round results justify a maintained candidate but do not establish a universal first-launch gain or the product's p95 target. Evidence and recordings: `.context/jit-v4/chrome-{production,seed}/`.

Zero-budget export warmup was proven state-neutral by full before/after snapshot identity, and V8 logs showed 476/512 exports optimized. However, actual Chrome spent 1.025 seconds synchronously warming the exports, with no first-Settings improvement (5.55 seconds). This warmup is deliberately omitted from the maintained implementation; asynchronous browser compilation and ordinary execution handle optimization.

The seed-only implementation now lives in `web/jit-runtime.c`, `web/seed-jit.js`, and `tools/jit/`. It contains no live sampler, profile arrays, native snapshot FILE harness, or exit-reason profiler. The browser build conditionally enables `S5LBOX_WASM_JIT`; other builds retain their interpreter loop without any JIT reference. A single machine is explicitly bound per worker. Generated code is derived cache state and does not alter the snapshot schema or Apple binaries.

The retained compressed opcode-position input plus pinned WABT generator reproducibly rebuilt all 512 exports into the exact original 12,341,313-byte WASM module, SHA-256 `d9c661556ab17bb8433ed9b0c3ecfd52e2fcafe58221b4cb84a920e4976b3881`. No ignored trace or guest RAM snapshot is needed for clean-checkout regeneration. Seed metadata hashes both code and definitions and matches the runtime's ABI revision and 16 C-computed structure offsets before admission.

Optional seed preparation does not delay phone startup and aborts after eight seconds; downloads, regions, function slots, and runtime tables are bounded. Missing assets/exports, bad hashes/ABI/schema, allocation failure, or partial admission fall back to the interpreter and reclaim owned table entries. Restore invalidates old generations before RAM changes, while capture retains valid derived code. `?jit=0` is a diagnostic opt-out. Independent maintained controller/actual-worker tests now cover 25 lifecycle contracts, including real init-to-memory binding order; the prototype's premature memory-publication bug was caught by a dedicated negative-control test. Final maintained-runtime snapshot replay and reversed Chrome timing are still required before the speed milestone is claimed.

Final maintained runtime replay passed: one browser API instance executes 600M interpreter instructions, restores the same fixture through `emu_load_snapshot`, then executes the same 600M using the maintained bound-machine seed runtime. All six complete snapshot SHA checkpoints agree exactly. Generated retirement is 496,605,837/600M (82.77%), with 56,932,774 calls and 509 surviving regions. Evidence: `.context/jit-maintained/replay.{mjs,log,err}`; stderr is empty. This replay ran alongside unrelated functional checks and makes no throughput claim.

### Reversed maintained-browser confirmation

A new quiet four-round pair ran the maintained seed first, then a separately rebuilt **JIT-OFF literal interpreter** from the same current source and release build settings. The control excludes both the optional hook and runtime object; it is not the older slowed prototype or a telemetry emulator. Both sites use the same genuine shipped phone snapshot, app UI, and firmware. The control's output path is isolated through `IPHONE1_WEB_DIST`; generated binaries remain outside the source tree. Both Chrome runs passed without page errors.

| Action | Literal interpreter (seconds) | Maintained seed (seconds) |
| --- | --- | --- |
| Settings, first | 4.40 | 4.60 |
| Settings, repeat 1 | 4.90 | 3.25 |
| Settings, repeat 2 | 4.80 | 3.40 |
| Settings, repeat 3 | 4.90 | 3.35 |
| Home, first | 1.60 | 1.20 |
| Home, repeat 1 | 1.25 | 1.05 |
| Home, repeat 2 | 1.51 | 1.05 |
| Home, repeat 3 | 1.50 | 1.15 |

Repeated Settings launches improve approximately 29–34%, and Home returns improve 16–30% in this bounded pair. First Settings is 0.20 seconds slower; this is not a first-launch or full-speed claim. Sampled per-action generated retirement accounts for 74–80% of instructions, with 509 regions retained after three stale-code retirements. There is no synchronous export warmup. Evidence, full metrics, screenshots, and matching recordings are in `.context/jit-maintained/chrome-{seed,control}/`; hashes are `e919bf5b…` (seed runtime) and `9353a385…` (literal interpreter). This reversed ordering supports the earlier production-first comparison rather than replacing it.

The reusable `npm run test:jit-replay` now reads the shipped multipart gzip snapshot without ignored context, defaults to the complete 600M Settings/Home sequence, and supports `INSTRUCTIONS=100000000` for CI. Both state identity and positive generated execution are required; a disabled accelerator cannot pass as two interpreter runs. The 100M maintained-default-fixture run also passes. Compile-time assertions pin the emitter's hardcoded 64-entry wasm32 cache subfield layout in addition to the runtime offset fingerprint, so a future same-size field reorder must update the emitter/ABI deliberately.

Actual browser fallback checks also pass: fresh `?jit=0` and a deliberately missing `jit/seed.wasm` both reach the genuine ready phone with zero generated regions/retirement and no page errors. Missing optional code is recorded in diagnostic metrics while startup continues. Evidence: `.context/jit-maintained/browser-fallback.json`.

### Maintained release-timestamp reporting

The performance harness now records the actual browser `pointerup` timestamp in a capture listener and reports `releaseToFirstVisibleMs` and `releaseToReadyMs` alongside the preserved historical action-start fields. Reports include host CPU/OS/memory and browser hardware metadata. A one-round functional replay verifies both actual release timestamps and correctly leaves p95 unset with zero warm samples; its concurrent timings are not a new comparison. `ROUNDS=21 npm run test:performance` supplies one first round plus the 20 warm Settings/Home samples required for the per-action navigation percentile. The separate 30-sample input-feedback gate and reference-laptop coverage still need their own runs. Evidence: `.context/performance-release-clock/report.json`.

### Negative region-lookup cache investigation (isolated, 2026-09-05)

The maintained runtime caches its last successful 1KiB compiled-region lookup. Repeated attempts on an unseeded page still hash and probe the region table. An isolated 11-line candidate adds one remembered missing page plus ARM/Thumb mode, checked after the existing positive fast path. Creation bypasses the negative entry; successful installation, reset/bind and stale-code notification invalidate it. Interpreter work inside an already installed region still uses the existing positive cache, so interpreter retirement percentage alone does not establish how often this candidate helps.

The rebuilt control module is byte-identical to current production, SHA-256 `c4d9e21cbc9da4209942c38857322dab7e086b9955b29b843213ebd6eb764f3c`. Candidate SHA-256 is `b3c22a15d7fb25dec1dff6d1520895e2a6f320bbb6ebf498f09e81d3d5d25fb3`. Both are frozen under `.context/jit-negative-cache/`; production runtime source is unchanged. Thirty focused cache contracts pass, covering absent-page installation, replacement, separate ARM/Thumb entries, stale entries, reset and rebinding. Independent source review found no invalidation issue.

Both modules pass the fixed 100M full-machine interpreter/generated equivalence replay, producing the same snapshot and identical generated retirement/call counters. The candidate additionally passes all six 100M checkpoints of the complete 600M Settings/Home sequence against its literal interpreter: final snapshot SHA-256 `bfcde2adfff879343c437ef0d0b55fb689525a2178e080c16b95f7956c77c7f5`, 496,605,837 generated instructions, 56,932,774 calls, and 509 retained regions. These preflight runs overlap the broader app audit, so their elapsed times are not performance evidence.

The quiet control → candidate → candidate → control 600M comparison completed with every one of its 24 full snapshots and generated-execution counters equal. CPU run time is measured separately from checkpoint hashing. Results are:

| Run order | Module | CPU run time | Run plus snapshot verification |
| --- | --- | --- | --- |
| 1 | Control | 18.128 s | 25.542 s |
| 2 | Candidate | 18.450 s | 26.048 s |
| 3 | Candidate | 18.188 s | 25.734 s |
| 4 | Control | 18.633 s | 26.188 s |

**Rejected for promotion:** results change direction across the two orders, with only a 0.34% mean CPU-time difference and a slightly slower candidate mean when including snapshot verification. This is not a consistent net gain. The first timing attempt overlapped another agent's brief native build; it was stopped, preserved under `contended-attempt/`, and excluded before the complete clean comparison above. Production runtime remains unchanged. Patch, 30 cache contracts, whole-machine preflight and clean `quiet-pair.json` artifacts remain in `.context/jit-negative-cache/`; no browser speed claim or visual milestone is based on this experiment.

A read-only comparison of the current emitter and seed against the older 557.4M-opcode Settings census finds 95.95% supported opcode frequency and 92.99% exact opcode positions already seeded. Supported but unseeded work is only 2.96% of that historical sample. Thumb `POP` is the largest remaining unsupported mnemonic group at 0.733%; simple opcode additions do not imply a large whole-app gain. This static sample is not a measurement of present worker fallback time (`profile-readonly.json`, `unsupported-current-summary.json`).

The next **current-seed 100M exit census** uses the byte-identical production runtime above, the exact production seed SHA-256 `d9c661556ab17bb8433ed9b0c3ecfd52e2fcafe58221b4cb84a920e4976b3881`, and a separately emitted module with only existing diagnostic exit counters enabled. All 512 region masks match. Interpreter, original seed and diagnostic seed produce identical full snapshot SHA-256 `1d95750bdd3880a408f8e9cfee9451ae642894eaff8129e81bb954058034a5a8`. Both seeded runs retire 87,140,487 instructions through exactly 8,561,513 calls, and the diagnostic exit counts sum to those calls:

| Exit reason | Count | Share of calls |
| --- | ---: | ---: |
| Leave current 1KiB region | 3,208,589 | 37.477% |
| Unsupported position in region | 2,333,723 | 27.258% |
| Memory cache/alignment/span guard | 1,750,584 | 20.447% |
| Exhaust current instruction budget | 1,268,614 | 14.818% |
| Other early exit / stale code | 2 / 1 | <0.001% |
| Instruction-mode change / physical code-write alias | 0 / 0 | 0% |

The emitter reloads and spills all sixteen registers plus CPSR around each generated call. That makes excessive region returns a concrete candidate cost, but these counts are not CPU-time attribution. The next bounded diagnostic should split the memory-guard category into cache misses versus alignment/span/host-write restrictions, or identify destinations of region-crossing exits before proposing guarded chaining. It must preserve current MMU, alias, instruction-budget and snapshot semantics. No chaining, guard relaxation or new compiler feature is implemented from this census. It overlaps functional/build work and supplies no wall-time speed claim. Complete source and results: `.context/jit-negative-cache/exit-census.{mjs,json,log}`.


## Data-cache conflicts: measured cause, rejected XOR experiment

The follow-up current-seed memory diagnostic preserves all 512 region masks, the exact 100M full snapshot, 87,140,487 generated instructions, 8,561,513 calls and 511 surviving regions. Of 1,750,584 memory exits, **89.970% are pure cache-tag mismatches**, 8.583% alignment, 0.710% cache-generation only, 0.481% tag plus generation, 0.225% crossing a 1KiB boundary, 0.026% alignment plus crossing, and 0.006% null pointer plus tag/generation. No host-write-consent refusal occurs. These are disjoint combinations of diagnostic flags, not CPU-time attribution.

Recording the previous cached tag confirms actual reciprocal read-cache conflicts: **768,980 exits between pages 0x534000 and 0x38514000**, 267,524 between 0x30680000 and 0x30750000, and 139,902 between 0x30680800 and 0x30740800. Their respective old indices are 16, 0 and 2. The complete reports and instrumented emitter are `.context/jit-memory-profile/{memory-profile,cache-pairs}.json`; the separate uninstrumented seed and interpreter still produce identical complete snapshots and counters. Profiling overlaps functional work and makes no wall-time claim.

An isolated candidate tests `((va >> 10) ^ (va >> 16))` before the existing privilege offset and 64-entry mask. The interpreter, C span import and generated accesses change together. Capacity, 1KiB tag granularity, full tags, privilege, generation, alignment/span, explicit host-write consent and physical code-alias checks remain intact. ABI revision 2 prevents mixing the changed emitter with the old runtime. The generated 512-region seed is 12,489,286 bytes; all 26 clean native suites pass. Production remains on the original index and ABI 1.

Within each build, 100M interpreter and generated snapshots match byte-for-byte. Cross-build raw hashes differ solely because the new data-cache policy changes the number of host TLB lookups. An independent C tool examines both full **245,043,170-byte** snapshots, validates their original FNV checksums, and proves that only `tlb_hits` bytes 566–573, `tlb_misses` bytes 574–581 and the checksum trailer can differ. The actual changed counter bytes are 566–569 and 574–575. It zeros precisely those two counters, recomputes both checksum trailers, and compares every resulting byte successfully. TLB flushes remain 387,578 and are included; no guest-state byte is omitted. The normalized full-snapshot SHA-256 is `ca93c4b52b486b836f28b838512b7d532c78e8129cd68781a0800ca6d20628c2`. Original raw states/hashes remain in `.context/jit-index/`.

Source review independently confirms that these two TLB counters are incremented, reset, serialized and exposed to host reports; neither the guest nor emulator control flow consults them. Snapshot documentation explicitly calls them run measurements. The timing harness records raw hashes and a separately named architectural hash of the complete header/payload with only these 16 telemetry bytes zeroed, excluding the checksum derived from that payload. It never labels cross-policy raw snapshots identical.

The quiet **ABBA 600M** comparison is insufficient for promotion:

| Order | Engine | Time inside `emu_run` |
|---|---|---:|
| 1 | Current index | 19.130706 s |
| 2 | XOR candidate | 18.212052 s |
| 3 | XOR candidate | 18.095022 s |
| 4 | Current index | 18.110470 s |

All 24 architectural checkpoints match; raw hashes and generated/call/region counters repeat exactly within each engine. Generated retirement rises from 496,605,837 to 499,124,310 and generated calls fall from 56,932,774 to 54,527,078. However, the first pair's 4.80% advantage collapses to **0.085% in the reverse pair**. The 2.51% mean difference does not establish a repeatable speedup against that baseline variation. **The XOR candidate is not promoted.** No browser launch, p95 or full-speed improvement is claimed. Full source, hashes, native results, independent normalization and timing logs are in `.context/jit-index/`.

A possible next isolated test is 256 entries with the original index. It separates the first two dominant pairs without adding an XOR, at a cost of 4,608 extra WASM bytes (12-byte entries, two caches) or 6,144 native bytes (16-byte entries). The third pair still collides at that capacity. It requires coherent 32→128 privilege-offset and 63→255 mask changes, seed/ABI regeneration and native struct-size guard updates; the host-only caches need no snapshot schema change. This is source-only planning, not a measured or implemented production optimization.


## MBX workload profile before further CPU changes

The accelerated composition workload changes the CPU profile enough that the old software-cache experiment is not a sound basis for a larger cache. A bounded **100M genuine MBX Settings** replay uses the final production renderer/runtime SHA-256 `da43870dab0e151d6a31597bb8ddbbfb3a480f7398ddb88a9334bea9b1314c6d`, unchanged ABI-1 seed `d9c66155…b3881`, and explicit raw accelerated fixture `764486ea35ba9832f35c8118397f361026ed21119295321e0a29519509de1244`. Interpreter, unmodified production seed and diagnostic seed produce the same complete snapshot SHA-256 `d4ca9be92fc97409eef7e93545c4ac1cb2963eacb079a405181840ea6a41e38c`. Both seeded runs retire **73,093,840 instructions through 9,273,308 calls**, retaining 509 regions; all 512 admission masks remain identical. This is a fixed-instruction diagnostic, not a wall-clock launch measurement.

| Exit reason | Count | Share of calls |
|---|---:|---:|
| Unsupported position | 3,521,832 | 37.978% |
| Leave current 1KiB region | 3,209,650 | 34.612% |
| Memory/cache/alignment/span guard | 1,478,248 | 15.941% |
| Instruction budget exhausted | 1,063,574 | 11.469% |
| Other early exit / stale code | 1 / 3 | <0.001% |

The previous software fixture retired 87.14% through generated code; the accelerated fixture retires 73.09%. This difference describes two distinct deterministic workloads, not a loss in measured product speed. Memory exits are now a smaller share. Pure tag mismatches account for 1,140,987 of the 1,478,248 memory exits (77.185%); the largest observed displaced-page direction is `0xe0099000 ← 0xc0029000` with 111,472 exits. The old software profile's dominant reciprocal conflicts no longer dominate. No cache-capacity change has been implemented.

A second read-only diagnostic records the actual instruction and mode at each unsupported-position return. **582,245 returns (16.532% of that category)** point to instructions the existing emitter already supports but the retained seed did not record. The largest is `LDR r0, [r4], r2` at `0x30592118` (190,447 returns). Other prominent unsupported sites include conditional POP-to-PC, CPSID, CP15 cache operations, LDR-to-PC, STREX and unprivileged STRT; these have distinct architectural requirements and are not candidates for relaxing a common guard. All 3,167 observed position/opcode/mode combinations are absent from the recorded seed positions; this is consistent with how the current emitter's unsupported-position exit is reached.

Of the 3,209,650 region crossings, **2,934,390 (91.424%)** target a region represented in the seed. Region presence alone does not establish that the destination instruction is admitted, the current raw bytes still match, or the existing MMU/mode/budget checks can be skipped. These counts suggest either bounded seed-tail coverage or carefully guarded region transitions as future experiments; they do not measure the CPU time saved by either. A bounded extension using only existing supported instructions is the narrower next candidate to investigate. No compiler feature or production optimization is derived automatically from the profile.

Reproducible isolated sources and full results are `.context/jit-mbx-profile/{profile.mjs,cache-pairs.json,sites.mjs,site-profile.json}`. Diagnostic callbacks only inspect host cache/code fields and count existing exits; the unchanged production arm and complete raw snapshot equality remain the oracle. An initial diagnostic-only Thumb read used an unexported `HEAPU16` view and stopped; the retained corrected run reads the same two bytes through `HEAPU8` and passes. Profiling was stopped before the strict continuous sixteen-app graphics run to avoid shared-CPU interference.


### Isolated MBX seed coverage candidate

The next candidate unions **959 observed supported instruction positions across 210 existing regions** into the retained seed input. It adds no instruction semantics and changes neither the runtime, compiler, ABI, memory guards nor region capacity. All original admission bits remain; the extra 959 bits exactly match the retained input diff. The generated module grows by 154,722 bytes (1.254%) to **12,496,035 bytes**, SHA-256 `6e6c5a3069c4a92528e791828918fa13d547c38759ee7df0446e83097964edb6`. It remains isolated in `.context/jit-mbx-coverage/`.

Interpreter, original seed and expanded seed match at all six complete raw snapshot checkpoints through **600M Settings → Home instructions** (18 checkpoint hashes), including TLB/read counters; no normalization is used. The final raw SHA-256 is `642bd79aa34ec60617b745f2eceeff4c5f7c9e64ccf694cbf4b1f439adc958b4`. At 100M, generated retirement rises from 73,093,840 to 73,689,830 and calls fall from 9,273,308 to 9,159,251. At 600M, retirement rises from 460,306,256 to 463,655,298 (+0.558 percentage points), with calls falling from 64,437,078 to 63,315,171 (1.74%). Both retain 509 regions. These modest counters are not a throughput result. Quiet comparison is pending and no production change is made.

`expand.py`, `input-diff.json`, frozen runtime/compiler, `preflight.json`, `generate.mjs`, `replay.mjs`, and `correctness.json` preserve the construction and exact-state evidence. The separate Thumb POP design uses a different isolated directory and is not combined with this coverage experiment.


The coverage-only candidate's quiet ABBA600M comparison has now finished. Time inside `emu_run` is **23.946893 / 23.801715 seconds for the original seed**, and **23.584311 / 23.617707 for the expansion**, in original/expanded/expanded/original order. All 24 full raw checkpoints and each engine's known retirement/call/region counters pass. Pair advantages are 1.51% and 0.77%, with a 1.14% mean difference. The directions agree, but this is a small engine-only gain and does not establish a material browser launch improvement. The candidate is **parked without production promotion or a visual speed claim**; all input, code and quiet evidence remain in `.context/jit-mbx-coverage/quiet-pair.json`. The separate Thumb POP candidate starts from the original seed, so its instruction effect is not combined with this expansion.


### Separate Thumb POP-to-PC correctness candidate

The next isolated instruction candidate handles only **Thumb POP {low registers, PC}**. The original MBX exit census observes 405,214 such returns; condition-normalized ARM POP adds 435,310 but remains unsupported in this candidate. The previous emitter explicitly declined Thumb POP when both load and the extra-PC bit were present.

The new path retains the existing aligned, whole-span, one-1KiB-block, privilege/tag/generation and plain-RAM requirements. It peeks the final return word only after that span is admitted. Target bits 00 select ARM and 01/11 select Thumb; 10 falls back to the interpreter before any low-register, SP, CPSR or read-counter commit. Successful loads account each word once and preserve flags other than T. Shared writeback retains prior generated work on all exits. ARM POP, LDR-to-PC, exception returns and other unsupported instructions stay unchanged.

An oracle linked against the **current production C core and runtime** passes 9,552 differential cases, including all 256 register masks, all target low bits, flags/user/SVC/SYS, aligned/unaligned/crossing sources, cache cold/stale/tag/privilege cases, alignment/permission abort, observed MMIO callbacks, exact/split budgets and stale code after prior generated work. State comparison includes architectural CPU, TLB/read/write/MMIO/status counters and SHA-256 of the complete 1MiBRAM. Another 36 checks prove exact last-valid spans and zero-budget identity. Negative controls that account reads early or write destination registers before validating an invalid target each fail exactly as required. These are correctness checks, not hardware throughput measurements.

The candidate starts from the **original seed input**, without the parked 959-position expansion, and adds 503 captured POP positions across 209 regions. Its 12,487,288-byte module has SHA-256 `545df9e20fde3eb4e9aba57cc04e636cabfd332b3f3a52f4c1831dca33692294`. A three-way per-region WAT comparison is explicit: old and new compilers given the unchanged original input produce identical bodies for 512/512 regions. With the expanded POP input, 209 bodies change and 303 remain identical; exactly 503 new admission bits are added and every old bit remains. This does **not** mean that the expanded candidate contains no executable changes.

All six full raw snapshot checkpoints match the interpreter through 600M genuine MBX Settings → Home instructions, with no counter normalization. The final raw SHA is `642bd79aa34ec60617b745f2eceeff4c5f7c9e64ccf694cbf4b1f439adc958b4`. Generated retirement rises from 460,306,256 to 463,782,161 (+0.579 percentage points), while calls fall from 64,437,078 to 64,357,407 (0.124%); both retain 509 regions. Production is unchanged. Quiet timing, any actual browser gain and software-snapshot compatibility remain separate gates before promotion. Reproducers, exact per-region hashes, positive/negative oracles and whole-guest proof are in `.context/jit-thumb-pop/`.


The separate Thumb POP quiet ABBA600M run passes all24 full raw checkpoints and expected retirement/call/region counters. Original-seed CPU time is **24.005039 / 24.339340 seconds**; POP-seed time is **23.568474 / 23.149363 seconds**, in original/POP/POP/original order. Pair gains are **1.82% and4.89%, mean3.36%**. This warrants the next bounded compatibility/browser gates, not production promotion. The older software-composition fixture (`28591a30…b0c82d`) also passes all six full raw600M checkpoints against the interpreter, ending at `bfcde2adfff879343c437ef0d0b55fb689525a2178e080c16b95f7956c77c7f5`; generated retirement is498,401,946 with56,888,370calls and509regions. No normalization is used.

Actual MBX browser ABBA is prepared with the same normal Import,30ms pulse and50ms/500ms observer method as milestone055, eleven rounds per fresh session and two sessions per engine. Every action must confirm the expected seed byte count and increasing generated retirement. It is **held behind the separate public-input causal fix**, so both arms can use its final identical production runtime/frontend if promoted. The CPU experiments above retain their frozen pre-input-fix runtime and are not relabeled as post-fix measurements. Candidate code/admission stays separate and unchanged. Evidence: `.context/jit-thumb-pop/{quiet-pair.json,software-proof.json,browser/preflight.json,run-browser.py}`.


### Thumb POP browser decision: parked

The browser comparison now uses the final short-tap-corrected production runtime `603679fbc7961ddad95ac8822257074fecde5940bc013246d520ea3b6240c84f` in both arms, with byte-identical frontend and normal Import of the same MBX snapshot. Only the POP seed/admission assets and derived offline revision differ. Separate 100M MBX and software-fixture preflights each match complete raw interpreter snapshots, with positive generated execution. Earlier 600M CPU measurements retain their original frozen runtime.

The quiet order is original → POP → POP → original, with eleven rounds per fresh Chrome session. First launches are separate; each engine contributes twenty warm observations per action. Per-session values in milliseconds:

| Order | Seed | First Settings | Warm Settings median | First Home | Warm Home median |
|---|---|---:|---:|---:|---:|
| 1 | Original | 4352.9 | 3141.8 | 1063.7 | 867.15 |
| 2 | POP | 4034.1 | 3017.15 | 1059.1 | 866.8 |
| 3 | POP | 4403.3 | 3142.55 | 1066.3 | 867.1 |
| 4 | Original | 4952.0 | 3167.3 | 965.9 | 867.0 |

Aggregated warm Settings p95 is **3316.6 ms for both engines**; median is 3166.6 → 3091.95 ms (2.36%). Home p95 is 917.6 → 917.0 ms and median 867.05 → 866.85 ms, effectively unchanged. The two first Settings observations per engine are insufficient to establish a repeatable startup gain.

All eight first Settings/Home references were manually reviewed and show the correct complete apps. **86 of 88 screenshots pass the existing maximum-three-level RGB comparison** below the status bar. Two fail and remain included: POP session 3, Settings round 8 has maximum delta16 at x293–297/y62–318; original session 4, Settings round 8 has delta37 at x293–298/y62–318. Both visibly show the correct Settings page with a still-fading scroll indicator. The tolerance is not widened and neither sample is removed. Thus this is diagnostic timing, **not a completed strict browser speed gate**, and it does not replace milestone055's verified 3.57-second Settings p95 in the project summary.

**Decision: park Thumb POP without production promotion.** Correctness and the small CPU-only gain are preserved, but warm browser p95/Home do not materially improve. The original production compiler, seed and ABI remain unchanged. Reproduction, all raw samples, hashes, per-order results, image differences and the decision are retained in `.context/jit-thumb-pop/{post-input-proof.json,browser/preflight.json,browser/quiet-navigation/comparison.json,browser/quiet-navigation/review.json}`. historical recording (removed) and historical thumbnail (removed) are linked from milestone063 as a rejected follow-up experiment, not an input-fix speed claim.

The next bounded investigation counts actual opcode guards under the original production seed. It will preserve all checks and execution order, require complete raw state and baseline retirement/call counter equality, and make no timing claim under concurrent functional work. Static block sizes alone do not establish redundant runtime work; no per-instruction guard policy change is implemented.

[Durable POP browser evidence](../docs/traces/thumb-pop/browser-review.json) includes all raw timing values, per-order results, strict pixel differences, the two screenshots, post-input full-state proof and earlier CPU pair. The transient workspace artifacts remain the full reproduction source.


### Actual opcode-guard load census

The original production seed, not either parked expansion, now has a bounded execution-weighted guard census on the current MBX Settings workload. Interpreter, original seed and diagnostic seed produce the same complete raw100M snapshot (`d4ca9be9…41e38c`). Original and diagnostic retirement/call/region counters are exactly **73,093,840 / 9,273,308 / 509**. Removing only inserted diagnostic WAT expressions reproduces all512 original region bodies byte-for-byte; admissions, every guard and their order are unchanged. The counters occupy2176 separately allocated host bytes outside serialized guest state. This was a functional run alongside other work, with no timing claim.

| Actual event | Count |
|---|---:|
| Basic-block arrivals | 22,663,905 |
| Attempted raw opcode checks | 92,847,493 |
| Checks before the current block-entry instruction | 11,201,176 |
| Checks at or after entry | 81,646,317 |
| Instruction bodies begun after the budget check | 74,572,089 |
| Generated instructions retired | 73,093,840 |
| Stale-code mismatches | 3 |

This is **1.27025 checks per retired instruction**, or1.24507 per body started. Earlier-prefix checks account for12.064% of checks. The actual histogram implies92,847,519 possible check slots; the26 fewer observed attempts arise from early stale exits. Its earlier-prefix capacity is11,201,179, three above the actual count. Body starts include1,478,249 fallbacks that do not retire the current instruction. The aggregate7,074,228 non-prefix checks beyond body starts are not individually classified as removable: exact budget exhaustion, early memory fallback and stale behavior need explicit instruction-level accounting.

The result is substantially smaller than the earlier unweighted static estimate; it does not predict a percentage CPU saving. It identifies a bounded follow-up: if testing per-instruction raw checks after the budget check, preserve every raw/MMU/alias requirement and explicitly test changed earlier prefixes, changed later suffixes, branches, zero/exact budgets and stale exits after prior generated work. Changing invalidation policy can change host generated-call counts, so architectural equivalence and host counters must be distinguished. **No such policy or production change is implemented.** [Durable report and reproduction source](../docs/traces/jit-code-guards/README.md).


### Isolated per-instruction raw-check candidate

A separate candidate moves each **unchanged raw opcode check immediately after its instruction's existing budget check and before its PC/body**. It uses only the original seed input, with no POP or coverage expansion. A structural comparison preserves all76,964 exact guard expressions and their sequence; removing those expressions from both versions leaves every one of512 WAT bodies byte-identical. Admission JSON is byte-identical, with unchanged runtime/ABI/capacity. The seed remains12,341,313bytes, SHA`342e3ab994c55e3c3baa63c070f1b35179dba0b1243fa7947b6ab9d1a64d0d86`.

The current-core oracle passes976 interpreter/original/candidate cases: ARM/Thumb stale earlier/current/later code, tail entries, zero/exact/split budgets, undefined replacements, conditional loops, mutation between calls, previous committed work, identity writes and a second virtual mapping of the physical fetched code page. It includes480 explicit zero-budget observations and12 true virtual-alias cases. Architectural CPU bytes, TLB/read/write/status counters and full1MiBRAM hashes match. Deliberately removing the opcode check fails changed-current execution; bypassing shared stale writeback fails after previous generated work.

**456 focused cases intentionally differ in derived JIT counters or region lifetime.** A changed prefix that is never executed or a suffix beyond this budget no longer invalidates a region early. The later instruction still checks its actual raw bytes when reached. This is a policy difference, not a claim that all host counters stay identical.

Both genuine MBX and older software phones pass all six complete raw snapshots through600M under interpreter, original seed and candidate:36 hashes, without normalization. Fresh original-seed arms use the same current runtime. Final derived counters are explicit:

| Workload | Seed | Generated instructions | Calls | Regions retained |
|---|---|---:|---:|---:|
| MBX | Original | 460,306,256 | 64,437,078 | 509 |
| MBX | Per-instruction checks | 460,358,264 | 64,489,085 | 510 |
| Software | Original | 496,605,837 | 56,932,774 | 509 |
| Software | Per-instruction checks | 496,627,781 | 56,954,717 | 510 |

The full raw final hashes remain`642bd79a…958b4` (MBX) and`bfcde2ad…c7f5` (software). All checks above are functional evidence under concurrent work. **Quiet timing is pending; no production change or speed claim is made.** [Durable patch, structural proof, focused oracle and full raw reports](../docs/traces/jit-instruction-guards/README.md).


The guard-relocation candidate's quiet600M ABBA has finished. Original-seed CPU time is **23.964878 / 23.980380 seconds**; per-instruction checks take **23.806295 / 23.950573 seconds**, in original/candidate/candidate/original order. All24 complete raw checkpoints and each policy's own known retirement/call/region counters pass. Pair advantages are **0.662% and0.124%, mean0.393%**, effectively a tie. The count reduction does not produce a material throughput result here. **The candidate is parked without production promotion, a browser speed claim, or combination with the separate register-transfer experiment.** [Exact quiet results and checkpoint evidence](../docs/traces/jit-instruction-guards/quiet-pair.json). The original guard policy remains enabled in production.


### Register transfer census and isolated omission candidate

A separate original-seed profile counts actual entries into each generated region. It preserves every register transfer, guard and instruction; all 512 instrumented bodies strip back byte-identically to the production emitter. Interpreter, original and instrumented engines produce the same full 100M MBX snapshot, with exactly 73,093,840 generated instructions, 9,273,308 calls and 509 retained regions. This is an untimed functional census.

Weighting the unchanged emitter by those calls gives 148,372,928 nominal register loads and the same number of nominal spills. Of the spills, 38,427,980 (25.90%) target registers never written anywhere in that region. Of the loads, 30,505,917 (20.56%) refer to registers neither read nor written. These are emitted operations; the host compiler may already eliminate some, so neither percentage is a CPU-time saving forecast.

The isolated candidate removes only those unconditional transfers. Loads for any written register stay, including write-only destinations: an untaken condition, tail entry or early exit may need its initial value. The original instruction bodies, raw checks, memory/alias/budget checks, input and ABI remain unchanged. A separate source review found no issue in this bounded transformation. The seed shrinks by 24,920 bytes to 12,316,393, SHA `c6b855944ba9b1cd172a01668a994032be32c4a164dd86a5087a20cff0b05b19`; admissions are byte-identical. It is not combined with any parked candidate.

The current-core oracle passes 2,335 three-engine cases, including 480 zero-budget checks, 12 true physical-alias cases and all-register conditional/write-only destinations. Architectural bytes, full 1MiB RAM hash, TLB/read/write/status counters and all generated retirement/call/region counters match. Deliberately dropping a required load or spill fails immediately in architectural state. Fresh 600M replays now pass on both MBX and software fixtures: all twelve complete raw checkpoints and all original generated/TLB counters match the previously verified same-runtime interpreter/original references, without normalization. The isolated runtime initially lacked its CommonJS package metadata and failed before guest execution; that failed attempt is preserved, then the correct production metadata was copied and both full runs passed. Quiet timing remains pending; production is unchanged. [Durable census, source and focused evidence](../docs/traces/jit-register-layout/census.json) accompany the isolated `.context/jit-register-layout/` reproducer.


The structural check confirms all 512 non-transfer WAT bodies and compiler metadata remain identical, with exactly 1,580 loads and 1,980 spills removed. Quiet alternating 600M measurements are original **24.034760 / 23.800708 seconds**, candidate **23.778380 / 24.077312 seconds**. The forward advantage is 1.067%, but the reverse pair is -1.162%; the combined result is -0.042%, effectively a tie. All 24 full raw checkpoints and original counters pass. These times sum synchronous emulator-run intervals, excluding compile/import/checkpoint hashing; they are not scheduler CPU measurements. **Parked without promotion**: fewer nominal transfers did not produce a repeatable throughput gain. Actual browser stack sampling is the next investigation.

[Durable register experiment evidence](../docs/traces/jit-register-layout/README.md).


### Actual browser CPU attribution

After the small coverage, POP, raw-check and register-transfer candidates failed promotion, a production browser run now measures where execution actually spends sampled time. The original runtime603679, seed and frontend are unchanged. Separate worker/main profiles cover first and warm Settings/Home, stable Home and ordinary Save. Linux scheduler CPU ticks and browser process CPU seconds are retained separately. This is one instrumented attribution run, **not a replacement speed benchmark or p95 gate**.

Production WASM has no names. A direct symbol-map relink changed function order and was rejected for equal-index use; the no-map relink reproduced the production bytes exactly. A reviewed complete module bijection then maps223 defined functions and8 imports: complete bodies after call-index translation, types, tables, exports, memory, globals, data and JS host bindings all agree. Independent regeneration verifies both WAT inputs against their actual binaries. Names below use only that proven mapping. Post-sampling bytecode retrieval timed out, so captured module identity remains a stated limitation; the second WASM script is inferred to be the expected generated seed from the actual production worker pipeline and positive seed metrics.

| Exclusive worker sampled residency | First Settings | Warm Settings |
|---|---:|---:|
| `arm_step` itself | 23.03% | 29.09% |
| Generated seed module | 28.02% | 28.07% |
| `emu_run`, including fused work | 17.91% | 12.22% |
| Guest memory/MMU/bus helpers | 10.32% | 11.83% |
| JIT `region_find` | 4.88% | 6.37% |
| Distinct MBX functions | 0.58% | 0.84% |
| `emu_frame` | 2.05% | 1.85% |

Other helpers, JS, wrappers, GC/unknown and idle remain in the full report; this table is not a complete sum. Inlined work belongs to the containing function, so named MBX rows are not proof that every graphics cost is separated. Worker sampled non-idle residence is4614/3616ms, compared with4490/3660ms of actual dedicated-worker scheduler CPU, a useful independent crosscheck. First Settings additionally consumes5930ms on generically named renderer pool threads, versus70ms warm; their names do not identify the work conclusively.

The stronger actionable finding is on Save. An automatic Save overlaps first Home and warm Settings. First Home includes298ms in `emu_snapshot_advance`; warm Settings has3438ms of main-thread sampled time in an unresolved native frame, with3690ms actual main-thread CPU. The explicit ordinary Save stage reproduces6315ms in native `resolve`, with6650ms main CPU. SavedAt/capture metrics confirm Save activity, but these native labels alone do **not** prove that compression is the cause. The next bounded task isolates actual snapshot encoding from IndexedDB commit, then considers a dedicated compression worker only if that attribution and byte-exact persistence checks support it.

[Durable raw profiles, scheduler measurements, symbol proof, screenshots and exclusive analysis](../docs/traces/browser-cpu-attribution/README.md). All original CPU candidates remain separate and parked. The latest fully verified navigation p95 remains milestone055's result.


### Save compression moves off the browser page thread

The independent immutable-snapshot experiment resolves the native Save attribution: encoding the actual 244,887,522-byte checkpoint takes **6,653.6 ms / 6,720 ms renderer CPU**, while committing the already encoded 102,885,269-byte gzip Blob takes **192.1 ms / 20 ms CPU**. Both original and decoded complete snapshots keep SHA `764486ea35ba9832f35c8118397f361026ed21119295321e0a29519509de1244`. Read/decompression is a separate 1,229.3 ms step and not part of Save.

The reviewed, integrated change gives each fresh capture to a short-lived module worker through explicit `writeOwnedCheckpoint`. The older `writeCheckpoint` keeps its non-consuming contract. Size is saved before transfer, startup can fall back only while the original bytes remain owned, and failures after transfer preserve both existing generations and require a fresh-capture retry. The existing gzip/raw schema and atomic two-generation transaction are unchanged. Native snapshot finalization and the guest CPU are unchanged.

A quiet **baseline / worker / worker / baseline** actual-phone comparison warms Settings/Home, then repeats both during an explicit Save. Identical diagnostic-only suppression of automatic saves isolates the same stage. Renderer main-thread scheduler CPU drops from **6,770 / 6,820 ms to 310 / 360 ms** (about95%). Encoding-only8ms timer p95 gaps drop from **44.1 / 44.6 ms to 8.1 / 8.1 ms**, maxima from **98.6 / 93.9 ms to 17.1 / 26.6 ms**; observed long tasks go from two per baseline to zero. Compression elapsed stays **6.783 / 6.792 s versus6.672 / 6.945 s**. Settings/Home timings are mixed; **no general navigation speed or p95 claim** follows from two runs per arm. All16 genuine Settings/Home screenshots match every RGB pixel below the predefined status bar exactly, with no tolerance expansion.

Validation includes16 real-browser ownership/codec/error/atomicity contracts; complete245MB hash-exact roundtrip; persistence23; offline6; Save/Pause14; an independent actual old-shell/no-worker→new-shell upgrade24 preserving exact metadata and compressed bytes, then successful offline-worker Save and reload; and34 unit checks. Dedicated module packaging and test spies were updated for multiple workers. [Evidence](../docs/traces/checkpoint-worker/README.md) and [original-speed milestone068 recording](../docs/traces/checkpoint-worker/README.md). This is a verified Save responsiveness improvement; the fully verified app-navigation baseline remains milestone055, and full-speed functionality is still incomplete.


The final production clock lifecycle gate passes all 12 checks with generated execution required. App/storage/worker/service-worker bytes equal the tested preview; offline bundle is `1eeff3480ab8b2bec802d2e9`, with unchanged `603679…` runtime and `d9c661…` seed. All 16 codec contracts also pass on production and now run in browser CI.


### Isolated interpreter decode partition, after sampled attribution

The next bounded candidate targets interpreted fallback, without changing the seed. The accepted warm profile attributes 29.09% of exclusive worker samples to `arm_step`, but does not separate decode from inlined instruction work. Its actual production WAT body contains 12,152 lines. The isolated prototype groups the existing conditional ARM chain by bits 27:26 into DP/misc, media/single-transfer, branch/block-transfer and coprocessor/VFP/SVC classes. Every guard and handler body is retained, in original within-class order; fetch, IRQ, cycles, conditions and the common abort/undefined/PC epilogue are unchanged. Thumb and unconditional instructions are untouched.

The concrete mechanism is avoiding earlier impossible-family comparisons on fallback instructions. Additional switch/layout cost could cancel the benefit; no percentage is predicted. Selection projection passes 2,097,185 checks, and the unchanged native ARM suite passes 1,067 checks against the prototype. Swapping media/single-transfer precedence fails the selection oracle on `06000010`. Original seed/ABI/guards remain intact. Full raw guest replay precedes any quiet performance gate. [Source, proof and build evidence](../docs/traces/interpreter-dispatch/README.md). No production edit or speed claim.


Decoder prototype correctness update: both genuine MBX/software fixtures pass 100M-instruction interpreter and original-seed runs against preserved original `603679…` references. All four full raw snapshots match, with exact original TLB and generated retirement/call/region counters; no bytes are normalized for acceptance. Quiet timing remains pending, production unchanged. [Complete raw/counter report](../docs/traces/interpreter-dispatch/correctness.json).


Decoder fidelity gate extended: all24 full raw checkpoints now match across600M MBX/software candidate interpreter and original-seed runs, with exact original TLB/JIT counters. A second parser audits actual final C artifacts independently of the transformer and proves guard dependency projection completeness, including the unique exact-WFI exception; another2,097,185 noinline/noipa selection comparisons pass. Timed arms are being rebased onto the separately promoted snapshot classifier so both retain identical Save/capture behavior. No production decoder change.


Decoder screening decision: **parked**. After rebasing both cores onto the promoted snapshot classifier, fresh100M MBX/software checks again pass raw state and counters. Prewarmed100M quiet ABBA is baseline **3,666.136 / 3,588.738 ms**, decoder **3,574.121 / 3,766.938 ms**: forward+2.510%, reverse−4.966%, combined−1.188%. All four full raw checkpoints and original JIT counters match. This23.093-second screening window demonstrates no repeatable gain; it is not a precise regression estimate or browser speed result. The first whole sequence, contaminated by brief concurrent native relinks, is retained separately and not used selectively. **No production decoder change or additional timing.** [Complete decision and evidence](../docs/traces/interpreter-dispatch/README.md#decision-parked-after-quiet-screening).


Next measurement is source-only preparation: correlate real V8 compilation events and scheduler CPU with first Settings, ordinary warm Settings, and the original Home checkpoint restored inside the same worker/module. The earlier 5,930 ms first-use pool CPU versus 70 ms warm is unresolved, not proof of compilation. Identical paused Import staging, exact served assets, stage work counters and strict screenshots distinguish host compilation from guest startup work without changing the CPU. [Bounded design and interpretation rules](../docs/traces/browser-cpu-attribution/first-use-design.md). No new heavy job or production edit.


### First-use browser compilation is measured, not inferred from thread names

One corrected three-stage trace on exact production b3237/original seed shows first Settings top-tier compilation consumes 10.938–11.573 s of background thread CPU, versus 42.986 ms warm and 2.330 ms after restoring the original guest in the retained worker. First/restored guest work is similar (114.26/115.11 M retired), while ordinary warm work is lower (94.93 M). Diagnostic ready times are 4.851/2.767/3.616 s; traced observations are not p95 or a causal savings estimate. C0/C1 Settings and every Home frame match exactly; warm Settings retains a three-level scrollbar mismatch against the strict two-level threshold. The initial BigInt report failure and full repeated trace are preserved. This identifies both host first-use and guest warmup contributions without promoting a warmup policy. The next source feasibility target is guarded region continuation; original census masks admit 2,601,329 of 3,209,650 crossing targets (81.05%), less than region-presence coverage. [Complete evidence, scope and failure gates](../docs/traces/browser-first-use/README.md).


Guarded continuation prototype is isolated and pending acceptance: it reuses the existing non-walking fetch-TLB/RAM refill to continue only after positive generated progress into another admitted region, with the original remaining device budget. Original seed and ABI are unchanged. Initial100M MBX/software interpreter+seed comparisons preserve complete raw snapshots and TLB counters; generated retirement rises73.094→75.581 M and87.140→90.051 M respectively. Those functional runtimes are not timing evidence. Actual-emitter fault/alias/interworking/timer-boundary contracts and600M replay are prepared; heavier validation is held during the separate fixed-budget SDIO boot. Production remains unchanged.


### Smaller generated functions: separate first-use candidate

An isolated emitter keeps original basic blocks and public admission masks, splitting 265 large regions into private groups of at most 32 blocks. Public ABI and the production runtime remain unchanged. The largest function body decreases from 67,111 to 41,235 bytes, while the module grows from 12.34 to 12.70 MB. These structural changes might reduce first-use compilation; extra register transfers and calls could make warm execution slower.

All 358 actual group-boundary cases and 404 additional original-runtime guard cases pass. Two deliberately incorrect budget/raw-guard variants fail architecturally. Both genuine fixtures match every raw checkpoint through 600 million interpreter/candidate instructions: 24 outputs, 12 exact pair comparisons. No runtime-continuation change is combined with this seed. The completed quiet browser ABBA passes all 24 matched-stage screen checks, but first Settings rises from 4.934/4.956 s to 5.014/5.265 s and warm Settings from 2.767/2.817 s to 3.216/2.967 s. Seed baseline compilation rises from 226 functions and 258.7–264.5 ms thread CPU to 551 functions and 279.3–279.7 ms; anonymous top-tier work remains unassigned. **Parked with production unchanged.** Two observations per stage/configuration are diagnostic, not p95 estimates. [Source, full traces, original-speed recording and decision](../docs/traces/jit-lazy-groups/README.md).

Guarded continuation correctness is now complete: 404 actual-emitter cases pass, including 96 full timer/deadline snapshots, both deliberate budget/refill mutations fail architecturally, and all 24 full raw MBX/software checkpoints match through 600M instructions. TLB counters are unchanged; generated retirement/call classification changes explicitly. Full-workload warmup and a frozen quiet ABBA are prepared, but no timing acceptance or production change is claimed yet. [Source, exact build provenance, retained setup failures and complete evidence](../docs/traces/jit-continuation/README.md).

Continuation quiet CPU result: baseline22.261389/22.308597s versus candidate21.273364/21.422782s, a4.438% forward and3.971% reverse reduction (4.204% combined). All24 raw/TLB checkpoints and each engine's frozen generated counters pass; both cores completed600M warmup before explicit quiet START. Process CPU agrees (4.215%). Parent authorized an isolated actual-phone ABBA next; original seed and production remain unchanged. [Full values and scope](../docs/traces/jit-continuation/README.md#quiet-result-repeatable-cpu-improvement-browser-gate-open).

Continuation browser decision: **parked**, production unchanged. Quiet fresh-session ABBA retains20 warm samples/action/configuration, exact served core/glue/seed receipts, original seed and normal30-second Save behavior. Settings median2.9671→2.9172s is small; p953.1676→3.1677s and Home median.86705→.86730s/p95.9179→.9174s are effectively tied. First Settings is mixed4.1163/4.055 baseline versus4.3135/3.9165 candidate. Strict screenshot comparison fails4/88 (three baseline, one candidate) solely in the recorded scrollbar bounds, maximumRGB5/6 against existing3. All8references/fourfailures manually reviewed, all88screens/samples retained; no tolerance expansion or replacement of055's verified result. A full original-speed diagnostic recording is queued after the next quiet window. [Complete decision and artifacts](../docs/traces/jit-continuation/README.md#browser-decision-parked).

Source-only deadline reassessment finds no supported large batching shortcut: the browser already crosses inactive timebase edges with256-instruction active groups and samples host time every4,096 retirements. Budget exhaustion re-enters generated execution without requiring a fallback instruction. Distinct device/clock symbols are only1.15% of accepted warm samples, with fused costs explicitly unresolved; deterministic68/69-instruction exit rates do not describe this browser policy. The WFI wake table is not an observable-device-event contract for a running CPU. [Evidence, boundaries and minimal falsifiable next gate](../docs/traces/jit-continuation/device-budget-reassessment.md). No implementation or new profile.

Existing-data copy/fill reassessment finds no strong current hot-loop witness. Named kernel/libSystem routines occupy1.289% of the older recorded software PCs, but that profiler drops42.56M events and cannot bound all work. Current browser generated regions overlapping those routines account for1.13% first/1.39% warm sampled residency, with unrelated in-region code included and interpreted fallback unresolved. Region calls cannot be substituted for loop iterations; inline copy/fill loops remain unmeasured. [Inputs, exact mapping and limitations](../docs/traces/jit-continuation/copy-fill-reassessment/README.md). No new guest run, profile or optimization.

Profiler methodology source check: exact V8 15.2.124.21/commit4323497a… follows plain CDP sampling through code logging and existing-code enumeration, with no forced Wasm tier-up in that path. Debugger transitions are separate; source-position/protector bookkeeping and normal observer overhead remain. [Pinned sources and collector audit](../docs/traces/browser-cpu-attribution/v8-profiler-source/README.md). The parked continuation now has its complete106.36-second diagnostic MP4/thumbnail linked in the progress index; no successful speed milestone is implied.

Verified streaming-loader investigation (2026-09-06): the isolated candidate preserves the original seed/ABI and byte-identical controller lifecycle while passing the actual fetched Response to compileStreaming only after bounded SHA verification of its clone. Exact Chrome152 source retains URL/cache metadata through that clone. All49 loader, HTTP and lifecycle contracts pass; a deliberate integrity bypass fails four checks. Production remains unchanged and no browser native-code cache hit or speed gain is claimed. The next bounded gate uses separate fresh processes sharing one persistent profile and distinguishes native deserialization from HTTP caching. [Source, retained failures and mechanism plan](../docs/traces/jit-stream-cache/README.md).

Compiled-seed reuse mechanism accepted (2026-09-06), production unchanged: a fresh Chrome process successfully deserializes21,104,920 bytes of seed-URL cache metadata from the previous profile. Exact digest/success nesting and independent source review distinguish this from an HTTP hit. All8 strict original-reference phone frames pass and were manually reviewed;49 loader/HTTP/lifecycle checks and12 trace-negative controls pass. First Settings5.665→3.466s and warm2.568→2.867s are single diagnostic observations, not repeatable speed acceptance. Realistic no-cache/ETag return-visit timing and actual service-worker/offline gates remain. [Milestone076 and both full recordings](../docs/traces/jit-stream-cache/README.md).


Return-visit benchmark transport correction: the first original measured arm hit its240-second cap after20 passing screens; no candidate or retry ran. Preserved counters locate152.93s in19 validation gaps, and an immutable-frame diagnostic proves numeric-array protocol cost7–9s versus byte-exact base64~0.2s. New isolated profiles/references retain the same screen rules and cap; fresh equal training is next, with no production or phone-speed claim. [Failure accounting and transport proof](../docs/traces/jit-stream-cache/return-visit/compact-transport/README.md).

Fresh V2 return-visit training passes8/8 strict/manual screen checks in59.79/61.05s with all24 scoped Chrome process identities closed. Original0 versus candidate16 native serialization callbacks are retained; accepted reuse still gates each future measured resume. Actual unchanged validation waits plus compact transport take1.185–1.271s/stage. Frozen ABBA awaits quiet authorization; production unchanged. [Complete V2 training and pins](../docs/traces/jit-stream-cache/return-visit-v2/README.md).


V2 return-visit functional gate passes: all88 strict raw/reference frames,8manual references,48scoped Chrome process closures and both actual no-cache/ETag304 accepted native-cache receipts. Timing is **contaminated by an independently observed external workspace Chrome/encoder workload**, so no quiet speed acceptance or promotion. Warm Settings p95 also regresses in the retained diagnostic; no selective dismissal. Original/candidate firstSettings5.002/5.715 versus3.516/3.359s are diagnostic only. V3 preparation is separately authorized with new empty profiles/origins and identical method/cap; no new browser started. [Full orders, failure history and source evidence](../docs/traces/jit-stream-cache/return-visit-v2/README.md).

V3 independent fresh training passes8strict/manual stages and24scoped Chrome closures in59.91/61.21s, with original0/candidate15 seed serialization callbacks. Directory-normalized method exactly matches V2; neworigins/profiles/references and unchanged240s cap. Final measured phase awaits coordinated release; no production or speed claim. [V3 sources and training proof](../docs/traces/jit-stream-cache/return-visit-v3/README.md).


V3 coordinated return-visit comparison: all88 strict/raw stages,8manual reference screens and48scoped process closures pass. FirstSettings5.112/4.909→3.566/3.417s (~30.2/30.4% paired improvement); warm Settings median3.314→3.117s, p953.667→3.417s. Home pooledp95.9167→.9665s worsens49.8ms, so blanket no-warm-regression is **not** passed. Root accepts narrower Settings evidence for actualSW/offline investigation; production unchanged. Conductor git housekeeping atsetup is preserved with unknown exact exits; no unrelated browser/encoder/compiler captured in boundary receipts. [All values, source, recordings and limitations](../docs/traces/jit-stream-cache/return-visit-v3/README.md).


### Real offline compiled-seed reuse passes (2026-09-06)

The fourth separately named functional pair passes **71 checks and ten strict/manually reviewed guest screens**. The first process hash-caches firmware and installs the unchanged real service worker before starting the phone; after the local origin listener and all connections are physically closed, the genuine phone navigates and performs its ordinary autosave. A fresh Chrome process restores exactly the same metadata and compressed checkpoint SHA, loads both seed/interpreter from CacheStorage with exact body hashes, and accepts **20,677,964 bytes** of native code belonging to the exact seed URL. All 25 scoped process identities close, and actual origin requests remain zero after the barrier. The previous three failures remain retained. [Milestone 084 with both full uncut recordings](../docs/traces/jit-stream-cache/sw-cache-offline/README.md) and [full evidence](../docs/traces/jit-stream-cache/sw-cache-offline/README.md) document the explicit cache-preparation fixture and transport limits.

The loader-prefix-only patch preserves the entire SeedJIT lifecycle/admission class byte-for-byte and passes 49 focused loader/lifecycle checks, including real HTTP URL/body/hash, corruption and MIME fallback. It remains isolated pending the combined RTC-runtime/loader offline upgrade and fallback gates. This functional pair is not a speed benchmark, does not establish ordinary first-visit behavior, and leaves milestone 080’s **49.8 ms worse pooled Home p95** explicit. Production loader remains unchanged at this checkpoint.


### Warm interpreter priority from retained profiles (2026-09-06)

Reanalysis of the two original b3237 warm Settings profiles, with exact hot-function body identity and conservative release-to-ready clipping, finds only 0.66–0.81% worker idle. Generated execution is 29%, arm_step itself 28.4–28.6%, separate memory/MMU helpers 11.7–11.9%, and region_find 5.7–6.5%; most memory-helper samples have arm_step ancestry. Compilation is only 43–45 ms across each longer stage. This is historical sampled residency, not a new benchmark or hardware-cycle attribution. [Source, counters, timing uncertainty and full results](../docs/traces/warm-settings-reassessment/README.md).

The next isolated measurement counts **all interpreter steps**, including wholly unseeded stretches omitted by generated-exit censuses. It partitions absent pages, other mode, missing admission, retired regions and actual guard/exit provenance without additional guest-memory reads. Fixed-capacity overflow is explicit and rejects coverage acceptance. [Prepared contract/source](../docs/traces/jit-fallback-census/README.md); no new guest run, coverage result or optimization yet.


**All-fallback census completed:** the corrected isolated observer passes 404 boundary scenarios, four exact100M whole-machine checkpoints and one actual warm Settings/Home capture with five strict/manual frames and zero lost rows. Warm Settings executes30.211M interpreter steps;13.189M are outside original seed bases and9.515M lack an admitted position. Actual compiler and old-candidate cross-checks identify64 absent pages plus2048 newly measured positions with7.858M direct syntactically emittable attempts, explicitly excluding mandatory prefix fallbacks. This motivates a bounded isolated576-region/16MiB candidate; no speed forecast or production CPU change. Original observer overflow failures and compilation/guard limitations remain retained. [Complete evidence and recording](../docs/traces/jit-fallback-census/README.md).

### Mixed coverage candidate: structural and exact-state gates (2026-09-06)

The actual all-fallback census now supports one isolated candidate with 64 additional hot pages and 2,048 new positions, preserving all 512 original regions/bits and the unchanged compiler/guards. The seed is 14.36 MB with 576 regions; maximum function size remains 67,111 bytes. All 441 unaffected original bodies are byte-exact. Loader/lifecycle53, actual runtime capacity1,744 and boundary404 scenarios pass. One100M candidate replay matches all four complete raw snapshots and framebuffer of the exact7a72 control; generated retirement rises73.09M→78.98M but calls also rise9.27M→9.88M. These counts are not a measured gain. Actual browser and compilation budgets remain the next gate; production is unchanged. [Source, selection, proof and limitations](../docs/traces/jit-mixed-coverage/README.md).

### Mixed coverage browser decision: parked (2026-09-06)

All 88 strict settled frames and eight manual first views pass, along with both compilation CPU budgets. The frozen three-arm series stopped before the fourth browser at its disk floor; after preserving the failure and clearing only completed HTTP/Code caches, the separately authorized unstarted fourth original arm passed unchanged gates. No earlier observation was rerun or dropped. This cleanup gap remains explicit.

Across 20 warm samples/config, Settings median is 3.167→3.067s, p95 3.467→3.261s. Median gains are 6.86% forward and 2.35% reverse. First Settings disagrees: 6.364→5.506s forward, 5.201→5.588s reverse. Home median remains ~0.867s; pooled p95 improves 0.966→0.917s but reverse p95 regresses 0.867→0.967s. Compilation thread CPU is original 19.284/17.105s versus candidate 20.662/20.626s. This is fresh-profile controlled-import work, not cached return-visit speed. **Parked without promotion**: modest warm gains do not overcome the mixed first/Home result and larger compiled seed. [Milestone 088, full original-speed recording and complete evidence](../docs/traces/jit-mixed-coverage/README.md).

A source-only follow-up proves every current original512 body/read/write set matches the already parked register-omission experiment. Static and historical100M weighting remain unchanged; the current fallback census contains no new generated per-region weights. No repeat prototype/timing is justified from those same nominal load/spill counts. [Identity recheck](../docs/traces/jit-mixed-coverage/browser/register-recheck.json).

## Full-web LLVM LTO structural candidate (2026-09-06)

The released web build uses -O3 without LLVM LTO; CMake IPO is enabled only for native targets. A separate full-source build with only -flto=full at compile/link passes structural checks: control reproduces released7a72 exactly, candidate259,019B (+3.39%) reproduces on relink, all35 public functions/liveABI1/16offsets match, and all48 objects are LLVM bitcode. VFP keeps -ffp-contract=off. No seed/compiler/runtime source policy changes, guest correctness run, timing claim or production promotion. [Frozen source/build evidence and proposed actual-engine/VFP/100M gates](../docs/traces/web-lto/README.md). This investigates cross-file interpreter/helper optimization rather than reopening a parked generated-code microcandidate.

LTO correctness follow-up: all404 boundary scenarios/1,212 engine rows per build have exact architectural/state/counter vectors. The candidate interpreter and seeded100M runs match eight full raw checkpoints and both final frames; original seeded counters remain exact. The literal actual-WASM VFP suite exposes an existing control failure:595pass/12fail, reproduced identically under LTO. Missing host exception status affects guest flags and an FZ vector refusal; host directed-rounding requests silently leave nearest rounding. [Separate baseline VFP defect](../docs/traces/wasm-vfp-conformance/README.md). Relative LTO correctness passes; fullVFPconformance does not. No fix/test relaxation is combined, no timing yet.

The sole coordinated LTO600M CPU ABBA passes every full-state/counter/frame gate after equal600M warmups. Process CPU original22.736519/22.762182s versus LTO21.480879/21.374689s:5.52% forward,6.10% reverse,5.81% combined reduction. Summed API elapsed improves5.79%. [All values, fresh RTC-aware references and limits](../docs/traces/web-lto/README.md). This justifies a separately reviewed browser comparison with original512seed/current085UI; it is not a navigation or production gain. The identical baseline VFP595/12conformance failure remains unresolved and unmixed.


### Whole-web LTO actual navigation diagnostic (2026-09-06)

The isolated full-LTO core has5.81% fixed-work CPU benefit, but actual fresh-profile Settings median changes3266.75→3216.90ms and Home pooledp95 increases917.7→966.7ms. Settingsp95 improves3816.8→3417.0ms; both firstSettings observations improve, while reverseHomep95 worsens50.4ms. All88 strict raw comparisons and eight manual first screens pass. The first original control completed all22 stages then failed its asset reporter; its complete trace/pixels were independently recovered but graceful closure/full recording are absent. The three unstarted arms continued separately, retaining every observation and failed profile. LTO stays parked; production is unchanged. [092 full actual recording and scope](../docs/traces/web-lto/browser/README.md), [complete per-arm evidence](../docs/traces/web-lto/browser/README.md).


### Executed-work census preparation (2026-09-06)

The isolated original512 observer adds per-generated-retirement counters while retaining the actual interpreter census. All404 focused scenarios/1212 engine rows preserve state and policy counters;26 precise cases,3 deliberately wrong-count negatives and14 retained-byte decoder controls pass. Physical/routine mapping stays unknown without independent binding. All original emitted bodies strip byte-identically. The one deterministic100M gate is prepared but held for Safari/Photos disk priority; no phone census or performance measurement has run. [Contract and evidence](../docs/traces/jit-executed-work/README.md).


### Executed-work100M correctness and retained ranking (2026-09-06)

The single count-only observer trajectory passes all four complete raw snapshots, original JIT policy counters and final frame. Independent saved-byte audit reconciles73,093,840 generated and26,906,160 interpreted steps with zero count loss; all nine observed process identities closed. This is no timing or warm-browser result. Retained first100M work identifies a3,883,986-step byte-hash-shaped range that is already99.734% generated. Routine/physical identities remain unbound; no optimization follows from count share alone. [Evidence and limits](../docs/traces/jit-executed-work/README.md), [next measurement hypothesis](../docs/traces/jit-executed-work/hotspot-note.md). Production unchanged.
