# Faster snapshot page classification

> Historical recordings and publishing artifacts have been removed from Git history. The summaries below describe the original runs; remaining test reports and screenshots are retained. Historical hash receipts may predate this archival cleanup.

Milestone 071 demonstrates the reviewed capture change, now integrated with all final production checks passing. The timing recordings remain the original isolated comparison. No Apple binary, snapshot version, page encoding, loader path, guest state, or execution seed is changed by this candidate.

## Change and correctness

`snapshot-original.c` and `snapshot.c` preserve the exact compared sources; `provenance.json` pins both SHA-256 hashes. Only the serializer's page-equality classifier changes: repeated-byte pages are recognized with bounded eight-byte memcpy loads, followed by the byte tail. `classifier.inc` is the isolated helper and `independent-review.md` records the separate source review. The sole caller passes nonempty pages of at most4096 bytes. memcpy avoids alignment/aliasing assumptions; the repeated-byte word is endian-independent.

- `contracts.log` and `test.c`: **1,136,648** sanitizer-backed comparisons pass. All byte values and first-difference positions, lengths1..4096, offsets0..7, random inputs and protected following pages are covered.
- `snapshot-tests.log`: **485** existing snapshot checks pass.
- `fixture-verification.json/.log` and `verify.cjs`: blocking snapshot bytes and immutable incremental-capture bytes match the baseline exactly for MBX and software fixtures, across two evolved iterations each. Each incremental finalize interleaves **11,650,000** genuinely executed instructions; the snapshot still equals its pre-execution capture. This is a correctness run under concurrent work, not a performance measurement.

## Quiet direct capture measurement

`bench.cjs`, `quiet-{1-baseline,2-candidate,3-candidate,4-baseline}.json`, `quiet-processes.json` and `quiet-analysis.json` retain the full measurement. This is the C capture entry point **compiled to WebAssembly and run in Node24.14.1**, using one hash-pinned paused phone. It is not a native executable timing or browser navigation benchmark. Initialization, restore, finalization and compression are excluded. Each arm retains three warmups and20 measured observations.

| Ordered arm | Warm mean capture ms | Warm p95 ms |
|---|---:|---:|
| Baseline1 |279.776|282.113|
| Candidate2 |97.748|99.238|
| Candidate3 |96.793|97.368|
| Baseline4 |282.864|286.163|

The mean reduction across the two arms is65.4%. This scope applies only to initial capture.

## Quiet actual Chrome Save/Pause/reload

`browser.mjs` adds a read-only diagnostic message to the actual worker so it can check the guest tick/instruction counters. The application and genuine phone perform real Save/Pause/Resume/reload. Four fresh disposable sessions ran in baseline/candidate/candidate/baseline order with other heavy jobs stopped. Each passes14 checks, no reported page errors, and positive JIT retirement before the workflow.

| Arm | Initial capture ms | First Pause acknowledgement ms | Finalization ms |
|---|---:|---:|---:|
| Baseline1 |272.6|328.3|529.0|
| Candidate2 |135.6|180.9|546.9|
| Candidate3 |142.8|206.2|563.5|
| Baseline4 |224.9|250.2|508.8|

Full observations are in `browser-analysis.json` and each `browser-*/report.json`; screenshots and logs are retained beside them. Pause checks compare actual guest ticks/cycles: they stay fixed while paused, advance after Resume, and remain fixed when Save completes in the paused state. This establishes a repeated initial-capture reduction and shorter first acknowledgement. It does **not** establish faster finalization, compression, total Save, or app navigation. First Pause remains above150 ms in both candidate runs. These are two ordered pairs, not broad hardware/browser coverage.

The recorded video is the complete first accepted pair at original speed, with labels only: historical recording (removed), historical thumbnail (removed). Baseline0–17.72s, candidate17.72–35.40s. Thumbnail29.72s shows the genuine paused phone while Save is active. `video.py` and `video.json` retain the exact input paths, timing and encoding recipe. Raw source recordings remain in `.context/snapshot-page-scan/browser-{1-baseline,2-candidate}/`; the durable MP4 contains both in full, with no speedup or omitted interval.

## Preserved diagnostic failures

`attempt-browser-bigint/` and `attempt-browser-bigint-repeated/` retain the earlier diagnostic script/log/screenshot artifacts. The initial workflow completed but report serialization failed when the added metrics object contained a BigInt. The repeated attempt also failed before accepted JSON reporting. Those attempts are excluded from all performance/acceptance tables; their raw recordings remain at the matching `.context/snapshot-page-scan/` paths. The accepted harness uses an explicit BigInt-to-string JSON replacer. This was a diagnostic reporting defect, not an observed application failure.

## Reproduction and artifact limits

The source benchmark/test scripts preserve their original workspace paths and expect separately built baseline/candidate runtime artifacts and the original firmware fixtures; they do not embed proprietary firmware. Source and runtime hashes are in provenance and individual reports. `bench.cjs` labels select its sibling baseline/candidate runtime directories; `verify.cjs` similarly expects its prepared context. Run those from the prepared `.context/snapshot-page-scan/` experiment, or reconstruct the two runtimes before copying the scripts elsewhere. The Chrome harness accepts `BASE_URL` and `ARTIFACT_DIR` and requires installed Playwright and Chrome.

The change is integrated. Final production validation is recorded separately below; the isolated timing recordings retain their original provenance.


## Independent picture review and final production gates

`review.py`, `screen-review.json` and `screen-contact.png` retain the independent PNG comparison and manual review. All eight baseline/candidate Home stages contain the complete genuine icons/dock. Across1,012,800 compared pixels below the predefined status-bar exclusion, maximum RGB difference is exactly0 (the baseline reference itself is included). No substitution image or relaxed threshold is used to establish equivalence.

The integrated runtime SHA-256 is `b3237ef3843412aea164d3db111299650ea8433bf16311417116799959ab6606`, exactly matching the measured candidate. Production offline revision is `2637a8ea60ba6bd2416605fe`; the original seed remains `d9c661556ab17bb8433ed9b0c3ecfd52e2fcafe58221b4cb84a920e4976b3881`. `production-validation.json` and the retained build/test logs identify the release gate.

- All26 native suites pass. The maintained snapshot suite now has602 checks, adding partial-page encoding/restore cases to the prior485 isolated-stage checks.
- All34 JavaScript checks and the WASM smoke check pass.
- `production-clock/report.json`: all12 lifecycle checks pass, including exact paused/hidden/frozen/import counters and genuine generated execution before and after restore (generation1→2). Foreground wall ratio0.99837 is a short functional observation, not another speed benchmark.
- `production-persistence/report.json`: all23 checks pass, covering real Settings Save/reload/Import/Export, physical ringer retention, corruption rejection and profile ownership. The optional native-tab-blur check is not enabled in this run; real-tab lifecycle behavior is covered separately by the clock suite.
- `offline-runtime/report.json`: all24 checks pass. The actual old runtime saves the genuine Settings phone; an update waits while a phone tab remains open, preserves identical saved bytes and metadata, and activates only after all phone tabs close. The new runtime restores that save, installs the original seed, starts disconnected, performs another real worker-encoded Save, and reloads it again disconnected. No page errors occur.

`offline-runtime.mjs` pins original underlying runtime hashes603679…→b3237… before constructing its isolated generations. It appends harmless WebAssembly custom sections and shell/manifest identifiers solely to distinguish versions while observing cache selection. Accordingly the report's marked A/B hashes and cache generations differ from production hashes; executable runtime content is the pinned original/new pair. No diagnostic markers are integrated. These functional tests establish compatibility and release behavior, not lower total Save time.
