# Browser CPU attribution, original production603679

This is one statistical attribution run, not a speed benchmark or p95 gate. All12 raw profiles, six stage scheduler CPU snapshots, actual screenshots, main report and exclusive analysis are retained. The production core, original CPU seed and frontend were not changed. Settings/Home and ordinary Save completed. Debugger was enabled only after all profiling stopped; its two bytecode retrieval calls timed out and remain in modules.json.

`analysis.json` groups every sampled top-of-stack frame exactly once, without adding inclusive parent/child time. It preserves every function row, raw sampled interval, scheduler CPU delta and uncertainty. `analysis-unresolved.json` retains the earlier analysis before internal C names were proven. Sampled residency and scheduler CPU are separate measurements; no per-function figure is claimed to be a hardware-cycle counter.

The direct --emit-symbol-map relink changed function ordering and was correctly rejected for equal-index mapping. A normal no-map relink was byte-identical to production. Root's full-module function-renumbering proof then established a unique bijection for223 defined functions and8 imports, exact complete bodies/call references/module structure and JS host bindings. Independent regeneration bound both WATs to their real binaries. Only that reviewed mapping supplies C names. The separate renumber-proof folder retains its exact proof, source, bindings and negatives.

Captured bytecode identity was not obtained. The core URL/indices correspond to the unchanged production module; the second worker WASM script is inferred to be the generated seed from the actual worker pipeline, expected seed byte count and positive increasing JIT metrics. This limitation is explicit. Inlined C work stays with the containing function. Distinct MBX-symbol residency cannot prove all graphics-related work has been separated from inlining.

First/warm Settings worker residency: arm_step23.03%/29.09%, generated module28.02%/28.07%, emu_run fused17.91%/12.22%, memory/MMU/bus10.32%/11.83%, region_find4.88%/6.37%, distinct MBX symbols0.58%/0.84%, emu_frame2.05%/1.85%. The former tiny JIT changes target only part of this path, and no new optimization is accepted from these percentages alone.

An automatic Save overlaps first Home and warm Settings: snapshot advancement is298ms of worker self residency during first Home, while main warm Settings has3438ms in an unresolved native frame and3690ms actual scheduler CPU. Explicit ordinary Save has6315ms in native resolve and6650ms main CPU. No claim yet identifies that native work as compression rather than another browser stage. SavedAt/capture metrics and the explicit Save reproduce the association. The subsequent [immutable snapshot isolation and worker comparison](../checkpoint-worker/README.md) establish that encoding is the dominant stage and verify the resulting page-thread offload; the original profile alone is still not used to infer a native function identity.

First Settings also consumes5930ms across renderer ThreadPoolForeg threads, versus70ms warm; these generic thread names alone do not prove compilation. Chrome GPU process CPU is separately retained. No table merges these background threads with the dedicated emulator worker.


The next [source-reviewed first-use experiment](first-use-design.md) separates actual V8 compilation events from original guest startup using first launch, ordinary warm launch and the same restored guest in the existing worker. It is a design only; no new trace or optimization has run.

Methodology follow-up: an [exact V8 15.2.124.21 source review](v8-profiler-source/README.md) finds no forced WebAssembly tier-up in plain CDP CPU-profiler/code-logging startup. Debugger enable/disable is a separate path. Profiling still changes JavaScript bookkeeping and adds logging/sampling overhead; observed compilation and residency retain those limits.
