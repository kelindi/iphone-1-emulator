# 138 — A simpler phone and sharing preview

## Tweet idea

The original iPhone, with a little less around it. Removed the 3D switch and made the sharing preview just the real phone and a light, condensed Instrument Serif title reading “The Iphone”.

## What you're seeing

[Fresh recording](videos/minimalist_phone.mp4) · [Thumbnail](thumbnails/minimalist_phone.jpg) · [OG image](../web/og-image.png)

The 47.76-second recording shows the browser shell after removing the view switch. It predates the Apple-logo loader and final removal of the bottom instruction label. Around 40–42 seconds, the physical controls change silent mode and put the display to sleep; around 44–47 seconds, it wakes and the real slider unlocks to Home. The thumbnail and OG image use the final genuine guest home screen from this run. The static OG image is a separate asset, not shown inside this controls recording.

## ELI5 technical explanation

The phone's screen is still drawn by the original iPhone OS running inside the emulator. The optional 3D case needed another renderer, its own mouse-to-screen mapping, model loading, and extra files in the offline bundle. We removed those pieces and their Three.js dependency, while keeping the existing touchscreen and physical buttons.

The sharing card is a 1200 × 630 PNG on a warm off-white background. It uses a fresh screenshot of the actual unlocked phone, enlarged to about 450 pixels wide and cropped by the bottom edge. The title reads “The Iphone” in Instrument Serif Regular, lightly condensed into a 600 × 168-pixel title above the phone. The year is removed. No substitute home screen was drawn. Canonical, Open Graph, and Twitter metadata now reference https://2007.mobile/ and its image. Page and social-preview text read “📱 iPhone (2007)”; the bottom unlock/explore instruction label is removed.

Verification on the local build: 115 unit tests and the WebAssembly smoke check pass. Physical control tests pass silent mode, sleep, wake and actual slide-to-unlock. Offline checks pass installation, disconnected reload, corrupt-cache detection and recovery. Ten additional browser checks cover desktop/mobile fit, mobile touch unlock and Settings launch, keyboard Home/sleep/wake, no 3D requests, canonical metadata and image delivery/dimensions. Screenshots and reports are retained under `.context/minimalist-phone/`.

## Why it matters

There is one clear way to use the emulator, and fewer rendering assets to download or keep offline. Shared links have a restrained preview using the real 2007 phone.

Vercel reports that 2007.mobile is already verified with valid DNS configuration. Deployment and redirects from the old domains were deferred at the user's request; this milestone establishes local implementation and verification only. Saved phones remain tied to their browser origin.
