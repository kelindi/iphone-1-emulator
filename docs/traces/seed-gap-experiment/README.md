# Filling measured gaps in the existing seed

> Historical recordings and publishing artifacts have been removed from Git history. The summaries below describe the original runs; remaining test reports and screenshots are retained. Historical hash receipts may predate this archival cleanup.

**Outcome:** modest improvement in this pilot; retain the candidate in isolation. Do not replace the production seed. Warm Settings median improves5.47%, below the10% threshold declared before browser testing. Its measured p95 improves449ms. This is not full-speed acceptance.

## One bounded change

Keep the production CPU, compiler,512-region limit, every original opcode position and every safety check. Rank absent positions from four warm Settings/Home census windows using reasons4/22 and valid observed opcodes. Reject conflicting byte variants across **all** valid reason rows. Add only the top256 supported positions, affecting23 existing regions. The selection covers44.47% of eligible observed gap events, not44.47% of CPU time. The seed grows from12,341,313 to12,381,571 bytes; installed positions rise76,964→77,220.

[Build and selection](build-report.json) contains every added address/word, separate Settings/Home and exit/rejection weights, source census hashes and source/module pins. No parameter sweep or new opcode implementation was used.

## Measured result

Fresh control/candidate/candidate/control sessions,11Settings/Home pairs each:88actions total,20warm samples per action/version. All88actions reach their expected endpoints. Normal clock/autosave are enabled. Core, seed and actual served worker identities are checked.

| Metric | Original | Candidate |
|---|---:|---:|
| Warm Settings median | 2292.6ms | 2167.3ms |
| Warm Settings p95 | 2667.4ms | 2218.3ms |
| Warm Home median | 867.3ms | 818.05ms |
| Warm Home p95 | 867.8ms | 868.0ms |
| First Settings, two sessions | 3360.7 / 3361.7ms | 3311.7 / 3361.7ms |

The Home p95 difference is0.2ms, effectively unchanged at this observer's50ms sampling interval. First Settings is essentially unchanged. Settings session medians vary: controls2367.55/2167.55ms and candidates2167.35/2142.85ms. The pooled5.47% improvement is a pilot result, not a confidence interval or a prediction for other machines. The long-tail result is encouraging but only20warm observations per version support each p95.

A separate fixed400M replay ABBA yields0.71% less process CPU time and0.77% less elapsed time after warmup. It is not app timing; routine source preparation and a bounded delegated test overlapped parts of the fixed-work experiment. Compiled retired instructions rise301,802,126→306,850,840, while compiled calls fall42,050,405→41,010,679. More compiled instructions alone do not establish a proportional gain.

## Correctness and visual review

All original inputs and admission bits remain present; exactly256 bits are added.272 focused comparisons pass:256 selected instructions in full changed region bodies at normalized addresses, plus16ARM/Thumb stale-guard cases with MMU off/on.175 selected instructions execute generated code in these fixture states;81 safely fall back, so this suite alone does not cover all warm-memory paths. The initial harness setup failure occurred before guest execution and is retained in the guard report.

Complete real-phone snapshot hashes match at25/50/75/100M instructions and at400M for both warmups and every timed run. The replay uses the existing fixed-dismiss snapshot and fixed Settings/Home input, not the browser host-clock schedule.

All44Home frames match exactly below the status bar. Three Settings frames also match;41 differ solely in the right scrollbar area,x313–317/y66–339, with exact alpha. The differences occur in controls and candidates and match the existing scrollbar fade behavior. The strict pixel gate remains failed in raw evidence; it was not silently cropped or relaxed. Representative candidate Settings/Home screens were visually reviewed and show the expected original UI.

## Retained interrupted attempt

The initial controller completed one control session, then its first candidate Settings attempt timed out with a black display. Investigation found an unrelated **openLocked** worker edit at02:31:29UTC, between control startup02:30:09 and candidate startup02:31:31. The original staging used hardlinks, allowing that workspace edit to change both staged files. The failed recording at10seconds visibly shows the lock screen: the harness tapped a Settings coordinate without first having a Home screen. A separate input diagnostic also showed the lock screen and confirmed touch delivery. There is no demonstrated compiler fault from this attempt.

Retain the initial controller, failed report/video, diagnostic, [10second frame](failed-startup-10s.png), and [source audit](opcode-audit.md). The first failed harness did not retain its served worker body, so attribution combines source timing/inode evidence with the actual recording. No samples from this attempt enter the clean comparison.

For the clean comparison, shell/runtime/seed files were copied independently; only immutable firmware links remained. Restore the original pre-edit worker and HTML **only in the test sites**, verify both site manifests, and check the worker body actually received by each browser. A complete new ABBA starts from scratch. Concurrent production lock-screen work is preserved. These results measure the frozen original Home-start shell, not acceptance of the new lock-screen startup flow.

## Decision and remaining work

[Protocol](protocol.md) required at least10% Settings median improvement plus no tail/Home regression for promotion consideration, followed by lifecycle validation. The5.47% result does not clear that screen. Keep this as a promising small candidate; this experiment changes no production seed, CPU or compiler. It does not establish save/upgrade/offline compatibility, all-app speed or a physics ceiling.

The main lesson is that measured seed gaps can help, but this256-position change delivers a modest gain. Larger position counts are not automatically better: additions can change basic-block boundaries and add early raw-code guards. Any follow-up should retain the measured baseline and correctness gates.

## Evidence and reproduction

[Clean browser summary](browser-summary.json), [frame review](frame-review.json), [fixed replay](screen-result.json), [focused guard report](guard-report.json), [frozen site hashes](frozen-site-hashes.json), and [decision](decision.json). Raw screenshots,RGBA.gz files, complete WebM recordings and executed scripts are archived. Script paths are original workspace-relative paths; restore them under `.context/seed-gap` for reproduction. Build requires the archived103warm census rows at their original paths, pinned compiler/WABT, production runtime and firmware. Firmware and full249MB replay snapshots are not duplicated; checkpoint hashes are retained.

historical recording (removed) · historical thumbnail (removed). This uncut recording is the first clean candidate session: startup, first pair, then ten warm Settings/Home pairs. The interrupted lock-screen attempt remains separately archived.

`disk-preservation.json` records lossless compression of closed historical snapshots to maintain browser disk reserve. Each gzip was decompressed and hash-verified before removing the old path. Restore those files by decompression if an older script needs the raw path. The experiment's firmware and replay fixture were untouched.
