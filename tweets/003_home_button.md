## Tweet idea

> Home button works. Press Escape in the browser, and the genuine 2007 iPhone OS exits Settings and goes back to SpringBoard — the same way the real sleep/wake button did.

## What you're seeing

Screen recording: `videos/browser_home_settings_safari_cpi1.mp4` (0:14–0:29)

Settings opens, then Escape is pressed. In ~6 seconds of guest time the app closes and the 12-icon home screen reappears. Later in the same clip, Safari opens and Escape takes us home again.

## ELI5 technical explanation

- The physical Home button on the original iPhone is just a GPIO line on the S5L8900. A low/high signal on a specific pin is routed through the GPIO interrupt controller (`gpioic.c`) into the VIC and then into the kernel HID stack.
- We model the M68 GPIOs used for `hold`, `menu/Home`, `volup`, `voldown`, and `ringerab`.
- When you press Escape in the browser, the emulator sets the GPIO for 100 ms of guest time and then releases it. The kernel HID driver produces a Consumer-page `Menu` event that SpringBoard consumes.
- Getting this right required fixing the MBX 2D blit path: without it the app-close animation wouldn't draw correctly and the return-to-home transition broke.

## Why it matters

Home-button navigation is the core interaction model of iPhone OS. If it doesn't work, the device feels broken even if individual apps launch.
