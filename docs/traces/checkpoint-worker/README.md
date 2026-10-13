# Checkpoint compression worker investigation

> Historical recordings and publishing artifacts have been removed from Git history. The summaries below describe the original runs; remaining test reports and screenshots are retained. Historical hash receipts may predate this archival cleanup.

The actual browser CPU profile identified large renderer-main-thread native work during Save. A separate Chrome diagnostic then loaded one immutable genuine MBX checkpoint, without running the emulator, and separated the unchanged encoder from the unchanged IndexedDB transaction.

The snapshot contains **244,887,522 bytes**, SHA-256 `764486ea35ba9832f35c8118397f361026ed21119295321e0a29519509de1244`. Encoding took **6,653.6 ms elapsed / 6,720 ms renderer CPU**, producing **102,885,269 gzip bytes**. Committing that already encoded Blob took **192.1 ms / 20 ms renderer CPU**. Read/decompression was measured separately (**1,229.3 ms / 1,270 ms CPU**) and is not part of Save. Both the original input after encoding and the complete decoded snapshot retained the exact original SHA-256.

These are one-machine stage diagnostics, not navigation percentiles. The renderer process CPU count in this standalone page contains no emulator worker. The original sources, actual stage profiles, hashes and results are in [isolation](isolation/). The diagnostic exports the unchanged private encoder and appends the verbatim original transaction body after encoding; production sources were not instrumented.

The isolated candidate adds an explicitly consuming owned-capture API, keeping the old non-consuming API intact. Each Save owns one short-lived module worker and transfers its fresh capture once. Size is remembered before detachment. Encoding finishes before the existing atomic current/previous transaction opens. Failure before transfer can use the original codec; failure after transfer rejects Save and requires a fresh capture retry, preserving earlier generations. Worker termination, result validation and bounded startup/work waits are part of the candidate.

The isolated candidate passes 16 real-browser codec/ownership/failure contracts, including actual worker success/detachment, raw fallback, bounded failures, fresh-capture retry and an aborted IndexedDB transaction after staging the previous generation. The complete 244,887,522-byte fixture has the same exact SHA-256 after decoding the worker result. Genuine-phone persistence passes 23 checks, ordinary offline loading/recovery 6, and Save/Pause 14. An independent actual old-shell (no compression-worker asset) to new-shell upgrade passes 24 checks, preserving all saved metadata and the compressed-byte SHA, then successfully running the new worker during an offline Save and reloading that record offline. Reports are in [contracts](contracts/). The exact old/new shell sources, upgrade harness, original report, screenshots and reproduction instructions are preserved in [offline-upgrade](offline-upgrade/README.md).

## Quiet actual-phone comparison

Four independent fresh Chrome phone sessions ran in baseline / worker / worker / baseline order. Each first warmed Settings/Home, then explicitly saved and launched Settings/Home while compression was active. Both configurations suppress automatic saves solely within this diagnostic, so one known Save is isolated. The original guest, seed, runtime, frontend observer and recordings otherwise match. No CPU profiler is enabled; Linux scheduler counters and an 8 ms page timer provide independent observations.

| Measurement | Baseline first / reverse | Worker first / reverse |
|---|---:|---:|
| Renderer main-thread CPU over Save/navigation | 6,770 / 6,820 ms | 310 / 360 ms |
| Encoding elapsed | 6,782.8 / 6,791.8 ms | 6,671.9 / 6,945.4 ms |
| Main 8 ms timer gap p95 during encoding | 44.1 / 44.6 ms | 8.1 / 8.1 ms |
| Maximum timer gap during encoding | 98.6 / 93.9 ms | 17.1 / 26.6 ms |
| Long-task total during encoding | 156 / 140 ms (two each) | 0 / 0 ms |
| Settings release to ready during Save | 3,393.8 / 3,172.6 ms | 3,117.3 / 3,417.8 ms |
| Home release to ready during Save | 863.0 / 856.8 ms | 820.6 / 867.0 ms |

The approximately **95% reduction in main-thread CPU** and consistently shorter timer stalls support offloading compression. The total work and Save duration remain similar; actual Settings/Home results are mixed. Two runs per configuration do not establish navigation p95 or a general app-launch speed gain. This improves the browser page's responsiveness during Save. Native snapshot finalization remains a separate latency cost and is unchanged.

All 16 original Settings/Home screenshots were manually reviewed and every RGB pixel below the predefined status-bar exclusion matches the first baseline reference **exactly** (max delta 0, stricter than the unchanged allowable delta 3). All samples are retained in [quiet](quiet/), including full raw scheduler fields and per-action overlap/readiness records. The Linux renderer main thread is identified by TID equal to PID, not an unreliable truncated thread name. A terminated compression worker disappears from the ending thread inventory; total renderer-process CPU still includes its work.

historical recording (removed): baseline 0–25.88 s, worker 25.88–49.84 s. Labels only, no accelerated playback. The script source and source provenance are retained here; the immutable firmware fixture itself is provided by the existing project firmware workflow rather than duplicated in this evidence directory.

The reviewed worker/storage/app change is now integrated. `npm run test:checkpoint-worker` runs the maintained real-browser contracts against a built local server; `npm run test:save-pause` checks actual Save/Pause/reload. The offline manifest includes the new worker, and emulator-observation tests select the emulator worker explicitly instead of assuming there is only one worker. The existing 34 unit checks pass. No C/WASM, guest clock, CPU instruction policy or Apple binary changes are part of this change.


Final production verification: all **12 clock lifecycle checks** pass with generated execution required, covering Pause/Resume, Save, Import and restored execution. The production app, storage, worker and service worker are byte-identical to the tested preview (offline bundle `1eeff3480ab8b2bec802d2e9`). Runtime SHA remains `603679fbc7961ddad95ac8822257074fecde5940bc013246d520ea3b6240c84f`; original seed SHA remains `d9c661556ab17bb8433ed9b0c3ecfd52e2fcafe58221b4cb84a920e4976b3881`. The maintained 16-case real-worker contract also passes on production and now runs in browser CI with its report retained as an artifact.
