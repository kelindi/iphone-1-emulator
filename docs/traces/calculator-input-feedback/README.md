# Calculator input feedback diagnostic

On the unmodified local RTC/streaming-loader build with the current recovery UI,
all 60 measured Calculator presses produce the independently reviewed result:
30 presses of **9**, each followed by **C**. One warmup pair is excluded by the
predeclared contract. All six actual core/seed/worker/loader/app/manifest response
hashes match the local bundle, and all 16 frozen source/reference hashes remain
exact after the run. The original guest supplies every pixel and handles every
pointer event. No production runtime or input behavior changed.

| Observation | 9 median / p95 | C median / p95 |
| --- | --- | --- |
| Down to sampled button highlight | 98.9 / 165.4 ms | 98.7 / 165.6 ms |
| Down to sampled correct display | 148.9 / 165.8 ms | 149.1 / 165.8 ms |
| Release to sampled correct display | 117.1 / 134.5 ms | 117.2 / 134.4 ms |

**The 150 ms input gate remains unresolved.** The literal sampler threshold fails,
but its sampling cadence is insufficient to attribute this small excess to guest
input handling. All 60 first button detections are on the first observed new
frame after pointerdown: there is no observed unchanged post-down frame bounding
the highlight onset. Between recorded samples the median interval is 42.8 ms and
p95 is 83.3 ms; 162 of 210 intervals skip one or more canvas sequence numbers.
These are RAF-observed canvas results, not exact frame-delivery or physical display
timestamps. Do not subtract a guessed frame period, turn these upper observations
into a hardware-latency claim, or tune worker scheduling from them alone.

[Complete report](run-2/report.json), [executed test](executed-test.mjs),
[predeclared contract](contract.md), [source pins](pins.json),
[post-run verification](run-2/post-run-pins.json) and
[closed process identities](run-2/processes-after.json) are retained. The test is
maintained as `npm run test:input-feedback`. Its nonzero exit records the literal
observational threshold; all 60 result checks and observed highlights succeed,
with no page errors. All 13 scoped processes and the local server close.

Normal clock and autosave rules stay active, but `savedAt` remains zero throughout
this continuously active keypad sequence: this is not an autosave-overlap test.
The browser is Chrome 152 on the cloud VM, with full recording active; there is
no Mac or Safari measurement and no app-navigation comparison.

The [independent audit](independent-audit.json) reconstructs counts, timestamps,
percentiles and served hashes and rejects eight altered reports. Per-press raw
crops were not retained; this audit checks source and recorded comparison results,
not a second comparison of all 60 pixel arrays. The [scheduling review](frame-scheduling-review.md)
finds that the app paints immediately on frame delivery. A separately prepared
delivery observer is needed before attributing these RAF delays to the emulator.

## Preserved reporting failure

The first attempt stopped while writing its first warmup record because metrics
contain a JavaScript BigInt. Its [source](run-1/executed-test.mjs),
[error](run-1.log), [failure receipt](run-1/failure.json), screenshot and original
WebM remain retained. No latency dataset survived that process. The successor
adds only a tested BigInt-to-decimal-string JSON reporter; input counts, clock,
frame thresholds, timing limits and genuine guest are unchanged. This is separate
from retrying an unfavorable latency result.

## Recording

[Full uncut 73.24-second MP4](../../../tweets/videos/calculator_input_feedback.mp4)
and [thumbnail](../../../tweets/thumbnails/calculator_input_feedback.jpg).
Actual repeated presses run approximately 00:15–01:12. The thumbnail at 00:15.8
shows the first measured 9. Original WebM and converted MP4 durations match
[exactly](video-durations.json); no playback-speed change or reconstructed UI.
