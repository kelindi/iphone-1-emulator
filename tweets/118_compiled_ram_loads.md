# 118 — Keep warm RAM loads inside compiled code

## Tweet idea

The iPhone compiler can now load data straight from already-validated RAM.
Cache misses, devices and faults still hand off precisely to the original CPU
engine—without counting the same memory operation twice.

## What you're seeing

This is a nonvisual compiler milestone. [Evidence](../docs/traces/adaptive-ram-loads/README.md)
records actual generated loads and full-machine comparisons. The Settings/Home
section of [106's recording](videos/adaptive_compiler_baseline.mp4) is contextual
footage of the unchanged browser build, not a new speed demonstration.

## ELI5 technical explanation

A load reads a number from memory. Previously these instructions sent execution
back to the interpreter. Now the compiler asks whether the emulator already has
a valid RAM mapping for that address. If so, generated code reads the value and
continues. If not, it stops before changing the destination or base register.

The tricky part is knowing what “valid” means: privilege, memory generation,
alignment and region boundaries all matter. A refusal also must not count a
cache miss before the interpreter counts that same miss. Tests check the saved
machine bytes and those counters separately.

## Why it matters

37 integrated cases, 33,043 interpreter comparisons, 107 unit tests, 52 native
cache checks and all 30 native suites pass. Real MMU-backed generated loads now execute, expanding the
compiler beyond arithmetic and branches. Stores, remaining writer qualification
and real-app performance acceptance remain; no launch-speed gain is claimed yet.
