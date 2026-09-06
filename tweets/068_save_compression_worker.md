# Save without monopolizing the browser page

## Tweet idea

Saving the browser iPhone was spending nearly seven CPU seconds on the page itself. A dedicated compression worker moves that work off the UI thread: measured Save-time timer stalls fell from about 94–99 ms to 17–27 ms. Same genuine phone, same backup bytes.

## What you're seeing

[Original-speed recording](videos/save_compression_worker.mp4) · [thumbnail](thumbnails/save_compression_worker.jpg).

- **0:00–0:25.88:** original Save, after warming genuine Settings/Home, then the same navigation while saving.
- **0:25.88–0:49.84:** worker candidate, using the same sequence and original playback speed. At **0:47**, the actual Settings rows are visible while the surrounding UI reports “Saving your iPhone…”.

This is the first pair from a quiet four-session baseline/worker/worker/baseline comparison. All 16 retained Settings/Home screenshots match exactly below the status bar. The recording illustrates the unchanged real phone and Save workflow; the timing and CPU claims come from the retained browser measurements, not visual impression alone.

## ELI5 technical explanation

A save is a photograph of the whole emulated machine's memory and devices—about 245 MB here. Before storing it, the browser compresses it. “Asynchronous” compression still spent almost seven CPU seconds on the page's thread, competing with input, painting and controls. A separate measurement showed the database commit was only about 0.19 seconds; compression was the large cost.

The page now hands each fresh snapshot to a short-lived worker. The worker compresses it and returns a Blob; the existing database transaction still moves current to previous and installs the new save atomically. Ownership is explicit: the fresh capture can be transferred without another 245 MB copy. An older API still leaves its caller's bytes readable. If the worker fails after receiving the bytes, the earlier saves remain intact and a later fresh capture can retry.

Actual measurements show main-thread CPU falling from 6.77/6.82 seconds to 0.31/0.36 seconds. The 8 ms page timer's p95 gap falls from 44.1/44.6 ms to 8.1/8.1 ms. Encoding itself remains around 6.7–6.9 seconds, and Settings/Home launch times are mixed; this is a Save responsiveness improvement, not a claim that every app is now faster or running at full hardware speed.

## Why it matters

The original phone can keep running while a backup is encoded without making the surrounding browser page do most of that work. Full snapshot hashes, two-generation failure checks, genuine Save/Pause/reload and an actual old-shell-to-new-shell offline upgrade all pass. Native snapshot finalization is unchanged and remains a separate latency issue.

[Evidence, exact results and limitations](../docs/traces/checkpoint-worker/README.md). The reviewed worker, explicit capture-ownership API and offline asset are now integrated; CPU/WASM and Apple software remain unchanged.


The final production build also passes all 12 clock lifecycle checks with generated execution required. Its app/storage/worker/offline bundle match the tested preview exactly; the guest CPU runtime and original instruction seed retain their previous hashes.
