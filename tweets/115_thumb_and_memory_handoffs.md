# 115 — Thumb instructions and precise memory handoffs

## Tweet idea

The iPhone compiler now handles more Thumb instructions—the compact instruction
set used alongside ARM. Full-machine tests also preserve tricky memory faults,
partial stores and guest code changes exactly.

## What you're seeing

This is a nonvisual compiler milestone. [Execution evidence](../docs/traces/adaptive-thumb-memory/README.md)
contains the test results and preserved fixture failures. The Settings/Home
section of [106's recording](videos/adaptive_compiler_baseline.mp4) is contextual
footage of the unchanged browser phone, not a new speed demonstration.

## ELI5 technical explanation

The same CPU understands two instruction formats. We added arithmetic and shifts
for its compact Thumb format. A real MMU-enabled machine run now executes that
compiled code and produces exactly the same saved state as the original engine.

Memory is more delicate: a store can write its first word and then fault on the
second. The compiler currently hands these instructions to the original engine,
which must keep that first write and report the right fault. New tests check
those details, register preservation, timer access and code changed by the guest.
Three mistaken test setups were caught and retained in the evidence.

## Why it matters

32,631 interpreter comparisons, 100 unit tests and nineteen integrated cases
pass, including actual Thumb execution and precise
memory fallback. This builds the coverage and correctness needed for real-app
trials. It does not establish faster app launches; generated memory fast paths,
physical code versions and the performance gate remain unfinished.
