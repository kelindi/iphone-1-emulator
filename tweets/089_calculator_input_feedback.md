# 089 — Measuring real Calculator input

## Tweet idea

Sixty presses on the original iPhone Calculator, sixty correct results. We also learned why measuring responsiveness needs care: a browser sampler can see a screen late even when the emulator delivered it earlier. The timing target stays open until we measure that boundary directly.

## What you're seeing

[Full recording](videos/calculator_input_feedback.mp4) · [Thumbnail](thumbnails/calculator_input_feedback.jpg)

The uncut 73.24-second recording opens genuine Calculator, warms up one pair, then presses **9 → C** thirty times. The measured section runs about **00:15–01:12**; the thumbnail at **00:15.8** shows 9. These are real pointer events and guest pixels, with unchanged production input and clock behavior.

Both keys have sampled button-feedback medians near **99 ms** and p95 near **165–166 ms**. The correct result is observed within about **134 ms of release at p95**. However, the sampler skips some delivered frames. Its observation gaps are larger than the apparent 15–16 ms excess over our input target, so **the 150 ms gate remains unresolved**. This is neither a speed improvement nor proof that the guest itself misses that target.

## ELI5 technical explanation

A tap travels through browser input, the emulated touchscreen, the real operating system, and the guest display. We checked the entire Calculator result against previously reviewed original images and separately watched its button highlight.

All sixty results match. But checking the canvas once per browser animation callback did not observe every update: 162 of 210 recorded intervals skip at least one frame sequence. Seeing a change late cannot tell us exactly when it was delivered. The next diagnostic records actual delivery alongside the sampler, before changing the emulator’s scheduling.

The first attempt stopped in warmup because the reporter could not write a large integer to JSON. Its failed source and recording remain preserved. The corrected report writer passes, all six actual served asset hashes match, and the independent audit verifies counts and timings while rejecting eight deliberately altered reports. Raw per-press crops were not retained in this run; that evidence limit is explicit. [Source, reports and limitations](../docs/traces/calculator-input-feedback/README.md).

## Why it matters

This separates ordinary keypad behavior from multi-second app launches and gives us a repeatable correctness workload. It also prevents a measurement delay from becoming an unnecessary emulator change. No production input change, Mac/Safari result, or completed full-speed claim is attached to this diagnostic.
