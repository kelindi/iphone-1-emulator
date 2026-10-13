# Original apps: local services and content opportunities

> Historical recordings and publishing artifacts have been removed from Git history. The summaries below describe the original runs; remaining test reports and screenshots are retained. Historical hash receipts may predate this archival cleanup.

This is a read-only protocol and workflow audit, not app acceptance. Stocks is the strongest next service target after the shared guest transport works: its original request templates and response parsing are now identified. Photos has a separate, partly working content path whose remaining failure is rendering/navigation. No Apple executable, saved phone, production asset or network endpoint was changed or contacted for this audit.

The starting evidence is [app coverage](app-coverage.md), [compatible services](legacy-services.md) and the later workflow milestones linked below. Recognizable original UI is not proof that a service, account or media decoder works.

| Priority | App | Current proof | Smallest useful original-app workflow | Dependency / first unresolved gate |
|---|---|---|---|---|
| 1 | Stocks | Original app now displays four sample quotes through the own gateway in an isolated browser run. Chart remains unfinished. | Clearly labeled deterministic quotes for the existing list, one chart with distinguishable points, symbol search/add/delete, then ordinary autosave/reload. | Route the hardcoded gateway through the guest network; observe exact POST and actual parser/display behavior. |
| 2 | Mail | Original provider screen and Other account form. Original Message framework has IMAP, POP3 and SMTP implementation symbols. | One local demo mailbox: read a plain message, reply into a local sink, delete and reload. | Qualify accepted account/host/port settings, authentication and actual protocol exchange. No real credentials or external delivery needed. |
| 3 | Maps | Original blank map, controls and keyboard. Original GMM framework names a concrete gateway and tile/search/route request classes. | One owned or suitably licensed map area; show it, pan to an adjacent view and reopen it. | Discover the legacy binary transaction and response framing; do not assume modern XYZ tiles. Search/routes are additional protocols. |
| 4 | YouTube | Original catalog UI ends with Cannot connect to YouTube. Original feed and login URL templates exist. | First show one clearly labeled owned clip with title/thumbnail; qualify actual playback separately. | Legacy feed/media-reference schema, supported encoding and original decoding/audio. A catalog alone is not video playback. |
| Separate content track | Photos | Own guest importer and exact payload readbacks pass. Camera Roll recognizes two images and shows the first; next-image rendering fails. | Two distinct images, next/previous, Home/revisit and autosave/reload. | Resolve the retained graphics/navigation failure before expanding import. No network service is required. |
| Later media track | iPod | Original No Content screen. Original media framework exposes library/player interfaces. | One correctly indexed owned DRM-free track: metadata, play/pause/seek and actual audible output, then reload. | Establish genuine library indexing and complete guest-to-host audio. Merely copying an audio file is not an accepted import. |

## Stocks: original protocol evidence

The retained original Stocks executable is 137,740 bytes, SHA-256 `b73fcd49f6128cd57fcc602c05665536193dd639b265c56f5d603812c5002a95`. Read-only HFS extraction used `.context/graphics-speed/rootfs-mbx2d.img`, parent catalog ID 1624. Private extraction receipts, original method addresses and annotated instruction excerpts are in `.context/app-services-opportunities/`; original executable bytes are not added to the repository.

The original gateway template is `http://iphone-wu.apple.com/dgw?imei=%@&apptype=finance`. Requests use POST and `Content-Type: text/xml`. The surrounding request attributes name `api="finance"`, `apiver="1.0.0"`, `app="YGoiPhoneClient"` and an original client version. These are original string templates, not a statement that the historical endpoint still serves requests.

The three query shapes are:

```xml
<query id="%d" timestamp="0" type="getquotes">
  <list><symbol>%@</symbol><!-- repeated requested symbols --></list>
</query>
<query id="%d" timestamp="0" type="getchart">
  <symbol>%@</symbol><range>%@</range>
</query>
<query id="%d" timestamp="0" type="getsymbol">
  <phrase>%@</phrase><count>%d</count>
</query>
```

Each is inside the original XML declaration and `<request ...>` envelope. Query IDs are formatted dynamically; Weather's query IDs or response shape must not be reused blindly. The chart interval method at `0x1115c` selects `1d`, `5d`, `1m`, `3m`, `6m`, `1y` or `2y`, with `1d` as its default.

The response walkers descend through `response/result/list`:

| Request | Original parsing evidence | Fields actually consumed |
|---|---|---|
| Quotes | `StockUpdater processDocument:` at `0x108bc`; quote parser around `0x10b30` | `quote` children contain **text elements** `symbol`, `price`, `change`, `changepercent`, `status`, `link`. Numeric fields are converted by the original parser. |
| Chart | `ChartUpdater processDocument:` at `0x11770`; point parser at `0x1193c` | `point` entries have **attributes** `close` and `timestamp`. Both must be present before a point is appended; values are converted to float/double. |
| Symbol search | `SymbolValidator processDocument:` at `0x16154`; quote parsing in the retained `stocks-symbol-fields.dis` | `quote` children contain **text elements** `name`, `symbol`, `exchange`. |

The distinction is backed by the helpers: `0x16e78` calls `xmlNodeGetContent` and trims text; `0x16eec` first calls `xmlHasProp`, then extracts its content. This is more evidence than matching nearby strings, but it is still static evidence. Historical timestamp units, market-status meaning, complete error handling, minimum useful chart points and visible formatting need an actual controlled response. Do not claim real prices or a working chart from these templates.

A bounded first implementation can return a fixed, visibly disclosed demo dataset matching only the requested symbols, range and query ID. Its acceptance should retain the exact guest request/response bytes, show quote and chart values that can be checked independently, exercise search/add/delete, and verify normal persistence. Unsupported symbols and malformed requests need explicit bounded behavior. Live financial data, market freshness and links to external finance pages are later work.

