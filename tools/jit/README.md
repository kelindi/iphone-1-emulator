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

## Seeds for other firmware (iPhone OS 3.1.3)

A prepared firmware can carry its own seed, recorded from its own code:

1. `tools/iphone1 --record-ops FILE --record-from STEP` single-steps from STEP
   and keeps every executed instruction position with its mode and code word
   (Thumb halfwords individually). It writes the hottest 512 1 KiB regions in
   `seed-input.json` form. A 2.6 G-instruction 7E18 app workload recorded
   599,640 positions, and the top regions covered 96% of what ran.
2. `SEED_INPUT=FILE JIT_OUTPUT=DIR node tools/jit/generate-seed.mjs` compiles
   it. For a recording, regions with no supported instruction are skipped,
   and so is any region whose 16-slot probe window in
   `web/jit-runtime.c` is full. Export indices stay dense.
3. `tools/firmware/prepare_ios3_web.py --seed` does both into
   `web/firmware/7E18/jit/`. The page passes `?seed=` to the worker only for
   that firmware (`web/firmwares.json` "seed"). `seed-jit.js` then loads it
   with the same `abi.json` SHA-256 binding.

These seeds are compiled from Apple code, so like the firmware they stay
local and git-ignored. `FW_DIR=web/firmware/7E18 node tools/jit/replay-ios3.mjs`
compares interpreter and seed runs from the shipped snapshot. It covers the
machine snapshot and the root volume, restores the volume before each run,
and ignores only the TLB measurement counters and the stream checksum.
`REPEAT=3` reports steady-state timings and `CONTROL=1` compares two
interpreter runs.
