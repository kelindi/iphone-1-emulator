# Per-frame input delivery diagnostic — harness only

One separately named run on the unmodified current production files. No worker, CPU, renderer, guest firmware, clock, input protocol or frame scheduling change. This does not repeat the original unfavorable RAF latency test: it measures the previously unobserved frame-message and canvas-completion boundaries while preserving the old RAF observation alongside them. Both earlier input-feedback attempts remain untouched.

Run after explicit coordinated quiet START. Fresh Chrome context, genuine default phone, no private import/reset. Open Calculator, verify the independently hashed full0 display, warm up one9/C pair, then retain30 pairs/60 presses in the same order. Requested pulse30ms with actual pointerdown/up timestamps;650ms spacing follows the unchanged legacy RAF-result100ms confirmation phase, plus separately reported crop-encoding/disk time. Keep normal foreground clock/autosave. The existing2s autosave idle guard may suppress saves during repeated inputs; record rather than assume overlap.

## Boundaries and original-call preservation

The harness wraps the Worker constructor through a Proxy, forwarding exact arguments/newTarget and returning the real Worker. It attaches an early listener only to emu-worker.js before the original app registers its onmessage handler. Record receipt time and sequence at that listener, then copy only the active button/display source ROIs before the original app transfers the buffer back. The copy cost occurs before original painting and is explicitly recorded.

Wrap CanvasRenderingContext2D.putImageData. Call the original method exactly once with the original receiver/arguments; preserve its return and thrown exception. For the actual screen only, timestamp immediately after that native call completes. Then independently read the real canvas button/display ROIs and require byte equality with those copied from the same source frame. Record receipt→source-copy completion→native-call start→native completion→observer analysis completion. No method call, frame or buffer transfer is suppressed, delayed deliberately, reordered or replaced. Observer work itself adds measurable copying/comparison overhead, which is not subtracted or hidden.

The ordinary app updates the canvas sequence immediately after putImageData. RAF observations must bind that sequence to a recorded completed canvas write. Keep actual first button/display completion and RAF first observations independently, including when they refer to different frame sequences. A delayed RAF cannot become an invented delayed canvas timestamp.

Baseline crops are taken directly from the canvas before pointerdown and tagged with the latest completed frame sequence. There is no separately retained source-buffer baseline ROI because the press target was not armed when that older frame arrived. This association limit is explicit. Post-down selected actual crops retain their matching source-buffer crops; ROI identity is not a full-frame byte-identity claim.

## Pixels and bounds

Use the same40×40 button crop and threshold (at least16 pixels differing by more than12RGB), and the full320×88 display with at most2RGB difference/exactalpha against original0/9 PNG references. Preserve raw gzip-compressed RGBA for baseline, actual first button, actual first matching display, RAF first button/display and final confirmed display, including corresponding source ROIs for actual/confirmed evidence. Base64 transport must decode canonically, match byte lengths and SHA-256; independent Python code reconstructs the frozen RGBA PNG references and recomputes these selected pixel comparisons. Per-frame metadata is complete, but raw crops are retained only at those declared evidence points; earliest-event selection additionally relies on per-frame logged comparison results.

Maximum1,024 frame receipts and1,024 RAF receipts per press,32 errors. Startup120s, per-input5s, outer browser process180s. One run, no favorable-latency retry. Stop on identity, pixel, association, timeout or instrumentation errors; retain all partial raw evidence. Diagnostic completion does not require p95≤150ms; both canvas and RAF metrics and the unchanged150ms threshold are reported regardless of outcome. This is canvas timing with observer overhead, not physical-paint/photon timing or an uninstrumented speed comparison.

The outer controller checks frozen source/current-site/reference/browser hashes before and after, requires5GiB free for one fresh browser, records scoped process identities before navigation and close plus a bounded process-tree watcher, and closes only its own Chrome/node/server processes on failure. The exact current six runtime/app/manifest response bodies must match the frozen local hashes. Own server uses unchanged tools/web-server.mjs on8220; no shared server or product file changes.

## Preflight and frozen entry

Nine synthetic Node checks pass. They exercise delayed RAF, skipped frame sequences, source/canvas disagreement, source-less canvas calls, original method receiver/return/exception, unrelated compression worker, all256 byte values/BigInt serialization, canonical packet corruption rejection, and a separately executed raw-crop auditor with three altered-frame/pixel negatives. Both frozen original PNG pixel hashes and a wrong-reference negative pass. The initial synthetic all-byte fixture had incorrect row indexing; its failure is retained and the fixture alone was corrected. No browser or guest was used for these checks.

After root review and quiet START only:

```
python3 .context/input-delivery/run-once.py
```

Then, without another guest run:

```
python3 .context/input-delivery/audit.py .context/input-delivery/run-1
```
