# 124 — Correct compiler, failed speed gate: time for review

## Tweet idea

We cut thousands of repeat compilations from our browser iPhone experiment. All 54 machine-state checks still pass, but the second speed test remains 2.6x slower than the existing engine. The lesson: translating instructions is not enough when compiled runs are tiny and handoffs dominate.

## What you're seeing

[Recording](videos/compiler_second_gate.mp4), entire 00:00–00:13.820 sequence: the labeled seeded A then corrected adaptive B replay the first 100M Settings workload credits. [Thumbnail](thumbnails/compiler_second_gate.jpg). These are actual emulator pixels, ending on Settings still loading, not a finished app launch. This separate visualization includes recording overhead and is not the source of CPU speed figures or a Mac/browser interaction benchmark.

Both recorded checkpoints match the frozen reference. All 205 source frames and encoded presentation timestamps pass validation; full video decoding passes. [Recording evidence](../docs/traces/adaptive-second-gate/recording-report.json).

## ELI5 technical explanation

We stopped rebuilding the same translated code repeatedly by keeping a bounded cache of compiled modules. Every use still gets fresh safety checks and its own execution instance. We also reject incompatible CPU contexts before doing unnecessary mapping work. Five cache/ownership tests and 18,257 native runtime checks pass; all nine preliminary full-app checkpoints match.

The final controlled test passes all 54 checkpoints but reaches only **0.3798x the existing engine's throughput**, against a target of at least 2x. The first trial was 0.3371x; these separately run trials do not constitute a controlled direct comparison of the two candidates.

The bigger problem remains: compiled calls average only 6–7 instructions, and about 86% hand back to the interpreter before a reference-owned operation. The existing engine executes much more code in its compiled path. Even optimistically subtracting measured compiler-callback CPU leaves this candidate well behind; moving compilation to a worker is not established as a sufficient fix.

## Why it matters

The accepted plan allowed one correction and required review after a second failure. That allowance is exhausted. We have stopped this tuning cycle and kept the product on its existing engine. The usable-speed goal remains unmet.

[Gate evidence](../docs/traces/adaptive-second-gate/README.md) and [required review](../docs/traces/adaptive-second-gate/review.md) preserve the results. The proposed next design would retain the seeded engine's broad coverage and add adaptive compilation selectively, after solving shared writer tracking and tier handoffs. This is a hypothesis requiring a new scope decision, not an implemented speedup.
