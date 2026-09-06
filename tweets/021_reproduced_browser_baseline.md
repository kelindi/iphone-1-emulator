## Tweet idea

Before improving the browser iPhone, we rebuilt it from a clean baseline and replayed the real 2007 apps: Home → Settings → Home → Safari → Home. Now we have a repeatable recording and test harness to catch regressions as the phone gets faster and more useful.

## What you're seeing

Recording: [browser_baseline_20260905.mp4](videos/browser_baseline_20260905.mp4), with [thumbnail](thumbnails/browser_baseline_20260905.jpg).

- 0:00–0:14: initial local load and the genuine home screen.
- 0:14–0:38: Settings launch and inspection.
- 0:38–1:07: return Home.
- 1:07–1:27: Safari launch.
- 1:27–1:50: return Home.

This is the original page and existing app functionality, reproduced on the new VM. It does not show the new physical phone shell. The supplied snapshot already has the Repair Needed alert dismissed.

## ELI5 technical explanation

The emulator runs hundreds of millions of tiny ARM instructions to do things a real iPhone did in seconds. Before changing those instructions or their scheduling, we need a repeatable way to tell whether the real operating system still behaves correctly.

We installed the native build tools and pinned Emscripten 4.0.16. All 22 original tests passed. Browser startup also revealed that the SDK's default WebAssembly stack was too small for snapshot validation; explicitly reserving 4 MiB fixed loading without changing Apple software or snapshot format.

A new Playwright harness uses actual Chrome, clicks the real guest display, exercises Home navigation, and records screenshots, console output, and video. Local startup was 3.13 seconds; the old page reported about 31.1 million instructions/second. The navigation waits deliberately include settling and recording overhead, so they are evidence of behavior, not precise app-speed measurements.

## Why it matters

We now have a reproducible starting point for the clickable phone, worker-based browser execution, input fixes, and durable saves. Future claims can be compared to actual runs instead of relying on older screenshots or stale run commands.
