# Original startup and input ordering, observed without a device change

One isolated observational run passes. Removing the five new `ORDER` log lines reproduces the complete Stage J trace exactly; every prior binary artifact matches, including the first/second boundaries, full final SRAM, actual input, queued bytes and first PCM. Both real artifact corruption controls reject. The original 075 and second-input audits pass separately.

All four observed instructions retire in ARM/SVC mode with their expected successors:

| Observed instruction | Cycle | Actual finding |
| --- | --- | --- |
| `c027602c` | 9,873,234,586 | Session configuration returned zero; the original `SUBS` proceeds to `6030`. |
| `c0274778` | 9,873,246,952 | The original refill enters with actual input index 1 and byte count 16,384. |
| `c0274878` | 9,873,247,423 | That refill reaches its normal return path. |
| `c0274778` | 9,873,362,046 | The original refill enters with actual input index 2 and byte count 16,384. |

At every sampled point, the verified driver's raw state word is 3, output bookkeeping words at `15c/160` are zero, and bytes `24d/24e` are zero. No configured target at `619c`, the `61c0` output-start call, or `61c4` return is reached before the unchanged third-request stop.

**This does not establish that the initialization thread was never interrupted, or that its caller was indirect.** The successfully retired session return rules out the earlier absence-of-log inference. The remaining possibilities include a different caller and preemption/suspension after that return. Likewise indices 1 and 2 establish actual descriptor selection, not a complete preroll policy. Original input completion remains distinct from decoder-needs-input.

The first PCM has still not been read or released. No third transfer, device timing change, service command, mask change, consumption or sound is introduced.

## Evidence and scope

- [Actual trace](trace.log), [run report](run.json), and [independent ordering/byte audit](ordering-audit.json).
- [Actual record-byte rejection](negative-record.log) and [actual queue-byte rejection](negative-queue.log), using the same frozen auditor.
- [Source patch](ordering.patch), [observer](source/ordering.c), [external auditor](ordering-audit.py), [build manifest](build.json), [pins](review-pins.json), and [durable inventory](provenance.json).
- [57 native observer checks](test_ordering.log) and [nine auditor checks](ordering-audit-test.log), alongside all prior contracts.

The observer retains full CPSR and all registers. It stages records before execution, then verifies the next PC/mode after the existing instruction step; interrupt diversions cannot count as retired entries. Its six new targets fit the sixteen-slot hook ABI alongside all ten old targets, with a 128-record cap. Driver peeks use the unchanged RAM-only cloned CPU/TLB reader. Tests distinguish wrong successor, changed mode, IRQ diversion and successful branch retirement.

One 4.88-second run, no retry, under the same fixed limits. This is nonvisual protocol evidence, not a speed or playback milestone. No production or snapshot changes.
