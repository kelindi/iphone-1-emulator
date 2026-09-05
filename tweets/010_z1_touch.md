## Tweet idea

> Reverse-engineered the original iPhone's Zephyr Z1 multitouch protocol from Apple's own driver, then fed it real touch frames so the OS thinks a finger is on the screen.

## What you're seeing

Screen recording: `videos/browser_home_settings_safari_cpi1.mp4` (0:09–0:13)

A click on the Settings icon becomes a Z1 touch frame; SpringBoard animates the Settings app opening.

## ELI5 technical explanation

- The original iPhone screen is not a simple coordinate device. It has a **Zephyr Z1** touch controller connected over **SPI2**. The chip has firmware that must be downloaded, an attention/result handshake, and a specific report format with density and timing fields.
- We disassembled the genuine AppleMultitouchSPI kext and reverse-engineered:
  - Bootloader mode (`0x02`) and the firmware download sequence.
  - The attention signal/result read protocol.
  - Touch frame payloads (`0xce`, `0xd0`, `0xd1`) and checksum algorithm.
  - How to transform raw sensor data into X/Y coordinates and finger density.
- The emulator implements `core/src/soc/mtz1.c` which mimics all of that. When the browser sees a mouse click, it calls `mtz1_inject_touch(x, y, phase)`; the Z1 device returns a report that the Apple driver parses and forwards to SpringBoard.
- We hold the touch down for ~100 ms of guest time because very short clicks were ignored by the guest (the emulator runs much slower than real hardware).

## Why it matters

Once Z1 works, the emulator stops being a video and becomes a real touch device. It also removes the earlier workaround that disabled the Z1 driver entirely.
