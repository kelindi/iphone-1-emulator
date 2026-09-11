# 129 — Safari service survives save, reload and unlock

## Tweet idea

Original Safari now submits a local form, survives saving and reloading the whole emulated phone, unlocks with the real slider, and submits a different form successfully. We verified the actual request and response bytes inside the original OS.

## What you're seeing

[Recording](videos/safari_gateway_persistence.mp4) · [Thumbnail](thumbnails/safari_gateway_persistence.jpg). The first form displays `Message: hi`; the middle section retains ordinary save/reload and the original lock screen/slider. The final section displays the new `Message: ok`. An early swipe happened after the display slept; waking and swiping again succeeds. The final toolbar has a visible rendering blemish, so this is service/persistence evidence rather than complete graphics acceptance or a speed benchmark.

## ELI5 technical explanation

A saved screen alone can look like a working app. We instead sent a new message after restoring the phone and checked that the server actually received and answered it. The original Safari, keyboard, touchscreen, kernel sockets and our own gateway all participate. An independent read of the saved phone recovers both distinct form requests and complete HTTP200 responses; the restored autosave timestamp also matches exactly.

## Why it matters

The combined gateway has a demonstrated useful Safari workflow across normal persistence. It is still an isolated imported phone. Weather and Stocks need their hostname route fixed, and general websites, default distribution, toolbar rendering and broad lifetime testing remain separate work. [Evidence](../docs/traces/safari-gateway-persistence/README.md).
