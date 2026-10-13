# 140 — Loading progress and a secure phone address

## Tweet idea

The little Apple loading bar now tells you how far along the download is. And 2007.mobile finally opens securely, without the mobile startup crash.

## What you're seeing

[Recording](videos/loader_progress.mp4) · [Thumbnail](thumbnails/loader_progress.jpg)

The recording shows the mobile Apple logo, filled bar and percentage, then the genuine iPhone OS lock screen. Downloads are paced and served from local copies of the verified firmware; Web Crypto is deliberately removed from the emulator worker to reproduce the missing browser capability. It demonstrates recovery from that limitation, not live download speed or a physical Safari-device test.

## ELI5 technical explanation

Some browsers expose their built-in cryptographic hash function only on secure origins. The firmware downloader assumed that function always existed, so an HTTP visit could crash with a `crypto.subtle.digest` error. A small SHA-256 fallback now verifies the same firmware hashes before caching or decoding. Secure origins keep the native implementation. The optional instruction accelerator skips preparation when native Web Crypto is unavailable; the interpreter still starts the phone.

The manifest now records each firmware file's byte size alongside its hash. The worker adds up only the files needed for this startup, reports cumulative received bytes and avoids counting a corrupt-cache retry twice. The percentage follows those bytes, stops at 99% during verification/preparation, and reaches 100% only when the guest is ready. Retry resets it to zero. The small numeric label uses tabular figures; the bar fills with a transform and respects reduced motion. The hash helper is included in offline packaging and the offline-upgrade fixture.

The live domain had valid DNS and a Vercel certificate record, but its TLS handshake closed without serving that certificate. Reissuing the certificate restored HTTPS. A Vercel routing rule now permanently redirects HTTP requests for 2007.mobile to HTTPS, preserving paths and queries. Verified HTTPS 200 responses for the page and OG image, and HTTP 308 responses for the root, image and a nested path with query parameters. This repair did not require deploying the frontend changes.

Verification: 119 unit tests, 19 loader/recovery checks, six offline checks, physical controls and genuine desktop/mobile startup with Web Crypto disabled pass. The fallback hash is compared against Node's SHA-256 at padding boundaries and on a large input; corrupt firmware is still rejected. Reports and screenshots are in `.context/loader-progress/`.

## Why it matters

Users get useful progress feedback and a working secure address. An unavailable browser API no longer prevents verified firmware from starting. The loader changes are in a follow-up PR; the certificate and HTTP-to-HTTPS routing repair are already live.
