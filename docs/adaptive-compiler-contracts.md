# Adaptive compiler: Stage 1 correctness contracts

Status: **specified, not implemented or qualified**. Stage 1 establishes the baseline and these acceptance contracts. It does not install a new execution engine, alter the original guest, remove guards, or change the product's lock-screen startup. Existing test results cited below are evidence for the legacy engine, not passes for a future adaptive compiler.

The accepted design retains the current ARM CPU, MMU, devices and interpreter; adds a separate ABI2 IR/connected-code engine; and keeps the legacy ABI1 seed and `jit=0` path usable. A compiled unit has at most **32 basic blocks, 512 static guest instructions and eight distinct physical 1 KiB code granules**. Loops may revisit those instructions, but every invocation remains bounded by the scheduler's remaining retirement budget. Compilation starts at the configured **1,024-entry hotness threshold**, with at most **eight queued requests, 2,048 installed units and 32 MiB installed WASM binary bytes**. These are rejection/eviction bounds, not reasons to skip guest work. Browser module/native-code memory can exceed binary size and must be measured separately.

## 1. Reference state and accounting

The reference is the exact pinned production interpreter, with independent literal architectural expectations for targeted instructions. Compare registers, CPSR and banked registers/SPSRs, CP15, VFP registers/status, exception and exclusive-monitor state, physical RAM, device state, interrupt levels, outputs and scheduler-visible progress. Retain complete raw snapshots without normalization. Derived host addresses cannot be compared across processes; use the existing serializer plus explicit cache/lifecycle assertions, not a weakened guest-state comparison.

[arm_step](../core/src/arm/arm_interp.c) and [s5l8900_run](../core/src/soc/machine.c) have distinct notions of progress. Tests must record **fetched instruction, attempted instruction, completed architectural effect, CPU cycle delta, returned status and scheduler credit** separately:

| Existing reference event | CPU cycle delta | Status / scheduler behavior |
|---|---:|---|
| Invalid CPU mode before fetch | 0 | `ARM_UNDEFINED`; no ordinary successful-step credit |
| Unmasked FIQ/IRQ entry | 1 | No opcode fetched; exception entry returns `ARM_OK` and receives one step credit |
| Prefetch abort | 1 | No opcode executed; exception entry returns `ARM_OK` and receives one step credit |
| Failed ARM condition | 1 | Opcode fetched; PC advances, no conditional body effects; one successful-step credit |
| Data abort during an instruction | 1 in the ordinary attempted path | Fault handling may return `ARM_OK`; preserve instruction-specific partial effects and exception state |
| Unsupported/refused instruction after fetch | May already be 1 | Non-OK status need not receive a successful-step credit; do not infer credit from cycles alone |

A generated success returns the exact completed prefix `n`, with `0 <= n <= supplied_budget`. The owner accounts that prefix **once**. Legacy [arm_wasm_jit_try](../web/jit-runtime.c) adds `n` to cycles/generated totals after generated code returns. ABI2 must document its equivalent ownership before implementation; helpers and the outer loop must not double-count it. Zero-budget entry performs no guest memory operation or architectural change. A zero-progress refusal must lead to a defined interpreter/exception path, never an unbounded retry of the same unit.

Legacy generated exits with budget left are followed by one mandatory interpreter step in the production run loop. An ABI2 dispatcher may explicitly replace this policy, but must prove equivalence through `s5l8900_run`; a synthetic “try until something executes” helper does not test that seam. Generated-call counts can legitimately change. Instruction counts, exit counts and cache hits are not CPU-time or latency attribution.

## 2. Scheduler and interrupt boundaries

The scheduler supplies the budget; compiled code must not enlarge it, resample elapsed host time to manufacture extra budget, or suppress device advancement. The deterministic path uses `retirement_batch_limit` to stop at the next timebase edge. The active host-clock path currently caps batches at **256 instructions**, with immediate boundaries for dirty device levels and changed host inputs. Thus “budget exhausted” does not imply an active device deadline; it may mean caller remainder or the host-clock sampling ceiling.

