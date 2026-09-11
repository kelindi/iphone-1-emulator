# 112 — The compiler runs through the real machine scheduler

## Tweet idea

The new iPhone compiler's pieces now run together: generated WASM, code guards,
function ownership and the real machine scheduler. Three small workloads match
every byte of the interpreter's saved machine. Next: broader instructions and
real-app workloads.

## What you're seeing

This is a nonvisual integration milestone. The Settings/Home section of the
[106 recording](videos/adaptive_compiler_baseline.mp4) shows the unchanged phone
for context; it does not use the new runtime. [Execution evidence](../docs/traces/adaptive-runtime/README.md)
contains actual emitted-WASM runs and full-state comparisons. No browser speed
result or new screen recording is claimed.

## ELI5 technical explanation

Previously we had tested the compiler, guards and scheduler separately. Now real
compiled code goes through all of them. Two loops and a changed-code fallback
produce exactly the interpreter's saved machine, with actual generated execution
verified rather than inferred from a passing fallback.

The runtime also owns compiled function handles until it safely returns them for
removal. Tests fill the registry and retirement queue, replace functions, and
attempt changes during execution. Another check catches a code mapping that
changed physical address even though its bytes and generation still look the
same. Fatal results now block later single-step calls too.

## Why it matters

This is the first end-to-end compiler/runtime integration on synthetic workloads.
8,473 registry checks and 1,067 existing interpreter tests pass. Real-app coverage,
MMU-on compiled execution, device timing, invalidation and performance gates
remain. The user-facing phone is not faster from this milestone yet.
