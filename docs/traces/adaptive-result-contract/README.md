# ABI2 C invocation/result contract

The C header now defines and statically checks the 32-byte result ABI, validates
all exit reasons, bounds counts/dependencies, checks coherent resume addresses,
and rejects nonzero reserved/status/detail fields. The invocation wrapper starts
with an invalid sentinel and credits a valid generated prefix exactly once.
Zero budget never invokes a function. Unexpected cycle changes, protected CPSR
changes, or instruction-set changes without a completed BOUNDARY exit are fatal
results. Fatal validation is not rollback; callers must stop rather than retry.

[Report](report.json) includes source hashes and exact successful compiler/test
commands. [Source](source/) and the tiny [test bridge](bridge.cjs)/[WASM](bridge.wasm)
are preserved. Native C tests cover positive and malformed results, duplicate
cycle credit, mode changes and incorrect interworking exits. The generated WASM
emitter also executes through the real C wrapper via an Emscripten `viii` table
slot: zero budget, exact positive credit, stale-prefix preservation and malformed
result rejection pass.

Reproduce from the repo root:

```sh
node tools/jit/adaptive/test-result-contract.mjs
```

Set `EMCC` and `CC` if the installed compiler paths differ. Sources are hashed
before and after qualification to detect concurrent edits.

This wrapper assumes the caller has already admitted and pinned a unit and
supplied a machine-derived budget. It does not implement those responsibilities.
Guards in this isolated test remain stubs. The actual machine scheduler, real
mapping/permission checks, code invalidation, slot lifetime, host trap recovery
and full-phone equivalence are not yet integrated or qualified. Production ABI1
and the phone's performance are unchanged.
