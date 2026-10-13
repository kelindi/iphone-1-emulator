# Adaptive compiler implementation

> Historical recordings and publishing artifacts have been removed from Git history. The summaries below describe the original runs; remaining test reports and screenshots are retained. Historical hash receipts may predate this archival cleanup.

Accepted target: genuine iPhone OS running locally in Chrome on the user's Mac,
with warm common-app launch p95 ≤1s, first launch after opening a saved phone
p95 ≤2s, touch feedback p95 ≤100ms, scrolling ≥30 distinct frames/s and cached
startup to interactive lock screen ≤5s. These are targets, not achieved results.

## Stage 1 — Baselines and contracts

- Freeze independent baseline assets and selected source/dependency inputs;
  detect concurrent source changes and verify served hashes.
- Qualify the benchmark against the current real lock-screen startup, requiring
  a genuine unlock and verified Home before navigation.
- Record one first and thirty warm Settings/Home pairs in cloud Chrome; record
  equivalent reference-Mac metadata/results when Mac execution is available.
- Establish [correctness contracts](../docs/adaptive-compiler-contracts.md),
  including precise faults/retirement, all-writer physical invalidation,
  background installation lifetimes and snapshot compatibility.

Tooling and commands: [baseline instructions](../tools/jit/adaptive/README.md).
Reference Mac remains required for acceptance; no callable RunLocalCommand is
exposed in the current cloud session. This does not prevent local architecture
work, but cloud timing must not be labeled Mac qualification.

Cloud baseline completed: 62 actions, 30 warm samples/action; Settings p95
2566.7 ms and Home p95 867.9 ms. See [preserved evidence](../docs/traces/adaptive-compiler-baseline/README.md).
Independent copies, served hashes, real unlock and correctness contracts are in
place. Mac measurement remains outstanding; no adaptive compiler is installed.

## Stage 2 — Architectural prototype

Retain CPU/MMU/devices/interpreter. Build explicit decoded operations and control
flow, connected hot paths, shared memory slow paths and physical code versions.
Preserve literal guards until every relevant writer is covered. Units are bounded
by32 blocks/512 static instructions/eight physical1KiB granules; scheduler budgets
still bound dynamic execution. Reuse existing legacy execution as the reference.

Freeze representative Settings, Contacts and Notes instruction/input workloads
before performance changes. Require exact architectural results and2× aggregate
CPU throughput against the current seeded engine, with no workload regression.
Allow one measured corrective revision, then stop for review if the gate fails.
Do not automatically start a QEMU port or sweep arbitrary sizes.

## Stage 3 — Adaptive coverage

Discover actual hot code; compile at1,024 observed entries in one background
worker. Specify entry-event counting before collection. Bound the queue at8,
installed units at2,048 and installed binary bytes at32MiB. Reject stale work;
install and retire safely. Add validated persistent cache reuse and a precompiled
seed from qualified startup/common-app runs. Extend integer coverage by measured
remaining cost; retain precise fallbacks for sensitive operations.

## Stage 4 — Product qualification

Use a separate ABI2 and opt-in `jit=adaptive`; preserve legacy ABI1, `jit=0`,
original Apple software, saved-phone format and concurrent lock-screen work.
Qualify actual Mac interaction targets, all16 app launch smoke tests, save/import,
previous-runtime restoration, offline startup, corrupted caches and cancellation.
Only then consider changing the default engine. Known legacy WASM VFP failures
remain explicit; relative equality does not establish architectural conformance.

Record significant milestones in the engineering log. Keep visual diagnostic
recordings and thumbnails in ignored `.context/` paths. Preserve raw results, failures,
source identities and declared limitations. Work in independent copies to prevent
cross-session edits from invalidating comparisons.

### Stage 2 implementation progress — 2026-09-08

The first explicit decoder handles a bounded branch/immediate-arithmetic slice
and emits fallback boundaries for unsupported operations. It does not yet emit
WASM. Settings, Contacts and Notes fixed input trajectories each replay twice
with exact full-state checkpoint equality. [Evidence and predeclared aggregation](../docs/traces/adaptive-workloads/README.md).
The [ABI2 proposal](../docs/adaptive-compiler-abi.md) specifies scheduler/retirement
and slot ownership. Next: build connected decoded units and integrate the explicit
result contract before expanding memory helpers or asynchronous compilation.

Connected-unit construction now follows decoded successors across captured
windows, splits loops/joins, tracks physical aliases and enforces all three unit
limits. Nine focused tests cover its boundaries, including conditional dynamic exits. [108 evidence](../docs/traces/adaptive-connected-units/README.md).
The next step is executable WASM emission and ABI2 runtime integration; graph
construction alone does not meet the architectural performance gate.

