# 116 — Track changes to physical code memory

## Tweet idea

The iPhone compiler now remembers which physical RAM regions have changed.
That catches stale compiled code even when a program changes an instruction and
then puts the original bytes back.

## What you're seeing

This is a nonvisual runtime milestone. [Execution evidence](../docs/traces/adaptive-code-versions/README.md)
contains full-machine comparisons, write-observer tests and the remaining writer
audit. The Settings/Home section of [106's recording](videos/adaptive_compiler_baseline.mp4)
remains contextual footage of the unchanged browser build, not a speed demo.

## ELI5 technical explanation

Different virtual addresses can point at the same physical RAM. We give each
1 KiB physical region a version number and advance it after committed writes.
Captured code remembers that number, so changed code can be rejected before
installation or execution. Old function handles still retire through their
existing ownership queue.

Cached CPU stores and graphics copies can bypass ordinary bus writes, so they
need notifications too. Tests check the actual spans and bytes after CPU, DMA
and graphics writes. Overflow stops the tracked lifetime instead of wrapping
around and accidentally accepting old code.

## Why it matters

24 integrated cases, 652 writer checks, 8,511 registry checks and all 30 native
suites pass. This supplies a foundation for replacing repeated instruction-byte checks with
cheaper validity checks. Literal guards remain in place: the audit found legacy
generated stores and boot/restore lifetime paths still needing coverage. No
browser speedup or version-only execution is claimed.
