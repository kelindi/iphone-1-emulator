## Tweet idea

The real 2007 iPhone OS now lives inside a clickable phone in the browser: silver rim, glass, earpiece, Home button and edge controls. The emulator runs in its own worker, so the surrounding controls stay responsive even while the original apps are busy.

## What you're seeing

Recording: [clickable_browser_iphone.mp4](videos/clickable_browser_iphone.mp4), with [thumbnail](thumbnails/clickable_browser_iphone.jpg).

- 0:00–0:04: local startup and the genuine home screen inside the new phone case.
- 0:04–0:19: opening Settings; its real list finishes drawing.
- 0:19–0:27: physical Home button returns to SpringBoard.
- 0:27–0:39: opening genuine Safari.
- 0:39–1:11: Home returns from Safari, including the remaining slow graphics transition.

The clip intentionally retains the slow transition. It demonstrates the physical shell and interaction, not completion of the speed or graphics goals. The displayed phone UI is guest framebuffer output; only the surrounding physical case is CSS.

## ELI5 technical explanation

Previously the emulator executed on the same browser thread that handles clicks and draws the webpage. We moved the whole machine to a dedicated worker, deliver input through an ordered API, and transfer a reusable display buffer back to the page. Only changed guest frames need presentation. The browser can respond to its own controls while the emulated ARM CPU continues working.

The new page supplies real, accessible physical-button elements. Pointer capture, key release, lost-focus cleanup, and a stateful ringer control prevent ordinary browser interactions from leaving buttons stuck. The Z1 touch model now preserves finger identity and make/move/break transitions; the host input queue coalesces movement while retaining touch boundaries.

The recorded Chrome run reached the phone in 2.92 seconds from local assets. Help, Pause, and Resume responded in 49, 78, and 110 ms. Those numbers measure the browser controls; they do not measure guest touch latency or internet download speed. Both autosaves completed in this run, but a separate run exposed an intermittent save error still under investigation. Raw snapshot capture paused guest execution for roughly 0.6–0.9 seconds.

The final Safari-close sequence logged an MBX graphics restart and took much longer than the product target. Graphics completion and real app speed remain active work.

## Why it matters

The emulator is becoming a usable browser object with an actual phone shape and controls. Its presentation and execution are now separated, giving us a foundation for reliable input, persistent state, and measured performance work without rebuilding Apple's apps.
