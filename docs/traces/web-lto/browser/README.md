# Full-web LTO browser outcome — parked

LTO reduces deterministic fixed-work CPU by5.81%, but warm Settings median improves only49.85ms and Home pooled p95 worsens49.0ms. Keep the candidate isolated. This does not meet two-second navigation or establish broad nonregression. [Milestone092 and full candidate recording](../../../../tweets/092_web_lto_navigation_diagnostic.md).

The original A/B/B/A controller failed after its first original arm completed all22 stages: two post-run loops called stored string `a.url` as `a.url()`. The original runner, first samples, failed series, complete trace, raw pixels, actual response hashes, failed profile and truncated WebM remain untouched. Independent artifact recovery passes all22 stages and required assets. Graceful browser-close/before-close CDP receipts are absent; the outer controller closed all14 unique scoped identities. The recording ends prematurely at112seconds and is not called complete. [Failure assessment](reporter-fix/assessment.md), [receipt](reporter-fix/failure-receipt.json), [original failed series](measured/series.json).

Root authorized only the three unstarted B/B/A arms in a separately frozen continuation after storage reclamation and a new quiet start. No guest or favorable sample was repeated. The input/timing/trace collection prefix is byte-identical; only the asset validator and finalizer change.39 actual-row asset tests and7 source/actual-finalizer scenarios pass. All three new arms finish normally, in390.17seconds under the800second overall bound, with240seconds perarm unchanged. All42 unique scoped identities and three listener ports are closed. New recordings finalize normally. This is an interrupted comparison with a recovered, failed first control, not an uninterrupted successful ABBA. [Continuation contract](successor-contract.md), [frozen manifest](successor-frozen.json), [controller results](continuation/series.json).

## Navigation

All values below are milliseconds. Per-arm warm p95 uses nearest rank over10 samples (therefore its maximum); pooled p95 uses rank19 of20. First-use samples remain separate. Full arrays are in [overall-metrics.json](overall-metrics.json), recomputed by [analyze-overall.py](analyze-overall.py).

| Arm | First Settings | First Home | Warm Settings median / p95 | Warm Home median / p95 |
|---|---:|---:|---:|---:|
| Original1, recovered failed report | 5453.1 | 915.9 | 3266.45 / 4066.1 | 892.25 / 1016.9 |
| LTO2, continuation | 5138.9 | 866.3 | 3216.40 / 3417.0 | 866.85 / 966.7 |
| LTO3, continuation | 5459.4 | 916.4 | 3242.00 / 3608.2 | 867.40 / 967.9 |
| Original4, continuation | 5706.6 | 866.5 | 3316.40 / 3816.8 | 892.00 / 917.5 |

| Pooled20 warm samples | Original | LTO | Interpretation |
|---|---:|---:|---|
| Settings median | 3266.75 | 3216.90 | 49.85ms /1.53% less |
| Settings p95 | 3816.8 | 3417.0 | 399.8ms /10.47% less |
| Home median | 892.15 | 867.0 | 25.15ms less |
| Home p95 | 917.7 | 966.7 | **49.0ms worse** |

Settings median gains are1.53% forward and2.24% reverse; p95 gains15.96%/5.47%. Home p95 improves50.2ms forward but worsens50.4ms reverse. Both first Settings pairs improve (314.2/247.2ms), while first Home is mixed (49.6ms better forward,49.9ms worse reverse). Two first observations per configuration do not establish a first-use distribution.

## Correctness, identities and compilation

[Independent raw audit](raw-audit.json) rechecks all88 before/after rawpairs and frozen-reference comparisons, all actual local response-body hashes, and positive generated progress. The strict crop/tolerance remains unchanged: below raw row30, RGB delta<=2 and exact alpha. No scrollbar crop, tolerance change or failed-frame removal. [Eight human-reviewed first screens](human-review.json) show genuine complete Settings and Home.

Both sites use the same frozen085 recovery UI, original512 seed d9c661, normal30second autosave, clock/input policy and streaming loader. Only emu.js/WASM differ: released7a72 versus isolated LTO7a5288. All imports install512 regions and match original paused cycles/ticks; ordinary initial startup511 is a separate known path. Both use actual HTTP no-cache/ETag, with service workers blocked/bypassed; this is not cached-return or offline upgrade acceptance. No Profiler/Debugger/interception, forced compilation, timing-observer redesign or guest binary patch. [Site pins](site-pins.json), [original method](contract.md).

| Compilation CPU, all scoped foreground/background threads | Lower / upper ms |
|---|---:|
| Original1 | 18004.879 / 18004.879 |
| LTO2 | 17447.253 / 17447.253 |
| LTO3 | 17846.010 / 17846.010 |
| Original4 | 17019.815 / 17019.815 |

Both predeclared bounds pass: candidate upper <= original lower*1.25+100ms. No missing thread fields or trace loss. Exact per-thread interval unions cover navigation-start through fixed-work-end; no inferred module binding. These totals are CPU across threads, not elapsed compilation wall time. [Recovered first compilation](first-arm-compilation.json), remaining rows in continuation report.

The separate baseline WASM VFP defect remains595pass/12fail identically in both builds. Relative CPU/snapshot equality does not turn it into full VFP conformance. [Exact defect](../../wasm-vfp-conformance/README.md).

## Retention and scope

The complete117.84second candidate2 recording is converted to MP4 without cuts or speed changes. Actual45second Settings thumbnail is reviewed. [Recording hashes](recording.json). All four raw WebMs remain, including the explicitly truncated original1.

Only each successfully closed new profile is extracted/cleaned according to the predeclared policy. Complete IndexedDB/blob/LocalStorage/SessionStorage/Preferences/LocalState remain locally in its `saved-profile` with exact hash inventories in the controller report. The original failed profile stays entire. This durable package does not duplicate large IDB files or the import fixture; original paths and hashes remain. Sites, reports, source, traces, rawpixels, network/process receipts and recordings are archived here. Scripts retain original `.context/web-lto/browser` paths and require that layout plus the pinned Chrome/SDK/fixture for reproduction.

No production core, loader, seed, UI, R2 manifest or service worker was changed. Candidate parked; no automatic repeat or promotion follows from the pooled Settings tail improvement.
