# Compiled seed reuse through the real offline cache

> Historical recordings and publishing artifacts have been removed from Git history. The summaries below describe the original runs; remaining test reports and screenshots are retained. Historical hash receipts may predate this archival cleanup.

The separately named fourth functional pair passes 71 checks and ten independently re-read and manually reviewed full guest screens. Twenty-five scoped Chrome process identities closed. Production remains unchanged pending the combined runtime/loader upgrade and fallback gates. This is a cache-mechanism and functionality test, not a speed comparison.

The first process installs the unchanged generated service worker and hash-caches the current remote firmware through the actual `assets.js` loader before the phone or CPU seed starts. This explicit setup fixture is not an ordinary first-visit UI flow. The origin listener is then physically stopped, every connection closed, and a fresh TCP connection must fail with `ECONNREFUSED`. Playwright offline emulation also remains enabled. Page, actual emulator worker and service-worker nonce requests all fail, and the actual origin records no request after the barrier. The remote firmware server itself is not physically stopped; its assets were verified and cached before this phase. The final upgrade gate separately records all remote request URLs.

The genuine phone opens Settings and returns Home twice, then an ordinary 30-second autosave writes its checkpoint. It does not click hidden Save/Import controls or change the latest minimal phone UI. After all Chrome processes close, a fresh process starts the already installed service worker offline from `about:blank`, verifies its actual JavaScript source before starting the guest, and reopens the real saved phone. Debugger is enabled only briefly on the service-worker JavaScript isolate for source identity, then disabled/detached. It is never enabled on the emulator worker.

## Exact results

| Gate | Training process | Fresh return process |
|---|---:|---:|
| Functional checks | 46 | 25 |
| Strict paired/reference guest frames | 7 | 3 |
| Scoped process identities closed | 12 | 13 |
| Session duration, diagnostic only | 55.57 s | 15.61 s |
| Post-barrier actual origin requests | 0 | 0 |
| Native seed cache accepted | No; 15 serialization callbacks | Yes; 20,677,964 bytes |

The return trace binds one exact `http://127.0.0.1:8202/jit/seed.wasm` hit to the same PID/TID compilation-consumption scope, a completed digest, deserialization and nested successful compile completion. No invalid-cache event occurs in that accepted chain. HTTP cache presence alone would not satisfy this gate. The actual dedicated-worker seed and interpreter responses both report `fromServiceWorker: true`; their response bodies hash exactly to the original d9c661… seed and b3237e… interpreter.

All saved metadata and the compressed payload match across process closure: raw size 249,413,602 bytes, gzip size 103,649,276 bytes, compressed SHA-256 `199353389f60353003104a752ecdc9f559e9204b89d0f97d8ab9f112fcf1c080`. Generated execution remains positive in both processes. The independent image audit preserves every pixel below raw row 30, permits at most two RGB levels and requires exact alpha; no scrollbar is excluded.

The first three attempts remain preserved separately: [storage/install failure](../sw-install-storage-failure/README.md), [stale offline indicator](../sw-offline-indicator-failure/README.md), and [browser-managed update fetch bypass](../sw-update-transport-failure/README.md). The last failure motivated the physical listener barrier. Training request failures include `ERR_INTERNET_DISCONNECTED`; on a fresh return the barrier also produces `ERR_CONNECTION_REFUSED`, retained explicitly rather than described as a successful offline-emulation signal.

## Evidence and limits

`run/series.json` contains both reports, source/module pins, storage/free-byte observations and complete scoped process identities. Each process includes its cache receipt, compressed raw trace, screenshots and compressed original RGBA pairs. `independent-screen-audit.json` re-reads the raw files separately from the running harness; `manual-review.json` records actual image inspection. `stopped-server-smoke/report.json` proves the transport setup with two tiny browser processes and no guest. `physical-server.mjs` implements only the isolated listener barrier.

`frozen-site/` retains the exact small frontend/runtime files. The 12,341,313-byte seed is not duplicated here: it must be supplied with the exact SHA in `site-pins.json` (the maintained deterministic seed generator produces it). Runtime code, seed admissions, ABI, class lifecycle and Apple firmware are unchanged by this candidate. `promotion/` contains the prefix-only patch and 49 passing focused loader/lifecycle checks, including actual HTTP body/URL, corruption and MIME fallback coverage.

Both full recordings retain original duration with no cuts or speed changes: historical recording (removed) and historical recording (removed). `recordings.json` gives the original WebM hashes, MP4 hashes, exact duration equality and encoder command. Milestone 084 presents the visible result.

The separate returning-Settings speed result remains [milestone 080](../return-visit-v3/README.md), including its pooled Home p95 increase of 49.8 ms. This functional pair neither repeats those timings nor erases that tradeoff. Ordinary first-load/update behavior and the final combined runtime upgrade remain separate release gates; full speed is not achieved.
