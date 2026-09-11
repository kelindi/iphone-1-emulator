# Adaptive compiler ABI2 proposal

Status: **initial isolated runtime integration is implemented; broader qualification remains pending**.
See [end-to-end synthetic workload evidence](traces/adaptive-runtime/README.md). The C result layout
and invocation validator are implemented and tested in isolation; [bridge evidence](traces/adaptive-result-contract/README.md) does not qualify admission or scheduler dispatch.
An isolated WASM emitter now implements the proposed result layout and integer
execution slice; [CPU-only evidence](traces/adaptive-emitter/README.md) does not
qualify machine scheduling, admission, code invalidation or real guard behavior. This specifies an initial
Stage 2 integration interface, subordinate to the [accepted plan](../plans/adaptive-compiler.md)
and [correctness contracts](adaptive-compiler-contracts.md). ABI1, its existing
512-region seed, `jit=0`, snapshot bytes and the current lock-screen startup stay
independent. No performance benefit is implied by this interface.

## Execution ownership

The emulator worker owns the CPU, physical memory, installed-unit registry and
WASM function-table slots. A background compiler receives immutable copied
instruction bytes and dependency metadata; it receives no live CPU pointer or
writable guest memory. Installation occurs on the emulator worker at a boundary
between synchronous `emu_run` calls, after generation and dependency validation.

The machine scheduler supplies each invocation's budget. Generated code keeps
integer registers and CPSR in locals across admitted connected blocks, but spills
all architectural locals on every normal exit. Initially use a complete spill,
not an unqualified dirty-register omission. Banked registers, CP15, VFP and
exception state remain in the existing CPU; instructions that could modify them
use the full-instruction slow boundary described below until separately qualified.

C-facing declarations, now implemented in `core/include/arm_adaptive_jit.h`:

```c
#define ARM_ADAPTIVE_ABI 2u

enum arm_adaptive_exit {
    ADAPT_EXIT_INVALID = 0,       /* sentinel: never a successful return */
    ADAPT_EXIT_BUDGET = 1,        /* supplied budget consumed */
    ADAPT_EXIT_EDGE = 2,          /* completed prefix, next PC outside unit */
    ADAPT_EXIT_BEFORE = 3,        /* current instruction has made no effects */
    ADAPT_EXIT_STALE = 4,         /* raw/code dependency guard refused */
    ADAPT_EXIT_CONTEXT = 5,       /* execution mapping/context refused */
    ADAPT_EXIT_BOUNDARY = 6       /* completed prefix requires machine check */
};

typedef struct {
    uint32_t retired;            /* generated successful prefix only */
    uint32_t reason;             /* enum above */
    uint32_t status;             /* ARM_OK for this initial generated ABI */
    uint32_t resume_pc;          /* equals spilled cpu->r[15] */
    uint32_t instruction_pc;     /* instruction associated with refusal */
    uint32_t dependency;         /* unit dependency ordinal, or UINT32_MAX */
    uint32_t detail;             /* versioned diagnostic subreason */
    uint32_t reserved;           /* zero */
} arm_adaptive_result_t;

typedef void (*arm_adaptive_fn)(arm_cpu_t *cpu, uint32_t budget,
                               arm_adaptive_result_t *result);
```

The generated WASM type is `(i32, i32, i32) -> ()`, registered with the
Emscripten signature `viii`. Assert the result's 32-byte size and field offsets
in C. Export a separate ABI2 layout descriptor for CPU/cache offsets, result
layout, helper signatures and feature bits. Do not reinterpret
`emu_jit_offset()` or silently declare ABI1 modules ABI2-compatible.

The C runtime initializes a call-local result to the invalid sentinel and pins
the selected unit before invocation. It checks `retired <= budget`, the reason,
reserved/status fields and coherent resume PC on return. `BUDGET` requires exact
budget consumption; `EDGE` and `BOUNDARY` require positive progress. The
zero-budget path returns before calling generated code or querying guest memory.
Zero-progress `BEFORE`, `STALE` or `CONTEXT` cannot retry the same unit indefinitely.
Invalid results and unexpected WASM traps are fatal execution errors: pause and
offer recovery. These checks cannot roll back effects already performed.

Generated instructions do not increment `cpu->cycles`. The ABI2 C runtime adds
the accepted generated prefix once, as legacy `arm_wasm_jit_try()` currently
does. The machine adds the same prefix once to scheduler progress. A reference
`arm_step()` executed at a slow boundary increments its own cycles; its cycle
delta is never added again. An `ARM_OK` exception-entry step receives the same
one scheduler credit as today even when no instruction was fetched. A non-OK
step gets no successful-step credit, even if it incremented cycles.

