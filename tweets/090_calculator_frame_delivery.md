# 090 — Measuring when Calculator pixels actually arrive

## Tweet idea

The original iPhone Calculator answers all 60 presses correctly. Watching actual canvas delivery puts button-feedback p95 at 134–136 ms in this run. The animation-frame sampler sees those same frames later. We improved the measurement, not the emulator.

## What you're seeing

[Full uncut recording](videos/calculator_frame_delivery.mp4) · [Thumbnail](thumbnails/calculator_frame_delivery.jpg)

The real phone opens Calculator, warms up one 9/C pair, then executes thirty measured pairs at **00:15–01:17**. The thumbnail shows an actual 9. Inputs, OS code, clock, renderer and production scheduling are unchanged.

Button feedback reaches the canvas at p95 **135.7 ms for 9 and 134.0 ms for C**. Those same frames are observed by the RAF sampler at **148.6/148.9 ms**. The entire display is independently verified after every press. Correct results reach the canvas within **138.8/129.3 ms of release at p95**; their down-to-result delays are a separate, longer measure.

## ELI5 technical explanation

Checking a screen periodically can miss the moment a new picture arrived. This test timestamps the real worker message, lets the original canvas function run unchanged, and timestamps its completion. It also keeps the old sampler so we can compare the two clocks on the same frame.

All sixty first-button frames match across those observations. The sampler sees them a median 12.9 ms later. Saved source and canvas pixel crops let an independent checker verify both the correct numbers and their frame associations, rather than trusting only the test's reported deltas.

The observer itself does work: post-draw checks cost a median 2.4 ms and p95 4.1 ms per observed frame, and crop transport lengthens spacing between presses. No overhead is subtracted. The earlier 089 results and failed warmup remain preserved; this different diagnostic is not a before/after speed comparison. [Full source, raw evidence and limitations](../docs/traces/calculator-frame-delivery/README.md).

## Why it matters

This establishes the 150 ms canvas button-feedback threshold for this specific Calculator workload in one observed Chrome run. It avoids changing production scheduling based on a late sampler. Physical display paint, other apps, sustained typing, Mac/Safari behavior and the two-second Settings target remain unproven. No production speedup is claimed.
