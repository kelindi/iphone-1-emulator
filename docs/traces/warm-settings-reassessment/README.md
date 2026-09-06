# Warm Settings: retained-profile reassessment

This is a source/data-only reanalysis, not a new speed run. The two original-engine sessions from the accepted [smaller-function experiment](../jit-lazy-groups/README.md) used the b3237 runtime and original d9c661 seed, after the Save worker and snapshot classifier changes. Candidate sessions are excluded. These historical profiles do not measure the newly accepted loader or RTC runtime.

The warm launch is primarily guest execution. On the conservative common release-to-ready interval, only 0.66% and 0.81% of emulator-worker samples are idle. Named guest MBX functions account for 0.89% and 0.92%; distinct device/clock functions for 0.66% and 0.96%. Compilation consumes about 43 and 45 ms across the longer observation stages. These figures do not support another inactive-device batching or compilation-layout change as the main warm-launch fix.

| Exclusive sampled residency, common readiness interval | Original arm 1 | Original arm 4 |
| --- | ---: | ---: |
| Generated seed (existing module attribution) | 29.90% | 28.97% |
| arm_step, including inlined operations | 28.35% | 28.61% |
| Separate memory/MMU/bus helpers | 11.67% | 11.95% |
| emu_run, including inlined operations | 11.89% | 11.06% |
| region_find | 5.70% | 6.51% |
| Separate ALU helpers | 3.82% | 3.48% |
| Separate VFP helpers | 1.70% | 1.30% |
| Idle | 0.66% | 0.81% |

Of the memory-helper samples, 307.8/321.6 ms and 320.2/332.9 ms have arm_step in their ancestor chain. The containing functions and separately sampled helpers must not be double-counted. These are sampled residency proportions, not hardware-cycle attribution or proof that deleting a source path saves its entire measured share. Function-start-only WASM sample locations cannot divide inlined arm_step or emu_run operations further.

The 2.767/2.817 s release-to-ready windows are shorter than the 3.312/3.380 s traced stages because the observer confirms stability for 500 ms. Whole-stage worker scheduler CPU is 3.100/3.170 s. Whole-stage retired counts are 95,222,333/94,814,554, of which 26,853,018/26,843,208 are interpreter steps. Those counts include the confirmation interval; they are not exact release-to-ready instruction counts. Host GPU-process CPU includes compositor, readback and recording work and is not a guest MBX measurement.

## Provenance and clipping

[analyze-existing.py](analyze-existing.py) reads retained profiles, stage traces and reports only. [analysis.json](analysis.json) retains every category, function, source hash, whole-stage counter, scheduler measurement and conservative timing interval. [body-identity.json](body-identity.json) proves all non-code sections and every same-index function body except snap_ram (154) are identical between the previously named 603679 runtime and b3237. The [independent full symbol bijection](../browser-cpu-attribution/renumber-proof/README.md) therefore applies to unchanged hot functions. Function 154 remains changed/unattributed here. Generated-module naming retains the prior seed/pipeline attribution limitation.

The page clock's action start was not recorded in trace coordinates. Its position is bounded by the trace stage duration minus the observer duration: 11.969 ms and 29.461 ms. The script reports both the intersection and union of all possible release-to-ready intervals, rather than choosing a favorable alignment. The table uses their guaranteed intersection. Profiler overhead remains; these are two historical sessions, not a p95 estimate.

## Next falsifiable measurement

The interpreter fallback is the strongest remaining measured CPU target. Existing exit censuses count transitions out of generated functions; they omit entire interpreted stretches outside the 512 seeded regions. The parked coverage experiment added 959 positions within 210 existing regions, so it does not answer whether different whole pages contain substantial supported hot work. Conversely, the parked continuation experiment already demonstrates that reducing crossings by itself does not guarantee navigation improvement.

Prepare an [all-fallback census](../jit-fallback-census/README.md) before changing coverage: count every actual interpreter step and fetched opcode during one warm Settings window and a Home control, partition original seed-page absence, missing admission, and actual guard/exit provenance. No coverage percentage or optimization is inferred until those counts pass conservation and state-equivalence checks. A roughly 3.1-to-2 s target requires about 35% less elapsed time; eliminating a six-percent lookup alone cannot supply it.
