# The proposed loop shortcut is too small to deliver the goal

## Tweet idea

Before another emulator rewrite, we measured a real warm Settings launch. Our proposed loop shortcut touches regions accounting for only 4.5% of sampled worker time. That cannot support a 20% speed target. We stopped before building it and kept the evidence.

## What you're seeing

[Full original-speed profiling workflow](videos/trace_optimizer_profile.mp4) · [Settings thumbnail](thumbnails/trace_optimizer_profile.jpg).

The recording shows an unprofiled Settings launch and Home return, followed by the profiled warm Settings launch in the final section. This is the unchanged phone, not a before/after improvement. Actual final frames, profiler samples, module hashes and measurement scripts are retained in [the evidence](../docs/traces/trace-optimizer-profile/README.md).

## ELI5 technical explanation

The emulator repeatedly translates the old phone's instructions into work that a modern browser can execute. We considered making short repeated loops skip some routing and checking overhead. Source inspection found 95 eligible loops across 47 compiled regions; registers and flags already stay local, so caching them again would not be a new optimization.

One actual warm Settings profile assigns about 59.7% of sampled worker time to the emulator core, 27.8% to generated code and 11.0% to idle. The entire 47 candidate regions account for only 4.5%; the proposed removable overhead is smaller still. The page main thread is about 96.5% idle during this interval, which points away from the surrounding HTML interface as the primary problem.

These are exclusive sampled residency shares in one approximately 2.96-second interval, including readiness confirmation and profiler overhead. They are not exact launch-time fractions or an all-app benchmark. Even with those limits, the observed candidate share is far too small to justify a 20% claim. Most internal core function names are unresolved in this build; we have not pretended to identify a new specific core optimization.

## Why it matters

The agreed stop rule avoids another long implementation and qualification cycle for a low-return idea. No production compiler change was made, and no speed improvement was achieved in this experiment. A larger gain requires evidence for a broader execution-engine change; this profile does not establish what that change should be. The original product and speed goals remain open.
