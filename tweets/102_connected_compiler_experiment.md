# Connected code works, but this version is slower

## Tweet idea

We built a compiler that keeps the original iPhone's CPU registers in fast browser locals across connected code. Correctness checks pass. It makes 7.8% fewer compiler handoffs—but takes 4.1% longer. Fewer handoffs alone aren't enough; the next measurement must explain why compiled execution stops.

## What you're seeing

[Full candidate browser recording](videos/connected_compiler_experiment.mp4) · [Thumbnail](thumbnails/connected_compiler_experiment.jpg).
The complete recording includes startup, first Settings/Home pair, then a second Settings/Home pair. It runs the isolated connected compiler and genuine guest framebuffer with the clickable phone frame. Both final app screens were visually reviewed; the browser observer completes all four actions and verifies fetched candidate core/seed hashes. This short demonstration is not a navigation speed comparison or save/reload acceptance. The 4.1% regression comes from the separate fixed-work replay, not video timing.

## ELI5 technical explanation

Normally the accelerator handles a small piece of phone code, packs away the phone's registers, and returns to the emulator. We connected up to four pieces so those registers can stay ready between them. The prototype still checks memory, changed instructions, interrupts and timing. It passes 32 compiler checks, 403 focused CPU/MMU checks and matching complete phone-state hashes during a real Settings/Home replay.

But choosing the next piece also costs work. This version reduces compiled entries from42.05 million to38.78 million, yet the same400 million guest instructions take11.614 seconds of CPU instead of11.160 after warmup. That is4.07% slower. The evidence rejects this particular small-cluster design; it does not prove all connected compilation is slow. An exact exit-reason census should identify where larger changes could help.

We also checked QEMU-WASM's ARM1176 support. Its compiler expects QEMU's own CPU state and memory machinery, so importing it requires substantial adapters. We chose the existing emitter for this bounded experiment. QEMU performance on our phone remains unknown.

## Why it matters

There is now an executable, correctness-tested architecture prototype and a clear rejection result. It remains isolated; users keep the current engine. This avoids replacing the emulator based on an attractive idea without an actual gain. It establishes no physics ceiling and promises no2× improvement. [Source, evidence and limitations](../docs/traces/connected-compiler-experiment/README.md).
