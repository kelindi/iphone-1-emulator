# Second compiler throughput gate — failed; review required

All 18 runs and 54 complete frozen machine checkpoints pass correctness.
The candidate fails the performance gate on every workload. No source mismatch
or harness exception occurred; process exit 1 reflects the failed speed gate.

| Workload | Seeded A mean CPU ms | Corrected B mean CPU ms | A/B throughput |
|---|---:|---:|---:|
| settings | 16772.514 | 44697.005 | 0.375249 |
| contacts | 17150.107 | 45731.026 | 0.375021 |
| notes | 17046.173 | 43787.230 | 0.389296 |

Geometric mean: **0.379797123x**, about **2.63x slower**
than A. Required: >=2x aggregate, with every workload >=1x.

See [report](report.json), [controller](benchmark-workloads.mjs),
[progress](progress.log), and [corrective implementation and tests](../adaptive-corrective-revision/README.md).
The first gate measured0.337144168x; the corrected gate measured0.379797123x.
These are separately run trials, not an interleaved comparison of the two
candidates; do not claim the ratio between them as a controlled optimization
speedup. Neither trial is Mac/browser interaction qualification.

The baseline, workloads, warmup1/version/workload, timed ABBA order, full-state
comparisons and CPU accounting are unchanged. New module-cache source is pinned,
and nested cache statistics are copied before teardown clears the owner cache.
Compiler CPU remains included. No cache survives into the next replay.

The one permitted corrective revision is now measured and exhausted. The
accepted plan says to stop for review on a second failure. No Stage3 expansion,
product activation, threshold adjustment or further tuning has started.

[Required review and proposed next design](review.md).
