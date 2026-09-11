# 127 — One local gateway for original apps

## Tweet idea

We built a tiny service inside the emulated iPhone for Safari, Weather and Stocks. Original Safari submits a real form to it. Weather and Stocks still fail before reaching it—so the next problem is the old networking path, not drawing replacement apps.

## What you're seeing

[Recording](videos/combined_app_gateway.mp4) · [Thumbnail](thumbnails/combined_app_gateway.jpg). The opening app checks show Weather and Stocks failing. At05:55 original Safari displays `Form received`, `ECHO-1`, and `Message: go`. The final section retains ordinary save/reload and the original lock screen. No speedup or complete app-suite acceptance is claimed. The visible demo banner identifies sample data; Weather/Stocks have not displayed it yet.

The [hostname control recording](videos/app_hostname_failure.mp4) and [thumbnail](thumbnails/app_hostname_failure.jpg) show the correctly entered server name failing in original Safari at02:40. The page underneath is the previously loaded numeric-address page, not a successful named request.

## ELI5 technical explanation

The old apps expect services that speak their original formats. Our own program now listens inside the phone and can answer Safari pages, Weather XML and stock-quote XML. Original launchd starts it; its real sockets handle Safari's request. We only added our program, its startup configuration and a disclosed hostname mapping to a separate phone image, preserving the Apple executables.

Builder tests prove our responses are internally consistent. They do not prove an app can reach or understand them. The diagnostic log shows a successful Safari request addressed by number, but no Weather/Stocks requests, which use a name. A follow-up observer captured Weather’s actual cannot-find-host error. Its asynchronous resolver skips the monitored hosts-file lookup, explaining why the mapping alone was insufficient in this run. Startup recovery is added, but the earlier server SIGBUS cause is not fixed.

## Why it matters

There is now one reusable service implementation instead of a separate fake UI for each app. Safari provides an actual end-to-end foothold. Weather/Stocks routing, their original display behavior and post-reload service continuity remain open gates. [Evidence](../docs/traces/combined-app-gateway/README.md) and [other-app opportunities](../plans/app-services-opportunities.md) separate achievable service work from graphics, media and audio dependencies.
