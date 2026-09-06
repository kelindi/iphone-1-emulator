## Tweet idea

Next destination for the original iPhone emulator: a phone you can actually use in your browser. Clickable hardware buttons, saved notes and photos, sound, a camera, and useful original apps—with measured speed targets and compatible demo services for the online features.

## What you're seeing

Planning milestone: [the browser iPhone delivery plan](../plans/web-iphone.md). No new visual feature has been implemented or recorded in this milestone.

The existing [browser recording](videos/browser_home_settings_safari_cpi1.mp4), 0:00–0:34, and [thumbnail](thumbnails/browser_home_settings_safari_cpi1.jpg) show the current starting point: snapshot startup, Settings/Safari navigation, and Home input. They do not show the planned case, networking, audio, Camera, or performance improvements.

## ELI5 technical explanation

The real 2007 software already draws the phone's screen. To make it feel like a complete phone, we need three more pieces: an easy browser wrapper, faster and more complete pretend hardware, and services that answer requests in the old apps' language.

The plan defines a testable workflow for all 16 apps. It starts with a reproducible baseline, physical frame and controls, a dedicated emulator worker, and a browser performance experiment. Next come durable personal data, audio/media/camera, guest Wi-Fi, and protocol adapters for online apps. Calls and texts use a simulated carrier; legacy online apps receive explicit demo or supported live data.

Two independent subagent reviews checked browser performance/persistence and app/hardware coverage. Their findings added real sensor-driven rotation and Cover Flow, reliable touch event delivery, guest clock and audio-DMA timing, preserved saves across emulator upgrades, and separate delivery gates so Camera research does not block network services.

Moving computation to a worker keeps the page responsive, but faster guest execution still needs engine work. A network relay connects the emulated Wi-Fi to external services, but it does not make old Safari understand every modern website. Saved snapshots speed startup, while durable checkpointing is separate work needed to preserve what a user changes.

## Why it matters

“An iPhone in a browser” now has concrete acceptance criteria instead of meaning only a successful boot. Early previews can ship while the highest-risk work is measured, and every app must demonstrate a useful workflow before the complete experience is considered delivered.
