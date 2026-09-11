# Required review after the second failed gate

## Decision

Stop this prototype's tuning cycle as specified in the accepted plan. Retain
its correctness work and evidence, but do not activate it in the product or
advance automatically to the background-worker stage. The usable-speed goal is
unmet; a new architecture/implementation cycle needs an explicit scope decision.

## What the measurements establish

All 54 full-state comparisons pass; the corrected candidate remains about2.63x
slower than the frozen seeded engine. The cache avoids5,362–6,017 compilations
per trajectory. Runtime counters match the previous candidate exactly, so this
correction changes overhead rather than instruction coverage.

The full-workload candidate averages6.45–6.97 generated instructions per call.
85.5–86.9% of generated invocations exit BEFORE a reference-owned/unsupported
instruction. It generates135–145M instructions versus418–460M for the seeded
engine. The earlier profile identifies admission/dispatch, compilation and
interpreter execution as major costs; its percentages describe that earlier
revision, not an unperformed second-revision profile.

| Workload | Seeded mean CPU s | Candidate mean CPU s | Minus measured compiler-callback CPU s |
|---|---:|---:|---:|
| Settings | 16.773 | 44.697 | 40.035 |
| Contacts | 17.150 | 45.731 | 40.097 |
| Notes | 17.046 | 43.787 | 39.157 |

The subtraction is an optimistic counterfactual, not a measured alternative or
rigorous bound. Callback process CPU can include overlapping background work,
and background work outside those intervals is not removed. Even this estimate
is only0.4273x baseline throughput. A worker may improve responsiveness, but
moving compilation off-thread does not remove its cost from the accepted CPU
gate and is not supported as a sufficient fix.

## Proposed next design, not authorized implementation

Preserve the seeded engine's existing broad fast coverage and use adaptive
compilation for measured opportunities to improve connected execution. The
current prototype replaces too much existing fast execution with expensive
short compiled bursts and interpreter handoffs.

A new proposal must explain three concrete things before another tuning cycle:

1. How it retains existing coverage and chooses a tier without doing expensive
   admission work on almost every interpreted instruction.
2. How connected execution avoids the measured short-run/fallback burden, using
   weighted actual hot-path/exit evidence to choose missing operations.
3. How ABI1 and ABI2 safely share code-memory invalidation, lifetime, scheduler
   accounting and precise handoffs. Existing uninstrumented ABI1 stores cannot
   simply be mixed with ABI2's tracked code assumptions.

The existing decoder, IR, emitter, differential tests, snapshot contracts,
physical-version work and strict installation remain useful. A hybrid design
is a hypothesis, not a promise of2x speed or native performance. No additional
parameter sweep, QEMU port, gate change or product rollout follows automatically.
