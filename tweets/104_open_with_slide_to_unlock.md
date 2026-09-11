## Tweet idea

Open the browser iPhone and you now get the original Earth lock screen. Drag the actual 2007 “slide to unlock” control to enter your phone—even after reopening a saved session.

## What you're seeing

Recording: [open_with_slide_to_unlock.mp4](videos/open_with_slide_to_unlock.mp4), with [thumbnail](thumbnails/open_with_slide_to_unlock.jpg).

At approximately 10 seconds, the fresh phone opens locked. At 11–14 seconds, a short drag returns the slider to its starting position. At 14–18 seconds, a complete drag opens Home. Saved Home, locked, and sleeping phones reopen to the lock screen at approximately 30, 42, and 53 seconds; the final slide unlocks again by 57 seconds. The recording runs at its original speed.

## ELI5 technical explanation

The browser previously restored a running Home-screen snapshot and displayed it immediately. The original operating system already knew how to lock and unlock, so startup now briefly operates the emulated physical buttons before revealing the display: Sleep turns off a lit phone, then Home wakes it. A phone saved asleep only needs the wake step.

The guest must actually receive each button press and finish its display work. Startup advances the real emulator in short, yielding slices, holds buttons for 180 ms of device time, and waits for the LCD to turn off/on. Input and frames remain behind the existing loading screen until this finishes. A stalled transition uses the existing retry error flow. No Apple binaries or lock-screen pixels are replaced.

`npm run test:open-lock` passes seven real Chrome checks covering fresh startup, incomplete/full drags, and saved Home/locked/asleep sessions. All 58 JavaScript unit tests pass after updating their hardware mock to advance the clock and respond to startup buttons. The [browser report](../docs/traces/open-lock/report.json) retains check timestamps. The [first test attempt](../docs/traces/open-lock/first-attempt.json) stopped because the test tried to click the deliberately hidden Save control; invoking its existing handler corrected the test. App-state restoration after unlocking and startup performance across devices are outside this bounded check. Other navigation tests that assume immediate Home now need to unlock before their app actions.

## Why it matters

The first interaction now feels like opening an original iPhone: its real lock screen and slider lead into the running phone. Saved sessions get the same opening experience without resetting their data.
