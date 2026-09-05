## Tweet idea

> Why we temporarily `unmatch` USB OTG: the real USB PHY timing is subtly wrong, and it makes `AppleUSBDeviceMux` crash before SpringBoard can start. We understand the race; fixing the timer model is the permanent solution.

## What you're seeing

No dedicated recording; the failure was a kernel panic during early boot. The workaround keeps boot alive, visible in `tools/iphone1.c` and the browser load path.

## ELI5 technical explanation

- The original iPhone's kernel starts a **USB OTG** driver to talk to iTunes. It has two halves: a physical layer (`AppleS5L8900XUSBPhy`) and a multiplexer (`AppleUSBDeviceMux`).
- The multiplexer registers to be notified when the PHY is published. In the real phone the notification arrives asynchronously because the PHY registers *after* a long power/clock/VIC setup.
- In the emulator, the timer/scheduler runs in a way that makes the PHY register *synchronously* while the multiplexer is still half-initialized. The callback function pointer hasn't been stored yet, so Apple code dereferences NULL and crashes.
- Current workaround: in the device tree we tell the kernel not to match `arm-io/usb-otg`. This removes the USB driver entirely, so SpringBoard starts without it.
- Permanent fix: model the S5L8900 timer4 as a proper one-shot decrementer and accurately route the VIC/GPIO-IC ordering, so the PHY thread wakes and registers after the multiplexer is fully set up.

## Why it matters

Workarounds are documented as exactly that: temporary. Understanding the race is what tells us which hardware model is wrong, instead of papering over symptoms.
