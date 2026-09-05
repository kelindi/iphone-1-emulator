## Tweet idea

> Counter-intuitive emulator lesson: doubling the guest CPU clock speed (CPI) doesn't make the UI respond faster. The limiting factor is how many guest instructions the host can retire per second, not how guest time flows.

## What you're seeing

Screen recording: `videos/browser_cpi2_timing.mp4`

The page is loaded twice: once with `?cpi=2` (status ~24 M instr/s, `cpi 2`) and once with `?cpi=1` (status ~28 M instr/s, `cpi 1`). Settings opens in both cases; the Clock Stopwatch advances wall clock faster at CPI 2 but the tap-to-response latency is similar.

## ELI5 technical explanation

- **CPI = cycles per instruction**. It changes how many guest CPU cycles each retired instruction consumes. CPI 2 means each guest instruction takes 2 ticks of the 412 MHz clock, so guest time runs 2× faster for the same number of instructions.
- But the host machine can only execute a fixed number of guest instructions per second (~28 M in this browser). CPI 2 makes the guest *think* more time has passed, but it does not increase that host throughput.
- Result: the guest clock (stopwatch) advances ~0.65× real time at CPI 2 vs ~0.74× at CPI 1, but Settings still takes ~8 s to fully draw in both cases because drawing requires the same number of real instructions.
- Real speedup comes from retiring more instructions per host second, not from fudging the clock. That's why the next phase targets a decoded-block interpreter.

## Why it matters

It's a public example of the difference between **host performance** (MIPS) and **guest timing fidelity**. You can't just "run the guest clock faster" and expect a snappier UI.
