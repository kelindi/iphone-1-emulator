# MMU-on generated execution and a real timer interrupt

The [nine-case integrated report](report.json) now includes full saved-machine
comparison with the interpreter for MMU-on connected execution, cross-window
execution, a guarded slow instruction, and a nonidentity small-page mapping.
The existing identity, stale-code and fatal-latch cases remain. Generated guards
are counted so an interpreted fallback cannot silently qualify the generated path.
[Build commands/source identities](build-report.json), [sources](source/) and
[actual module](machine-bridge.wasm)/[loader](machine-bridge.cjs) are preserved.

The first MMU-on cross-window run [failed](first-failure.json): generated code
recorded 2 TLB hits where the interpreter recorded 86. No snapshot fields were
excluded to make this pass. After the pure mapping/literal predicate succeeds,
the runtime guard now uses the existing nonwalking fetch-cache refill to reproduce
the displaced fetch's bookkeeping. Failed predicates still do not refill.
Successful guards may update fetch-cache fields and TLB-hit counters, but cannot
perform instruction/register/MMIO effects. A following slow instruction reuses
the warmed fetch and does not double-count it. The guarded LDR fallback case
executes 84 guards and matches all snapshot bytes with 86 TLB hits.

A Timer4 fixture uses the real machine timer/VIC/IRQ path at the normal 412MHz /
6MHz ratio. The generated loop executes exactly 69 guards before the timer
boundary; the CPU then enters IRQ and executes its handler. At 128 scheduler
credits, checks require r0=35, handler marker r2=0x5a, CPSR=0x1d2, timer latch
0x30000, LR=8 and PC=0x1c, plus complete snapshot equality with interpretation.
This qualifies one deterministic timer deadline, not all active-clock, DMA,
WFI, MMIO partial-effect or interrupt configurations.

Reproduce with `node tools/jit/adaptive/test-machine-runtime.mjs`.
The [95 JavaScript unit tests](unit-tests.log) pass; the existing native registry
suite was also rebuilt and passes 8,473 checks. No production build activation or
real-app performance result is claimed. Broader decoded operations, physical
code versions, dynamic compilation and the frozen three-app gate remain ahead.
