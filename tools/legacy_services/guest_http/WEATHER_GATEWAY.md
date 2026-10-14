# Combined local Safari, Weather and Stocks gateway transport

> Historical recordings and publishing artifacts have been removed from Git history. The summaries below describe the original runs; remaining test reports and screenshots are retained. Historical hash receipts may predate this archival cleanup.

**2026-09-08 isolated guest qualification passed:** original Weather displays
Cupertino 72°F and six days, swipes to New York 64°F, returns through Home, and
converts Cupertino to 22°C using its own unit control. Original Stocks displays
AAPL 120.00, GOOG 500.00, YHOO 25.00 and T 40.00 on the same corrected gateway.
The retained run passed with no harness errors; actual DNS and complete HTTP
exchanges accompany the original framebuffer recording.
[131](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/weather-stocks-local/README.md) and
[source, wire audit and recording](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/weather-stocks-local/README.md).
All displayed values are fixed, visibly labeled sample data. Charts, symbol or
city search/addition, live data, default distribution and persistence of this
combined build remain unqualified; the chart HTTP400 is retained.

`weather_server.c` uses the existing ordinary libSystem socket/readiness pattern
on **127.0.0.1:80**. It now delegates known Safari GET routes to the unchanged
`http.c` response builder, alongside Weather and Stocks POST. The separate old Safari
port18081 executable is not needed in a newly provisioned combined image.
The qualified isolated gateway uses original configd/mDNSResponder with an owned
supplemental resolver entry and its own loopback UDP DNS service. The earlier
hosts-only routing attempt failed and remains preserved.

Safari opens `http://127.0.0.1/` (implicit port80). GET `/`, `/next` and
`/echo?message=...` require numeric Host `127.0.0.1`, at most2048 header bytes,
no body and no duplicate/ambiguous framing. Optional Content-Length:0 is accepted.
All actual HTML, form decoding/escaping and response bytes come from `http.c`;
the wrapper does not rewrite them. Other GET paths are refused. This is distinct
from the Weather hostname and POST route. A combined program/plist avoids the
catalog-capacity failure encountered when adding two separate services.

Finance requests use `/dgw?imei=...&apptype=finance`, the same original gateway
Host and XML framing. Only `api="finance"` / `getquotes` is supported by
`own_stocks_request`; chart and symbol search return HTTP400. Weather requests
must select `apptype=weather` (or the retained bare `/dgw` default). The respective
adapter validates its own XML API, so neither service accepts the other's body.
Quotes are fixed demo values for the original five-symbol roster, not live or
historical market observations. Dynamic numeric query IDs remain in the response.

`weather_http.c` accepts POST `/dgw` or the original
`/dgw?imei=...&apptype=weather` form, requires the original gateway Host,
`text/xml` (optionally UTF-8 charset), and exactly one positive Content-Length.
The IMEI query value is opaque. Headers are capped at4096bytes and XML at16384.
Transfer-Encoding, Expect, duplicate framing headers, wrong service routes and
already-buffered bytes beyond the declared body are rejected. A connection is
closed after one response; later pipelined requests are never processed.

The collector waits for the complete declared body before calling
`own_weather_request`. Valid supported XML gets HTTP200 and the original-shape
demo XML. Invalid/unsupported XML gets HTTP400 with an empty body. Invalid HTTP
framing or incomplete/aborted input closes without building a response. Responses
have an exact Content-Length, Connection:close, no-store and demo metadata.
Metadata does not satisfy the required visible demo label.

Each request has at most512 read calls and four readiness waits; each wait has
at most20 polls of100ms. Sending has at most2048 write calls and four analogous
waits. EOF, short I/O, EINTR, EAGAIN, disconnects and non-loopback peers are handled
without losing socket ownership. These are finite operation/poll bounds, not a
measured guest wall-time guarantee. The single-client listener stays alive until
an actual listener failure; only its host-test seam terminates a healthy loop.

Quiet builds omit full request/response diagnostics. Diagnostic builds retain
exact wire bodies, including any identifier present in the request, for the
isolated protocol gate. Do not treat those logs as a user-facing service mode.
The supplied launchd plist starts `/usr/libexec/iphone1-weather-http` separately;
the installer and root own copy-only routing/provisioning and boot qualification.

## Build and evidence

Use `build.py` with custom source files `weather_server.c`, `weather_http.c`,
`weather_request.c`, `weather.c`, `weather_support.c`, `http.c`, `stocks.c`; pin headers
`weather_http.h`, `weather_request.h`, `weather.h`, `http.h`, `stocks.h`. The executable name is
`iphone1-weather-http`, and the expected original imports are the unchanged
`macho_audit.EXPECTED_IMPORTS` set. The existing build API accepts these lists via
`sources`, `headers`, `expected_imports`, and `name`; its CLI exposes corresponding
`--source`, `--header`, `--import-symbol`, and `--name` arguments. Use a new output
directory for every attempt.

