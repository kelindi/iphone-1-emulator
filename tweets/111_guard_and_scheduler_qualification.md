# 111 — Real fetch mappings and the scheduler protocol

## Tweet idea

The compiler's guards now pass tests against mappings created by the real iPhone
CPU interpreter. Its optional scheduler path also preserves complete machine
snapshots across the tested stopping reasons. Next: join these pieces to the
actual WASM compiler runtime.

## What you're seeing

This is a nonvisual native integration milestone. The Settings/Home section of
[106's recording](videos/adaptive_compiler_baseline.mp4) remains contextual footage
of the unchanged phone. [Native execution evidence](../docs/traces/adaptive-dispatch/README.md)
contains the new guard and scheduler tests; it is not a new browser speed result.

## ELI5 technical explanation

A code address only works while it still maps to executable memory. Tests now
let the real interpreter establish that mapping, then change the code or revoke
execution permission. The guard refuses without touching the device bus or
creating a premature fault; the interpreter delivers the fault normally.

The scheduler also understands compiled stopping reasons. It can follow a
completed edge directly, interpret an unsupported instruction once, or stop at
a requested boundary. In native tests, each policy produces exactly the same
saved machine as ordinary interpretation. Fatal results stop without replay.

## Why it matters

The guard and scheduler pieces now have real-core evidence. The scheduler test
still uses a controlled callback, so actual WASM, registry installation and
lifetime management must be connected before full-phone testing. No speedup or
product activation is claimed.