## Shared transport is an explicit dependency

**Current experiment:** the combined port80 gateway serves original Safari by numeric address. The hosts-file attempt failed, but the subsequent original IPMonitor resolver configuration and own loopback DNS server now allow original Stocks to display sample quotes. The final corrected build also displays Weather forecasts for Cupertino and New York, supports real city swiping and Celsius conversion. Its own HTTP parser needed support for the original Weather `t` parameter. [Final integration evidence](../docs/traces/weather-stocks-local/README.md). Charts and live data remain unfinished. [Retained integration and diagnosis](../docs/traces/combined-app-gateway/README.md).

[087](../docs/traces/guest-loopback/README.md) proves own-client loopback HTTP; [091](../docs/traces/guest-safari/README.md) proves original Safari using a numeric high-port URL in an isolated imported phone. Neither proves that Stocks resolves its hardcoded hostname or reaches port 80. The subsequent [default qualification failure](../docs/traces/guest-http-default/README.md) remains a failure; root is investigating the Safari runtime separately.

Weather and Stocks can share a bounded local gateway transport because their templates use the same host and `/dgw` path, with distinct `apptype` and XML semantics. The hosts entry and LocalOnly registration experiments did not route the app’s actual asynchronous queries. The next isolated candidate is an original IPMonitor supplemental resolver configuration plus our own loopback DNS and port-80 listener; guest resolution, launch order and listener lifetime must be observed. Do not patch Apple client URLs or silently redirect unrelated destinations. Existing saves must remain untouched. Original request framing, declared Content-Length, bounded body collection and complete response transmission matter independently of XML builder tests.

## Other protocol leads and limits

**Mail.** Original MobileMail imports Message.framework and `libssl.0.9.7`. The retained Message framework contains `IMAPConnection`, `IMAPResponse`, `CAPABILITY`, `STARTTLS`, `UID FETCH`, `POP3Connection`, `UIDL` and SMTP interfaces. This supports investigating a conventional local mail server; it does not establish which settings the original account validator accepts or whether modern TLS/authentication works. A local inbox and outbound sink avoid those external-service dependencies initially. Verify actual MIME body display and protocol bytes, not just successful form submission.

**Maps.** Original Maps imports GMM.framework. The latter contains `http://iphone-wu.apple.com/glm/mmap`; Maps references `GMM::TileRequest::addTile`, `TileResponse::getNextTile/getNextImage/getNextColor/getNextTraffic/getNextImageMissing`, plus search and route request classes. The endpoint is a useful lead, but request encoding, tile coordinates, compression, image framing and missing-tile behavior are not yet decoded. Serving a modern tile URL or HTML map would not demonstrate compatibility with this client.

**YouTube.** Follow-up [132](../docs/traces/youtube-service-check/README.md) maps the original catalog parser and two-stage device authentication. [133](../docs/traces/youtube-auth-probe/README.md) now verifies the original signed second request against a matching device certificate and public key. The local probe deliberately refuses the final token; token delivery, catalog and playback remain untested. See the [bounded experiment plan](youtube-services.md). Original templates include `http://%@/feeds/videos/%@`, search/related feeds and standard feeds `recently_featured`, `most_viewed`, `top_rated`. Host strings include `iphone-wu.apple.com` and `gdata.youtube.com`; login templates name `http://dm5.google.com/youtube/accounts/applelogin1` and `applelogin2`. Original `YTVideo` interfaces choose a video reference/profile. These strings establish separate catalog, authentication and media concerns; they do not establish a complete feed schema, usable anonymous behavior, codec support or playback. Start with an owned clip and explicit demo catalog, without attempting real accounts or claiming current YouTube access.

**Photos.** The [original camera-roll contract](../docs/traces/photo-library-contract/README.md) establishes `NSHomeDirectory()/Media/DCIM`, DCF directory/file discovery and JPEG/THM companions. The [actual first workflow](../docs/traces/photo-bootstrap/README.md) supersedes the earlier empty-library observation: own guest filesystem calls install two images, exact readbacks pass, and original Photos sees them. Its next-image failure remains open. The later default MBX correction does not by itself constitute a Photos rerun or persistence acceptance. Camera capture and synced albums are separate.

**iPod.** The inspected MobileMusicPlayer framework has playlist/song-count and player-state interfaces, but this audit did not establish the on-disk library schema or a successful track import. The [audio plan](audio.md) distinguishes verified experimental PCM generation/consumption from unfinished downstream transport and sound. Local content is a useful future fixture, but a service backend cannot substitute for those missing playback gates.

## Reproducibility and next decision

The audit used targeted catalog extraction and method/string inspection, not boots, whole-image string sweeps or contact with retired services. Additional inspected identities are Maps `b4b393f01f17400ad53da0f7def51c13e232fbcb1e59d2e084e3bd3106d8b403`, GMM `f749249e6e7cdccfd459b659c3b4ba09cfb577eb928eb4f9c1f3ab31a080eea5`, MobileMail `8cefdcde5879c36f059e75a49b4294a8b26210eaf1c4e925f9719a5e31890da8`, Message `462de9d57fee4c2350482b3f6d851cd5d0103445d94f2604d418ae8334f68c54`, and YouTube `3b72f4c26177244594687c4843eae7a9c8908c5197233e0dcc6ce3fcf6d15993` (SHA-256).

The shared transport now passes original Weather forecast and Stocks quote workflows. Next, qualify Weather city search and Stocks chart/search and persistence; Mail is the strongest additional service candidate. Keep Photos rendering and iPod audio as distinct content/device work rather than treating every empty app as a missing web service.