Two genuine link failures are retained under `.context/weather-gateway/`:
`build-quiet` required unavailable compiler division support; the parser owner
replaced its base10/base16 overflow expression with equivalent constant bounds.
`build-quiet-v2` then exposed compiler-emitted memset with no freestanding startup
stub. `weather_support.c` supplies an independently authored exact byte-loop
definition, with no added library dependency. It is not copied SDK code.

The historical POST-only quiet build at `build-quiet-v3/iphone1-weather-http` is26004bytes,
SHA-256 `33c1e097f4aa83d3509ee03b01a644a0a677baf6eb54c0940db40460bf786ef1`.
The diagnostic build at `build-diagnostic-v3/iphone1-weather-http` is26024bytes,
SHA-256 `98526f93b9379b50055aa033b2ec4ae835dfcda401d87b1f0b1979165daa4d7f`.
Both `build.json` receipts pass ARMv6 Mach-O auditing with exactly16 eager original
libSystem imports. This audit does not run the executable.

The earlier Safari/Weather combined builds are under `.context/weather-gateway/combined/`:
quiet `build-quiet/iphone1-weather-http` is30244bytes, SHA-256
`0fd2885ee99fd744325befbf5c5c23d602248450b559dd2763395f6ee125ed12`;
diagnostic `build-diagnostic/iphone1-weather-http` is34360bytes, SHA-256
`8ba7be5b64db3b3a72e5b359b75e12183838f20747f2fcc77a3b8f971ea737e1`.
Both audits retain the same16 eager original imports. The POST-only source was
preserved separately under `post-only-source/` before the combined change.

The final Safari/Weather/Stocks diagnostic build is
`.context/weather-gateway/combined-stocks/build-diagnostic/iphone1-weather-http`,
38604bytes, SHA-256
`1a9dd53bf5cc054b0278df41c16d833dfccd294b387295eea5f771c0cd16ec83`.
Its `build.json` audit passes the same16 imports and pins all compiled sources.
No final ARM quiet variant was built; quiet versus diagnostic socket behavior is
covered by native tests. The previous combined sources remain in `combined/source/`.

The ARMv6 `-fstack-usage` audit records53416bytes for the inlined `start`,4000
for the Weather parser,1588 for its leaf reader and920 for its attribute reader.
A nested own-code path therefore approaches60KiB before original-library stack
use. This is static compiler evidence, not a observed maximum or an established
guest stack limit. No evidence here ties stack size to the earlier, much smaller
Safari daemon's SIGBUS; no speculative stack workaround was applied. Per-function
receipts are retained in `combined-stocks/stack/`.

## Focused contracts

