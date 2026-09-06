# The phone is still far too slow

## Tweet idea

The browser iPhone looks right, but testing makes the next priority clear: speed. Settings takes about 7–8 seconds; returning Home can take 14–16. We're measuring the real guest's response and separating CPU work, animation timing, and input buffering before the next optimization.

## What you're seeing

[Full original-speed recording](videos/current_speed_baseline.mp4) · [thumbnail](thumbnails/current_speed_baseline.jpg).

The full recording shows four Settings → Home rounds on the compact default phone. Nothing is accelerated or cut. This is a baseline of the current limitation, not an improvement demo. The first Settings launch becomes ready 7.35 seconds after dispatch; the first Home return takes 14.20 seconds. Across all four rounds, Settings ranges from 6.85 to 7.92 seconds and Home from 6.20 to 16.41 seconds.

Each click is a 30 ms press. The test samples guest pixels every 50 ms and requires the Settings rows or matching Home layout to remain stable for 500 ms. Reported readiness starts that stable interval, excluding the confirmation wait. Reproduce with `npm run test:performance`. This small Chrome/VM sample does not establish a percentile or another machine's performance.

The first run completed all eight actions but its final JSON writer rejected a diagnostic BigInt. Its timings were recovered from stdout in `.context/speed-input100/report-recovered.json`; the maintained writer now handles BigInt.

## ELI5 technical explanation

Moving CPU emulation off the page's main thread makes the surrounding controls responsive, but the original apps still need the emulated CPU to finish their work. Roughly 30 million instructions per second is insufficient for the intended experience. The retained direct-memory-write optimization gained about 7% in a WASM replay. Larger generated-code experiments were slower and were not shipped.

Guest timing and input add separate delays. A minimum touch hold is measured in guest time; animations also follow the guest clock. We are testing those paths independently. Shorter holds must preserve real touch delivery, and clock improvements must keep time honest. An isolated shorter-hold browser experiment has not materially improved Settings/Home navigation. Subsequent independent traces locate the long Home delay in roughly 30 seconds of guest-time connection retries to the absent lockdownd daemon. The real daemon exits when it cannot find AppleUSBDeviceMux, a service excluded by the current USB workaround. This is a concrete service-startup problem to fix alongside CPU throughput; it is not yet a completed fix.

## Why it matters

It records why the current preview is not the requested end result. The navigation target remains at most two seconds. Hardware expansion has yielded to focused performance work; no fix has yet reached that target. See [execution-engine experiments](../plans/speed-experiments.md) and [the execution log](../plans/execution.md).
