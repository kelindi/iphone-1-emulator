# Auditing all 16 original iPhone apps

## Tweet idea

We opened all 16 original iPhone apps in the browser. The real Calculator does 2 + 3, Weather opens city management, and Mail opens its account editor. The audit also shows what still needs work: networking, media, Camera, and cellular hardware.

## What you're seeing

[Screen recording](videos/real_app_inventory.mp4) · [thumbnail](thumbnails/real_app_inventory.jpg)

- **0:00–0:27:** the genuine Calculator receives 2, +, 3, = and displays 5.
- **0:27–0:45:** Weather's info control opens the original city-management screen. Forecast retrieval still fails.
- **0:45–1:03:** Mail's Other option opens its manual account editor. No account or server connection is configured.
- **1:03–1:21:** Safari's Done button closes Bookmarks and reveals the original browser controls. No internet page is loaded.

These are selected excerpts from real browser recordings, at their original speed, with loading/waiting time trimmed. Each excerpt starts from an independent restored phone session. The complete 16-app audit uses per-app screenshots and guest logs; this video illustrates four workflows and does not establish full app functionality or performance.

The separate [Notes persistence recording](023_persistent_hello_note.md) shows a nonempty note surviving browser reload. The separate [touch calibration milestone](024_touch_calibration.md) explains corrected tap coordinates. That work also verifies one slow Settings scroll; flick and pinch gestures remain unverified.

## ELI5 technical explanation

Each icon launches Apple's actual 2007 application. Those applications expect hardware and services that a picture of an iPhone cannot provide: a photo database, camera sensor, audio device, cellular modem, Wi-Fi connection, and older server protocols.

We opened each app in a fresh browser session, tried a small visible action where possible, and saved screenshots plus the guest's diagnostic messages. That exposed specific dependencies. Photos and iPod have empty libraries. YouTube cannot connect; Stocks and Weather report failed data retrieval. Phone and Text report that their baseband DMA device is not ready. Camera reaches its closed-iris screen, produces no preview, and returns to Home during the capture probe.

The sweep also exposed incorrect touch coordinates near the top of the screen. After the input calibration fix, we verified the recorded calculator, Weather, Mail, and Safari actions through their genuine controls. A rendered account editor verifies navigation; sending mail still requires a working guest network and compatible server.

## Why it matters

The [app coverage audit](../plans/app-coverage.md) gives every original app an observed state and a concrete next test or missing dependency. It supports useful offline work immediately and keeps the larger hardware and service tasks visible. Full speed and all-app acceptance remain separate release gates.
