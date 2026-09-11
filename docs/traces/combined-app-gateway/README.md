# Combined local app gateway — integration result

The independently authored ARMv6 gateway now boots under original launchd and serves original Safari on loopback port80. Its Weather and Stocks quote builders pass host contracts, but **the original Weather and Stocks workflows fail**: their requests have not reached this listener in the retained diagnostic log. No default-phone promotion or live-data claim.

## Retained evidence

- `gateway-build-tests.json`: source/build pins and quiet/diagnostic wire parity. Final diagnostic executable SHA256 `1a9dd53bf5cc054b0278df41c16d833dfccd294b387295eea5f771c0cd16ec83`.
- `install-report.json`: copy-only image, one own executable/plist, explicit hosts configuration; all5119 other old file forks preserved. Image SHA256 `488a19acca93daea1ab3b3fac6039eae8394c6596fa75d7cce56a32a8daaa682`.
- `native-processes.json`: fresh10B-instruction boot, original launchd starts gatewayPID21. Startup log separately confirms bind/listen127.0.0.1:80.
- `browser-report.json` and retained PNGs: genuine Weather Update failed, Stocks noquotes/chart error, original Safari HOME-1 then ECHO-1 with `Message: go`. Ordinary interval save timestamp is restored exactly; fresh service response after reload remains unverified. Original phone wakes to lock screen.
- `routing-report.json`, `service.log`: read-only diagnostic snapshot audit, captured after SafariHOME-1 but **before form/reload**. One complete numeric-host SafariGET/HTTP200; no Weather/Stocks POSTs in the recovered file. Gateway remains alive, launchd status0. File-cache/readback limits in report apply; this is not a complete network-event trace.
- `original-request-paths.json`: original Weather submits directly through NSURLConnection. Its inspected reachability callback retries failed cities; it does not gate initial submission. Capture the actual NSError before attributing failure to DNS.

The browser's frozen required labels were Weather forecast, Stocks quotes, Safari form, and fresh service after reload. Only Safari form passes. Overall report is deliberately failed, not relaxed after testing. The diagnostic export filename `final-home.snap` is historical harness naming: it contains SafariHOME-1, not a final accepted Home screen.

## Implementation limits

The gateway implements local Safari pages, deterministic Weather forecast/location XML, and fixed Stocks quotes. Stocks charts/search are unsupported. Diagnostic logs and supported original launchd KeepAlive add observability/recovery; they do not fix the previously identified SIGBUS root cause. A visible surrounding banner identifies sample data. The isolated candidate contains a hosts entry mapping only `iphone-wu.apple.com` to127.0.0.1; the hostname path is not yet qualified.

[Source and contract instructions](../../../tools/legacy_services/guest_http/WEATHER_GATEWAY.md). [Other original apps and dependencies](../../../plans/app-services-opportunities.md).

## Hostname failure isolated

The follow-up `hostname/` evidence narrows the blocker. Original Safari, given the correctly typed `iphone-wu.apple.com`, displays its cannot-find-server alert. The exported backing-file log still contains only the earlier numeric GET; no new hostname transaction was recovered. The browser observation run completed (not an accepted working-app suite); the first attempt failed during import and remains private evidence, followed by a successful interactive run.

A host-only native observer then captures original Weather's `NSURLConnection` error before the app discards it: **NSURLErrorDomain −1003**. Original Foundation's `-code` and `-domain` getters independently verify the object offsets. Three hostname lookups use the asynchronous resolver; the monitored flat-file resolver and hosts-file fopen path are never entered before the same failure. One early async-entry byte/peek refusal is explicitly retained; later verified entries supply the positive observations. No guest instructions or Apple executable bytes were patched.

This explains why the hosts mapping was insufficient in this tested path. It does not prove all iPhone OS name lookups ignore hosts. Next inspect the asynchronous query's actual resolver and supported local record/DNS configuration. Do not change the XML response to fix a request that has not reached HTTP.
