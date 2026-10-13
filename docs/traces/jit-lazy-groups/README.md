# Smaller generated functions: isolated first-use candidate

> Historical recordings and publishing artifacts have been removed from Git history. The summaries below describe the original runs; remaining test reports and screenshots are retained. Historical hash receipts may predate this archival cleanup.

2026-09-06. **Parked after the browser comparison: no production change.** This experiment tests whether smaller generated functions reduce the compilation work observed in [the first-use Chrome trace](../browser-first-use/README.md). Correctness passes, but first-use and warm Settings are slower.

The emitter preserves the exact original basic blocks, including whole-block raw-code checks before entering a block's tail. Regions with more than 32 blocks move those blocks into private functions in groups of 32. A small public dispatcher calls the appropriate group, retaining the original remaining budget. It continues only after positive retirement and an unsupported/group exit to an exact admitted entry in a different group. True unsupported entries, stale code, faults, memory fallback, mode/page exits and zero progress return immediately. The stale callback remains inside the original body and runs once. Register/flag writeback at each group boundary is the main performance tradeoff.

The public 512 exports, all admission masks and ABI1 remain unchanged. Private functions return retirement and exit reason together; that result type never crosses the runtime ABI. The runtime is the existing `b3237…` production binary. This seed is **not combined** with the separate runtime-continuation candidate.

## Structural result

[The build](build-report.json) regenerates the original 12,341,313-byte seed byte for byte (`d9c661…`) before building the candidate (`712bca…`). Candidate size is 12,700,227 bytes, below the unchanged 32 MiB cap. It splits 265 regions and contains 1,100 defined functions, including the 512 public entries. Maximum function body decreases from 67,111 to 41,235 bytes; the median decreases from 22,589 to 10,762. These distributions include the new small dispatchers and do not measure browser compilation time or a speed gain.

## Correctness evidence

- [358 targeted cases](focused-report.json) exercise actual ARM and Thumb programs spanning three groups, all group and tail boundaries, remaining budgets, stale source/destination bytes, true unsupported instructions, page/mode exits and physical code aliases. Interpreter, original seed and candidate seed match exact architectural state, TLB/memory counters and complete 1 MiB RAM hashes. Generated retirement, public calls and stale callbacks match between seeds. Sixty-four cases also compare complete machine snapshots across literal timer/interrupt boundaries.
- [404 additional guard cases](guard-report.json) reuse the [continuation oracle](../jit-continuation/README.md) with its original-runtime-only switch. Its two generated rows both use the selected seed and original runtime; these duplicate rows do not constitute a comparison against the original seed. They extend interpreter comparisons to actual XN faults, privilege, invalid fetch state and other guards. The grouped suite above supplies the original-seed comparison and real group crossings.
- [Full replay](correctness-report.json) compares interpreter and candidate at every 100 million instructions through 600 million on both original fixtures, including Settings and Home input: 24 raw snapshot outputs, 12 exact pair comparisons, no byte normalization. Final accelerated/software hashes remain `642bd79…` / `bfcde2…`. The earlier [100M check](mbx-100m.log) also passes. Logged functional runtimes overlap other work and are not benchmark results.
- The deliberately [wrong remaining-budget variant](negative-budget.log) retires 67 instructions when allowed 65 and fails. The [missing raw-guard variant](negative-raw.log) changes full state when entering the tail of a block with a stale earlier operation and fails. Neither variant is an accepted candidate or a production change.

## Browser result

Four fresh Chrome profiles ran original/candidate/candidate/original on the same production runtime, with actual served asset hashes, 512 installed regions, positive generated execution, no browser errors and no trace loss. C0 is first Settings/Home use after a paused Import and one-second resume; H1 repeats the actions; C1 restores the same guest into the same worker and repeats them. The diagnostic frontend suppresses periodic Save in all arms. All four arms use identical profiling and video recording. These are two observations per configuration/stage, not p95 estimates or the normal production navigation protocol.

| Settings stage | Original run 1 / run 4 | Candidate run 2 / run 3 |
| --- | --- | --- |
| First use C0 | 4.934 / 4.956 s | 5.014 / 5.265 s |
| Warm H1 | 2.767 / 2.817 s | 3.216 / 2.967 s |
| Guest restored C1 | 3.414 / 3.566 s | 3.617 / 3.467 s |

First-use and warm Settings regress in both paired directions. Home observations are mixed. During C0 Settings, identifiable seed baseline compilation rises from 226 functions / 5,645,619 body bytes / 258.7–264.5 ms thread CPU to 551 functions / 6,342,066 bytes / 279.3–279.7 ms. Smaller individual bodies cause more helper bodies to be compiled; they do not reduce the measured baseline compilation work. Total compilation thread CPU remains roughly 10–11 seconds distributed across threads, with overlapping conservative bounds. Anonymous top-tier scopes cannot be assigned to a module. CPU sums are not wall time, and profiled measurements include collector overhead.

All **24 matched-stage raw screen comparisons pass** the predeclared ≤2 RGB tolerance below guest row 30, with exact alpha. Root manually reviewed all 24 images; they show the correct original Settings and Home content. Same-run cold/warm comparisons remain a separate diagnostic with fading scrollbar differences retained. See the [decision and individual timings](quiet-first-use/decision.json), [screen report](quiet-first-use/screen-comparison.json), [contact sheet](quiet-first-use/contact-sheet.png), and full per-run traces, profiles and asset receipts under [quiet-first-use/](quiet-first-use/). Rows in the contact sheet are runs 1–4; columns are C0 Settings/Home, H1 Settings/Home, C1 Settings/Home.

The historical recording (removed) preserves run 2 at original speed, including Import, pauses and all three stages. historical thumbnail (removed). It is evidence of an unsuccessful speed experiment, not a promoted improvement. Original WebM hashes and all durable evidence hashes are in [artifact-pins.json](quiet-first-use/artifact-pins.json).

## Reproduction

The [source directory](source/) retains the exact experiment scripts and emitter. Their working location is `.context/jit-lazy-groups/`; relative module paths assume that location, not this evidence directory. Run `prepare.py`, `build.cjs` and `test.cjs` there. `replay.mjs` retains the maintained production replay with only the seed path changed. The synthetic oracle is frozen locally; [its hashes](local-oracle-pins.json) and [original source/build provenance](../jit-continuation/README.md) identify it. It contains a continuation entry point, but this experiment calls only the original entry point.

`first-use.mjs` applies the same diagnostic frontend/worker seams as the accepted first-use trace and routes the frozen production runtime and selected seed, definitions and ABI metadata. `run-first-use.py` records final source pins before the quiet run and rechecks them between arms; `review-first-use.py` applies the declared screen gate, and `bind-lazy.py` resolves baseline events only when function index and body size uniquely match a pinned module. The earlier [pre-browser pins](pre-browser-local-oracle-pins.json) are retained separately from the final source identities. Production keeps `b3237…` and the original `d9c661…` seed.
