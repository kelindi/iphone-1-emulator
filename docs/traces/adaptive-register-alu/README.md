# ARM register operands and shifts

The decoder/emitter now supports ARM register operands for integer ALUs, with
immediate and register-controlled LSL/LSR/ASR/ROR, including RRX and the different
zero/32/greater-than-32 rules. Register shift counts use the low eight bits.
Invalid register-controlled PC operands, miscellaneous encodings, multiplies,
extra memory encodings and writes to PC remain explicit fallback boundaries.
Existing immediate and Thumb-immediate paths are retained.

[Actual WASM/C differential](differential.json): **12,731 checks pass**, covering
all admitted ALU opcodes, shift boundaries, carry/overflow, condition flags,
register aliases and accepted/rejected PC operands. The comparator remains the
retained C interpreter, including the complete pre-cycle CPU header and separate
cycle ownership; this is relative equivalence, not independent ISA conformance.

[Unit tests](unit-tests.log): **97 pass**. Newly accepted zero words are valid
ANDEQ instructions, so fixtures that intended an unsupported tail now explicitly
use an unsupported encoding and write the same bytes into the oracle RAM.
Larger admitted units also required bounded dispatcher groups: no group nests
more than 64 instruction cases. Actual WASM tests exercise 512-instruction units
and tail entries around group boundaries; the accepted unit cap was not reduced.

[Integrated runtime](runtime.json): ten cases pass. The new MMU-enabled register
shift/ALU loop executes 122 generated guards and matches every saved-machine byte
with the interpreter. Existing MMU, slow-load, timer and failure cases remain.
[Build/source identities](build-report.json) and [compiler/test sources](source/)
are preserved.

[Static coverage count](static-coverage.json): the retained 76,964 seed positions
contain 6,333 newly admitted positions, increasing non-fallback positions from
24,033 to 30,366. This is unweighted static code, not execution frequency, real-app
coverage, elapsed-time reduction or a speed claim. Memory instructions and wider
Thumb coverage still dominate the unsupported set; profiling is needed before
assigning time savings to any category.

Reproduce with:

```sh
node tools/jit/adaptive/test-emitter.mjs
npm run test:unit
node tools/jit/adaptive/test-machine-runtime.mjs
```

Product defaults remain unchanged. Memory/code-version handling and the frozen
real-app correctness/throughput gate remain outstanding.