The separately built DNS successor at `.context/weather-gateway/combined-dns/`
adds UDP `127.0.0.1:53` beside HTTP port 80. It answers only
`iphone-wu.apple.com` A with `127.0.0.1` (TTL 60); other types for that name
receive NODATA and unknown names receive NXDOMAIN. It performs no upstream
lookup. Requests admit one uncompressed IN question and at most one bounded
EDNS0 OPT; malformed framing, oversize packets and non-loopback peers are
dropped. Wire layout follows [RFC 1035](https://www.rfc-editor.org/rfc/rfc1035)
and [EDNS OPT layout](https://www.rfc-editor.org/rfc/rfc6891).

Each readiness turn handles at most one DNS datagram and one HTTP client.
HTTP keeps its existing finite I/O and wait bounds; an incomplete HTTP request
can delay subsequent DNS turns within those bounds. Datagram sends are atomic,
with at most four interrupted-call attempts and no partial-datagram replay.
Diagnostic logs retain received and proposed response bytes; `DNS_RESPONSE_SENT`
records a complete successful send. Both listener descriptors close on failure.

This successor includes the owned empty environment and reserves original
libSystem's r9 pthread register in every ARM translation unit. The new diagnostic
binary is 42924 bytes, SHA-256
`54d2b12e11d024fba18bb8f340da43813a6186de63f890739b4e3dd2748212d5`,
with 18 exact imports. Native checks pass 24 packet/HTTP tests and 7092 socket
assertions per logging mode. The previous HTTP wire hash remains unchanged.
The raw all-word r9 audit rejects a literal pool; an additional audit uses the
exact compiler objects' `LC_DATA_IN_CODE` metadata and verifies all 7018 remaining
linked text words have no r9 writes. Receipts and prior source are retained in
the successor directory. Original-app DNS integration still requires a guest run
and appropriate configd resolver configuration; these checks do not prove it.

Post-build review found that flattened name comparison could accept a literal
dot within a DNS label. The maintained parser now also requires label lengths
9/5/3; both crafted alternatives are negative tests. A separate corrected build,
`combined-dns/build-diagnostic-labels/iphone1-weather-http`, is 42968 bytes,
SHA-256 `d122fb9bad757916ca533d62016fcca60dec23dc8a917b373df90c6ad07c575f`.
The earlier binary and source remain preserved for the already-started guest run.
The corrected build passes the same native checks and code-metadata r9 audit;
it has not been guest-qualified here.

The subsequent Weather failure was recovered from the real saved phone: two
identical 299-byte headers included `&apptype=weather&t=3`, matching the original
Weather URL template. The gateway had rejected `t` before reading the 355-byte
body. The maintained route now accepts one optional decimal `t` in
`0..4294967295` on Weather routes only, rejecting duplicates, signs, overflow,
unknown fields and finance crossover. Diagnostic builds retain bounded rejected
request bytes. The fixture pair `../fixtures/weather-original-t-{header.http,body.xml}`
uses the recovered header layout with an equal-length synthetic IMEI and the
actual recovered XML body. All 654 incomplete prefixes wait; the complete request
produces one Cupertino forecast with matching location ID and six days.

The separate labels-plus-`t` build is
`.context/weather-gateway/combined-weather-t/build-diagnostic/iphone1-weather-http`,
42968 bytes, SHA-256
`915268fb0e2d6192ea650650a4d1b353111851259fb17d03ce4898eaf4b4d5a8`.
Six targeted CTest entries pass, including 7134 socket checks per logging mode
with unchanged HTTP wire hash. Code-metadata audit checks 7095 linked instruction
words without r9 writes; the exported environment remains valid. Prior binaries,
captured failure, recovered bytes and source receipts remain preserved. This
build passed the isolated original-app qualification above. The final diagnostic
export is 254078946 bytes, SHA-256
`cae763b95e4ef87e7915b36a520a0d8f5f5d924a937889b28aaa7339643e97c4`.
Export success does not establish a reload/persistence gate.

```sh
python3 tools/legacy_services/guest_http/test_weather_http.py
cc -std=c11 -O2 tools/legacy_services/guest_http/test_weather_server.c \
  tools/legacy_services/guest_http/weather_http.c \
  tools/legacy_services/guest_http/weather_request.c \
  tools/legacy_services/guest_http/weather.c \
  tools/legacy_services/guest_http/http.c \
  tools/legacy_services/guest_http/stocks.c \
  tools/legacy_services/guest_http/dns_packet.c -o /tmp/weather-server-test
/tmp/weather-server-test
```

Seven Python tests load the actual C framing/builders: all incomplete prefixes of
both retained request fixtures, exact complete bodies, independent Python XML
response comparison, malformed/ambiguous framing, caps and freestanding memset
span/return identity, all three Safari routes including escaped/malformed form
values, exact delegated response identity, fixed Stocks quotes/query IDs and
rejection of GET/POST and finance/Weather crossover.
The mocked persistent socket test passes5447 assertions in
both quiet and `-DOWN_HTTP_DIAGNOSTICS=1` builds, with identical transmitted-byte
hash `9aa294eead140b3b`. It includes all three Safari routes and fragmented Weather forecast/search and Stocks quote POST bodies,
partial sends, EOF before headers and midway through a body, errors, timeout,
listener lifetime and peer refusal. These are host mocks, not guest TCP evidence.
Current logs are `http-tests.log`, `server-tests.log` and
`server-diagnostic-tests.log` under `.context/weather-gateway/combined-stocks/`.

## Actual integration status

The final `915268…` build passes isolated Weather forecast, two-city navigation,
unit conversion and four Stocks quotes. Its wire audit retains four complete
forecast responses with six days each and one four-quote response. A chart
request still receives HTTP400; charts, search, live data, persistence of this
build and default distribution are unqualified.

The earlier hosts-only attempt failed before Weather HTTP: its asynchronous
resolver skipped the monitored hosts-file path. Later DNS routing reached the
gateway but exposed the unsupported original `t` parameter. Both failures remain
preserved in the [earlier evidence](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/combined-app-gateway/README.md)
and [final recovery archive](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/weather-stocks-local/README.md).


## Supplemental resolver candidate

The newer combined gateway also owns UDP127.0.0.1:53. Its bounded DNS packet
handler answers only `iphone-wu.apple.com` with A127.0.0.1, gives no address data
for other types of that known name, and NXDOMAIN for unknown names. It does not
forward DNS or HTTP to the Internet. This remains a disclosed sample-data demo.
The server polls DNS and HTTP, handles one datagram per readiness turn, checks
loopback peers, and keeps the existing bounded HTTP request/response budgets.

Use `install_services.py --resolvers-sha256 <original-sha>` instead of
`--hosts-sha256` to append the owned unicast dictionary to the original
`/System/Library/SystemConfiguration/IPMonitor.bundle/Resolvers.plist`.
The two options are mutually exclusive. All other required source/program/plist
hash pins are unchanged. The installer copies to a new image, preserves the
old resolver entry and XML bytes, and audits every other existing file fork.
Historical hosts-mode installation remains available to reproduce its failure.

The original configd resource consumer and resolver serialization are verified
in [the retained contract](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/combined-app-gateway/dns-runtime/resolver-config-contract.json).
The final isolated phone now demonstrates this route through actual original
Weather/Stocks requests and displayed sample responses. The config contract
alone remains a static claim; the separate guest wire and frame evidence above
establishes the functional result.

The new executable also includes our fixed-config standalone environment and
reserves the original OS thread register r9 during compilation. These fix
own-program ABI prerequisites discovered by the diagnostic, without modifying
Apple binaries or the emulator CPU.
