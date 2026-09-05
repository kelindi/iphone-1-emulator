## Tweet idea

> Running four independent Devin sessions in parallel to attack the biggest remaining pieces: a decoded-block speed engine, real USB-OTG timing, Marvell SDIO Wi-Fi, and an SDL desktop window.

## What you're seeing

*In progress.* Sessions were launched in `ultra` mode after `fusion` and `swe` modes were unavailable; one of them received an environment warning.

## ELI5 technical explanation

- A modern Devin workflow can split a project into multiple **child sessions**, each given one focused task and its own branch off the main PR branch. They work independently and return their changes; the parent reviews and merges them.
- The four tracks we chose are the highest-leverage remaining pieces:
  1. **Decoded-block engine**: retire many more ARM instructions per host second.
  2. **USB OTG / PHY timing**: remove the `--unmatch arm-io/usb-otg` workaround so `AppleUSBDeviceMux` registers cleanly, which also fixes lockdownd crash loops.
  3. **Marvell 88W8686 SDIO Wi-Fi**: emulate the Wi-Fi chip enough that MobileSafari can browse real HTTP sites.
  4. **SDL desktop window**: a native application with mouse, keyboard, and Home/hold buttons for local testing.
- The environment warning in the screenshot means the child sessions couldn't set up `cmake/ninja/cryptography` on their VMs; the parent repo needs its blueprint fixed (maintenance was accidentally a build step, not dependency installation).

## Why it matters

Parallel exploration lets different hard problems advance independently without blocking each other. The parent task (this session) is pausing to document progress in the `tweets/` folder while the children continue.