Check FIQ before IRQ and preserve masking, mode validation and exception priority. Existing pre-step hooks, privileged SVC callbacks, WFI handling, MMIO effects and clock policy must remain visible at their original boundaries. Before a helper can advance time, raise an interrupt, consume input or invoke a callback, make prior progress available to the scheduler exactly as the interpreter does. After such an effect, re-evaluate eligibility before executing another guest instruction. An asynchronous browser event is processed according to existing worker scheduling; the compiler cannot promise physical input latency from an instruction budget alone.

## 3. Helper, fault and deoptimization contract

Each helper must declare its inputs, architectural reads/writes, memory transactions, possible faults, callbacks and exit behavior. Use three explicit categories:

- **Pure eligibility/read-only query:** may inspect valid host-owned metadata or an already validated RAM mapping; cannot walk MMIO, deliver a guest exception or commit instruction state. A refusal leaves architectural state unchanged. Any derived-cache counter mutation must be declared.
- **Architectural instruction helper:** sees the correct current-instruction PC, flags, privilege, operand values and preceding progress. Commit dirty compiler locals before it reads them, or pass an equivalent complete explicit state interface. After it writes state, invalidate/reload affected locals before reuse.
- **External/device-effect helper:** additionally preserves transaction size/order, side effects, timing and interrupt visibility. Do not duplicate, speculate or replay it.

Deoptimization before an instruction may commit the preceding completed prefix and run that instruction once in the interpreter. Deoptimization after a completed instruction resumes at its actual next PC. A partially executed faulting instruction must finish through the original architectural fault path; **never replay earlier stores or MMIO reads/writes**. Preserve the existing base-restored multiword-load abort behavior and the actual partial-store behavior. Test a fault on every transaction position, including cross-granule accesses.

Raw guarded refusal is not an architectural fault. The original [compiler](../tools/jit/compiler.cjs) can return without setting an exit reason for VFP access/rounding refusal or invalid ARM BX target low bits. Every new unit/block entry must initialize control metadata; a previous page-exit reason must not turn a later bare refusal into a retry. Retain actual current member/PC/ISA provenance for stale-code reports.

Unexpected WASM traps are not safe deoptimization points: some effects may already have happened. **Pause execution, preserve the first error and expose recovery. Do not blindly fall back and replay the instruction.** Expected guest faults must use reviewed guest-fault paths, not WASM bounds traps.

## 4. Fetch and data-memory contract

[MMU translation](../core/src/mmu.c) checks access type, privilege, CP15 context, permissions and generation. Execution identity must distinguish virtual PC/ISA and translation context from physical code identity. A matching virtual address alone cannot validate code after remapping; a matching physical page alone cannot validate execution permissions through another alias.

Acquire compiler input through validated executable RAM mappings or a separately specified read-only snapshot interface. Never read arbitrary guest MMIO to discover code. Copy source bytes into immutable request-owned storage, recording mapping/context and content dependencies. Validate them again at installation and execution as required by the guard/version protocol. An unsuccessful speculative fetch must not create guest faults or alter bus state.

Legacy `arm_fetch_cache_try_refill` is specifically nonwalking: it accepts the current valid fetch mapping or an existing successful FETCH TLB entry with matching MMU context, then obtains a whole 1 KiB RAM span. Its successful TLB-derived refill increments a hit counter. It does not establish permission to create an arbitrary new mapping or to ignore a future context change.

Data fast paths require the correct access-specific cache generation, tag/privilege, host mapping, alignment and span bounds. [arm_bus_t](../core/include/arm.h) grants reads through `host_ram`; direct writes require the separate `host_ram_write` consent. A write interposer can revoke that consent. RAM pointers are not permission to bypass framebuffer observers, device windows or write-policy changes. A cache miss is a reason to use the exact slow path, not proof of MMIO or a guest fault.

## 5. Code invalidation and watched writes

Literal guards remain until the physical-version implementation has passed the complete writer audit. Legacy guards compare every instruction in a basic block before selecting its tail; adding an entry can change that guard footprint. Such changes can conservatively invalidate earlier without proving corruption, but cannot authorize stale instructions.

