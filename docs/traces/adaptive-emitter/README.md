# First executable adaptive compiler slice

The connected IR now emits actual WASM with the proposed ABI2 function type:
`run(cpu, budget, result) -> void`. It keeps integer registers/CPSR local across
admitted code, supports entry into block tails, and spills every register and
all eight result words on normal exits. It uses a balanced PC selector and
straight-line fallthrough inside blocks. It does not update CPU cycles; the
future runtime owns that accounting.

Implemented operations: current decoded ARM/Thumb immediate arithmetic and
logical operations, ARM conditions, direct branches/link, and ARM BX. Unsupported
instructions stop before making effects. BX interworking returns a completed
boundary; invalid target alignment returns before execution. Literal guards run
before each instruction, after the budget check. A pure imported guard must
validate executable mapping/context and literal identity without guest effects;
this milestone's guard is only a test stub. Its boolean result conservatively
maps all refusals to STALE, not a specific mapping-versus-literal diagnosis.

[Actual WASM/C comparison report](report.json): **4,098 checks pass**. They compare
the complete 248-byte CPU header before the cycle counter (integer registers,
CPSR, CP15 and banked registers) against the retained C `arm_step` oracle, and
separately check that generated code leaves cycles untouched while the reference
credits its own steps. Arithmetic tests cover all implemented ALU opcodes, all
NZCV combinations, extrema, rotated immediates and flag preservation. Additional
checks cover conditions, architectural PC reads, loops, cross-window execution,
exact budgets, entry into tails, prefix fallback/stale exits, BX and trap propagation.

[Unit results](unit-tests.log): **95 tests pass**, including seven standalone
actual-WASM emitter tests covering all result fields, guard ordering, flags,
branches and an empty unit. [Frozen sources](source/) and [hashes](SHA256SUMS)
identify the emitter, tests and existing oracle. The [initial harness setup failure](setup-failure.json)
is preserved: the oracle had not exposed its memory before module linking;
initializing it first fixed setup. It was not a guest execution mismatch.

Reproduce from repository root with pinned npm dependencies:

```sh
node tools/jit/adaptive/test-emitter.mjs
npm run test:unit
```

The differential runner reuses the existing oracle at
`docs/traces/connected-compiler-experiment/oracle.cjs` and its adjacent WASM.
That oracle contains the C interpreter and prior experimental integration code;
these tests call only its interpreter path. This is relative CPU semantics,
not independent architectural conformance, real MMU/cache/device qualification,
whole-machine equality or a phone speed benchmark. No production dispatch or
seed changed. Unexpected guard traps propagate; pause/recovery belongs to the
not-yet-implemented runtime integration.

Next: ABI2 admission and result validation in the actual machine scheduler,
permission-safe guards and invalidation, broader decoded operations, then the
frozen three-workload correctness and 2× throughput gate. Do not enable this
emitter in the user-facing phone before those requirements are qualified.
