## Tweet idea

> We fixed the S5L8900 timer model and the OS 1.0 kernel's scheduler finally woke up on time. Before this, the 100 Hz tick fired 5× in 114M instructions instead of 27×.

## What you're seeing

No dedicated recording yet — this is invisible in the UI but traceable in `--serial` logs. A screen recording of the smoother boot after the fix will be added later.

## ELI5 technical explanation

- The S5L8900 has four timers. Timer4 is the XNU decrementer source.
- We originally modeled it as an auto-reload up-counter. OS 1.0 sets it as a **one-shot down-counter**: you write `COUNTBUF`, then set `STATE=START|UPDATE`, and the hardware counts down to zero, fires an interrupt, and stops.
- With the wrong model, `hertz_tick` (the kernel's 100 Hz heartbeat) was grossly late, and `IODelay` wakeups arrived millions of instructions after they should have. That scrambled driver registration order and made the USB PHY/wrangler race much worse.
- After making timer4 a true one-shot down-counter, the tick spacing matches what the kernel expects, and the scheduler wakes up drivers in the right order.

## Why it matters

Guest timing is the foundation of everything. If the kernel thinks time is wrong, every timeout, sleep, and driver start sequence is wrong, and you chase phantom bugs in random places.
