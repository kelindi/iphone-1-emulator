# First-use browser compilation attribution

One completed diagnostic distinguishes real browser compilation from original guest application warmup. No production code changed, no optimization was promoted, and these traced observations are not a p95 benchmark. The strict comparison retains one warm Settings scrollbar mismatch; it is not presented as a completed all-stage speed gate.

## What ran

Chrome 152.0.7977.82 / V8 15.2.124.21 loaded the production phone in a fresh temporary browser profile. Actual response bodies match production core `b3237ef3843412aea164d3db111299650ea8433bf16311417116799959ab6606` and original seed `d9c661556ab17bb8433ed9b0c3ecfd52e2fcafe58221b4cb84a920e4976b3881`. The original MBX Home fixture is hash-pinned in the report. No Debugger or tiering flags were used. Service workers were blocked to make diagnostic source routing and served asset identity explicit.

Only routed JavaScript differs: periodic Save is suppressed, a counter accumulates actual `api.run` return values once per call, and a read-only worker message reports counters at action boundaries. Both small source patches are retained. Native execution, original seed, clocks and public controls remain unchanged.

Tracing begins before navigation. C0 imports the genuine checkpoint while paused, finishes both normal Import saves, resumes and waits one second before first Settings/Home. H1 repeats Settings/Home without restore. C1 performs the same paused Import from the identical original bytes inside the same worker, then repeats the same resume delay and actions. The seed's compiled module is retained by its module-level promise, but its runtime region registrations are reset normally. Both imports have all 512 regions installed while paused; every measured action makes positive generated progress.

The trace reports `dataLossOccurred: false`. Six action stages have separate main/worker statistical profiles and Linux scheduler CPU deltas, with counters before/after and actual Settings/Home screenshots. Sampled stage intervals include the unchanged 500 ms readiness-stability check. Release-to-ready intervals below exclude that final observation wait; consequently CPU/counter totals cannot be divided by those shorter ready intervals to claim throughput.

| Settings stage | Release to ready, diagnostic | Retired guest work | Generated work | Emulator-worker CPU | Renderer pool CPU |
|---|---:|---:|---:|---:|---:|
| C0: first use | 4,851.0 ms | 114,258,395 | 82,483,464 | 5,020 ms | 11,620 ms |
| H1: ordinary warm | 2,766.8 ms | 94,931,257 | 67,949,472 | 3,100 ms | 60 ms |
| C1: restored guest, retained host module | 3,616.4 ms | 115,111,672 | 82,379,527 | 4,120 ms | 80 ms |

## The background work is now identified

Actual `wasm.TopTierCompilation` trace scopes account for **10,938.445–11,572.553 ms of thread CPU** during C0 Settings. The range conservatively handles scopes crossing a stage boundary; it is not a statistical confidence interval. H1 has 42.986 ms and C1 has 2.330 ms. This agrees with the separate pool-thread scheduler deltas and resolves the earlier attribution's generic thread-name uncertainty. Thread CPU can exceed stage wall time because multiple compiler threads run concurrently.

C0 also executes 250 lazy compilation scopes on the emulator worker, consuming 307.640 ms of thread CPU; their nested baseline compilation consumes 282.646 ms, **not additional time to add**. H1 has one lazy scope taking 0.118 ms; C1 has none. Top-tier scopes batch functions and carry no module identity, so their counts are not function counts and their CPU is not assigned to either core or seed.

Baseline events additionally expose complete body size and function index. An independent code-section parser binds both exact response-hash-matched WASM files, then resolves only unique index/size pairs across them. All 616 observed baseline events have a unique match: 452 seed and 164 core. During C0 Settings, **226 seed bodies totaling 5,645,619 bytes consume 281.344 ms baseline CPU**, while 24 core bodies consume 1.302 ms. The largest newly touched seed body is 67,111 bytes. This identifies the seed as the dominant lazy-baseline cost, but does not bind anonymous top-tier events. Wrong size/index and deliberately ambiguous module-pair controls remain unresolved as required. [Binding source](bind-lazy.py), [complete pairs/layouts](run-2/lazy-binding.json) and [negative controls](lazy-binding-negatives.json).

