# Default MBX source-row correction

> Historical recordings and publishing artifacts have been removed from Git history. The summaries below describe the original runs; remaining test reports and screenshots are retained. Historical hash receipts may predate this archival cleanup.

Only `dist/emu.wasm` and its generated service worker changed in this local
promotion. Previous core: `7a72add6…9088`. New core:
`3b43ccf4a8292a4da776491aa92185b0ea88e2fc38a3002a6e1fe1d5a6aa4463`.
Offline generation: `c145cb60d9fe4ab0e1460ed8`.

`promotion.json` records all before/after hashes. `promote.py` independently
regenerates the expected service worker, requires both browser gates and manual
frame review, then atomically replaces the changed files. Historical hardlinked
inputs are preserved. No public deployment occurred.

## Results

- `native-tests.log`: all 30 suites pass. MBX has 2,847 passing checks including
  32 new checks. Actual WASM qualification and Safari recovery are retained in
  [the graphics evidence](../safari-stride472/README.md).
- `unit-tests.log`: 58 JavaScript tests pass; `wasm-smoke.log` passes. The original
  compiled seed, public API and JIT ABI are preserved.
- `upgrade/`: both saved-phone attempts and their frozen sources. The first
  remains an overall failure despite 30 functional passes. The successor passes
  all 30 checks and verifies 180 complete bodies. Two exact intentional corrupt
  bodies and two unavailable failed-startup bodies are classified separately;
  unavailable rows are never byte proof. The inherited local blob-worker probe
  fails URL parsing, not transport. Actual socket refusal, page-local failure
  and remote-worker denial remain recorded.
- `final-release/run-1/report.json`: 21 checks pass in 56.48 seconds of functional
  workflow. Genuine Home/Settings/physical Home, natural autosave and online/offline
  restoration preserve the exact saved metadata and compressed Blob hash. The
  origin listener is closed, uncached local/remote requests fail, and generated
  execution continues after restores. `controller.json` confirms post-run pins,
  all observed processes closed and no forced cleanup. Root inspected actual frames.

Final saved state: 249,057,250 raw bytes; 103,848,082 compressed bytes; compressed
SHA256 `a2da169211f6caec457ae1cfdf30ab23cc42690e6f4caaa2b7e5fbd1d2c29e51`.
The actual persistent Chrome profile remains at
`.context/mbx-stride-integration/final-release/run-1/profile`.

`release-archive.json` pins the retained root artifacts, including the original
WebM. Milestone 096 links the
uncut MP4 and thumbnail. The MP4 fully decodes without errors.

## Limits

This is a rendering and package compatibility change, not a speed comparison.
HTTP daemon exit, Photos navigation, audio and networking remain separate.
The existing browser VFP defect remains open. No Apple binary, normal input,
guest timing, save policy, firmware, minimal UI or seed bytes changed.
