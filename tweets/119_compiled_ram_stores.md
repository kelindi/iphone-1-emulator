# 119 — Compiled RAM stores

## Tweet idea

The iPhone compiler can now write to RAM without leaving compiled execution.
If that write changes code it is running, it stops immediately and discards
the old translation—even when two addresses point to the same memory.

## What you're seeing

Nonvisual compiler milestone. [Evidence](../docs/traces/adaptive-ram-stores/README.md)
contains full-state comparisons, source identities and preserved failures.
The Settings/Home section of [106's recording](videos/adaptive_compiler_baseline.mp4)
is baseline context only, not a demonstration of this compiler or a new speed result.

## ELI5 technical explanation

A store writes a number into memory. The compiler now checks whether the emulator
already has permission and a valid mapping, then commits that write once. If the
check fails, the original interpreter handles the instruction. If the write
touches code, the compiler must stop before using its old translation.

Tests caught missing setup permission and a mapping evicted by an aliased write.
Those fixtures were corrected without weakening production checks. A separate
negative test found that a malformed result could claim no instructions ran
after a code write; the runtime now rejects it to prevent replay.

## Why it matters

51 actual WASM integration cases preserve full machine snapshots. 33,455
instruction comparisons, 115 unit tests, 150 native cache checks, 529 runtime
checks and all 30 native suites pass. This expands executable compiler coverage.
The next decisive step is the frozen real-app correctness and throughput gate;
no browser speedup or default-engine switch is claimed.
