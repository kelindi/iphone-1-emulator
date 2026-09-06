# Compatible services for original apps

The browser roadmap calls for deterministic demo services first, exercised by Apple's original apps through the guest's network stack. Live data is a later adapter. Current networking is still at isolated Wi-Fi firmware transport; no app service has been delivered successfully.

## Weather: first protocol candidate

Read [the original Weather source contract](../docs/traces/weather-service-contract/README.md) before implementation. Static inspection now establishes its legacy gateway POST XML, forecast response attributes, six displayed forecasts, and location-search response text fields. This removes a protocol-format uncertainty, but does not establish guest parser or display success.

The next gate is an actual request and controlled response through guest networking, followed by forecast, units, city editing and persistence verification. Time units, icon mapping, HTTP handling and visible demo labeling remain part of that gate. A standalone deterministic Weather backend is now implemented under `tools/legacy_services/`; it is not yet routed to the browser phone. No UI replacement or Apple executable change has been added.

Stocks, Maps, Mail and YouTube need separate original-protocol discovery and app acceptance. Weather's XML must not be assumed to describe those services.

## Standalone Weather backend verified

The Python standard-library backend returns fixed Cupertino/New York forecasts and matching location-search results using the original discovered shapes. Ten tests pass, including actual loopback HTTP requests, exact requested identifiers, six forecast rows, demo metadata, empty unsupported-city results, input bounds and malformed requests. These tests are included in native CI. [Run instructions](../tools/legacy_services/README.md) and [retained requests/responses and report](../docs/traces/weather-service-contract/demo-backend/report.json) are available.

An initial malformed-URL test was rejected by the test client before reaching the server; its failure is retained under `demo-backend/attempt-client-url/`. The corrected test sends that target explicitly and verifies the actual server rejection. All final tests pass. The backend uses a declared numeric demo time convention consistent with original `City::isDay` comparisons; historical time units and actual code32 icon appearance are not claimed.

Integration still requires a genuine guest network request/response, visible demo mode in the surrounding product, original forecast rendering/units, city editing and persistence. An HTTP header or XML attribute alone does not label data for the user. The backend has no live provider or public deployment.


The original app's existing controls now have a separate [074 persistence gate](../tweets/074_weather_controls_persist.md): units and New York removal survive three exact Save/reloads. This used no backend response. City search/addition, forecast display and actual temperature conversion still require network integration.
