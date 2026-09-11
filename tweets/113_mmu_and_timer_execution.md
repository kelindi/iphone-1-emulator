# 113 — Compiled execution respects mappings and a real timer deadline

## Tweet idea

The new iPhone compiler now passes MMU-on and timer-interrupt tests through the
real machine runtime. We caught and fixed missing TLB accounting instead of
ignoring it. Compiled code stops at the timer deadline and the real IRQ handler
runs with the expected state.

## What you're seeing

This is a nonvisual CPU/runtime milestone. The Settings/Home portion of the
[106 baseline recording](videos/adaptive_compiler_baseline.mp4) remains contextual
footage of the unchanged phone. [Actual WASM results](../docs/traces/adaptive-mmu-timer/README.md)
show the new full-state comparisons. No new browser speed recording is claimed.

## ELI5 technical explanation

The iPhone translates virtual addresses into physical memory. Compiled code must
follow those translations and preserve the emulator's fetch bookkeeping. Our
first cross-window MMU test found that the answers matched but the translation
hit count did not. We fixed the missing fetch-cache update and kept the full
machine comparison intact, including a case that hands a load to the interpreter.

Timers are another boundary: compiled code cannot run past the moment an interrupt
must arrive. A real Timer4 test now stops after 69 compiled instructions, delivers
the IRQ and runs a marker handler. The resulting saved machine matches ordinary
interpretation byte for byte.

## Why it matters

Nine integrated cases pass, including nonidentity mappings, slow-instruction
handoff and one deterministic interrupt deadline. Real-app coverage and speed
are still unproven; broader instruction support is the next implementation step.
