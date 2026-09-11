# 107 — Give the compiler explicit instructions and a correctness reference

## Tweet idea

Our iPhone compiler now has its first instruction decoder and three repeatable
real-app workloads. Settings, Contacts and Notes each reached exactly the same
complete machine states twice. Next: connect decoded instructions into executable
blocks without changing the phone's behavior.

## What you're seeing

This is a nonvisual compiler/test foundation milestone. The existing
[106 recording](videos/adaptive_compiler_baseline.mp4), after its opening unlock
section, illustrates the unchanged Settings/Home experience; it does not show
the new offline replay or a compiler speedup. [Checkpoint images and evidence](../docs/traces/adaptive-workloads/README.md)
show the new workload states. No dedicated speed recording is claimed because
no candidate speed benchmark has occurred.

## ELI5 technical explanation

The compiler first needs to understand an instruction before translating it.
We added explicit descriptions for a first set of branches and arithmetic,
including backward jumps, flags and address wraparound. Everything else is
marked as requiring the reference engine; it is not silently skipped.

We also gave the reference phone exactly the same button/touch inputs twice
for three apps and compared whole-machine state fingerprints at three points.
All matched. Inputs are scheduled by emulated work, so a faster host cannot
accidentally change when a finger lands in the guest.

The ABI proposal assigns responsibility for registers, instruction effects,
device timing and faults. A helper cannot merely change guest memory and then
resume compiled code with stale register values. Connected code must also guard
all its instruction pages against writes, not just the page it entered from.

## Why it matters

The first decoder and reproducible correctness reference are implemented;
79 unit tests pass. This is an early compiler building block, not an executable
adaptive engine. Connected code generation, runtime integration, invalidation,
the 2× throughput gate and Mac qualification remain ahead.
