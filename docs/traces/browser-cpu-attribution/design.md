# Actual browser CPU attribution preparation

No production edits. No profiling or relink has run yet. Root's register-omission proof/timing has priority. The previous code-count experiments are retained separately; no new optimization is inferred from counts.

## Evidence available now

Current production emu.wasm and seed.wasm contain no custom/name section. The core has223 defined functions and8 imports after optimization; the seed has512 exported generated functions plus imports. Production code-section SHA is95b783f0a439f733f88823d91c7321e9e90d3c094ddde01575c773ad705e28aa. Seed code-section SHA isd3a56378551226bdeecc87743365252b72babe42a3c405f34ca815461c4ab2b1. Exact function indices can be symbolized only after checking their final optimized map.

Installed Emscripten tools/link.py:2273–2288 retains intermediate names for --emit-symbol-map;2413–2422 emits the final map and strips the temporary name section. symbols.py relinks the exact existing objects/archive with only that option to isolated output. It fingerprints inputs and requires every standard WASM section byte-identical to production, ideally the complete binary. A differing executable section rejects the map; do not assume a profiling build is representative.

## Sampling approach

Use the current production phone and genuine fixed MBX snapshot through normal Import. Connect to Chrome DevTools Protocol separately for the dedicated emulator worker and main renderer page, using flattened target sessions. Profiler.enable / setSamplingInterval(1000us) / start / stop are defined by the installed primary protocol in node_modules/playwright-core/types/protocol.d.ts. Start/stop at actual stage boundaries: stable Home idle, first Settings, Home return, warm Settings/Home, and an explicit ordinary Save.

The existing50ms pixel observer may be reused unchanged for navigation stage boundaries, but label its main-thread work separately. Save all raw profiles and screenshots after stopping sampling. Profiling changes overhead and host scheduling; these runs are attribution diagnostics, never replacement launch benchmarks or p95 acceptance. Avoid screenshot encoding/video while the sampled stage is active. Preserve stage elapsed time and profiler time span separately.

Do not enable Debugger during measured stages: that may change execution tiering. After every profile is collected, enable it once to enumerate wasm scripts, request getScriptSource bytecode for those scriptIds, and hash each module. Match exact production core and seed bytes to script IDs before symbolizing. If bytecode cannot be obtained or a frame cannot be mapped, retain it as unknown. Seed export indices map to retained region metadata; no name-section patch is needed.

## CPU time versus elapsed time

CDP samples/timeDeltas estimate sampled stack residence, not exact device CPU cost. Report self samples/time and inclusive ancestry separately; never add inclusive parent and child time. Keep (idle), (program), garbage collector, unclassified native and observer/harness categories explicit. Inlined C work can be fused into arm_step, machine run or emu_run; label it fused rather than pretend a function map recovers inlining.

Collect SystemInfo.getProcessInfo CPU seconds and Linux /proc renderer task utime/stime snapshots at stage boundaries, with actual CLK_TCK. Record newly created/exited threads and any incomplete accounting. Dedicated-worker and renderer-main CPU deltas provide a separate real scheduler-CPU check against stage elapsed time. Browser/compositor/compiler processes must remain separate; their work cannot be assigned automatically to guest execution.

Initial exclusive categories: generated seed regions; C interpreter/dispatch or fused C execution; MBX functions with verified distinct symbols; snapshot capture/checksum/copy functions; worker JS present/frame comparison/copy; main-thread frame upload/presentation; save compression/storage; other JS/native; GC; idle/unknown. Inspect actual final symbols before defining MBX/capture patterns, and retain every per-function row. Do not interpret a missing inlined MBX frame as zero MBX cost.

If large fused C functions prevent actionable attribution, only then design a separate instrumented/no-inline diagnostic, with exact full raw-state proof and explicit executable/overhead differences. Such a build cannot be silently compared with production as a speed arm. Prefer stack sampling and unchanged-binary mapping first.
