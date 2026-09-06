# 092 — Whole-core optimization helps Settings modestly

## Tweet idea

We rebuilt the iPhone emulator so LLVM could optimize across the whole CPU core. Fixed-work CPU time fell 5.8%, but real Settings launches improved only about 50 ms at the median, and Home's slow tail worsened. The experiment stays isolated. Measuring the phone matters more than winning a microbenchmark.

## What you're seeing

[Full candidate recording](videos/web_lto_navigation_diagnostic.mp4) · [Thumbnail](thumbnails/web_lto_navigation_diagnostic.jpg)

The complete 117.84-second first candidate session shows the genuine phone loading, controlled paused backup import, and eleven Settings/Home rounds at original speed. The thumbnail at 00:45 shows actual Settings. No cuts, sped-up footage or reconstructed screens. This uses the frozen 085 recovery UI and original 512-region seed; it is a fresh-profile cloud Chrome comparison, not the cached-return experiment in080.

Across20 warm samples per action/configuration, Settings median is **3.267→3.217s** and p95 **3.817→3.417s**. Settings median improves **1.53%/2.24%** in forward/reverse order, p95 **15.96%/5.47%**. First Settings is **5.453→5.139s** and **5.707→5.459s**. Home median improves **0.892→0.867s**, but pooled p95 worsens **0.918→0.967s**; reverse-order Home p95 increases **50.4ms**. All individual values are retained in the [full results](../docs/traces/web-lto/browser/README.md).

The original first control completed all22 stages, then a reporter mistook a stored URL string for a method. Its complete trace, asset hashes and pixels pass independent recovery checks; its graceful-close receipt is absent and its recording is truncated. We preserved the failed run and entire profile, fixed and tested only the reporter/cleanup, and ran the three previously unstarted arms separately. No guest was repeated or sample discarded. This is an interrupted comparison with an explicitly failed first control, not an uninterrupted successful ABBA.

## ELI5 technical explanation

The old phone's CPU is software running inside the browser. Its C source is split into many files. LLVM link-time optimization lets the compiler see those files together, potentially removing repeated work across their boundaries. The actual released web build did not enable that optimization.

We changed only the isolated build flags. The original guest code, generated seed, CPU checks, memory rules and device deadlines stayed the same. The new core matches complete guest backups and counters, and two fixed-work timing pairs show5.81% less CPU. Actual navigation gives a smaller benefit because it also includes guest scheduling, graphics, browser compilation and normal saving.

All88 retained stage pairs match their strict reference pixels, and eight first screens pass human review. Actual served files and both compilation budgets pass. A separate pre-existing WASM floating-point defect still fails the same12 literal checks in both builds; this experiment neither fixes nor hides it.

## Why it matters

We now have a reproducible whole-core candidate and real-phone evidence for its limits. The roughly50ms Settings median improvement does not resolve three-second navigation, while Home tail latency regresses. LTO remains parked; the production core is unchanged. Two-second launch acceptance remains open.
