# Original Weather demo backend

This is a deterministic, standalone backend for the original app's discovered XML protocol. **The browser phone is not connected to it yet.** These request fixtures substitute demo version strings into original format templates; they are not guest packet captures. No live weather data is fetched.

Run from the repository root using Python 3.9 or later:

```sh
python3 -m unittest discover -s tools/legacy_services -p 'test_*.py' -v
python3 -m tools.legacy_services.server --port 8180
```

The server listens only on loopback. In a second terminal:

```sh
curl -H 'Content-Type: text/xml' --data-binary @tools/legacy_services/fixtures/forecast-request.xml http://127.0.0.1:8180/dgw
```

`weather.respond(bytes)` is the transport-independent entry point for a future gateway. It accepts the original `getforecastbylocationid` and `getlocationid` request shapes for Cupertino and New York, emits attribute-based forecasts with six days, and emits text-based city-search results. Unknown identifiers/searches produce empty lists; unsupported requests fail. It does not forward requests to the historical Apple hostname, store device identifiers, or supply a guest TCP/IP interface.

Every response marks the fixed dataset as demo in XML and HTTP metadata. **That metadata is not a user-visible demo label.** Before product integration, the surrounding phone controls must visibly identify demo service mode. Forecast values are invented constants, not current or historical observations. Time fields use a declared minutes-since-midnight convention consistent with the original app's direct integer comparisons; this does not establish the old server's units. Icon code32 and actual day/night rendering still require the real guest gate.

The loopback HTTP harness bounds request size to16 KiB, city requests to16 entries and text fields to128 characters. It is an experimental fixture server, not a deployed public gateway. It has no provider credentials, accounts or persistence. Original protocol details and remaining acceptance are in [the source contract](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/weather-service-contract/README.md) and [service plan](../../plans/legacy-services.md).

Tests verify deterministic values, query/response identity, forecast versus search structure, unsupported input, empty results and actual loopback HTTP delivery. They do **not** execute Apple's XML parser or prove forecast display, unit conversion, city editing or saved city choices. Those remain acceptance gates after guest networking and demo mode are integrated.
