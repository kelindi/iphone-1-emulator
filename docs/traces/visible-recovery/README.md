# Visible recovery in the phone

The minimalist shell hid its retry and previous-save actions inside the hidden companion panel. A download failure could show an error but offered no clickable recovery. A later runtime failure also left the loading overlay hidden, including its error text.

The existing actions now live inside the phone's loading overlay. Failure first clears readiness and reveals that overlay, so the original error message is displayed even after startup. Error-only buttons accept pointer and keyboard input with 44-pixel minimum height. Normal loading and the running phone retain their existing appearance. Save/restore algorithms, worker, interpreter, seed and firmware are unchanged. The existing previous-save action is relocated, not a new data-recovery implementation.

## Verification

One real Chrome session passes 12 checks in 17.27 seconds. A controlled HTTP503 response for the actual manifest causes the original worker's real failure path. The visible Try again button is clicked normally; the genuine OS then starts and renders Home. A test-only Worker message injects a named runtime fatal after readiness; it is not evidence of an actual core crash. The error becomes visible, input is disabled, and focusing Try again then pressing Enter restarts the genuine guest. No page errors occur. The full 16.80-second recording contains both recoveries; no guest screen is mocked.

[Report](report.json), [executed test](check.mjs), [baseline hashes](baseline.json), [integrated hashes](integration.json), and [reviewed source](source/app.js) are retained. The maintained command is `npm run test:recovery-ui` against a running local server, with optional BASE_URL and ARTIFACT_DIR. The existing offline corruption test now uses a real visible Retry click. Its full offline sequence was not repeated for this presentation-only fix; the preceding core/loader release evidence remains scoped to its exact prior UI bundle.

[Full recording](../../../tweets/videos/visible_recovery.mp4) and [thumbnail](../../../tweets/thumbnails/visible_recovery.jpg). Approximately 00:00–00:02 shows the download error; 00:09–00:10 shows the recovered real Home screen; 00:10–00:12 shows the injected runtime error, followed by keyboard retry and genuine Home again. Capture times in report.json are runner-relative and include navigation overhead; these are functional observations, not speed benchmarks.

The three frontend files were baseline-hash-checked and atomically replaced after this test. The generated service-worker generation is `6fc3927f86a86269a30dc811`, covering the new shell and unchanged accepted RTC/streaming runtime. This local change is not a public deployment, a new offline-update acceptance run, or completion of the full app/speed goals.
