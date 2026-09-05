## Tweet idea

> Touch is working. A simulated finger tap dismisses the "Repair Needed" alert and launches the real Settings app from the genuine 2007 iPhone OS home screen.

## What you're seeing

Screen recording: `videos/browser_home_settings_safari_cpi1.mp4` (0:09–0:13)

A mouse click on the Settings icon is translated into a multitouch "finger down / finger up" event for the Z1 touchscreen controller. SpringBoard receives the touch, recognizes the icon, and animates the Settings app opening.

## ELI5 technical explanation

- The original iPhone used a **Zephyr Z1** multitouch chip on the SPI2 bus. SpringBoard does not talk to the screen directly; it talks to the `AppleMultitouchSPI` kext via the `Z1` driver.
- We reverse-engineered the Z1 protocol from the genuine driver: report IDs, checksums, scan frames, firmware download, attention/result reads, and touch-frame formats (`0xce`/`0xd0`/`0xd1`).
- The browser maps your mouse click to a 320×480 guest coordinate, then the emulator injects that as a Z1 touch frame with density/timestamp fields the driver expects.
- A tap has to last long enough in guest time (we hold it 100 ms) or SpringBoard treats it as a glitch. We also had to find the real Dismiss button location (~(160,315)) because the alert is rendered by the real app, not us.

## Why it matters

Touch turns the emulator from a movie into an interactive device. Without it the "Repair Needed" alert blocks everything and no app can be opened.
