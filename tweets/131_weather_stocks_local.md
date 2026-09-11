# 131 — Weather and Stocks share a working local service

## Tweet idea

The original iPhone Weather app is working again: sunny Cupertino, a New York forecast, swiping between cities, and Fahrenheit-to-Celsius conversion—all drawn by the real 2007 app. Stocks uses the same tiny local service. The data is a disclosed demo dataset.

## What you're seeing

[Recording](videos/weather_stocks_local.mp4) · [Thumbnail](thumbnails/weather_stocks_local.jpg). At about00:55, the opening app section shows Cupertino72°F with six forecast rows, followed by a real swipe to New York64°F. The middle section opens original Stocks and shows four sample quotes, then returns to Weather. The final settings section switches Cupertino to22°C. This is the original framebuffer inside the web phone frame; the banner identifies sample data. Charts, city search, live services and this build's save/reload lifetime are not demonstrated.

## ELI5 technical explanation

Two different problems looked like “the app cannot connect.” First, its old server name had to resolve through the original OS's actual lookup system. We supplied a small local DNS server and configured the OS's existing supplemental resolver support. Then Weather sent an extra `t` parameter that our HTTP parser did not yet accept. We recovered the original request, reproduced the rejection, and added narrowly checked support for that parameter.

The original app now sends its own request and parses our XML response. Its own rendering code draws the sun and forecast rows, and its original unit control converts72°F to22°C. Independent saved-phone readback verifies the complete forecast and finance HTTP exchanges. We did not change Apple binaries or inject display values.

## Why it matters

Weather and Stocks demonstrate a practical path for reviving original apps with compatible services. Safari already has a separate tested local-page/form workflow. Mail is the strongest next service candidate; Maps requires its binary protocol, and YouTube/iPod also require playback work. [Evidence and remaining limits](../docs/traces/weather-stocks-local/README.md).
