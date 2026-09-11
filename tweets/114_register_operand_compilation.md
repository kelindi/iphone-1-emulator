# 114 — Compile register operands and ARM shifts

## Tweet idea

The iPhone compiler now handles register-based arithmetic and ARM's tricky shift
rules. 12,731 comparisons against the interpreter pass, and a full machine run
with the MMU enabled still matches every saved byte.

## What you're seeing

This is a nonvisual compiler-coverage milestone. The Settings/Home section of
[106's recording](videos/adaptive_compiler_baseline.mp4) remains contextual footage
of the unchanged phone. [Execution evidence](../docs/traces/adaptive-register-alu/README.md)
records the expanded instruction tests and actual compiled runtime comparison;
no new browser speed recording is claimed.

## ELI5 technical explanation

ARM arithmetic often shifts a register before using it. Shifting by zero, by32,
or by more than32 has different rules, and the shifted-out bit can become the
carry flag. The compiler now represents and emits those operations, with tests
against the interpreter at the boundaries and when registers overlap.

More decoded instructions exposed another limit: one enormous nest of WASM
labels was too deep. Dispatch is now divided into bounded groups while keeping
the full512-instruction unit limit. Tests enter units both at their start and
across group boundaries.

## Why it matters

The retained seed has6,333 more statically admitted instruction positions. That
is broader coverage, not a measured speedup. All97 unit tests and ten integrated
runtime cases pass. Memory operations, wider Thumb support and real-app speed
qualification remain ahead.
