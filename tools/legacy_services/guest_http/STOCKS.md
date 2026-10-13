# Original Stocks quote demo module

> Historical recordings and publishing artifacts have been removed from Git history. The summaries below describe the original runs; remaining test reports and screenshots are retained. Historical hash receipts may predate this archival cleanup.

**2026-09-08 isolated guest result:** original Stocks displays AAPL 120.00,
GOOG 500.00, YHOO 25.00 and T 40.00 through the same corrected gateway that
serves original Weather. Actual DNS, finance POST and complete quote response
bytes accompany the original framebuffer evidence.
[131](../../../docs/traces/weather-stocks-local/README.md) and
[retained trace](../../../docs/traces/weather-stocks-local/README.md).
These are visibly labeled fixed samples. Charts, symbol search, live data,
default distribution and persistence of this combined build remain unqualified;
the chart HTTP400 is retained.

`own_stocks_request` accepts the original POST-body envelope with `api="finance"`,
`apiver="1.0.0"`, and a single `getquotes` query containing a symbol list. Query
IDs are decimal0..2147483647 and echoed exactly. At most16 symbols of128 decoded
ASCII bytes each are accepted. XML input is bounded at16384 bytes. The bounded
dialect follows the Weather parser's declared limitations; no general XML,
external entities, charts, symbol search or live market data is provided.

The original default roster is `^DJI`, `AAPL`, `GOOG`, `YHOO`, `T`. Responses
preserve requested order, remove duplicate recognized symbols and omit unknown
symbols. Each quote has text elements `symbol`, `price`, `change`,
`changepercent`, `status`, `link`. Numeric values are deliberately fixed demo
examples. `status` is numeric0, which the original parser converts to a boolean;
no historical market-session semantics are claimed. Links point to the local
landing page. XML demo metadata still requires visible surrounding UI labeling.

Protocol provenance: `plans/app-services-opportunities.md` and retained
`.context/app-services-opportunities/stocks-quote-fields.dis`; original Stocks
SHA256 `b73fcd49f6128cd57fcc602c05665536193dd639b265c56f5d603812c5002a95`.
The original module unit-test request fixtures are reconstructed from original
templates. The separate functional archive now retains genuine guest requests
and verifies quote parsing/display in original Stocks; the four displayed rows
do not establish every supported symbol or feature.

Run `python3 tools/legacy_services/guest_http/test_stocks.py`. Five tests compile
and call the actual C module, covering all default quote fields, order/dedup,
escaping, dynamic IDs, malformed/unsupported shapes, every truncated prefix and
exact response bounds. An ARMv6 object compiles with the maintained flags; its
only helper dependency is `memset`, supplied by own `weather_support.c`.

The gateway must explicitly route `/dgw?…&apptype=finance` and collect its full
bounded Content-Length body before calling this module. The existing Weather
hostname is shared with Weather. The successful isolated route uses the original
configd supplemental resolver entry and own loopback DNS; the hosts-only attempt
failed. Actual requests and quote rendering now pass, while charts and search
remain separate, unqualified protocols.
