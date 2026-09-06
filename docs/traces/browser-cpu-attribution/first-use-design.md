# Next measurement: browser compilation versus guest first-launch work

Status: this was the source-reviewed pre-run design. The subsequent [completed trace and retained failure](../browser-first-use/README.md) implement it; this document preserves the experimental contract. The decoder partition is parked; production remains on the original decoder and seed.

The accepted profile leaves two different questions. Warm Settings spends 29.09% of worker self samples in `arm_step`, but this function includes fetch, condition/interrupt handling and inlined instruction semantics. Its percentage does not identify decoder-selection cost, and the isolated decoder screening did not produce a repeatable gain. Separately, first Settings consumes 5,930 ms of renderer pool-thread CPU versus 70 ms warm. Pool-thread names do not identify compilation. Resolving this large first-use difference is the next measurement; further interpreter micro-optimizations wait.

## One experiment, three guest stages

Use the current production runtime and original seed in a fresh Chrome process and temporary profile. Begin a bounded browser-wide trace before navigation and module loading. Preserve the actual served core/seed response hashes, browser/V8 versions, stage markers, screenshots, worker counters and Linux thread CPU snapshots. This is a diagnostic with tracing overhead, not a new navigation acceptance benchmark.

1. **C0 — cold guest, first host use:** import the original genuine Home checkpoint, then open Settings and return Home.
2. **H1 — ordinary warm use:** open Settings and return Home again without restoring.
3. **C1 — cold guest, retained host code:** import exactly the same original Home checkpoint into the same emulator worker, then repeat Settings/Home.

Keep the import/paused-state protocol identical for C0 and C1: pause before Import, finish both persistence operations, confirm seed installation and Home readiness, then resume with the same bounded delay before input. This prevents guest execution during the variable-duration backup compression from silently warming only one restored guest. Record the delay and actual work counters; restored bytes do not make later wall-clock/input scheduling bit-exact. Startup and import remain separate trace stages, so compilation that completes before the first tap is still visible. The normal Import path contains two Saves; neither belongs in a navigation interval. Suppress periodic automatic Save identically in this diagnostic and retain the exact source seam. Do not change guest clock policy, firmware, CPU seed or Chrome tiering flags.

The source supports the control: `web/seed-jit.js` creates `seedPromise` once per worker and stores the result of `WebAssembly.compile`. `reset()` awaits that same promise and instantiates its existing module. `web/emu-worker.js` restores the machine inside the existing worker and core instance. This proves module reuse at the application level; actual compilation/tiering behavior still needs the trace. It does not guarantee that every optimized function survives unchanged.

## Capture and accounting contract

Discover categories with `Tracing.getCategories`; enable the available WASM categories and a bounded JSON stream. Record clock-sync markers for each stage, collect `Tracing.tracingComplete`, reject a trace reporting data loss, and read/close its stream. Keep Debugger disabled throughout: the earlier bytecode requests timed out, and debugging could alter execution. Use response-byte hashing for served asset identity instead. The protocol defines these operations and the completion loss flag. [Official CDP Tracing reference](https://chromedevtools.github.io/devtools-protocol/tot/Tracing/).

The earlier actual browser was Chrome 152.0.7977.82 / V8 15.2.124.21. That V8 source exposes `wasm.ExecuteCompilationUnits`, `wasm.BaselineCompilation`, `wasm.TopTierCompilation` and wrapper/other compilation scopes in category `v8.wasm`. Inner tier scopes enclose **batches of functions**, so event counts are not compiled-function counts. Detailed async scopes use `disabled-by-default-v8.wasm.detailed`. Recheck the runtime version and observed event schema rather than assuming a future browser retains these names. [Pinned V8 module compiler](https://raw.githubusercontent.com/v8/v8/15.2.124.21/src/wasm/module-compiler.cc).

Use a read-only diagnostic worker message to obtain stage-boundary ticks, existing `emu_cycles`, JIT retirement/calls/regions, worker time and a cumulative sum of actual `api.run` return values. The latter measures retired work without confusing the one-second metrics interval or the name `cycles` with an exact stage instruction count. Record main send/reply timestamps to bound message skew; never sample shared WASM state from an unsynchronized main-thread memory view. Increment the diagnostic sum once per run call, not per guest instruction. Keep the WASM bytes unchanged and retain this small worker-source diff.

Measure renderer-main, emulator-worker and background-thread scheduler CPU independently. Trace duration is elapsed residence, not automatically CPU. Prefer observed thread-time fields where available, but preserve their units and completeness. Union nested intervals by process/thread; do not add an outer compilation-job duration to its inner tier batches. For scopes crossing a stage boundary, report conservative bounds instead of proportionally inventing CPU attribution. If thread-time fields are absent, report wall-span unions alongside scheduler CPU, explicitly separately. New/exited threads and unaccounted CPU stay visible. Attribute core versus seed compilation only if actual trace fields or independently verified bindings support it; otherwise retain module-unknown compilation.

Retain the same strict Settings/Home visual checks used by the accepted navigation investigation, including all failed screenshots and without widening tolerance. Report cold/warm/restored stage elapsed, retired work, worker CPU, pool CPU and compilation spans together. A missed tap, failed readiness gate, missing seed, unfinished Save or trace loss invalidates the affected comparison.

## What the result can distinguish

| Observed result | Supported next direction |
|---|---|
| C1 is close to H1, C0 has substantial compilation, and C0/C1 guest work is comparable | Host first-use compilation is a plausible contributor; investigate scheduling or compilation cost with a separate controlled intervention. |
| C1 resembles C0, H1 retires less guest work, and C1 has little compilation | Original guest application startup/caches are the stronger explanation; profile that guest work rather than browser warmup. |
| C0 pool CPU remains large without matching compilation events | Retain it as unresolved; inspect actual trace categories/stacks rather than label it compilation. |
| Worker CPU per comparable retired work stays similar and launch time differs mainly with retired work | Focus the next attribution on executed guest paths and interpreter/generated coverage, without assuming one decoder suboperation dominates. |

Compilation overlapping a launch does not alone prove it lies on the critical path. One three-stage run is a mechanism diagnostic, not p95 evidence or grounds for production promotion. A mixed result remains mixed. Further quiet runs or a semantic interpreter suboperation census should follow only if this first trace leaves a concrete unresolved question.