Version identity is attached to **physical 1 KiB granules**, including all aliases. Audit CPU byte/halfword/word/multiword stores, atomic/exclusive operations, direct RAM stores, DMA, device engines, host loaders, debugger/import paths and restoration. Maintain a documented inventory tying each writer to its invalidation operation. Two-granule writes invalidate both. A generation/version wrap must invalidate dependent units before an old value could become valid again.

A watched write must finish its architecturally permitted effects and force an exit **before any subsequent changed instruction can execute**. If it faults partway through, retain the reference partial effects and invalidate every granule actually written. Do not depend on periodic sampling or a later callback to discover an already executable stale write. Direct links, queued compilations and installed units all depend on the same coherent version/context protocol.

Invalidating one entry cannot free a function-table slot still owned by another live entry or an active invocation. Emscripten can deduplicate the same exported function object: shared units need explicit ownership/refcounts or distinct wrappers. Bound retirement queues; never silently lose a pending free, leave a stale executable entry, or reuse a slot while old code can call it. Exact wrapper/table-management cost remains part of performance measurement.

## 6. Compilation requests, installation and cache lifetime

Background compilation must not mutate CPU state, RAM, clocks or device state. Queue saturation and compilation failure continue correct execution using a qualified existing tier. Define what an “entry” hotness event counts before collecting it; do not mix generated invocation, rejected admission, loop iteration and guest retirement. Scheduling/order affects opportunity, not semantics.

Every request binds immutable bytes to unit entry/ISA, translation context, physical dependencies/versions, compiler identity, ABI/policy identity and a machine/restore generation. Results are checked after every asynchronous boundary and immediately before publication. Cancellation, reset, replacement and restore make older generations ineligible. No stale result may install even if its virtual PC and bytes happen to match. Validate imports, function signatures, metadata, limits and dependency identities before making the unit executable; a partial installation must leave a consistent old tier or cleanly roll back.

The current [SeedJIT lifecycle](../web/seed-jit.js) already increments its generation on reset, checks it after awaiting compilation/instantiation, disables native entries before removing owned function-table slots, and drains retired functions. ABI2 must preserve those safety properties while extending ownership to background workers and multiple units. ABI2 must not silently reinterpret ABI1 masks, offsets or imports. `jit=0` must remain an independently functioning reference path, including when compilation is unsupported or fails.

Persistent artifacts are optional derived caches. Keys include **compiler version/hash, ABI, firmware identity and compilation policy**, with source/dependency metadata sufficient for revalidation. A key match is not proof that current RAM still contains that code. Corrupt or incompatible artifacts are rejected without modifying the phone. Native browser code-cache reuse is separate from HTTP/CacheStorage reuse and cannot be claimed from a download-cache hit.

## 7. Snapshots and restores

[Snapshot serialization](../core/src/snapshot.c) excludes host fetch/data pointers and TLB contents, clears them on restore, and preserves specified historical counters. It also resets host-clock anchors. Preserve that schema and behavior: no WASM table IDs, native code addresses, worker promises, compiler queues or host pointers enter a guest snapshot.

Capture a coherent architectural boundary even if compilation is pending. Restoring/resetting/rebinding a phone cancels old-generation work, disables old execution paths and rejects late results. Clear derived mappings; reinstall only after ABI/context/code validation against restored memory. Persisted compiled artifacts may survive outside the snapshot but are not automatically valid for it. Saved current/previous generations and their atomic storage transaction remain unchanged by compiler work.

For deterministic replay, compare full raw snapshot hashes at matching boundaries. If cache policy changes counters serialized by the current schema, first name the precise counter policy difference and retain original bytes; provide a separate exhaustive field-level architectural comparison. Do not strip or normalize fields and call the remaining hash “full raw equality.” Stage 1 observer-only changes require unchanged policy counters as well as state.

## 8. Required validation matrix

All future rows below are **requirements**, not completed adaptive-engine tests. Each fixture needs an independent expectation or interpreter comparator, proof that the intended generated/helper path actually ran, and first-failure artifacts. A passing fallback does not qualify the generated path it avoided.

