## Tweet idea

> The genuine June 2007 iPhone home screen — icons, dock, Earth wallpaper and all — running from Apple's actual OS 1.0 build 1A543a, inside an emulator. No CSS recreation, no mock UI. Real kernel, real SpringBoard, real binaries.

## What you're seeing

Screen recording: `videos/browser_home_settings_safari_cpi1.mp4` (0:05–0:08)

The browser loads the page, downloads the pre-made snapshot (home-screen state saved at ~9.79 billion guest instructions), and renders the original 2007 home screen: 12 icons, 4-icon dock, status bar, and the classic Earth wallpaper. The status line below shows `Running · 28M instructions/s · scanout 320×480`.

## ELI5 technical explanation

We are not drawing the home screen ourselves. We are running Apple's genuine instructions.

- **iPhone OS 1.0** is the real operating system Apple shipped on the original iPhone in 2007. We extracted it from the public 1A543a restore IPSW.
- **SpringBoard** is the real iPhone app launcher. It loads app icons from a real preference plist, composes them with the real CoreGraphics/WindowServer path, and blits them to a real framebuffer.
- The emulator is pretending to be the S5L8900 chip (CPU, MMU, timers, interrupts, LCD) well enough that the unmodified Apple kernel and SpringBoard think they are on a real phone.
- The screen image is the emulator copying the guest LCD framebuffer into an HTML canvas, frame by frame.

## Why it matters

This is the project's north-star milestone. If the real 2007 SpringBoard runs, every other goal (touch, apps, browser, speed, networking) is "just" more hardware emulation layered on top.
