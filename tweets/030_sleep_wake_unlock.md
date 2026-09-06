## Tweet idea

The browser iPhone can sleep, wake to the original Earth lock screen, and slide to unlock. The black-screen bug was a missing display-link chip: the real Apple driver was waiting for two SPI bytes to finish, with the whole display thread stuck behind it.

## What you're seeing

Recording: [sleep_wake_unlock.mp4](videos/sleep_wake_unlock.mp4), with [thumbnail](thumbnails/sleep_wake_unlock.jpg).

The unsped Chrome recording shows real volume-up/down and silent overlays, the physical Sleep button turning the display black at 11.4 s, wake revealing the Earth lock screen at 14.4 s, and dragging the original slider back to Home by 30.5 s. The slow return animation remains visible; this is functional acceptance, not full-speed acceptance. Four automated checks pass, and the volume/silent screenshots were visually inspected. Reproduce with `npm run test:controls`.

## ELI5 technical explanation

The screen needs both its LCD panel and a display-link transmitter. We had connected the panel but left the device-tree's SPI0 LM2512 transmitter unattached. On wake, Apple's actual driver sent register/value bytes `0x16,0xff` and waited for transfer completion while holding the display's workloop gate. Other display work piled up behind that wait, so the screen remained black.

The narrow fix attaches a transport endpoint to the correct SPI0 chip select. It clocks the measured configuration writes through the existing FIFOs and interrupts. The real driver then finishes its request, releases its gate, and draws the lock screen. The endpoint returns zero for unused full-duplex receive bytes; it does not claim register-read or full transmitter behavior. No Apple code, framebuffer contents, or window-enable bits are overridden.

Old version 39 checkpoints also inherited the wrong board's SPI0 route. Loading now accepts only the known legacy or correct route and reasserts the actual M68 endpoint; invalid routes and inappropriate read phases still fail validation. A native regression retains an in-flight two-byte request across restore and verifies real receive/interrupt completion. All 23 native suites and WASM smoke pass. The separate touch-velocity fix makes the genuine slider usable.

## Why it matters

The clickable physical frame now supports a complete sleep/wake/unlock interaction through the original OS. Debugging the guest's blocked thread found the missing hardware connection instead of concealing the black screen. Broader app-launch stability, repeated-cycle testing, sound, and performance targets remain open.
