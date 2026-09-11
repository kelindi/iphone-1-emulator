# 121 — First full real-app compiler replays

## Tweet idea

The compiler has completed real Settings, Contacts and Notes workloads.
All nine saved-phone checkpoints match the original engine exactly.
The next problem is clear: keeping compiled code usable as memory contexts change.

## What you're seeing

Nonvisual compiler qualification. [Evidence](../docs/traces/adaptive-app-replay/README.md)
contains full-state hashes and actual generated-execution counts. The Settings/Home
section of [106's recording](videos/adaptive_compiler_baseline.mp4) remains baseline
context only. This is not a new app-launch or speed recording.

## ELI5 technical explanation

We now watch real execution, translate repeatedly visited code and install it
without changing the phone's input schedule. An index also finds translations
without searching the entire table on every instruction.

Each app trajectory runs 600 million execution credits and checks the entire
saved phone three times. All nine checkpoints match. But the prototype uses up
its compilation attempts as memory contexts change, then falls back for most
of the run. Correctness passed; useful compiled coverage remains the problem.

## Why it matters

This is full real-workload evidence beyond isolated instruction tests. Generated
retirement is 569,061 / 736,058 / 760,959 for Settings / Contacts / Notes. Those
counts are far too small to establish the intended speed benefit. We are now
working on safe re-admission and continued collection, keeping pending-generation
checks strict. The 2× throughput trial and browser activation remain pending.
