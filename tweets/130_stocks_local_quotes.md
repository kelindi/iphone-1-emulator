# 130 — Original Stocks receives local demo quotes

## Tweet idea

The 2007 iPhone Stocks app displays quotes again. Its original code resolves its old server name, sends its own XML request, and draws prices returned by our tiny server inside the emulated phone. These are clearly disclosed sample prices, not live market data.

## What you're seeing

[Recording](videos/stocks_local_quotes.mp4) · [Thumbnail](thumbnails/stocks_local_quotes.jpg). Around 00:55–01:05, original Stocks displays AAPL120.00, GOOG500.00, YHOO25.00 and T40.00. Around 01:15–02:05, Weather still fails, including a settings change. Around 02:25–02:35, Stocks is revisited and retains its quotes. Its chart is unfinished and ends blank; the server explicitly rejects the unsupported chart request. This is an isolated imported phone, not the default distribution or a speed benchmark.

## ELI5 technical explanation

The old apps ask a server name that no longer gives them the answers they expect. A hosts-file entry did not reach their actual lookup path. We configured the original OS's supported supplemental resolver and wrote a small local DNS server. That server tells the phone where our local HTTP service lives. Stocks then sends its original finance request, and our service returns the XML fields its original parser expects.

We verified both sides: the visible prices match an independently recovered complete HTTP200 response in the saved phone. The preceding real DNS question and A127.0.0.1 answer are retained too. Our own executable also needed two old runtime conventions fixed: an exported environment pointer and preservation of the register used for the original OS thread pointer. Apple executables remain unchanged.

## Why it matters

This proves another original online app can become useful through compatible local services without waiting for full Wi-Fi emulation. Quotes are demonstrated; charts, symbol search, live data, default integration and complete persistence acceptance remain separate work. Weather's next concrete blocker is our overly narrow request target parser: the original app includes a `t` parameter. [Evidence](../docs/traces/stocks-local-quotes/README.md).
