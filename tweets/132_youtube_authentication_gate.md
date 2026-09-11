# 132 — YouTube needs device authentication before its catalog

## Tweet idea

Why doesn't the original iPhone YouTube app work just because Weather does? It signs in the device before it even asks for videos. We mapped that old handshake and its catalog parser. A local replacement looks possible; proving the phone can sign the challenge is the next test.

## What you're seeing

[Recording](videos/youtube_service_check.mp4) · [Thumbnail](thumbnails/youtube_service_check.jpg). Around01:05, the original Featured view shows Cannot connect to YouTube. This is a retained baseline on the working Weather/Stocks service phone, not a restored YouTube catalog or playback milestone. The rest of the finding comes from read-only inspection of the original request and response code.

## ELI5 technical explanation

Weather asks directly for a forecast. YouTube first asks a separate server for a challenge, gets the phone's own certificate and private key, signs the challenge, and receives a token. Only then does it request the video list. We identified the original catalog fields, prepared an independent three-entry XML candidate, and found that the connection hostname differs from the HTTP Host header.

The old authentication reply does not appear to require an Apple-held secret for a local demo server. But the original phone must still perform its own identity lookup and signing. Our existing activation setup is not proof that this works. The candidate XML contains placeholder URLs; no videos or thumbnails have been shown yet.

## Why it matters

There is now a specific next experiment instead of guessing feed formats: answer the first challenge locally and see whether the unchanged app sends its signed second request. Then test a real catalog and one short clip. Media decoding and audible audio remain separate emulator work. [Evidence](../docs/traces/youtube-service-check/README.md) · [Plan](../plans/youtube-services.md).
