# Original Weather service contract: static discovery

2026-09-06. This is a source-derived protocol proposal, **not a successful Weather update**. No guest request was captured, no response was delivered, and no obsolete public endpoint was contacted. The production phone still reports Update failed. This work prepares the deterministic demo service in the browser roadmap while guest networking is being implemented.

The input is the unchanged OS 1.0 `Weather.app/Weather`, extracted read-only from the existing MBX work image: 104,648 bytes, SHA-256 `bd489dac1952be0a595bd6592b8a3ea29489bb4880319f8a288cf2944843d4eb`. Its original Objective-C method lists, Mach-O symbol stubs and original instructions ground the observations below. The Apple executable is not redistributed here. Addresses are original application virtual addresses, not kernel addresses or file offsets.

## Request construction

`WeatherUpdater::updateWeatherForCity:` at `0xe014` constructs an HTTP POST to the original template `http://iphone-wu.apple.com/dgw?imei=%@&apptype=weather&t=%u`, with UTF-8 XML and `Content-Type: text/xml`. These are format placeholders, not a captured device identifier. The method sets a versioned Apple iPhone Weather User-Agent. Exact format strings and the request method are retained in `weather-strings.json` and `weather-request-dis.txt`.

The forecast query has `id="30" timestamp="0" type="getforecastbylocationid"`, containing `<list><id>…</id></list><language>en</language><unit>f</unit>`. The surrounding `<request>` carries device/application/API version attributes. The separately identified location-search template uses query id `3`, type `getlocationid`, and a `<phrase>` text element. This is the app's legacy gateway XML, not a claim that modern weather RSS is compatible.

The original default location identifiers include `USCA0273|12797509` for Cupertino and `USNY0996|2459115` for New York. A future bounded demo can support these exact identifiers and add search results whose returned identifier is reused unchanged in forecast lookup.

## Forecast response consumption

`WeatherUpdater::processDocument:` at `0xd7dc` walks element children through `response/result/list/item`. It collects each item's `location`, `astronomy`, `condition`, optional `link`, and the **first six** `forecast` elements. Its storage stops collecting after six; extra forecasts are ignored. The count check requires six collected forecasts, so fewer than six prevents this item's successful update.

At `0xda34–0xdae0`, it also requires a location node whose `id` **attribute** equals one of the pending City's `zip` strings, a condition node, and an astronomy node. The following fields are consumed:

| Node | Source value | Original destination |
| --- | --- | --- |
| `location` | `id` attribute | Pending city identity match |
| `location` | `city` attribute | City name |
| `condition` | `temp` attribute | Temperature string |
| `condition` | `code` attribute, integer conversion | Main weather icon |
| `condition` | `time` attribute, integer conversion | Observation time |
| Each of six `forecast` nodes | `high`, `low`, `code` attributes | Forecast temperatures and icon |
| `astronomy` | `sunrise`, `sunset` attributes, integer conversion | Sunrise/sunset fields |
| `astronomy` | `moonfacevisible` float and `moonphase` integer | Transformed moon icon index |
| Optional `link` | Element text | City's link |

The attribute helper at `0xd458` uses `xmlHasProp` then `xmlNodeGetContent`; the text helper at `0xd410` uses `xmlNodeGetContent` directly. Both create a UTF-8 NSString and trim it. `_timeStringToUnsigned:` at `0xd7bc` simply calls `intValue`: it does not parse an RSS timestamp, AM/PM time, or ISO date. **The intended time units/timezone and full icon mapping remain to be established from downstream consumers.** The downstream `City::isDay` at `0xcafc` compares these stored unsigned values directly: observation must be strictly greater than sunrise and strictly less than sunset. Stale data and the all-zero case have separate daytime defaults. Therefore a deterministic demo may choose a declared, consistently ordered numeric convention; a claim about the historical time unit or a live-data mapping needs more evidence. `weather-day-dis.txt` retains the actual comparisons.

Forecast weekday names are generated from the guest's `NSCalendarDate` and advanced one day per displayed row; this path does not consume a service-provided weekday label. At `0xdea4`, the accepted item calls `finishedUpdating:0` and removes its city from the pending array. These are static control-flow observations; the original parser has not run on a proposed response yet.

## Location-search response consumption

`WeatherValidator::processDocument:` at `0xf4e4` also walks `response/result/list/item`. It reads **element text** for `id`, `city`, `region`, `regionname`, `country`, and `countryname`. For `country == "US"`, display state comes from `region`. Otherwise it uses `regionname(countryname)` if regionname exists, or countryname alone. It constructs a `ValidatorCity` with city/state/zip and delivers a nonempty result array through `didValidateLocation:`; an empty array follows the failure path.

The inspected item path does not explicitly require every text field before creating its object. This is not permission to omit fields in a service: provide complete values and verify real guest behavior, especially malformed or empty identifiers. The forecast response's location attributes and search response's text elements are deliberately different.

## First implementation and acceptance sequence

1. Once the guest network works, capture the original request and HTTP status/header handling against a controlled gateway. Route this compatibility service through the guest network; do not replace the app UI or alter its executable.
2. Serve one complete, clearly identified deterministic demo response for each default city, with exactly six forecast elements and identifiers matching the actual request. Ground time/icon values before claiming correct day/night behavior. Keep any live provider adapter separate until its mapping is verified.
3. Verify actual guest parser success, forecast display, Fahrenheit/Celsius switching, search/add/remove city, and persistence across Save/reload. Retain a recording and packet/response evidence. Include wrong identity, missing forecast, empty search and service-unavailable cases.
4. Only then add broader data or provider integration. Demo values must remain labeled as demo in the surrounding product experience; never describe them as current forecasts.

## Evidence and reproduction

`weather-methods.json` records 18 original Objective-C classes and their method addresses. The inspector reads original Mach-O segment/section mappings, symbol/indirect-symbol tables, selector pointers and CFStrings. It requires Python and Capstone, plus the original executable supplied locally:

```sh
WEATHER_BINARY=/path/to/original/Weather python3 docs/traces/weather-service-contract/disassemble.py d7dc e014
WEATHER_BINARY=/path/to/original/Weather python3 docs/traces/weather-service-contract/disassemble.py f4e4 f9c0
```

The inspector pins the original binary hash. In this workspace Capstone is available with `PYTHONPATH=.context/graphics-speed/python`. The retained linear disassembly includes literal tables decoded as apparent instructions; only followed control-flow ranges and resolved literal data support the descriptions above. The XML-helper excerpt starts inside an earlier function; its first lines do not establish a function entry.

`attempt-loose-annotation/` preserves an inspection error: an early annotation helper skipped two-character strings and guessed CFStrings from adjacent words, mislabeling the forecast identity key `id` as `link`. The corrected inspector accepts short strings only within `__cstring`, and CFStrings only at aligned records within `__cfstring` with matching declared lengths. Original instruction bytes never changed. The corrected identity load at `0xda40` resolves to the original `id` pointer. This was a tooling correction, not a guest or service failure.


## Subsequent standalone implementation

A deterministic [Weather demo backend](../../../tools/legacy_services/README.md) now implements these bounded request/response shapes. [Ten final tests](demo-backend/test.log) pass, including actual loopback HTTP delivery. The retained fixture requests substitute demo versions into original format strings; they are not guest captures. `demo-backend/attempt-client-url/` preserves a test-client URL-validation failure, corrected by delivering the malformed request explicitly to the server. This does not change the original guest acceptance status: no forecast has been delivered through the phone, and no visual milestone is claimed.
