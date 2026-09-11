# Original Weather and Stocks: shared local-service acceptance

The final isolated candidate passes the declared Weather forecast and Stocks quote browser gates with no browser errors. Original Weather displays Cupertino72F and six forecast rows, swipes to New York64F, refreshes after Home/revisit, and converts Cupertino to22C through its original settings control. Original Stocks on the same build displays AAPL120.00, GOOG500.00, YHOO25.00 and T40.00.

`browser-report.json` retains explicit screenshot assertions, actual served asset hashes, actions and the final diagnostic export. `report.json`, `service.log`, numbered wire files and `wire-report.json` independently recover the final phone's DNS and HTTP traffic, validate exact block lengths and Content-Length, and match the two cities/six-day responses and four quote fields. The unsupported Stocks chart receives HTTP400. The exported phone SHA256 is `cae763b95e4ef87e7915b36a520a0d8f5f5d924a937889b28aaa7339643e97c4`.

The tested own executable SHA256 is `915268fb0e2d6192ea650650a4d1b353111851259fb17d03ce4898eaf4b4d5a8`. `build-runtime-report.json` retains the original import, standalone environment and r9 checks. This build includes strict DNS label validation and Weather-only bounded unsigned `t` support. `weather-framing-recovery.json` preserves the earlier exact request recovery and rejection cause. Six targeted CTest contracts pass, including actual-request framing, structured forecast fields and socket lifecycle checks.

`install-report.json` audits the new image: two own files are added, the existing IPMonitor resolver plist receives one own supplemental dictionary, and every other original file fork remains unchanged. No Apple executable, emulator CPU or UI is patched by this service work. Native boot and all browser qualification artifacts are under `.context/app-services-current/weather-t-boot/` and `weather-t-browser/`.

## Limits and use

This is fixed sample weather/quote data, visibly disclosed by the frozen test site's banner. It is not current weather or financial information. The default web manifest and existing saved phones have not been promoted to this candidate. City search/add/delete, Stocks charts/search, live feeds, arbitrary Safari websites and save/reload lifetime on this final build remain separate gates. Earlier Safari gateway persistence is documented independently in milestone129.

The final diagnostic phone is `.context/app-services-current/weather-t-browser/final-home.snap`; the clean pre-interaction test checkpoint is `.context/app-services-current/weather-t-boot/home.snap`. Use the frozen `.context/app-services-current/services-site` with its sample-data banner when reproducing the demo. Do not replace an existing saved phone without first exporting it.

[Recording and milestone](../../../tweets/131_weather_stocks_local.md). Earlier failures are retained in the combined gateway and Stocks milestone evidence rather than relabeled as passes.
