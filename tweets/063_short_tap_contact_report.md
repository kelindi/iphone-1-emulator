# A short tap reaches the real iPhone app

## Tweet idea

A fast first tap could disappear even though both press and release reached the emulated iPhone. The real OS received no held-contact sample between them. Letting the touchscreen send its next normal report makes the same short tap launch Calculator.

## What you're seeing

[Original-speed recording](videos/short_tap_contact_report.mp4) · [thumbnail](thumbnails/short_tap_contact_report.jpg).

The 28.64 s recording shows two fresh phones with the same accelerated graphics package and CPU seed. Around 00:04–00:13, the baseline receives an immediate 50 ms-requested tap and remains Home. Around 00:14–00:28, the isolated candidate loads and the same requested tap visibly opens the genuine Calculator at 0. These are actual guest pixels; no animation or screen is recreated.

This is a functional demonstration, not a matched speed or causal timing comparison. Actual ready-to-down times are 75/219 ms and physical holds 56/64 ms. The recorded candidate already has a held report when release arrives. The causal proof is the identical-schedule native comparison and three separate browser candidate runs where the release gate actually engages; their evidence is preserved in [verification.json](../docs/traces/short-tap/verification.json).

## ELI5 technical explanation

The touchscreen sends a small sequence of reports: a finger appeared, it is still touching, and it lifted. A busy startup turn could queue both mouse messages before the worker handled either. Catching up the guest's clock then exhausted the existing 20 ms minimum while the first report was still waiting to be read. Press and release survived, but no held report appeared, and this real app launch was ignored.

The correction keeps the normal release queued while that contact's last report is still MAKE. The emulated touchscreen's existing timer then generates a genuine held report, after which the original release path continues. It adds no guest patch, fabricated event, snapshot field, or browser waiting period. Cancellation and Pause retain their separate cleanup path.

With the same simulated 90 ms host delay, the old API delivers MAKE→BREAK and stays Home; the correction delivers MAKE→TOUCHING→BREAK and opens Calculator. Both native runs execute 300 million instructions and end at the same guest tick. 10,241 native checks pass, including repeated taps, partial multi-contact release, unread-report preservation and existing cancellation/checkpoint checks. The regression fails the old implementation. Three fresh candidate Chrome sessions also open Calculator with immediate 50 ms requested taps; every screenshot and report sequence was reviewed.

A separate image-test issue initially confused triage: exact software-renderer Calculator hashes differ under accelerated rendering. Only the immediate 50 ms public probe remained Home; the other three visibly opened Calculator at 0. That correction is explicit in [the investigation](../plans/input-latency.md#short-first-taps-retain-a-normal-held-contact-report).

## Why it matters

A short click should reach the genuine app even when startup is busy. This closes one reproduced input reliability hole without increasing all frontend waits. Root reviewed and integrated the API/test patch; rebuilt production acceptance follows separately. Flicks, pinch, and every app's gesture timing still need their own evidence.

Production follow-up: the integrated module (`603679fb…40c84f`) passes all 26 native suites and six actual controls checks. An initial Calculator assertion caught a blank display after the app opened. A new production phone reproduces that **byte-identical unfinished frame**, then reaches the original reviewed zero pixels without another tap. The [30-second timeline](../docs/traces/short-tap/production-render-timeline.json) separates input delivery from app rendering readiness. Root is adding bounded display readiness checks while keeping the immediate 50 ms tap. This does not claim faster app launch or that the original failed phone was resumed; that phone had already closed. The matching milestone video above remains the explicitly identified isolated before/after demonstration.


## Rebuilt production acceptance

The integrated correction is verified on production WASM `603679fbc7961ddad95ac8822257074fecde5940bc013246d520ea3b6240c84f`, offline bundle `9f4dbb3e5ab97a91138875d9`, with the unchanged guarded CPU seed. All 26 native suites, 34 JavaScript tests, and 59 browser checks pass: physical controls 6, clock lifecycle 12, persistence 23, and offline upgrade 18. The upgrade retains the older saved phone's complete checkpoint bytes and metadata. A reused isolated test directory also proves that stale firmware links are replaced without changing their preserved source files.

[The production Calculator recording](videos/calculator_accelerated_default.mp4) and [thumbnail](thumbnails/calculator_accelerated_default.jpg) show the same immediate 50 ms launch and twelve genuine keypad calculations, roughly 00:10–01:16. They produce `0`, `46`, `−5`, `56`, `9`, `0.25`, `3.75`, `2.5`, `3`, `333`, `20`, and `42`. All displays were independently read. The thumbnail at 01:02 shows `333`.

The maintained test now waits up to 30 seconds for two consecutive matches to the independently reviewed initial display, without changing its short clicks. It retains every original reference PNG and its SHA256, with exact alpha and a strict two-level maximum RGB difference. Independent decoding found just two pixels needing that second level, on antialiased digit edges; every other changed channel differs by one. All 132 comparisons against other answers fail this threshold. The original one-level test's two false negatives remain preserved. The final twelve-case test passes and produces the same twelve display hashes as the recorded run.

[Durable production evidence](../docs/traces/short-tap/production/verification.json) includes both failed test attempts, the independent pixel review and final acceptance. These checks establish input and arithmetic functionality. The bounded readiness wait, fixed keypad spacing and this recording are not launch-speed acceptance, maximum typing throughput or a claim of full speed.


A separate CPU follow-up used this same corrected production runtime in both arms and was **not promoted**. [The original-speed reverse pair](videos/thumb_pop_browser_diagnostic.mp4) runs for3:41 at original speed:00:00–01:49.88 is the isolated Thumb POP seed session, then01:49.88–03:41.04 is the original seed session; [the thumbnail](thumbnails/thumb_pop_browser_diagnostic.jpg) shows a retained fading-scrollbar mismatch. Warm Settings p95 and Home timing did not materially improve. Two strict image checks fail on the scroll indicator, without widening tolerance or dropping observations. This recording documents a rejected speed experiment, not a benefit of the touch correction. [Exact timing, order, image limits and decision](../plans/speed-experiments.md#thumb-pop-browser-decision-parked).

[Durable CPU follow-up evidence](../docs/traces/thumb-pop/browser-review.json) retains the full [timing comparison](../docs/traces/thumb-pop/browser-comparison.json), [all88 pixel results](../docs/traces/thumb-pop/browser-screen-comparison.json) and both actual scrollbar screenshots.
