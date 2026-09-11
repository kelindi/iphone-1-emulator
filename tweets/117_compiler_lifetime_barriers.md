# 117 — Retire compiled code before restore and release

## Tweet idea

Restoring the virtual iPhone now invalidates its experimental compiled code
before any saved RAM is applied. Even unfinished compilation requests from the
old phone state cannot sneak back in afterward.

## What you're seeing

This is a nonvisual runtime milestone. [Evidence](../docs/traces/adaptive-lifecycle/README.md)
records actual generated-WASM restore/continuation and native lifecycle tests.
The Settings/Home section of [106's recording](videos/adaptive_compiler_baseline.mp4)
is contextual footage of the unchanged browser build, not a new speed recording.

## ELI5 technical explanation

Compiled code remembers details about the RAM it came from. Restore, free, boot
and engine changes can make that information obsolete. The runtime now drops
that lifetime before the operation begins and releases code handles safely.

A corrupt snapshot leaves the current phone and compiled state intact. A restore
or free attempted while compiled code is running is blocked before changing RAM.
Disabling compilation also cancels captured work that has not been installed yet.

## Why it matters

25 integrated cases, 172 lifecycle checks and all 30 ordinary native suites pass.
This is needed for a reliable adaptive compiler around a saved phone. Background
compilation, memory fast paths, remaining writer qualification and real-app speed
acceptance remain unfinished; the product engine has not switched.
