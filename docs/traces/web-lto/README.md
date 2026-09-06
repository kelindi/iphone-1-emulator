# Isolated full-web LLVM LTO — relative correctness

The exact released source rebuilds byte-identically without LTO. Adding only `-flto=full` to every C compilation and the final executable link produces a reproducible candidate with the same live JIT ABI and public API. The candidate now matches eight complete raw guest checkpoints, final frames and unchanged seeded counters through two 100M runs. **Baseline VFP conformance fails twelve literal checks identically with and without LTO; relative correctness passes, full VFP conformance does not.** One coordinated CPU ABBA now shows a repeatable 5.81% reduction in process CPU. The subsequent browser comparison shows only a modest Settings median benefit and a Home tail regression, so LTO remains parked; see [all browser results and retained first-control failure](browser/README.md).

| Build | WASM bytes | SHA-256 |
|---|---:|---|
| Full control rebuild | 250,531 | `7a72add6d886440f64a853511375ba51cbbb23f2263831b6a05c51fde7969088` |
| Full LLVM LTO candidate | 259,019 | `7a528805814b7cf8ad061a1f5d2fb70aeb316c8a12585dfb25d6d08752dca740` |

The 3.39% size increase is not a speed prediction. The candidate targets cross-file interpreter/helper optimization, distinct from the previously parked generated-code register, guard, dispatch and coverage experiments. Searches of the retained speed plans and durable CPU evidence found native IPO work but no prior full-web LLVM LTO experiment.

The original CMake file enables IPO only outside `IPHONE1_WEB`; actual released core and API flags contain `-O3` without `-flto`. The pinned Emscripten 4.0.16 SDK supports `-flto=full` at compile and link. All 48 candidate C objects have LLVM bitcode signatures. VFP retains the original `-ffp-contract=off`; no fast-math, Ofast, SIMD or other feature changes are introduced. The SDK selects its established LTO library variants where supported, while deliberately retaining ordinary objects for some runtime functions. [SDK receipt](source/sdk-proof.json) and [contract with official documentation links](source/contract.md).

Only `cmake --build … --target emu --parallel 1` ran in separate source/build/dist paths. The seed-generation and offline ALL targets were never invoked. All 111 copied C/header/CMake source files match the exact released RTC tree and current corresponding source files. Production runtime, seed, ABI, loader and service-worker hashes remain unchanged. The initially guessed `web-source/web/build` path was absent; the actual released receipts reside under `.context/rtc-write/web-build`.

The full control matches released WASM; candidate relinking reproduces both WASM and glue exactly. A no-guest module inspection matches all 35 public JavaScript function exports, initial memory, revision 1 and all 16 JIT offsets. The original 512-region seed is unchanged. Internal symbol numbering and allocator addresses are not assumed stable under LTO. [Build result](source/build-report.json), [live ABI inspection](source/structure.json), [all compile/link commands](source/commands.json), [source and production pins](source/source-pins.json).

The original proposed gate is retained in the contract. Execution exposed the unchanged control's twelve VFP failures before the candidate ran; the first stop is preserved. A separately approved relative comparison then established identical entire VFP output (595 pass/12 fail) and 404 boundary scenarios/1,212 engine rows per build with every state vector and counter exact. Rows 1 and 2 both use the original runtime policy; no parked continuation is combined. [The guest-visible VFP defect and exact operations](../wasm-vfp-conformance/README.md) remain a separate correctness issue.

The authorized relative 100M pair passes complete snapshots at 25/50/75/100M, with no ignored bytes. Both final 320×480 RGBA hashes match. Seeded retirement is 73,093,840 with 9,273,308 calls and 509 active regions, all exact to original control; interpreter retirement/calls through JIT remain zero. [Aggregate result](correctness-result.json), [per-arm reports](replay/), and [retained-control applicability proof](source/control-applicability.json). Kernel/tree pins, the original seed and input trajectory are frozen. Eight complete gzip state files were written before comparisons and losslessly read back; their local paths and byte hashes remain in each report. No timing claim follows from functional runtime durations.

[Archived source tree](frozen-tree/), [control](control/), [candidate](candidate/), [build script](source/build.py), [inspection script](source/inspect.mjs), and [archive hashes](archive.json) preserve the isolated result. Scripts retain their original `.context/web-lto` paths; reproduction requires the pinned SDK and restoring the recorded source layout. This is a source/build investigation, not a visual milestone.

## Coordinated fixed-work CPU result

After two full600M warmups, the sole measured series runs released control → LTO → LTO → released control.

| Run | Process CPU within emu_run intervals | Summed emu_run elapsed |
|---|---:|---:|
| First control | 22.736519 s | 22.706889 s |
| First LTO | 21.480879 s | 21.467947 s |
| Second LTO | 21.374689 s | 21.364327 s |
| Final control | 22.762182 s | 22.756712 s |

Process CPU reduction is **5.52% forward and 6.10% reverse**, combined **5.81%**; elapsed reduction is **5.46%/6.12%**, combined **5.79%**. Warmups and all individual values remain in the report. There was one series, no retries or sample removal. The controller closes successfully in182seconds under its420second bound; coordinated peers were held during the run. Redacted process receipts contain no command arguments.

Every measured checkpoint matches the released-control warmup's complete raw state/size/TLB/JIT counters, and every final frame matches. There are24 measured checkpoint receipts and12 warmup receipts. The initial control warmup establishes new post100M references because historical600M references predate the RTC extension; its100M raw state also matches the independently retained7a72 reference. It is not falsely counted as an independent self-comparison. Final600M state is `064643fc16239fbceaae9a3519e66734fd6384a4dd963c107fbe9d21ec8b4133`, generated460,306,256/calls64,437,078/active509. No snapshot field is ignored.

The metrics exclude restore, install, input, snapshots, hashing and artifact writing, and include any process backgroundCPU occurring within sampled API intervals. They are deterministic fixed-work Node/WASM results, not navigation or first-use Chrome timings. The unchanged VFP defect remains explicit. [Frozen method](source/timing-contract.md), [complete values/checkpoints](timing/report.json), [independent audit](timing/analysis.json), [controller](timing/controller.json).

The completed separately reviewed browser comparison uses the same original512seed and current085 UI, with only the core/glue changed. Its original first control failed during reporting after all22 stages; three unstarted arms continued separately. [The complete browser evidence](browser/README.md) retains that interruption and supports parking the candidate.
