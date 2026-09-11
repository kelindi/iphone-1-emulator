# Thumb arithmetic and memory fallback qualification

The isolated compiler now emits Thumb immediate shifts, register/immediate ADD
and SUB, low-register ALUs (including MUL and register shifts), and high-register
ADD/CMP/MOV with PC reads. Writes to PC and Thumb BX/BLX remain explicit fallback
boundaries. Memory instructions still execute through the retained interpreter.

Actual generated WASM passes 32,631 comparisons against the retained C interpreter
(`differential.json`), including 19,900 new Thumb checks. These compare full
pre-cycle CPU headers and separate retirement/cycle ownership; they are not
independent ARM architectural conformance. Compiler hashes match the integrated
build below.

All 100 JavaScript unit tests pass (`unit-tests.log`), including direct expected
values for multiply flag preservation, shift boundaries and PC reads.

## Full-machine execution

`runtime.json` records 19 passing cases through actual emitted WASM, the C
registry, real MMU/bus and scheduler. Each nonfatal case compares complete raw
saved-machine bytes after 128 scheduler credits, including serialized counters.
`build-report.json` records build commands and before/after source identities.

The new Thumb loop executes 122 generated guards with the MMU enabled and matches
all 17,891,489 snapshot bytes. Memory cases cover successful writeback, a false
condition with an unmapped address, translation/alignment faults, partial LDM/STM,
a timer-register load/store, and a guest store replacing compiled code. The
faulting instruction is at 0x40; explicit checks require abort LR 0x48, vector
0x10, the expected fault address, and preserved base/destination registers.
Partial STM must retain its first RAM write. Guest code modification executes a
generated prefix first, then retires the stale unit once after the store.

These prove **fallback handoff**, not generated memory operations. The complete
reference instruction owns memory, partial effects, abort delivery and its own
cycle credit. No memory helper is called from inside a generated unit yet.

## Preserved fixture failures

- `first-failure/`: the fault occurred during the scheduler's initial reference
  steps, before any generated instruction. The required guard-path assertion
  rejected it. A bounded loop now establishes actual compiled execution before
  reaching the fault; the assertion remains.
- `ldm-expectation-failure/`: raw snapshot equality passed, but an extra fixture
  assertion wrongly expected the first loaded register to commit before the
  second access faulted. `exec_block_transfer` in the retained interpreter buffers
  all destinations until success. The test now explicitly expects both original
  destination values. This is qualification against that implementation, not a
  separate architectural-conformance claim.

- `thumb-fixture-failure/`: a seven-iteration loop expected r0 to start at zero,
  but the oracle initializes it to 0x100. The corrected fixture explicitly sets
  r0; the compiler was unchanged. The failed source and diagnosis are retained.

Reproduce integration with:

```sh
node tools/jit/adaptive/test-machine-runtime.mjs
```

No product switch, browser timing result, code-version implementation, DMA audit,
or real-app speed acceptance is implied. Physical writer coverage and memory
fast paths remain required before the frozen Settings/Contacts/Notes gate.