The first executable WASM emitter now implements the decoded integer/branch slice
with complete spill/result reporting, exact budget checks and pure guard imports.
4,098 comparisons against the retained C interpreter and all 95 unit tests pass.
[109 evidence and limitations](../docs/traces/adaptive-emitter/README.md).
This is isolated CPU execution: next implement ABI2 admission/result validation
and scheduler integration, then real guards/invalidation and wider coverage.

The ABI2 C header and invocation/result validator are now implemented. Native
negative tests and actual emitted WASM through a C function-table bridge pass;
cycle credit occurs once and ISA changes require BOUNDARY. [110 evidence](../docs/traces/adaptive-result-contract/README.md).
This is a tested interface, not scheduler integration. Next add unit admission,
real permission-safe guards and machine dispatch using this validated result.

Initial fetch-witness guards now capture validated fetch-cache metadata and
reject changed context/generation/epoch, pending exceptions/interrupts and changed
instruction bytes without walking tables or changing CPU state. Focused native
checks pass using constructed cache state. Real interpreter-created mapping
qualification and scheduler dispatch remain outstanding; no production activation.

Real interpreter-created identity/alias mappings now qualify witness capture,
literal mutation rejection and XN fault handoff. Opt-in scheduler dispatch is
implemented behind `S5LBOX_ADAPTIVE_JIT`; native callback tests match full machine
snapshots for all exit policies and verify fatal no-replay behavior. [111 evidence](../docs/traces/adaptive-dispatch/README.md).
The runtime registry, actual WASM guard import, lifetime management and full
emitter-to-machine integration are still required before the three-workload gate.

The emitted WASM, bounded C registry, current-mapping/literal guards and machine
scheduler now execute together in an isolated build. Three synthetic workloads
match complete machine snapshots; fatal state blocks repeated single-step calls.
8,473 registry checks and 1,067 interpreter tests pass. [112 evidence](../docs/traces/adaptive-runtime/README.md).
Next qualify MMU-on generated execution and device boundaries, expand instruction
coverage and physical versions, then run the frozen real-app correctness/speed
gate. Product defaults and background compilation remain unchanged.

MMU-on connected and nonidentity small-page execution, guarded load fallback,
and one deterministic Timer4 interrupt now pass full-snapshot comparison through
actual generated WASM. The first cross-window MMU run exposed missing TLB-hit
accounting; the runtime guard now reproduces nonwalking fetch-cache bookkeeping
after successful pure checks. [113 evidence](../docs/traces/adaptive-mmu-timer/README.md).
Next expand decoded instruction coverage toward the real-app workloads; active
clock/DMA/partial memory effects and physical code versions still need qualification.

ARM register-operand ALUs and immediate/register shifts now compile, including
carry and boundary rules. 12,731 differential checks, 97 unit tests and ten
integrated runtime cases pass. Dispatcher nesting is bounded without reducing
512-instruction units. [114 evidence](../docs/traces/adaptive-register-alu/README.md).
The retained seed admits6,333 additional static positions; this is not weighted
coverage or speed. Next address remaining memory/Thumb coverage and physical
code validity before the real-app gate.

Thumb shifts, register ALUs/MUL and high-register arithmetic now compile.
32,631 interpreter comparisons and 100 unit tests pass. Nineteen
integrated cases pass, including real MMU-enabled Thumb execution and precise
memory fallback after generated prefixes. Fault PC, partial stores, writeback,
MMIO and stale code handoffs have explicit checks alongside full snapshots.
[115 evidence](../docs/traces/adaptive-thumb-memory/README.md). Memory fast paths,
all-writer physical versions and frozen real-app qualification remain next.

Physical 1 KiB version tables now bind to the runtime epoch and captured fetch
witnesses. Committed CPU cached stores, bus/DMA writes, MBX direct spans and host
loads notify; stale captures/entries and active guards refuse changed versions.
24 integrated cases, 652 writer checks, 8,511 registry checks and 30 native suites pass. The
[writer audit](../docs/adaptive-ram-writers.md) identifies remaining ABI1 generated
stores, lifecycle barriers and optional native writers. [116 evidence](../docs/traces/adaptive-code-versions/README.md).
Literal guards remain mandatory; the all-writer/version-only gate is not closed.

Adaptive builds now invalidate bound lifetimes before snapshot apply, init/free,
firmware writes and boot edits. Explicit disable cancels even capture-only work.
25 integrated cases, 172 lifecycle checks and 30 native suites pass, including
actual generated-WASM restore and continued reference execution. [117 evidence](../docs/traces/adaptive-lifecycle/README.md).
File-apply I/O fault injection, arbitrary tier mixing, optional writers and browser
host integration remain unqualified; literal guards remain mandatory.

ARM immediate and Thumb immediate/literal byte/word loads now execute through a
warm-RAM helper with exact hit accounting and side-effect-free refusal. Cold,
MMIO, legacy-unaligned and cross-granule loads retain precise reference handoff.
37 integrated cases, 33,043 interpreter comparisons, 107 unit tests, 52 native
cache checks and all 30 native suites pass. [118 evidence](../docs/traces/adaptive-ram-loads/README.md).
Next: generated stores and remaining memory/writer coverage, then the frozen
real-app correctness/throughput gate. No product switch or speed claim.