## Machine boundary and helper protocol

The integration point is the JIT/interpreter batch in
[s5l8900_run](../core/src/soc/machine.c), not just replacement of the function
pointer in [arm_wasm_jit_try](../web/jit-runtime.c). Reuse
`interpreter_tick_batch_limit`, `run_retirement_batch_limit`, `run_clock_retired`,
`batch_pending`, dirty-level/input checks and WFI handling. Pre-step hooks and
privileged host-SVC callbacks continue to disable ordinary batching.

For each result, account the generated prefix and then:

| Exit | Machine action |
|---|---|
| `BUDGET` | Finish the existing scheduler batch; no extra interpreter step. |
| `EDGE` | Recheck machine/CPU eligibility and remaining budget, then attempt another admitted unit. A failed admission reaches the ordinary step. No unconditional interpreter step merely because a connected edge ended. |
| `BEFORE` | Run the current instruction exactly once through `arm_step`, using the existing `batch_pending` protocol. This is a complete slow instruction, not a retry after partial execution. |
| `STALE` | Disable the identified dependency's affected entries, preserve the completed prefix, and reach a defined ordinary step/revalidation path without repeatedly entering the stale unit. |
| `CONTEXT` | Reach the ordinary fetch/exception path when a safe nonwalking mapping cannot be admitted. Do not create a speculative guest exception. |
| `BOUNDARY` | Return to the machine's device/clock checks before another instruction. Initial emitters need not produce this reason until a qualifying completed-effect operation exists. |

The ordinary slow step must preserve the current sequence: set
`m->batch_pending` to the unflushed successful prefix, call `arm_step`, detect
whether a callback flushed that prefix, credit only `ARM_OK`, and stop/recheck on
dirty devices, changed input, WFI or a flush. Preserve active-clock sampling and
the deterministic timebase edge; never substitute a larger unit size for a
larger permitted execution budget. ABI2 direct edges deliberately replace the
legacy mandatory fallback policy and require tests through `s5l8900_run`.

For the first qualified interface, **fault-capable and device-effect helpers
execute outside generated code at this machine-owned boundary**. This also gives
all helpers one shared slow path. Their final registers are loaded on the next
generated entry, so a helper cannot leave stale compiler locals alive. Typed
decoded instruction helpers can later replace the decoder portion, but must
preserve the entire `arm_step` envelope before they are accepted.

In particular, the static `exec_single_transfer` and `exec_block_transfer`
bodies in [arm_interp.c](../core/src/arm/arm_interp.c) are not standalone complete
instruction helpers. `arm_step` supplies fetch, condition handling, cycle
increment, PC commit, pending-data-abort delivery and undefined-instruction
handling. Exporting those bodies and incrementing a generic success count would
lose semantics. A multiword instruction that has issued even one transaction
must finish through its original fault/commit path, never deopt and replay it.

Read-only eligibility imports may return a validated RAM span or refusal without
committing an instruction. Every declared cache-hit counter effect remains part
of policy accounting. No direct WASM call to `arm_step`, MMIO helper or WFI may
resume the current locals under this initial interface. If helper re-entry is
later introduced, it needs a separately reviewed segment-accounting callback,
fault/status result and explicit local reload contract; it is not implied by ABI2.

## Fetch, code and unit identity

Admission binds the actual entry PC/ISA and execution context to the selected
unit, verifies CPU mode, pending fault, FIQ before IRQ, alignment, and executable
mapping. `arm_fetch_cache_try_refill` in [mmu.c](../core/src/mmu.c) is a useful
nonwalking query: a successful TLB-derived refill changes its hit counter and
must be accounted explicitly. It is not permission to bypass a failed fetch or
read code from MMIO.

A unit owns at most 32 blocks, 512 static guest instructions and eight distinct
physical 1 KiB dependencies. Metadata includes virtual entries/ISA, physical
granule identities, immutable original bytes, context identity and compiler,
policy and machine-generation identities. Virtual and physical identities are
separate: an alias does not inherit another mapping's execute permission.

Keep original literal guard semantics until the full physical-writer/version
audit passes. Guards must use the actual member's validated executable bytes.
Stale reporting identifies that member/dependency, not just the unit's root PC.
An entry guard is not sufficient for later iterations after a watched write.

