# 085 — A failed download has a way back

## Tweet idea

The browser iPhone now puts “Try again” directly on its error screen. A failed OS download can recover with a click, and an error after startup no longer leaves you staring at a frozen phone.

## What you're seeing

[Full recording](videos/visible_recovery.mp4) · [thumbnail](thumbnails/visible_recovery.jpg). Around 00:00–00:02: controlled download failure and visible retry. Around 00:09–00:10: genuine Home after retry. Around 00:10–00:12: an explicitly injected runtime error, followed by keyboard retry and the original OS restarting. The complete 16.80-second capture is shown at original speed; this is not a latency benchmark.

## ELI5 technical explanation

The simplified page hid the extra controls, but the recovery buttons were still inside that hidden section. We moved them into the phone's error overlay. We also made failures reveal the overlay again after startup, before displaying their message. The working phone keeps its clean appearance.

A real browser test deliberately fails the manifest download, clicks the visible button and boots the actual emulator. It then injects an error message at the worker boundary to verify the later-failure display and keyboard retry. All 12 checks pass. Apple's software, the emulator worker and saved-state logic are unchanged. [Evidence and exact scope](../docs/traces/visible-recovery/README.md).

## Why it matters

A failed load is now recoverable from the visible phone. The same overlay also houses the existing previous-save action when a saved phone is available. This improves the usable browser experience while CPU speed, continuous audio and network services remain in progress.
