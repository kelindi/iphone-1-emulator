# Source-only reassessment: inactive-device deadlines are already largely batched in the browser

**Conclusion:** existing evidence does not support a large browser improvement from simply allowing execution across inactive-device timebase edges. The interactive browser already does that. Do not increase batching limits or reuse the WFI deadline table as a new optimization without an active-clock cost measurement. No code, profile, benchmark or new candidate was run for this reassessment.

## The two timing modes differ

`core/src/soc/machine.c:1797` (`retirement_batch_limit`) bounds deterministic execution at the next CPU-to-timebase edge. At the default 412:6 ratio, that is normally 68/69 retired instructions. This is the mode used in the fixed-instruction Node replay. It preserves the literal instruction/tick oracle, including exact timer snapshots.

The actual browser enables `emu_host_clock` on Resume (`web/emu-worker.js:96` and `web/emu_api.c:104`). `run_retirement_batch_limit` (`machine.c:1817`) explicitly bypasses that edge in active-clock mode and caps a clean retirement group at **256 instructions**. Active host time is sampled only after **4,096 pending retirements**, or required WFI/error/ending boundaries (`run_clock_retired`, `machine.c:2001`; constants in `core/include/soc.h:3942`). At the documented 16–30 M instructions/s that sample interval is about 0.14–0.26 ms, not a separate clock call for every generated region. Consequently the older deterministic exit census's 14.82% budget exits cannot be described as the browser's budget-exit rate.

A fully consumed generated budget does **not** force an interpreter instruction: `machine.c:2194` breaks the batch, accounts its retired work, then re-enters the outer loop and attempts generated execution again. The unconditional fallback step is used only when the generated attempt leaves remaining budget. That latter path was the just-tested continuation candidate; its exact fixed-workload gain did not become a meaningful browser p95 improvement.

Worker scheduling is also already batched: `web/emu-worker.js:71` invokes 50,000-instruction core slices until about six milliseconds of host execution, then yields through MessageChannel. `emu_api.c:351`'s 200,000-instruction input chunk ceiling is larger than this ordinary worker request and is not an extra short limit in those calls. Input responsiveness would not automatically improve from extending either bound.

## What still creates a boundary

- A non-RAM bus read or write marks `level_dirty` (`machine.c:359,470`). Reads cannot generically be declared harmless: FIFO drains, VIC acknowledge and other device reads change lines. The post-retirement refresh preserves these effects.
- External input changes, WFI pacing, callbacks and exceptions retain explicit boundaries. They are semantic events rather than inactive-device timer overhead.
- Every 256-instruction clean active batch repeats the eligibility/outer-loop accounting. Every 4,096 pending retirements samples host time. These are real costs, but the source alone does not attribute their share of the fused `emu_run` profile.
- Generated regions still exit for unsupported instructions, mapping/alignment/span guards and region changes before the budget. A larger device horizon does not remove those exits.

`machine.c:1070`'s `s5l8900_next_wake` is a **WFI wake-up** contract, not a complete next-observable-device-event oracle while the CPU runs. It ignores masked VIC lines because those cannot wake a sleeping CPU. A running CPU can still observe masked timer/display/PMU state through reads, and DMA or synchronous command completion must retain their original boundaries. Reusing that table for arbitrary retirement batching without a new contract would conflate wake eligibility and visible device evolution.

## Existing cost ranking and falsifiable next gate

The accepted original browser profile gives warm exclusive sampled residency of **29.09% `arm_step`, 28.07% generated functions, 12.22% fused `emu_run`, 11.83% memory/MMU/bus and 6.37% region lookup**. Distinct device/clock symbols account for **1.15%** (1.13% first use). These are statistical residency values with the documented original module/symbol limits; inlined clock/accounting work remains in `emu_run`, so 1.15% is not a strict upper bound on all timing machinery. It is still not evidence that repeated inactive-device advancement is the dominant cost. The independently preserved profile is [here](../browser-cpu-attribution/README.md).

If this hypothesis must be tested, use one bounded **actual active-clock** diagnostic that counts completed full 256-instruction groups, dirty/input/WFI/exception exits, short outer-call tails, and periodic versus forced clock samples. Correlate those counts with a name-preserving profile that separates the fused outer boundary from per-fallback dispatch. The question is: **does batch-boundary work itself consume a material part of launch CPU, or are most samples inside the fallback/generated instruction path?** Reject deadline widening if the latter holds. Do not infer saved time from deterministic budget counts or from the whole 12.22% fused function.

A useful structural sanity bound is that 100M retirements contain at most 390,625 completed full 256-instruction groups; short device exits and outer tails are separate and cannot safely be removed merely by raising the ceiling. This is a count bound, not a runtime saving estimate. Increasing the maximum group without keeping the exact 4,096-retirement host-sample boundary can also change interrupt/input latency and sample overshoot. Any later implementation would require a new explicit boundary contract and adversarial timer, masked-device, MMIO-read, DMA, input, WFI and host-clock-failure proofs; the deterministic full-raw replay alone would not establish active-clock fidelity.

For now the larger measured opportunity remains first-use compilation, which is being tested separately, and the unresolved combined fallback/dispatch work rather than a new inactive-device deadline shortcut. No parked candidate is reopened by this note.
