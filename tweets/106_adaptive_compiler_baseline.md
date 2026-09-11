# 106 — Freeze the starting line before building the compiler

## Tweet idea

Before building our iPhone browser compiler, we froze the starting line: 62 real
Settings/Home actions, a genuine slide to unlock, and hashes of every runtime
asset. Settings still takes about 2.3 seconds. Now we can tell whether the new
compiler actually helps.

## What you're seeing

[Complete recording](videos/adaptive_compiler_baseline.mp4) ·
[Thumbnail](thumbnails/adaptive_compiler_baseline.jpg).
The opening section loads and unlocks the real guest; the remainder repeats
31 Settings/Home pairs. All timings come from browser pointer-release events,
not video playback. [Evidence and raw results](../docs/traces/adaptive-compiler-baseline/README.md).

## ELI5 technical explanation

A compiler will translate frequently used iPhone instructions into code the
browser can run more efficiently. First we need an honest stopwatch and an
unchanging reference phone. Earlier experiments showed that another session
could change shared files during a test. We now copy the files independently,
check their fingerprints, and verify the bytes actually served to Chrome.

The phone now opens locked, so the test must slide to unlock before looking for
Home. Otherwise it could time the wrong screen. Thirty warm samples per action
show Settings median 2.318 seconds and p95 2.567 seconds; Home median 0.866 seconds
and p95 0.868 seconds. The cloud run completed correctly, but Settings misses
our one-second target. These are baseline measurements, not a speed improvement.

We also wrote the compiler's correctness rules: never lose or repeat a memory
write, stop at the right fault, honor device timing, and discard compiled code
when its underlying instructions change. Those rules are specified; the new
adaptive engine is not implemented yet.

## Why it matters

Future compiler trials now have reproducible inputs and explicit correctness
obligations. Mac qualification, other apps, touch, scrolling and startup remain
separate work. A single first launch here cannot prove the first-launch p95.

Validation: all 73 JavaScript unit tests pass. The final harness also passes a
separate four-action browser check with verified assets and clean shutdown.
