## Tweet idea

> We made the interpreter up to 17% faster without changing timing accuracy by batching privileged ticks. The home-screen benchmark went from 35.4 to 41.6 M instructions/s.

## What you're seeing

Screen recording to be added: terminal benchmark run showing native `iphone1 --load home.snap --no-fb --steps ...` throughput.

## ELI5 technical explanation

- The interpreter runs one ARM/Thumb instruction at a time, then calls the SoC clock tick function. That tick function advances timers, VIC, GPIO-IC, and so on.
- Most guest instructions don't interact with devices (they just compute on registers/RAM). For those, we can call `s5l8900_tick` once for a whole **batch** of retired instructions instead of once per instruction.
- The trick is stopping the batch at any point where timing could change: a timebase edge, an MMIO access, an interrupt, WFI (wait-for-interrupt), or a host callback. That keeps guest-visible timing exact.
- We previously only batched in user mode; the fix adds privileged (kernel) mode batching, which is where the OS spends most of its boot time.
- Result on the 400M-instruction homescreen benchmark: 35.4 → 38.4 M instr/s with `-O3` + IPO, then → 41.6 M instr/s with tick batching.

## Why it matters

Every host second we save means the guest gets closer to real-time. Faster interpreter lets the home screen appear sooner and makes the browser feel less sluggish.
