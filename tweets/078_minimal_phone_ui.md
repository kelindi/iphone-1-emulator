# The browser is just the phone

## Tweet idea

The original iPhone emulator now gets out of its own way. No heading, toolbar, help panel, or footer—just the 2007 phone, centered in the browser and scaled to fit every screen. Tap it and use it.

## What you're seeing

[The 16-second recording](videos/minimal_phone_ui.mp4), **0:00–0:05**, shows the small in-screen download indicator progress through the phone package, then disappear as the complete genuine iPhone OS 1.0 Home screen arrives. At **0:06–0:11**, a tap opens the original Notes app; the physical Home button returns to the icon grid at **0:12–0:16**. No surrounding web controls appear. The [thumbnail](thumbnails/minimal_phone_ui.jpg) shows the finished desktop layout.

The recording is the complete unedited page session, including startup. The captured page is 1000 × 1000 pixels. Separate checks at 1440 × 1000 and 390 × 844 confirm that the document exactly matches the viewport with no horizontal or vertical overflow.

## ELI5 technical explanation

The iPhone screen is not a web mockup: it is a fixed 320 × 480 stream of pixels produced by the emulated 2007 device. Around it, CSS draws the original phone body at a 380 × 716 ratio. That creates a sizing problem on short laptops and narrow phones—using only the browser width can make the bottom of the device fall off-screen.

The new layout calculates the phone width from both available width and available height, then chooses the smallest safe value. CSS Grid centers the result on a single flat background. The title, external loading line, checkpoint buttons, help copy, and footer are removed from the visible layout. Their internal elements remain in the document so the established runtime and automatic checkpoint code can continue to operate, while the side, Home, touch, and keyboard inputs stay active.

Startup feedback now sits directly over the black phone display: a tiny spinner reports the current iPhone OS, hardware-profile, or phone-state download and its received megabytes. The overlay is pointer-free and disappears only after the emulator announces that the real framebuffer is ready. A startup error replaces the spinner in the same space instead of leaving an unexplained black screen.

## Why it matters

The browser now presents the emulator as the object people came to use, rather than as a demo surrounded by a control panel. It is quieter, more immediate, and fits without scrolling on both the tested desktop and mobile viewports. The tradeoff is intentional: manual Save, Export, Import, Reset, offline setup, and diagnostics are no longer exposed as visible page options.
