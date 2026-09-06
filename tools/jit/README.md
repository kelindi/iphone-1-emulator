# Guarded browser seed

`seed-input.json.gz` retains only the supported guest opcode positions for the
512 measured 1 KiB regions used in the Settings experiment. It contains no
contacts, preferences or snapshot RAM. These are observations of genuine
1A543a code; Apple executables are not patched.

Build the browser runtime, then run `node tools/jit/generate-seed.mjs`.
The normal CMake browser build orders this step after `web/dist/emu.js` and
before offline bundle hashing. Install the repository's pinned npm dependencies
first (`npm ci`); generation uses WABT 1.0.39 and the retained CJS emitter.
No ignored workspace traces or emulator checkpoint is required to regenerate.

The generator obtains every layout offset and the semantic ABI revision from
the newly built C runtime. It emits `web/jit/seed.wasm`, `regions.json` and
`abi.json`. These generated assets are ignored; their metadata binds both asset
hashes and the runtime ABI. `JIT_RUNTIME` and `JIT_OUTPUT` optionally select a
separate built runtime/output directory for isolated reproducibility checks.

The compiled seed admits only matching raw code and already-authorized memory
cache spans. Translation privilege/generations, pending interrupts/aborts,
physical code aliases and exact retirement budgets retain interpreter
fallback. It is an optional acceleration cache, not part of saved phone data.
Current production preparation does not warm functions by executing guest
instructions or zero-budget loops. Unsupported or stale regions continue on
the existing interpreter.

Changing instruction admission or the generated ABI requires reviewing the
emitter/runtime together, updating `JIT_ABI_REVISION` when semantics/layout
contracts change, regenerating assets, and rerunning the differential and
browser lifecycle checks. Performance must be compared with the current
production interpreter, not only an instrumented disabled control.

Run `npm run test:jit-replay` after the browser build to compare complete machine
snapshots after 600 million interpreter and generated instructions, including
Settings and Home input. It reads the shipped multipart snapshot by default;
`FIXTURE_SNAPSHOT=/path/to/phone.snap` selects another checkpoint.
`INSTRUCTIONS=100000000 npm run test:jit-replay` is the bounded CI variant.
The check also requires positive generated retirement, so a broken enable path
cannot pass by comparing two interpreter runs.
