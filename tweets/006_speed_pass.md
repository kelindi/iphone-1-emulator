## Tweet idea

> First speed pass: compiler optimizations and tick batching pushed the interpreter from 35.4 to 41.6 M instructions/s on a real 400M-instruction benchmark. Honest next step: a decoded-block engine, because we're still about 10× slower than the real 412 MHz iPhone.

## What you're seeing

*No dedicated recording yet* — the work is measured on a controlled snapshot benchmark. A terminal/perf recording will be added once we have the SDL window or the decoded-block engine.

Numbers from the corrected benchmark (snapshot at 9.79G, run to 10.19G = 400M instructions):
- Baseline: 35.4 M instr/s
- `-O3` + IPO: 38.4 M instr/s
- Privileged tick batching: 41.6 M instr/s

## ELI5 technical explanation

- The interpreter currently fetches a 32-bit ARM instruction, decodes it from scratch, and walks a big `if/else` tower every single time.
- **`interpreter_tick_batching`** means: while the CPU is running inside an ARM instruction, instead of calling the device-clock function (`s5l8900_tick`) for every single instruction, we run a batch of privileged instructions and then advance the clock by the retired count. We stop the batch at any boundary that could change timing (MMIO access, interrupt, privilege change, WFI, etc.) so timing stays exact.
- **`-O3` + IPO** let the compiler inline and optimize across translation units, reducing dispatch overhead.
- Top remaining bottlenecks (from `perf`): `arm_step`, `thumb_step`, `s5l8900_tick`, `exec_data_processing`, `arm_mmu_translate`.
- The next gain is expected from a **decoded-block cache**: translate a straight-line sequence of ARM/Thumb instructions into pre-decoded operations and execute them in a tight loop.

## Why it matters

Until we get much closer to real time, the UI always feels sluggish. A decoded-block engine is the most plausible path to reach "interactively usable" in both native and WASM builds.