The analyzer unions CPU intervals per process/thread and keeps inclusive event-family rows explicitly non-additive. For boundary-crossing scopes, the lower bound omits their uncertain boundary contribution and the upper bound includes their whole CPU interval, unioned with contained scopes. Wall intervals are clipped to stage boundaries: C0 compilation's global wall union is 2,966.409 ms within a 5,481.416 ms observed stage. Its separately named per-thread wall sum is 17,645.955 ms, which is not global elapsed time. An earlier analysis field ambiguously called that sum `compileWallUnionMs`; the corrected field names and explicit global union replace it without changing CPU evidence. All selected compilation scopes include thread-time fields. [Analysis and complete scheduler rows](run-2/analysis.json).

The actual event names and batching interpretation are checked against the [pinned V8 compiler source](https://raw.githubusercontent.com/v8/v8/15.2.124.21/src/wasm/module-compiler.cc). Categories, clock markers, streaming and the loss flag use the [official CDP Tracing API](https://chromedevtools.github.io/devtools-protocol/tot/Tracing/). The earlier [design](../browser-cpu-attribution/first-use-design.md) remains the pre-run contract.

## What follows, and what does not

C0 and C1 have similar guest work (C1 is 0.75% higher), almost identical generated retirement, and exactly matching Settings pixels below the status bar. Yet C0 uses more emulator-worker CPU and has heavy compiler activity while C1 does not. This supports a substantial **host first-use contribution** alongside guest startup work. It does not isolate the exact causal share of compilation, compiler contention, baseline-tier execution or other retained host state. There was no tiering intervention and only one complete three-stage run.

H1 retires about 17% fewer instructions than C0. Restoring the guest restores much of that extra work even when host code is warm. Thus browser compilation alone cannot explain ordinary warm-versus-first differences or solve the remaining warm launch cost. The earlier zero-budget warmup candidate already had an unacceptable startup tradeoff; this evidence does not automatically reopen or promote it. It does justify avoiding unmeasured increases in generated module complexity and evaluating first-use behavior in future compiler changes.

All six screenshots were manually reviewed as genuine Settings and complete Home. C0/C1 Settings and all Home comparisons match every RGB pixel below row 30 exactly, with exact alpha. H1 Settings differs by at most three channel levels at 1,356 scrollbar pixels, bounded to x313–317/y67–338; this fails the retained two-level threshold. The pixels and failure remain, with no widened tolerance or discarded timing. [Explicit gates](gates.json) and [comparison details](run-2/visual-comparison.json).

The next concrete speed direction is guarded continuation across already supported regions, subject to a fresh cost/admission check and exact device/exception fidelity. The existing census records 3,209,650 region crossings in 100M instructions; 2,934,390 land in present regions, but only **2,601,329 (81.05%)** land at entries admitted by the original masks. The broader 91% region-presence figure is not executable-entry coverage. A forced interpreter return at these boundaries is a plausible target, not a measured percentage saving. Any continuation must preserve fetch translation, raw instruction checks, mode/interrupt state, cache generations and the original remaining device deadline. No such prototype has been implemented in this experiment.

## Preserved failure and reproduction

Run 1 completed six actions and saved trace/profiles/screens, then failed final report serialization because existing metrics contain a BigInt. The browser had already closed. Its complete surviving artifacts and error log are retained; its lost final trace-completion metadata prevents using it as the accepted trace. No individual timing was selected from it. A nested BigInt serializer preflight and incremental reporting were added, both hardware agents explicitly reconfirmed CPU-idle, and one whole run was repeated as run 2. Both quiet windows ended explicitly before other work resumed.

`run.mjs`, `cdp.mjs`, `prepare.py`, routed sources and patches retain the collector. Running it requires the local genuine fixture named by the report, a production server on port 8080 and the normal repository Playwright installation; firmware is not duplicated here. Raw traces are losslessly gzipped for storage. Recompute the accepted accounting without Chrome:

```sh
python3 docs/traces/browser-first-use/analyze.py docs/traces/browser-first-use/run-2
```

The latest fully verified navigation p95 remains milestone 055. This folder records an attribution investigation, not a new visual speed improvement.

Profiler-method review: [pinned V8 15.2.124.21 source](../browser-cpu-attribution/v8-profiler-source/README.md) separates plain `Profiler.start` from debugger state transitions. The reviewed sampling/logging path does not force all WebAssembly functions to tier up; JavaScript profiling bookkeeping and observer overhead remain. This supports the narrow interpretation of measured compilation events without claiming an unprofiled causal savings value or repairing the existing screen failure.
