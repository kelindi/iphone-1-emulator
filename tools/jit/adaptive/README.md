# Adaptive compiler baseline tools

Stage 1 only: independent baseline copies and verified browser preconditions.
No adaptive execution engine is installed by these tools.

Run from the repository root with the built production `web/dist` and `web/jit`
assets present and the pinned npm dependencies installed. Freeze into a **new**
directory; existing directories are deliberately rejected:

```sh
node tools/jit/adaptive/freeze-baseline.mjs .context/adaptive-compiler/my-baseline
node tools/jit/adaptive/freeze-baseline.mjs .context/adaptive-compiler/my-baseline --verify
WEB_ROOT=.context/adaptive-compiler/my-baseline/site PORT=8322 node tools/web-server.mjs
```

In a second terminal:

```sh
BASE_URL=http://127.0.0.1:8322 \
BASELINE_MANIFEST=.context/adaptive-compiler/my-baseline/freeze-manifest.json \
ROUNDS=31 ARTIFACT_DIR=.context/adaptive-compiler/my-report \
node .context/adaptive-compiler/my-baseline/harness/performance.mjs
```

On the reference Mac, also set:

```sh
export CHROME_PATH='/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'
```

The same command records CPU, RAM, operating system, Chrome version and Mac
hardware model where available. Mac performance must be measured there; the
cloud host is not a substitute. Run one browser trial at a time, with no competing
builds, compiler jobs or other benchmark sessions.

The harness recognizes the genuine lock screen, slides to unlock, and requires
both the SMS icon and Mail dock evidence before using Home as its reference.
Unknown screens fail preparation rather than becoming a timing baseline. A
sleeping display is woken with the real Home button. Each Settings action requires
Home again. Preparation time is reported separately from pointer-release-to-ready
latency; the first app launch is after opening/unlocking a saved phone, not a cold
OS boot. Normal guest clock and autosave remain enabled.

Thirty-one pairs produce one first pair and thirty warm samples for each action.
The p95 field is null below thirty warm samples. `passed` describes successful
execution and identity verification; the separate navigation-target field reports
whether p95 meets the 1-second warm target. A slow baseline can therefore pass
qualification while failing the product performance target. This harness does
not measure touch-feedback, scrolling, or cached-startup acceptance.

The manifest pins independent shell/runtime/seed copies, selected core/compiler
sources, contracts, dependency metadata and the executed harness. Firmware stays
at its shipped URLs with the existing manifest's hashes; mutable local firmware
copies and browser profiles are not included. Source changes during copying fail
the freeze. Later source changes cannot change copied files. Verify again after
a run; browser reports also retain actual served-file hashes, including failures.

Reports contain preparation evidence, per-action timings, raw RGBA gzip files,
screenshots and full WebM video. Preserve failed runs. Never hardlink editable
files into a baseline or silently rebuild an existing frozen directory.

Summarize a complete frozen baseline:

```sh
node tools/jit/adaptive/summarize-baseline.mjs .context/adaptive-compiler/my-report/report.json
```

This command rejects failed runs, missing frozen identities, missing Home
preparation and fewer than thirty warm samples. It reports the median and
nearest-rank p95 separately from whether the product target was met.

The initial C fetch-witness guard is in `core/include/arm_adaptive_guard.h`.
It only captures already validated interpreter fetch caches. Its caller must
supply an epoch that changes before reset, RAM replacement or generation wrap,
and retain the owning RAM for the witness lifetime. Per-instruction literal
checks remain mandatory. The current tests construct cache fields explicitly;
they do not prove real MMU admission or scheduler integration:

```sh
cc -std=c11 -Wall -Wextra -Werror -Icore/include \
  tools/jit/adaptive/tests/guard-contract.c core/src/arm/arm_interp.c \
  core/src/arm/vfp.c core/src/mmu.c -lm -o /tmp/iphone-guard-contract
/tmp/iphone-guard-contract
```

The isolated full runtime can now be qualified with
`node tools/jit/adaptive/test-machine-runtime.mjs`. This builds into `.context`,
connects emitted WASM to the C registry/guards and actual scheduler, and compares
complete snapshots on small integer workloads. It does not activate the product
build or substitute for the three real-app workload gate. Current-mapping peek
checks reject evicted or replaced translations even when their generation is
unchanged.

## Isolated real-phone workload runtime

This harness includes the actual browser API in a separate translation unit and
links only the adaptive scheduler/runtime, excluding ABI1 generated stores. It
does not change the product build or default engine. Use a fresh output directory:

