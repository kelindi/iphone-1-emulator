# Compatible services for original apps

> Historical recordings and publishing artifacts have been removed from Git history. The summaries below describe the original runs; remaining test reports and screenshots are retained. Historical hash receipts may predate this archival cleanup.

**2026-09-08 current isolated result:** original Weather and Stocks now receive sample data through the combined own gateway, original configd/mDNSResponder and real guest loopback sockets. Weather displays Cupertino 72°F with six days, New York 64°F after a swipe, a Home/revisit, and Cupertino 22°C using its unit control. Stocks displays AAPL120.00, GOOG500.00, YHOO25.00 and T40.00 on the same corrected `915268…` build. The browser gate passed with no harness errors; the wire audit retains four complete six-day forecast responses and one four-quote response. [131 recording](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/weather-stocks-local/README.md) and [actual DNS/HTTP/frame evidence](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/weather-stocks-local/README.md).

These are visibly labeled fixed sample data. Charts (retained HTTP400), symbol and city search/addition, live data, default distribution and persistence of this combined build remain unqualified. Earlier Safari workflows have their separate evidence below. No Apple executable or emulator CPU change was needed for these services; the owned gateway supplies compatible responses and narrowly scoped resolver configuration.

**Superseded routing failures:** the initial hosts-only gateway reached Safari but not Weather/Stocks HTTP. A later LocalOnly registration worked for LocalOnly DNS queries but did not resolve the apps' ordinary Any query. Supplemental resolver configuration enabled actual DNS routing, then the original Weather `t` query parameter exposed a gateway framing rejection. Those failures and the own environment/r9 ABI corrections remain in the [earlier archive](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/combined-app-gateway/README.md).

**2026-09-06 qualification result:** the maintained quiet daemon boots from the actual pristine accelerated-default image and original Safari renders its landing page and Next. The first form stalls with guest graphics hangs; the final process list also lacks the daemon, with exit cause unobserved. Bookmark/reload/final Home remain unfinished. No default promotion or retry. [093 failure evidence](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/guest-http-default/README.md).

The browser roadmap calls for deterministic demo services first, exercised by Apple's original apps through the guest's network stack. Original Safari now loads a clearly labeled local demo through an independently authored daemon and the guest's real loopback sockets. Links, a form, bookmarks and ordinary autosave/reload pass in one isolated imported phone. [091 recording and evidence](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/guest-safari/README.md). The demos are not in the default phone yet; external networking and live data remain separate work. This path does not require resuming the parked wireless-device investigation.

## Weather protocol and current acceptance

The [original Weather source contract](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/weather-service-contract/README.md) established its legacy gateway POST XML, response attributes and six displayed forecasts. The actual isolated workflow now verifies that the original parser renders those responses, switches between two cities, returns through Home and converts Fahrenheit to Celsius. The maintained guest implementation is documented in [WEATHER_GATEWAY.md](../tools/legacy_services/guest_http/WEATHER_GATEWAY.md).

The remaining Weather gates are city search/addition and persistence of fetched data and service behavior across an ordinary save/reload. Stocks quote display passes separately; chart and symbol-search protocols remain unqualified. Mail, Maps and YouTube require their own service contracts and original-app acceptance.

## Earlier standalone Weather backend verification

The Python standard-library backend returns fixed Cupertino/New York forecasts and matching location-search results using the original discovered shapes. Ten tests pass, including actual loopback HTTP requests, exact requested identifiers, six forecast rows, demo metadata, empty unsupported-city results, input bounds and malformed requests. These tests are included in native CI. [Run instructions](../tools/legacy_services/README.md) and [retained requests/responses and report](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/weather-service-contract/demo-backend/report.json) are available.

An initial malformed-URL test was rejected by the test client before reaching the server; its failure is retained under `demo-backend/attempt-client-url/`. The corrected test sends that target explicitly and verifies the actual server rejection. All final tests pass. The backend uses a declared numeric demo time convention consistent with original `City::isDay` comparisons; historical time units and actual code32 icon appearance are not claimed.

At this earlier backend-only stage, guest requests, visible demo labeling and original rendering were unqualified. The final isolated gateway now supplies those proofs; city editing and persistence of the combined service remain open. An HTTP header or XML attribute alone does not label data for the user. The backend has no live provider or public deployment.


The original app's existing controls now have a separate [074 persistence gate](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/weather-controls/README.md): units and New York removal survive three exact Save/reloads. This used no backend response. This older control-only gate is separate from the now-passing forecast/temperature workflow; city search/addition and persistence with the combined gateway remain unqualified.


## Independently authored guest execution qualified (2026-09-06)

The first ordinary guest program now runs: original launchd/dyld start our ARMv6 executable, real getpid returns21, real write returns all38 marker bytes, and original exit is entered with0. One bounded private boot and an independent audit preserve every5,178 old file forks; no Apple executable or production file changed. [Milestone086](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/guest-hello/README.md) and [source/build/install/actual-run evidence](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/guest-hello/README.md) document this nonvisual gate. The stop proves exit entry, not process reaping.

The subsequent 087 gate below qualifies a high-port loopback server/client with actual interface/address/route diagnostics and exact HTTP request/response bytes. Program startup precedes complete UI/configuration, so the bounded ordinary readiness check remains part of the service design. This work is independent of the parked wireless-device investigation.


## Own guest loopback HTTP verified (2026-09-06)

[087](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/guest-loopback/README.md) verifies a real local TCP exchange through original libSystem: lo0 was already up, no interface/route mutation was needed, and actual socket operations delivered an exact54-byte HTTP request and113-byte response. The corrected observer binds the actual process, matches full own code at exit and independently compares native/UART payloads. All source/image pins and old5,178 forks remain exact. Initial observer failure remains incomplete and preserved. This is still a nonvisual own-client/server gate; original Safari/Weather had not requested the service at that milestone.

The subsequent 091 gate below extends this to original Safari using a bounded own HTTP daemon and an independently verified running-server snapshot. No host response injection or wireless work is involved.

## Original Safari local demo verified (2026-09-06)

[091](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/guest-safari/README.md) passes one native boot and one 681.902-second browser workflow: actual numeric address entry, local page, link, `hi` GET form, bookmark creation, Home/revisit and real interval autosave/reload. Seven guest requests/responses have exact HTTP framing and successful send/close receipts; the final two occur after reload. Three independent screenshot comparisons match exactly, and final generated execution is unpaused. [Source, tests, complete trace and recording](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/guest-safari/README.md) retain the diagnostic setup and artifact-auditor corrections.

The current result is an isolated imported phone. The [concrete default integration proposal](local-demo-integration.md) covers maintainable own source/build/install tooling, a pristine starting phone with discoverable local bookmark, service lifetime policy, preservation of current USB/MBX settings and default app data, and actual new-phone/offline/old-save regression. Existing saves must remain unchanged; no in-place mounted-image modification or automatic reset is planned. Weather now passes the separate isolated forecast workflow above; this does not qualify default integration or combined-service persistence.