The existing [compiler's `span()`](../tools/jit/compiler.cjs) store guard rejects
aliasing only its current `$fetch` window. A connected unit must protect **every
physical code dependency** it may subsequently execute. The compiled-store protocol below commits an admitted RAM store and forces
an immediate boundary if it overlaps any such dependency, even when physical
version tracking is absent. Retire the unit and revalidate before executing
changed code. The same requirement applies to aliases and cross-granule writes;
do not copy the single-region check unchanged into a connected compiler.

## Slot ownership and asynchronous lifetime

Use a unit registry distinct from ABI1's region table. Entry-map records refer to
a unit ID plus generation, never independently own its function pointer. One
unit owns one registered function-table slot; its several guest entry PCs can
share that slot because they share this explicit owner. Track live entry/link
references and active invocations. Unpublish entries and invalidate incoming
links before queuing retirement. Remove the JS function exactly once only after
all references and active calls are gone; never recycle a slot while old code
can still reach it. Do not rely on distinct export names defeating
`addFunction`'s function-object deduplication.

Keep the eight-request, 2,048-unit and 32 MiB installed-binary bounds; rejection
or eviction falls back correctly. A full retirement queue cannot silently drop
ownership. Stage 2 can avoid direct inter-unit table links until this lifecycle
is tested; intra-unit connected branches do not require such links.

Reset/restore/rebind first unpublish ABI2, increment its machine generation and
cancel pending work. Validate generation after every await and just before
publication. Installation is atomic or rolls back. Dependency versions, function
IDs and jobs remain derived state outside snapshots. Persisted binaries require
compiler/ABI/firmware/policy keys plus current-code validation after restore.

## Proposed file and symbol integration

| Location | Change when implementation is authorized |
|---|---|
| `core/include/arm_adaptive_jit.h` | New result ABI and `arm_adaptive_try` declaration; preserve `arm_wasm_jit.h`. |
| `web/adaptive-runtime.c` / `.h` | Separate bind/enable/install/unpublish/retirement APIs, unit ownership, admission, prefix cycle accounting and layout descriptor. |
| `core/src/soc/machine.c` | Mutually exclusive adaptive/legacy selection inside the current batch path; explicit result dispatch with unchanged scheduler boundaries. |
| `tools/jit/adaptive/` | Decoded operations/CFG and emitter; consume ABI2 descriptor, emit explicit exits and guards rather than rewriting legacy WAT strings. |
| `core/src/arm/arm_interp.c`, `core/src/mmu.c` | Reuse reference slow step and validated mappings initially; any extracted typed helper is a separately tested complete-instruction seam. |
| `web/emu_api.c` | Bind/unbind ABI2 alongside machine init/destruction; reset generation for snapshot loading at the C boundary as well as the JS lifecycle. |
| `web/emu-worker.js` | Opt-in tier selection, installation between runs, fatal-error propagation, reset/restore coordination; preserve `openLocked`. |
| `web/adaptive-jit.js` and compiler worker | New host lifecycle and immutable-job transport; preserve `SeedJIT` and its ABI1 loader. |
| `CMakeLists.txt` | Explicit ABI2 source/export configuration; no implicit production/default promotion. |

## Focused acceptance tests before throughput claims

1. Actual generated ARM/Thumb entries at budgets 0, 1, exact and one-short;
   connected edges, conditions, interworking and repeated loops. Reject bad
   counts, invalid sentinel returns and repeated zero-progress dispatch.
2. Run through the actual machine batch: positive prefix plus slow instruction,
   device dirtiness, IRQ/FIQ, timebase boundary, callback-flushed `batch_pending`,
   WFI, pre-step hook and non-OK status. Compare cycles separately from scheduler
   credit and full raw CPU/RAM/device state.
3. Fault on each multiword transaction, condition-failed memory instruction,
   cold/warm MMU and permission failures. Count actual MMIO transactions to
   detect replay; compare fault PC, banked state and partial effects.
4. Write current or later unit code through physical aliases, all eight code
   dependencies and cross-granule stores; deliberately suppress invalidation to
   demonstrate that the oracle fails. Literal guards remain until all writers
   are covered.
5. Multiple entries sharing a unit, replacement/retirement while active, stale
   jobs after restore, queue/byte limits and slot reuse. Deliberate double-free
   and late-install mutations must fail. No host metadata enters snapshots.
6. Unexpected WASM trap after a visible store must pause, retain the first error
   and never replay the store. Known guest faults must not use host traps.
7. Pinned Settings/Contacts/Notes replay and full checkpoints, with actual ABI2
   path witnesses. Retain the existing 595-pass/12-fail WASM VFP baseline as a
   separate conformance defect; relative equality cannot turn it into a pass.

This interface removes ambiguity about ownership; it does not establish that
connected units or shared slow boundaries are faster. In particular, helper
exits still spill and reload. Measure that cost in the prescribed Stage 2
workloads rather than assuming fewer generated entries imply better throughput.

## Qualified fetch bookkeeping clarification

The instruction guard import performs a pure mapping/literal check first, then
may reproduce reference fetch-cache fields and TLB-hit accounting through the
existing nonwalking refill. It must not change architectural registers, execute
an instruction or access MMIO. Failed pure checks perform no refill. This fixes
a complete-snapshot mismatch on MMU cross-window execution; [113 evidence](traces/adaptive-mmu-timer/README.md) includes a guarded slow-load test proving no double-counted fetch.

## Initial physical-version implementation

The opt-in runtime now supports `adaptive_track_ram` immediately after bind and
before capture/install. The host attaches `adaptive_ram_written` to the RAM write
observer; captured witnesses include their physical `code_version`. Version
changes reject queued installation and installed entry, or cause a guarded STALE
exit during active execution. Same-byte writes still advance versions. Wrap or
invalid notifications stop the run; bind starts a distinct epoch and releases
the table. All function handles still retire through their existing owner queue.

This is additive to the literal and MMU checks. The [writer audit](adaptive-ram-writers.md)
identifies uninstrumented legacy generated stores and required boot/restore
lifetime barriers. Version-only execution is not enabled or qualified.


## Lifecycle barriers implemented

Adaptive builds call `arm_adaptive_forget(cpu)` before restore application,
init/free, direct firmware writes and boot edits. It ignores unrelated CPUs;
failure forbids the requested mutation. Restore reports `SNAP_ERR_BUSY` rather
than misclassifying runtime ownership as guest corruption. Explicit disable
advances every bound lifetime, including capture-only pending work, and releases
installed handles through the existing retirement queue. Browser host rebind and
background cancellation integration remain required. [117 evidence](traces/adaptive-lifecycle/README.md).

## Warm RAM load import

Load-bearing units may import `read(va,width,privileged) -> i32 hostPointer`.
`adaptive_read` permits only an active admitted unit and refuses privileged
lookup from User mode. `arm_read_cache_try` checks the existing read-cache tag,
generation, width/span and alignment policy without walking tables or touching
MMIO. Zero means BEFORE, with no instruction or miss-counter effects. On success
it credits exactly one reference read-cache hit; the generated load owns result
and writeback, and ordinary retirement remains unchanged. The pointer is consumed
immediately during that invocation, never retained across lifetime changes.
Only load-bearing modules require this import. [118 evidence](traces/adaptive-ram-loads/README.md).

## Compiled RAM store protocol

Units containing supported ARM/Thumb stores import
`env.write(va, width, privileged, value) -> status`. The runtime consults the
existing warmed write cache, including explicit direct-write consent, privilege,
generation, alignment and granule bounds. Refusal returns 0 before any effect;
the interpreter then executes the whole instruction. Status 1 means the write
committed and execution may continue. Status 2 means it committed and the unit
must apply architectural writeback, count the store and return a boundary.
Other statuses are fatal and must never cause replay.

Committed writes notify the physical-version observer. A write overlapping any
active code witness forces status 2 even without version tracking, including
same-value writes and virtual aliases. Subsequent guard/read/write helpers
refuse. The runtime retires that unit after invocation and rejects a result
with zero retired instructions or a reason other than BOUNDARY/STALE. A
post-commit forbidden registry mutation also fails closed before cycle credit.

Supported generated forms currently include ARM immediate STR/STRB addressing
and Thumb immediate word/byte stores. Unsupported forms retain interpreter
handoff. This protocol passes 51 isolated integrated cases; [store evidence](traces/adaptive-ram-stores/README.md) records the exact scope. Real-app performance acceptance remains pending.

## Stage 2 synchronous admission observer

`adaptive_observe(fn)` installs a trusted host callback at eligible admission
misses, including entries whose context or code versions no longer validate.
Code-stale entries retire first. The callback may capture, install and drain
retired handles, but cannot mutate guest state, recursively execute, rebind,
change enablement, replace itself or reconfigure tracking. Forbidden runtime
mutations fail closed. Even if it installs a unit, the current attempt returns
to the original interpreter step; a later attempt can use the new unit.
Bind resets the callback and all derived statistics. Full retirement queues
refuse before notifying; callers drain ownership at synchronous run boundaries.

`adaptive_stat` reports exact integer-valued doubles: generated retired (0),
attempted generated calls (1), eligible admission misses (2), and exit buckets
at 3 plus the result reason. Counters fail closed before exceeding 2^53−1.
The isolated workload collector counts admission misses, not every executed
instruction or every branch entry. This prototype is synchronous and does not
satisfy the Stage 3 background-worker requirement.
