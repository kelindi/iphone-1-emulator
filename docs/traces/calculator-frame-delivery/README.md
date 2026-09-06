# Calculator frame delivery: one observed production run

All 60 measured presses and two warmup presses pass independent raw-pixel verification. The unchanged production phone delivers the button change to the canvas within **135.7 ms for 9 and 134.0 ms for C at p95**. This meets the declared 150 ms **canvas button-feedback** threshold for this Calculator workload in this observed run. It does not establish physical paint time, every app's input latency, or a production speedup.

| Boundary from actual pointer event | 9 median / p95 | C median / p95 |
|---|---:|---:|
| Down → early page frame listener, button | 66.2 / 135.4 ms | 64.9 / 133.8 ms |
| Down → native canvas call completed, button | 66.5 / 135.7 ms | 65.3 / 134.0 ms |
| Down → RAF observation, button | 82.0 / 148.6 ms | 67.7 / 148.9 ms |
| Down → native canvas call completed, correct result | 137.2 / 171.4 ms | 144.9 / 161.1 ms |
| Release → native canvas call completed, correct result | 105.9 / 138.8 ms | 112.9 / 129.3 ms |

Thirty observations per key; p95 is the 29th sorted observation, medians average the middle pair. Maximum button canvas delays are 160.5/149.5 ms. Result commitment is a separate event and does not pass a universal 150 ms down-to-result gate. Every metric, including RAF result and signed release-to-result, is retained in [analysis.json](run-1/analysis.json).

The actual first-button and first-result frames each match their respective RAF-selected frame in all 60 measured presses. RAF first-button observation trails that same native canvas completion by a median **12.9 ms**, p95 **16.0 ms**. This directly demonstrates observation delay within this run. It does not retroactively establish exact delivery times for [089](../calculator-input-feedback/README.md), whose raw per-press crops and actual delivery timestamps were unavailable.

## What was changed and checked

Only the isolated test harness changed. An early listener on the real emulator Worker records frame receipt and copies the active button/display regions. The wrapper calls original `putImageData` once with identical receiver and arguments, timestamps its return, then reads the actual canvas regions and compares them byte-for-byte with the copied source. No frame, input or buffer transfer is replaced or reordered. The production worker, app, CPU, loader, seed, firmware, clock and scheduling remain unchanged. Six actual served response hashes match the frozen files; final generated retirement is positive at 492,318,882 instructions.

[The frozen contract](source/contract.md) preserves the previous 30 pairs in 9/C order, one warmup pair, 30 ms requested pulses, the legacy 100 ms confirmation phase and 650 ms spacing. All original failed and successful 089 attempts remain intact. Nine synthetic checks cover delayed RAF, skipped sequences, source/canvas mismatch, original call behavior, compression-worker isolation and byte transport. The independent raw-crop test rejects three altered frame/pixel cases. A preflight fixture's wrong row indexing was corrected before the sole browser run; its failure is retained.

The [independent Python audit](source/audit.py) decodes and hashes the frozen original PNG references, decompresses retained RGBA crops, recomputes every selected button/display comparison, checks actual/source identity and timestamps, and recomputes percentiles. Its [62-row result](run-1/independent-pixel-audit.json) passes. The separate [metadata analysis](source/analyze.py) verifies all seven timing summaries per key, complete observed-frame ordering, earliest qualifying logged frame and six served identities.

This proves identity for the 40×40 button and full 320×88 display regions, not every full framebuffer. Baseline is the actual prepress canvas tagged with the latest completed frame; its prior source-buffer region was not retained. First-event selection relies on complete per-frame comparison metadata, while lossless raw crops exist at the declared baseline/first/confirmed evidence points. The audit never substitutes a favorable screenshot.

## Observer and cadence limits

Across 520 measured frame deliveries, source-region copying takes median 0.1/p95 0.2 ms; the original native canvas call takes 0.1/0.2 ms. Receipt-to-canvas completion is 0.3/0.4 ms (maximum 0.5 ms). **Post-call observation takes 2.4/4.1 ms, maximum 10.1 ms**, before the app recycles its sole frame buffer. This can affect later delivery and guest interaction with the host; none of that cost is subtracted.

Per-press crop transport costs median 53.6/p95 67.6 ms and disk writing 9.6/14.6 ms, extending the interval before the next press. Actual down-to-down median is 1,030.7 ms, p95 1,066.7 ms. Actual pulses have median 31.8 ms, p95 32.5 ms and maximum 37.3 ms. The ordinary autosave timer remains enabled, but all measured `savedAt` values are zero: these intervals do not satisfy its two-second idle guard. No Save-overlap or rapid-typing result is claimed.

There is no uninstrumented paired control here. The older RAF-only run and this run have different observation/transport work and scheduling history. Their numerical difference is not an optimization gain. These are cloud Chrome measurements, not Mac/Safari or physical display measurements. Early page receipt is listener invocation, not worker send time or the browser's message-enqueue time.

## What this resolves next

The original app's actual canvas assignment adds less than 0.5 ms after the early frame listener in every measured delivery. A production change to defer or bypass that assignment is not supported by this evidence. Most button delay precedes this listener, which still combines host input delivery, guest touch handling, CPU/device work, frame polling and the worker-to-page queue.

The current worker checks presentation after roughly six-millisecond CPU slices and a 60 Hz elapsed threshold, while one transferable framebuffer may be unavailable until recycled. Those are source-grounded possibilities, not attributed causes. To distinguish them, a future separately reviewed diagnostic would need worker-side input receipt, API acceptance, presentation-attempt and actual send timestamps plus buffer availability, using a checked cross-thread clock relation. No such follow-up run or scheduling change is included here. The multi-second warm Settings target remains separate.

## Artifacts and reproduction

[Full uncut recording](../../../tweets/videos/calculator_frame_delivery.mp4) · [actual thumbnail](../../../tweets/thumbnails/calculator_frame_delivery.jpg) · [milestone 090](../../../tweets/090_calculator_frame_delivery.md).

[Archive hashes](archive.json), [frozen source/site/browser pins](source/frozen.json), [controller and scoped cleanup](run-1/controller.json), [actual report](run-1/report.json), [raw crops](run-1/crops/), [original WebM](run-1/page@95c0ee33573924a198ed6fe435081332.webm), and [unchanged worker source](runtime-source/emu-worker.js) are retained. The one run completed in 81.30 seconds under its 180-second cap; all scoped processes closed before other agents resumed heavy work. No retry followed its latency result.

The original command was `python3 .context/input-delivery/run-once.py`; its sources use that workspace layout and require the exact pinned site/browser. To re-audit without a guest, run the archived `audit.py` against this directory's `run-1` from the repository root, with the pinned reference files present under `web/tests/include`. The post-run analyzer records its original workspace path; restore the archived run there or adjust only its artifact root for an offline audit. These are diagnostic sources, not a new maintained production test or automatic benchmark.
