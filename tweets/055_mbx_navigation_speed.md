# Accelerated graphics makes Home returns 28% faster in this VM

## Tweet idea

The original iPhone's Home return now takes 0.92 seconds at the 95th percentile, down from 1.27 seconds in our browser benchmark. We fixed the real graphics commands, checked all sixteen apps, and measured both modes in alternating order. Settings still needs more work.

## What you're seeing

[Original-speed recording](videos/mbx_navigation_speed.mp4), **0:28–1:49**, shows the second accelerated run's eleven actual Settings/Home round trips in the clickable iPhone frame. The opening section imports the test phone. [Thumbnail](thumbnails/mbx_navigation_speed.jpg) shows genuine Settings at **0:43**. The recording includes deliberate between-action waits and the readiness observer's settling interval; it has not been sped up. It is one of four measured runs, not a side-by-side recording of the complete comparison.

The [durable machine-readable benchmark](../docs/benchmarks/mbx-navigation-20260905.json) retains every timing sample and exact host, browser, firmware and engine identity. Full reports, all four original videos and 88 screenshots are in `.context/graphics-final/quiet-navigation/`.

| Actual pointer release → usable screen | Software median | Accelerated median | Software p95 | Accelerated p95 |
| --- | ---: | ---: | ---: | ---: |
| Warm Settings launch | 3.45 s | 3.29 s | 3.88 s | 3.57 s |
| Warm Home return | 1.14 s | 0.87 s | 1.27 s | 0.92 s |

Home improves **27.5% at p95**, or **24.0% at the median**. Settings improves **8.1% at p95**, or **4.6% at the median**. First Settings launches are separate: software **4.71/4.76 s**, accelerated **4.21/3.86 s**.

## ELI5 technical explanation

The phone can draw its interface using its ARM processor or submit jobs to its PowerVR graphics chip. Our emulator initially rejected several genuine graphics jobs, which made Apple's driver restart. We decoded and tested the clipped rectangles, texture strips and transparency used by Calendar, Safari, Text, Photos and Clock. All Apple programs remain unmodified; the screen is still their real framebuffer.

The combined fresh-phone audit completes **5,870 2D and 14,902 3D commands without rejection**, with all sixteen apps and 61 screenshot stages manually reviewed. The focused recordings in milestones 052–054 show the final repaired families; they do not claim to record all sixteen audit sessions. The production build also passes its Text/Photos/Clock controls, exact Home comparisons, clock lifecycle, sleep/wake/unlock, 26 native suites and 34 JavaScript checks.

For speed, every other emulator/build job stopped. The order was **software → accelerated → accelerated → software**, using the same production WASM and guarded CPU seed. Each run imported its phone through the same normal UI and performed eleven rounds: one first launch plus ten warm rounds. That gives **20 warm samples per action per graphics configuration**. Sampling occurs every 50 ms; readiness must remain stable for 500 ms, and reported readiness starts that interval. Actual browser pointer-release timestamps remove intentional press duration. Default autosave remains enabled.

Root reviewed the Settings/Home reference images from all four runs. All 88 screenshots differ from their same-run reference by at most three channel levels below the status bar, preserving visible content. The VM's measurements do not substitute for testing the user's Mac. Two separate CPU-cache experiments were rejected because their apparent gains did not repeat; neither is in this build.

## Why it matters

Home navigation now has a repeatable improvement after the broader graphics corrections. Settings still exceeds the two-second warm-launch target, and first launches exceed the three-second target. This does not establish full speed, 30-fps animation, the 30-sample input-feedback gate, or working network/audio/camera services.

The measured package is now the default for new phones after the continuous sixteen-app rendering gate and 65 fresh-package browser checks passed. [Milestone 058](058_accelerated_default_phone.md) records promotion, the fresh-load controls video, and preservation of older saved phones. Existing saves keep their graphics mode; the full-speed and service limitations above remain open.
