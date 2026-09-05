## Tweet idea

> First pixels from the real 2007 iPhone OS: the "Connect to iTunes" lock screen appeared inside the emulator. It's not a recreation — it's Apple's actual SpringBoard rendering the original Earth wallpaper.

## What you're seeing

Screen recording: `videos/browser_home_settings_safari_cpi1.mp4` (0:03–0:08)

The browser loads the snapshot and the first rendered frame is the genuine iPhone OS 1.0 interface. From there it quickly wakes into the 12-icon home screen.

## ELI5 technical explanation

Before SpringBoard can draw anything, a huge chain has to finish:

1. The **kernel** (XNU, Apple's real `mach` kernel) boots from a hand-constructed environment: CPU registers, a flattened device tree, boot-args, and kernel memory map.
2. It initializes the **MMU** (memory management unit), which maps virtual addresses used by iOS to physical RAM addresses in the emulator.
3. It starts **IOKit** drivers for timers, interrupt controllers (VIC + GPIO-IC), power (PCF50635), SPI/NOR, LCD, and the Z1 touch chip.
4. It mounts the **HFSX root filesystem** we extracted from the 1A543a IPSW.
5. `launchd` (the first userspace process) spawns `lockdownd`, `CommCenter`, `SpringBoard`, etc.
6. SpringBoard calls **WindowServer** and **CoreGraphics** to composite icons and wallpaper into the framebuffer.
7. The emulator copies that framebuffer to the screen.

The big unblocking hack was: the genuine `AppleMultitouchSPI` driver would hang SpringBoard's main thread forever because we had no Z1 hardware yet. By preventing that driver from matching (same trick later used for USB), SpringBoard could reach `-[SBUIController finishedUnlock]`. Later, of course, we implemented Z1 for real.

## Why it matters

This is the "north star" proof: the unmodified 2007 Apple code can paint the original interface if the hardware model is close enough.
