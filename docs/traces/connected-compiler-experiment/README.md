# Connected ARM/Thumb compiler feasibility

Decision: reject this candidate. Production core and seed remain unchanged. This is a bounded architecture prototype, not completion of the phone or proof of a browser speed ceiling.

## Backend choice

The [QEMU source assessment](qemu-assessment.md) confirms ARM1176 and ARMv6 MMU support, but the WASM backend depends on QEMU CPU state, TLB and exception recovery. Reuse requires adopting those subsystems and adapting our RAM, devices, scheduling and snapshots. We chose the existing emitter for a smaller executable experiment; QEMU remains unbenchmarked here.

## Implementation

Combine up to four connected existing regions in a single compiled function. Keep guest registers and flags in WebAssembly locals across eligible region boundaries. Retain original opcode coverage, raw instruction checks, memory slow paths and retirement budgets. The 512 existing regions become 114 multi-member clusters plus 175 singletons; 337 regions participate in clusters. Grouping follows static known edges without runtime frequency weights.

A new isolated helper checks installed destination admission and safely reuses an existing instruction-fetch mapping, without walking page tables or spilling guest registers. Distinct export wrappers preserve each region's table-slot lifetime. Stale code is attributed to the actual member. Reset the exit reason before each member so an unsupported VFP instruction cannot inherit a previous continuation status. See [generator design](generator-design.md) and retained source.

## Validation and result

- 32 generator assertions pass, including connected execution, ARM-to-Thumb transitions, stale code, budget exits and distinct table ownership.
- 403 focused interpreter/original-seed/connected-seed comparisons pass using the actual current CPU/MMU. These cover memory faults, instruction translation permissions, aliases, stale code, interrupts, interworking and timer boundaries. Retain both the preliminary result and the final rerun after the exit-status fix. The old synthetic retire-after-C-return injection was replaced with distinct-slot and stale-target coverage because C return boundaries intentionally change.
- Real phone replay: Settings press/release at 0/5M guest instructions, Home at 200M/205M. All complete snapshot hashes match at 25/50/75/100M, then at 400M for both warmups and all timed runs. Guest state includes RAM and device state; compiler counters intentionally differ.
- Same augmented core in both arms, original versus candidate seed. Compile before timing, 400M warmup per version, then 400M control/candidate/candidate/control. Mean process CPU time: **11.160s original, 11.614s candidate; candidate 4.07% slower**. Mean elapsed run time: **11.136s versus 11.594s; 4.12% slower**. Two timed samples per version establish rejection of this pilot, not a broad confidence interval.
- Compiled entries fall from 42,050,405 to 38,783,557 (**7.77% fewer**); compiled instructions rise from 301,802,126 to 305,696,146. Fewer handoffs do not establish less total work. Source structure suggests added cluster routing offsets savings; no new host profile apportions that cost.

[Raw replay report](screen-result.json), [focused report](focused-report.json), [generator report](generator-test-report.json), and build/source pins are retained. Fixed-work replay runs in Node without browser host pacing, so these numbers are not app-launch timings. A separate short actual browser recording documents candidate behavior, not a browser performance comparison or save/reload acceptance.

## What this resolves, and what remains

Small statically grouped clusters with unchanged coverage and guards do not yield useful speedup. This prototype does not test frequency-weighted long traces, code-page write invalidation replacing repeated guards, richer memory fast paths, broader instruction coverage or an optimizing QEMU compiler. The baseline WASM floating-point conformance failures remain open.

Before another implementation, collect an exact exit-reason and destination-edge census for warm Settings: how many guest instructions return because of unsupported instructions, cold memory/fetch mappings, code boundaries, faults or scheduled deadlines? Rank those exits by attributable work and frequent paths. This should determine whether larger traces have enough eligible work, or whether memory/opcode fallback must be addressed first. Do not promise 2× or repeat blind cluster-size sweeps.

## Reproduction limits

Scripts retain their original workspace-relative paths. They depend on the repository compiler, Emscripten/WABT, original firmware fixture and the pinned current core archive described by runtime-build.json. Firmware and 249MB snapshots are not duplicated here. Hash equality was checked during replay; checkpoint hashes, not full snapshots, are retained. The oracle module and candidate runtime/seed are archived. No production files were installed.