Compiled ARM/Thumb warm RAM stores now pass 51 integrated full-snapshot cases,
33,455 instruction comparisons, 115 unit tests, 150 cache checks, 529 runtime
checks and all 30 native suites. Aliased and same-value code writes force exit
and retirement; malformed zero-retirement exits fail closed. [119 evidence](../docs/traces/adaptive-ram-stores/README.md).
Next implement passive cached-mapping capture and an isolated real-web-machine
adapter. Preserve frozen input/chunk/checkpoint boundaries, count all compilation
CPU, and qualify full-state equality before the predeclared ABBA performance gate.
Further ISA expansion must be driven by actual workload cost.

Passive `adaptive_capture_at` and the isolated real-browser-API harness now exist.
287 capture checks pass; identical initialized interpreter/empty-adaptive runs
match the complete saved phone after 10,000 credits. [120 evidence](../docs/traces/adaptive-workload-runtime/README.md).
Next add bounded actual-entry observation and synchronous Stage 2 compilation,
keeping run boundaries fixed and compiler CPU included. Current linear lookup
scans 2,048 unit slots per miss and exact-entry-only admission restricts coverage;
measure generated retirement and refusals in real workloads. The three-workload
gate has not yet run and its one-corrective-revision allowance remains unused.

All three frozen600M-credit app trajectories now pass their nine complete
checkpoints with actual generated execution. Fixed indexed entry lookup preserves
ownership/admission and passes6,417 index,8,511 registry and2,700 observer checks.
[121 evidence](../docs/traces/adaptive-app-replay/README.md). Generated coverage
stops early because of the prototype lifetime-attempt cap and context churn.
Next remove that artificial cap and qualify atomic installed-unit generation
refresh with unchanged physical code/context and all mappings revalidated.
Keep pending installation strict. Repeat complete workload equality before the
predeclared throughput trial; no ABBA trial or corrective performance revision
has been consumed.

Installed units now refresh generation only after every mapping/version/epoch/
mode/ISA/CP15 field revalidates; pending stale installs remain strict. Collection
continues past2,048 lifetime attempts while installed caps remain.18,178 native
checks and all nine full app checkpoints pass, with135–145M generated retirement
per trajectory. [122 evidence](../docs/traces/adaptive-generation-refresh/README.md).
The prototype now has full real-workload correctness evidence. Run the declared
warmup/ABBA throughput gate next; its one-corrective-revision allowance remains
unused. Coverage increases and concurrent diagnostic timings are not speed
acceptance.

### First declared performance gate — FAILED

All18 warmup/timed runs and54 complete checkpoints pass correctness.
Seeded/candidate mean CPU ratios are0.3321/0.3293/0.3504 for Settings/Contacts/
Notes; geometric mean0.337144, below2× with all workloads regressing.
[Exact evidence](../docs/traces/adaptive-first-gate/README.md).
Proceed with the plan's **one corrective revision**, informed by profiling.
Then requalify full state and repeat the same gate. A second performance failure
requires review; do not continue tuning indefinitely or change engines by default.

### Single corrective revision scope (2026-09-08)

The first failed gate's Settings diagnostic finds substantial admission and
compilation overhead, with only 7.83 generated instructions per call on average.
The one permitted corrective revision contains two bounded changes:

1. Cache exact emitted WAT to compiled WebAssembly.Module within one compiler
   owner, using an LRU capped at 2,048 entries and 32 MiB of accounted WAT and
   binary payload. Fresh captures, instances and strict installation are still
   required. Clear the cache on close; no cross-replay warming.
2. Reject invariant context mismatches before attempting installed-unit mapping
   recapture. Preserve the existing generation-only acceptance rule, physical
   versions, literal guards, statistics and interpreter handoff.

No broader context reuse, new instruction family or changed gate is included.
Focused tests and full app correctness replays precede the second and final
ABBA gate. These changes may reduce overhead; the profile does not prove that
they can meet the 2x target. A second failed gate requires review.

### Second gate: corrective revision failed (2026-09-08)

[Evidence](../docs/traces/adaptive-second-gate/README.md): all18 runs/54 complete
checkpoints pass; throughput ratios Settings0.375249, Contacts0.375021,
Notes0.389296; geometric mean0.379797123, about2.63x slower than baseline.
The one corrective revision is exhausted. Stop optimization for review; do not
proceed automatically to Stage3, activate the product or lower the threshold.
The compiler and product goal remain incomplete.

[Required review](../docs/traces/adaptive-second-gate/review.md) recommends a new
design preserving seeded coverage before adding adaptive improvements. This is
a hypothesis requiring a new scope decision, not authorization for another
tuning cycle or a claim of completion.
