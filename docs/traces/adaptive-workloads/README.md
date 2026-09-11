# Stage 2 workload and decoder foundation

Six seeded-engine runs (Settings, Contacts, Notes, each twice) completed with
identical full snapshot hashes at 100M, 300M and 600M scheduler credits for each
pair. All runs exercised generated code. [Raw report](report.json),
[lossless frames](frames.tar.gz), [source](source/), [unit results](unit-tests.log).
The 300M frames were manually inspected: [Settings](settings.png),
[Contacts](contacts.png), [Notes editor](notes.png).

This is workload determinism qualification, not a speed comparison. Reported
CPU/wall time is diagnostic only: startup tiering and unrelated lightweight
work were not excluded. No candidate engine was run, and no 2× gate was tested.
Credit is the machine scheduler's accounting unit, not necessarily a fetched
instruction. Inputs occur exactly at declared credit boundaries, with no host
clock pacing; this cannot predict browser latency.

Input snapshot was read once into owned memory and SHA-pinned in the report.
An independent copy is retained at `.context/adaptive-compiler/workload-home.snap`;
the starting state is the existing real Home fixture, without new guest patches.
It is not distributed in this small source/evidence archive. Frozen runtime and
seed identities are from [Stage 1](../adaptive-compiler-baseline/README.md).

Reproduce from the repository root into a new output directory:

```sh
FIXTURE_SNAPSHOT=.context/adaptive-compiler/workload-home.snap \
ARTIFACT_DIR=.context/adaptive-compiler/workloads-new \
node tools/jit/adaptive/capture-workloads.mjs
```

The initial decoder produces explicit immediate-ALU/branch descriptions and
fallback boundaries. It does not emit or execute WASM. Four decoder tests cover
signed branches, wraparound, link return addresses, conditional edges, reserved
Thumb encodings, rotated carry and unsafe fallbacks. Two workload tests cover
exact scheduling across chunk boundaries and invalid trajectories. All 79
JavaScript unit tests pass. ABI2 remains a proposal awaiting runtime integration.

Before performance changes, use these fixed trajectories and pin any separate
warmup/candidate setup. Gate aggregation: for each workload take mean CPU time
across two timed runs per version in ABBA order following one untimed warmup
per version; speedup is the geometric mean of the three control/candidate ratios.
Require geometric mean ≥2 and each individual ratio ≥1. Include all compiler
work in process CPU accounting; separately report wall time and compilation.
Validate all three full-state checkpoints on every run. This rule is declared
before a candidate exists and must not be tuned to candidate results.
