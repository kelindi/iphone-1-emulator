# Compatible services for original apps

**2026-09-06 qualification result:** the maintained quiet daemon boots from the actual pristine accelerated-default image and original Safari renders its landing page and Next. The first form stalls with guest graphics hangs; the final process list also lacks the daemon, with exit cause unobserved. Bookmark/reload/final Home remain unfinished. No default promotion or retry. [093 failure evidence](../tweets/093_default_local_demo_qualification_failure.md).

The browser roadmap calls for deterministic demo services first, exercised by Apple's original apps through the guest's network stack. Original Safari now loads a clearly labeled local demo through an independently authored daemon and the guest's real loopback sockets. Links, a form, bookmarks and ordinary autosave/reload pass in one isolated imported phone. [091 recording and evidence](../tweets/091_safari_local_demo.md). The demo is not in the default phone yet; Weather, external networking and live data remain separate work. This path does not require resuming the parked wireless-device investigation.

## Weather: first protocol candidate

Read [the original Weather source contract](../docs/traces/weather-service-contract/README.md) before implementation. Static inspection now establishes its legacy gateway POST XML, forecast response attributes, six displayed forecasts, and location-search response text fields. This removes a protocol-format uncertainty, but does not establish guest parser or display success.

The next gate is an actual request and controlled response through guest networking, followed by forecast, units, city editing and persistence verification. Time units, icon mapping, HTTP handling and visible demo labeling remain part of that gate. A standalone deterministic Weather backend is now implemented under `tools/legacy_services/`; it is not yet routed to the browser phone. No UI replacement or Apple executable change has been added.

Stocks, Maps, Mail and YouTube need separate original-protocol discovery and app acceptance. Weather's XML must not be assumed to describe those services.

## Standalone Weather backend verified

The Python standard-library backend returns fixed Cupertino/New York forecasts and matching location-search results using the original discovered shapes. Ten tests pass, including actual loopback HTTP requests, exact requested identifiers, six forecast rows, demo metadata, empty unsupported-city results, input bounds and malformed requests. These tests are included in native CI. [Run instructions](../tools/legacy_services/README.md) and [retained requests/responses and report](../docs/traces/weather-service-contract/demo-backend/report.json) are available.

An initial malformed-URL test was rejected by the test client before reaching the server; its failure is retained under `demo-backend/attempt-client-url/`. The corrected test sends that target explicitly and verifies the actual server rejection. All final tests pass. The backend uses a declared numeric demo time convention consistent with original `City::isDay` comparisons; historical time units and actual code32 icon appearance are not claimed.

Integration still requires a genuine guest network request/response, visible demo mode in the surrounding product, original forecast rendering/units, city editing and persistence. An HTTP header or XML attribute alone does not label data for the user. The backend has no live provider or public deployment.


The original app's existing controls now have a separate [074 persistence gate](../tweets/074_weather_controls_persist.md): units and New York removal survive three exact Save/reloads. This used no backend response. City search/addition, forecast display and actual temperature conversion still require network integration.


## Independently authored guest execution qualified (2026-09-06)

The first ordinary guest program now runs: original launchd/dyld start our ARMv6 executable, real getpid returns21, real write returns all38 marker bytes, and original exit is entered with0. One bounded private boot and an independent audit preserve every5,178 old file forks; no Apple executable or production file changed. [Milestone086](../tweets/086_own_guest_program.md) and [source/build/install/actual-run evidence](../docs/traces/guest-hello/README.md) document this nonvisual gate. The stop proves exit entry, not process reaping.

The subsequent 087 gate below qualifies a high-port loopback server/client with actual interface/address/route diagnostics and exact HTTP request/response bytes. Program startup precedes complete UI/configuration, so the bounded ordinary readiness check remains part of the service design. This work is independent of the parked wireless-device investigation.


## Own guest loopback HTTP verified (2026-09-06)

[087](../tweets/087_guest_loopback_http.md) verifies a real local TCP exchange through original libSystem: lo0 was already up, no interface/route mutation was needed, and actual socket operations delivered an exact54-byte HTTP request and113-byte response. The corrected observer binds the actual process, matches full own code at exit and independently compares native/UART payloads. All source/image pins and old5,178 forks remain exact. Initial observer failure remains incomplete and preserved. This is still a nonvisual own-client/server gate; original Safari/Weather have not requested the service.

The subsequent 091 gate below extends this to original Safari using a bounded own HTTP daemon and an independently verified running-server snapshot. No host response injection or wireless work is involved.

## Original Safari local demo verified (2026-09-06)

[091](../tweets/091_safari_local_demo.md) passes one native boot and one 681.902-second browser workflow: actual numeric address entry, local page, link, `hi` GET form, bookmark creation, Home/revisit and real interval autosave/reload. Seven guest requests/responses have exact HTTP framing and successful send/close receipts; the final two occur after reload. Three independent screenshot comparisons match exactly, and final generated execution is unpaused. [Source, tests, complete trace and recording](../docs/traces/guest-safari/README.md) retain the diagnostic setup and artifact-auditor corrections.

The current result is an isolated imported phone. The [concrete default integration proposal](local-demo-integration.md) covers maintainable own source/build/install tooling, a pristine starting phone with discoverable local bookmark, service lifetime policy, preservation of current USB/MBX settings and default app data, and actual new-phone/offline/old-save regression. Existing saves must remain unchanged; no in-place mounted-image modification or automatic reset is planned. Weather's compatible response must still reach the original Weather parser and render before a forecast claim.
