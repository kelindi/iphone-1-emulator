# 082 — The original time picker now survives autosave

## Tweet idea

The2007 iPhone's real time picker now changes the emulated hardware clock—and both changes survive automatic saving and a browser reload. No replacement Settings screen: Apple's original app, driver and picker all run together.

## What you're seeing

[Full original-speed recording](videos/rtc_manual_time_autosave.mp4) · [thumbnail](thumbnails/rtc_manual_time_autosave.jpg).

Around00:41 the picker selects4:15; around01:09–01:17 its natural autosave and reload complete. Around01:21 it selects4:16; around01:49–01:56 the second autosave and reload complete. The final seconds show the phone running again. The full recording is123.68seconds; setup imports an existing diagnostic fixture using the intentionally hidden controls. Exports are diagnostic instrumentation; persistence uses the app's actual30-second autosave interval, with no Save-button substitution.

This original recording uses the isolated candidate runtime. That exact clock model is now integrated in the default local web build. Both reloads match all294,400 compared RGB pixels below the status bar exactly, and generated execution advances after each reload and at the end.

## ELI5 technical explanation

The old clock model could count time but did not finish Apple's date-setting conversation. First we supplied the missing second-tick interrupt. Now a complete valid seven-byte date update is accepted and applied on the next real second pulse. The unchanged Apple UI, kernel and driver perform that conversation themselves.

Read-only inspection of seven actual exports confirms both new hardware times, cleared pending updates and zero unsupported writes. Existing unsupported-read counts remain documented. The initial browser attempt hit a separate low-disk Blob-storage failure; we preserved it, proved the unchanged storage code works after safe VM cleanup, and recorded this new instrumented attempt.

## Why it matters

Manual time setting now works through the real app, hardware model and browser persistence together. The supported scope is complete, calendar-consistent updates for2000–2099; partial/invalid writes and alarms remain outside this gate. The separate candidate lifecycle gate also passes all 12 original assertions; the combined integration now passes28 native suites,58 JavaScript tests,30 actual old-runtime upgrade checks and21 final-release browser checks. [Evidence, exact hashes, preserved failure and limitations](../docs/traces/rtc-write-contract/manual-time-persistence/README.md).

[Final default-release recording](videos/rtc_final_release.mp4) · [thumbnail](thumbnails/rtc_final_release.jpg): around00:16 genuine Settings opens, around00:39 the real autosave completes, and around00:56 the identical phone returns with the private server stopped. [Exact release evidence and limitations](../docs/traces/rtc-release-upgrade/final-release/README.md). This is local integration, not a public deployment; offline installation in this supporting recording is explicit hidden test instrumentation.