| Area | Positive and boundary fixtures | Required rejection/negative controls |
|---|---|---|
| Retirement / control flow | ARM/Thumb, both condition outcomes, flag extrema, backward loops, direct/indirect branches, interworking, entry at every block tail; budgets0/1/exact/one-short | Wrong returned count, skipped budget check, repeated zero-progress exit, inherited exit reason |
| Fault precision | No-fetch IRQ/FIQ and prefetch abort; data faults at each multiword transaction; invalid mode, undefined opcode, WFI | Deliberately wrong fault PC/CPSR, duplicate store/MMIO effect, incomplete register restoration |
| Helpers / memory | Cold and genuinely warmed fetch/read/write caches, privileged/user mappings, signed loads, writeback aliases, cross-page and physical-alias cases | Wrong cache tag/generation, revoked write consent, wrong permission, helper clobbered local, slow path counted as generated success |
| Code validity | CPU/DMA/device/host writers; write executing unit, linked successor and alternate virtual alias; restore/remap | Suppressed version bump; stale queued result; wrong-granule invalidation; premature table-slot free; version wrap |
| Asynchronous lifetime | Compile while guest modifies code; reset/restore/disable/replacement at every await; eviction under load | Late installation, malformed imports/ABI/dependencies, queue/unit/byte overflow, leaked/double-freed shared slot |
| Persistent cache / save | Fresh and returning process, cached artifact revalidation, save during compilation, exact restore followed by execution, offline fallback | Wrong firmware/policy key, corrupt artifact, detached old RAM dependency, stale generation after restore |
| Whole machine | Pinned real Settings, Contacts and Notes input trajectories; complete CPU/RAM/device/output checkpoints; both MMU paths | First divergence preserved; no tolerance widening, guest patches or silent interpreter substitution |
| Actual browser | Verified served assets and enabled tier; original lock-screen/unlock/Home prerequisites; normal clock/autosave; retained frames and complete recordings | Wrong startup state, concurrent file mutation/workload, hidden asset mismatch, missing samples/cleanup, unexpected trap |

Known reusable evidence includes [exit-census milestone103](../tweets/103_compiled_exit_census.md), [connected-code milestone102](../tweets/102_connected_compiler_experiment.md) and [gap-fill milestone105](../tweets/105_measured_seed_gap_trial.md). They do not substitute for ABI2 tests. In particular, the gap fixtures executed only175of256 added instructions through generated code in their chosen state;81 safely fell back. Fresh warm-cache and data-dependent tests are necessary.

## 9. VFP limitation and performance decision rules

The unchanged actual-WASM VFP suite has **595 passes and12 failures**: missing exception flags, ineffective directed rounding, and consequent incorrect short-vector refusal/commit behavior. See the [literal failure report](traces/wasm-vfp-conformance/README.md). Relative equality to that baseline is not ARM VFP conformance. Preserve failing expectations, distinguish pre-existing failures from regressions, and do not silently bless every opcode as safe because the interpreter agrees. Any VFP correction is separately scoped and validated for result bits, rounding/status, signed zeros/NaNs, nonfused multiply-add and vector atomicity; do not mix an unreviewed fix into a speed comparison.

Stage 1 records pinned baselines and contracts only. Subsequent Stage 2 requires **at least2× aggregate CPU throughput across the three recorded Settings/Contacts/Notes workloads**, with no regression, under a frozen aggregation rule and equal work; permit one corrective revision, then review the result rather than broad parameter sweeps. This is separate from user-facing Mac Chrome targets: warm app p95≤1s, first app≤2s, touch≤100ms, scroll≥30fps and cached startup≤5s. Baseline tooling must state input-to-readiness/delivery endpoints and whether touch measures canvas delivery or physical presentation.

Freeze source and served asset identities, profiles/cache policy, workload inputs, clock/autosave behavior, sample counts, caps and cleanup before each comparison. Use independently copied immutable experiment sites; shared hardlinks previously let a concurrent lock-screen edit invalidate a comparison. Preserve current product lock behavior and perform its genuine unlock flow before a Home-based workload. First use, warm execution and returning-process compiled-cache reuse are different measurements. Record compilation overhead and invalidation/eviction costs; do not subtract them from user-visible latency. Keep contaminated/incomplete runs and count-only diagnostics explicitly separate from accepted speed results.
