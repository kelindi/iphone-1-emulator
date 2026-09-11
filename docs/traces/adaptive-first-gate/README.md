# First adaptive compiler throughput gate — failed

The declared warmup and ABBA comparison completed all18 runs and54 full-state
checkpoints. Correctness passed; performance failed on every workload.

| Workload | Seeded mean CPU | Adaptive mean CPU | A / B throughput ratio |
|---|---:|---:|---:|
| Settings |16.346s|49.215s|0.3321×|
| Contacts |16.448s|49.953s|0.3293×|
| Notes |16.875s|48.158s|0.3504×|

Geometric mean: **0.337144×**, approximately2.97 times slower. The target was
at least2× with no individual regression. This is a failed CPU-throughput gate,
not evidence of a faster browser phone or Mac interaction qualification.

## Method

Each workload gets one warmup/version followed by A/B/B/A timed runs. A retains
the independently frozen seeded runtime/module; B retains the candidate runtime
while dynamically compiling its actual captured code. Every replay initializes
and restores the same phone identically. All fixed inputs/chunks and100M/300M/
600M full-state checkpoints remain unchanged. Installed units reset per replay.

CPU accounting spans engine setup through teardown and subtracts only main-
thread CPU for excluded snapshot/hash/report/statistics work. Background compiler
CPU during those exclusions remains included using Node's process and main-
thread counters. Main engine setup/install, execution, handle drains and teardown
are included. Shared machine initialization/restore and one-time module/WABT
loading precede the span; segmented wall time is separately diagnostic.

Raw per-run counters, source/runtime/firmware identities, CPU accounting and
aggregation are in `report.json`; progress is retained in `progress.log`.
`benchmark-workloads.mjs` and its digest retain the exact controller. Candidate
implementation evidence is in [generation refresh](../adaptive-generation-refresh/README.md).
No other builds or benchmark processes ran during this trial.

## Consequence

The accepted plan permits one measured corrective revision after this failure.
Profiling precedes that revision; higher generated retirement alone is not a
success criterion. After the correction, require full-state qualification and
repeat the same declared gate. If it still fails, stop for review as agreed.
No automatic QEMU port, arbitrary parameter sweep or default-engine change.
