# Letting the original animations follow real time

## Tweet idea

Home now returns in about 1.1–1.3 seconds in our browser iPhone. Settings opens in 2.7–3.0. The original animations were following a clock slowed by CPU emulation; they now follow real foreground time. No Apple code patches, sped-up video, or invented CPU speed.

## What you're seeing

[Full recording](videos/interactive_guest_clock.mp4) · [thumbnail](thumbnails/interactive_guest_clock.jpg).

The uncut, original-speed 34.44-second recording shows the current default phone completing four genuine Settings → Home rounds. The final Home screen is at 0:33, also used for the thumbnail.

| Round | Settings ready | Home ready |
|---|---:|---:|
| 1 | 2.650 s | 1.150 s |
| 2 | 2.950 s | 1.100 s |
| 3 | 2.700 s | 1.250 s |
| 4 | 2.900 s | 1.100 s |

The previous USB-enabled phone measured Settings at 6.7–7.6 seconds and Home at 3.35–4.01 seconds in [milestone 036](036_home_service_recovery.md). Both tests sample actual guest pixels every 50 ms and require the intended layout to remain stable for 500 ms; readiness begins that interval. This is a bounded Chrome/VM sample, not a product percentile. Source: `.context/clock-production-performance/report.json`. Reproduce with `npm run test:performance`.

## ELI5 technical explanation

The emulator used to advance its clock according to how many instructions it finished. While the CPU struggled to draw an animation, the guest's clock moved slowly too. The original app kept drawing intermediate animation frames for far longer than intended.

The interactive browser now connects the existing guest timer model to monotonic foreground time. When the guest waits for an interrupt, the worker waits asynchronously for the corresponding real deadline. It does not pretend a sleep occurred or advance future guest time. The original app chooses its own animation frames from the elapsed time; the CPU still retires only instructions it actually executes.

Pause, hiding the tab, restoring a backup, and checkpoint capture have explicit clock boundaries. A long host interruption cannot cause an unbounded catch-up burst. Ten maintained real-Chrome lifecycle checks pass, including a genuinely hidden/frozen tab, preservation of manual Pause, and exact clock/instruction restoration through the original Export/Import UI. All 24 native suites pass, including 10,195 browser-API checks. Final production checks also pass four physical-control actions, six offline cases, and 23 persistence checks with exact restored Settings pixels. See [the investigation and reproduction commands](../plans/transition-latency.md).

Older saves need different treatment. Some retain the old USB-unmatched state, where `lockdownd` disappears and Home retries service connections. A read-only check validates the retained boot arguments, RAM bounds and actual USB device-tree property once after loading; it enables interactive timing only for a known USB-enabled guest. Old or unknown states retain their previous deterministic timing. Five actual saved phones confirm the selection, including an edited Contacts backup. Production imports of the old compact phone retain Settings at 7.50/7.05 seconds and Home at 14.20/14.45 seconds. Old saves do not receive the new speed. No user data, backup bytes, or v39 snapshot format changes.

## Why it matters

Returning Home is substantially more usable, and Settings spends less time animating. Settings still misses the two-second target, and the full app/performance goal remains unfinished. This is a timing-policy improvement alongside the repaired guest service; it is not a faster execution engine or a claim that every original app now works at full speed.
