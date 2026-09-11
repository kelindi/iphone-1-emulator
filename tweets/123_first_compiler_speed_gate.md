# 123 — First compiler speed gate: correct, but slower

## Tweet idea

Our iPhone compiler passes all 54 saved-machine comparisons, but its first controlled speed test is about three times slower than the existing engine. Profiling shows why: too much time deciding whether compiled code can run and compiling it, with very short bursts of useful execution.

## What you're seeing

[Recording](videos/compiler_first_gate.mp4), entire 00:00–00:14.188 sequence: labeled seeded A followed by adaptive B, each executing the first 100M Settings workload credits. [Thumbnail](thumbnails/compiler_first_gate.jpg). This is a separate visualization replay, not the timing source for the gate. The final image is Settings still loading, not a completed app launch. Both recorded checkpoints match the frozen reference; all 205 video frames and timestamps were validated.

[Gate evidence](../docs/traces/adaptive-first-gate/README.md), [profile](../docs/traces/adaptive-first-gate/profile/README.md), and [media audit](../docs/traces/adaptive-first-gate/recording-media-review.json) preserve the results.

## ELI5 technical explanation

A compiler translates phone instructions into code the browser can execute. That translation only helps if we use it long enough to repay the work of preparing it and checking that it is still safe. In this diagnostic run, each compiled call executes only about eight instructions on average before handing control back. Repeated compilation and the checks around entering compiled code dominate the opportunity; making the generated arithmetic faster is not the main target.

The controlled Settings, Contacts and Notes trial completed 18 runs and passed all 54 full-state checkpoints. Its aggregate CPU throughput was 0.337 times the seeded engine, against a target of at least twice as fast. These are cloud CPU measurements, not Mac interaction latency. Profiling is a separate main-thread diagnostic and does not replace those measurements.

## Why it matters

We have a working experimental compiler and an honest failed performance result. The browser product has not been switched to this slower candidate. The accepted plan permits one corrective revision, targeting measured compilation and dispatch overhead, followed by the same correctness and speed gate. A second failure requires review rather than unlimited tuning. That correction has not yet been implemented or measured.
