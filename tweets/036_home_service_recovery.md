# Home returns without the missing-service timeout

## Tweet idea

The original iPhone was spending 30 seconds of guest time waiting for a missing service every time we pressed Home. Allowing its real USB services to start keeps that service alive. Browser Home returns now take 3.4–4.0 seconds. Still too slow, but the long black-screen stall is gone.

## What you're seeing

[Full recording](videos/home_service_recovery.mp4) · [thumbnail](thumbnails/home_service_recovery.jpg).

The unaccelerated 81.56-second recording shows importing the newly booted phone, then four genuine Settings → Home rounds. The final section ends on the original full-size Home screen; the thumbnail is at 1:20. Home readiness takes 3.372, 3.350, 4.012, and 3.850 seconds. Settings takes 7.600, 6.800, 6.700, and 6.800 seconds. The earlier [baseline recording](035_current_speed_baseline.md) included Home returns of 14–16 seconds, with one faster round. These are bounded Chrome/VM observations, not a product percentile or a claim of original-device speed.

The test samples actual guest pixels every 50 ms and requires the matching Home layout or Settings content to remain stable for 500 ms. Reported readiness begins that stable interval. CPI remains 1. The source report is `.context/speed-usb-matched/report.json`; reproduce against the new starting phone with `npm run test:performance`.

## ELI5 technical explanation

The Home handler asks an original system service called `lockdownd` for information. Our guest's service was dying because an old boot workaround hid the USB device it expected. Home then waited through failed local socket connections. Capturing the black screen and reading the genuine process stacks exposed that dependency.

We rechecked boot with the original device tree and USB matching enabled. It now boots successfully, including with the preserved `origin/main` emulator and the original larger work image. That shows the historical workaround was already unnecessary in the tested environment; a new USB implementation or a device-tree reorder was not needed. The genuine USB mux service starts, and the same `lockdownd` process stays alive before and after app use. Apple's executables and their timeout logic are unchanged. This does not implement USB cable transfers.

Native tracing corroborates the browser result: the old black interval lasted approximately 30 guest seconds. With the working service, the first visible Home animation follows black by 0.242 guest seconds, and the complete Home layout follows by 0.644 seconds. Guest memory pressure remains at level 20 before and after Settings/Home. See [the transition investigation](../plans/transition-latency.md) for the underlying evidence.

## Why it matters

A real guest dependency caused a large part of the navigation delay. Fixing that dependency removes the stall while preserving the original applications. Settings and Home still miss the two-second navigation target, so CPU execution and honest clock pacing remain active work.

This improvement needs a newly booted phone. Existing saved checkpoints preserve the old USB registry state. Export a backup before choosing Reset to start the new phone; resetting replaces current local phone data, and importing an old backup brings its old guest state back. There is no automatic migration of that state.