```sh
ARTIFACT_DIR=.context/adaptive-compiler/workload-runtime-new \
node tools/jit/adaptive/build-workload-runtime.mjs
ARTIFACT_DIR=.context/adaptive-compiler/workload-runtime-new \
node tools/jit/adaptive/smoke-workload-runtime.mjs
```

The smoke test verifies the frozen Home snapshot and firmware identities,
reinitializes both runs identically (including derived MMU stamps), restores the
saved phone, and compares all saved bytes after 10,000 scheduler credits with
an empty adaptive registry versus the interpreter. This is API/lifetime setup
qualification, **not compiled coverage, an app replay or a performance gate**.

`workload_prepare` binds a new epoch and physical RAM tracker after init/restore.
The host must drain retired function handles before installing replacements;
this adapter does not own JS table removal. `workload_layout` is an ABI2-only
layout descriptor: revision, register/CPSR offsets, CPU/witness sizes, witness
host/block/version offsets, result size and pointer size. All ordinary browser
input, run, framebuffer and snapshot functions retain their real implementation.

## Real-app compiler correctness pilot

Build the observer-enabled harness into a fresh output directory, then run the
fixed Settings trajectory (Contacts and Notes use `WORKLOAD=contacts` or `notes`):

```sh
ARTIFACT_DIR=.context/adaptive-compiler/workload-observer-runtime \
node tools/jit/adaptive/build-workload-runtime.mjs
RUNTIME_ROOT=.context/adaptive-compiler/workload-observer-runtime \
ARTIFACT_DIR=.context/adaptive-compiler/settings-adaptive \
node tools/jit/adaptive/replay-workload.mjs
```

The default executes all 600M credits and compares complete checkpoints at
100M/300M/600M against the frozen reference. `STOP_CREDITS` permits a diagnostic
prefix; a prefix is not full workload qualification. Timings include synchronous
compilation within emu_run but remain diagnostics, not the declared warmup/ABBA
performance trial. No performance gate has been run by this tool alone.

Collection policy is fixed in `workload-bridge.c`: 16,384 direct-mapped counters
keyed by PC/ISA/mode/TLB generation, one compilation attempt per 1,024
eligible admission misses for a resident key. Collisions restart counting. The
current collector retries after another 1,024 misses; its diagnostic attempt
counter saturates at 2^53−1 without stopping collection. Installed units and
binary bytes remain bounded by the C runtime. The original full-replay evidence
used a lifetime2,048-attempt cutoff; its removal is being qualified alongside
installed-unit generation refresh. This still does not establish complete hot coverage. The JS compiler follows captured connected
edges, owns installed function slots and removes only explicitly retired slots.

## Predeclared CPU throughput gate

After complete workload correctness qualification, run the fixed gate once:

```sh
RUNTIME_ROOT=.context/adaptive-compiler/workload-refresh-runtime \
ARTIFACT_DIR=.context/adaptive-compiler/workload-abba-first \
node tools/jit/adaptive/benchmark-workloads.mjs
```

Each workload gets one warmup per engine, then timed A/B/B/A runs. A is the
independently frozen seeded engine; B is the selected source-pinned adaptive
build. Both retain their runtime modules across runs and initialize/restore
identically before each replay. All54 complete checkpoints must match, and both
engines must actually execute generated code. Two CPU-time means per workload
produce three A/B ratios; their geometric mean must be at least2 and none may
be below1. This is a CPU gate, not browser/Mac interaction qualification.

The report states the exact timing boundary and includes compilation/setup,
execution and handle-management costs. Keep other measurement/build work idle
during the gate. Do not overwrite a previous output or treat a diagnostic prefix
as a gate run. The accepted plan permits one measured corrective revision after
a failed trial, then requires review if the gate still fails.

## Corrective revision after the first failed gate

The compiler owner now keeps an exact-WAT module LRU for the duration of one
replay: at most 2,048 entries and 32 MiB of accounted UTF-16 WAT plus original
binary payload. This is an accounting limit, not a measurement of V8 native
code/metadata memory. Modules may be reused; captures, instances, function
handles and installation validation are fresh. Closing the owner clears the
cache, so no previous replay warms the next. `compiler.moduleCache` reports
hits, misses, evictions, entries and accounted bytes before teardown.
`compiled` and `binaryBytes` now count actual compilation rather than cache hits.
Both replay and gate pin the cache implementation along with compiler sources.

Focused cache tests: `node --test tools/jit/adaptive/tests/module-cache.test.mjs`.
The second gate still uses the original workloads, ordering and thresholds.
