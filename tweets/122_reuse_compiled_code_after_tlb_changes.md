# 122 — Reuse compiled code after translation-cache changes

## Tweet idea

The iPhone compiler now keeps more translations useful across memory-cache
changes: roughly135–145 million generated instructions per app workload,
with all nine saved-phone checkpoints still matching exactly.

## What you're seeing

Nonvisual execution qualification. [Evidence](../docs/traces/adaptive-generation-refresh/README.md)
records complete app trajectories and generated retirement. The Settings/Home
section of [106's recording](videos/adaptive_compiler_baseline.mp4) is baseline
context, not a new speed demonstration.

## ELI5 technical explanation

A cache flush does not necessarily mean the code changed. We now check every
code mapping again; if its permissions, physical bytes' version and execution
context still agree, the installed translation can keep its function handle
with the new cache generation. If even one check fails, nothing is refreshed.
Old queued compilations remain invalid.

We also removed a prototype limit that stopped collecting after2,048 attempts.
The limit belongs on live installed units, not on the lifetime number of tries.
Collection now retries while installed memory and handle ownership stay bounded.

## Why it matters

All three600M-credit trajectories again pass all nine full-state checkpoints.
18,178 native checks pass. Generated retirement improves substantially, but
more compiled instructions alone do not guarantee speed. The next step is the
accepted ABBA throughput trial against the existing seeded engine.
