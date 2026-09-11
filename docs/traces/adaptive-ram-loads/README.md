# Compiled warm-RAM loads

The compiler now emits ARM immediate LDR/LDRB (including pre/post-index,
add/subtract and translated/unprivileged forms) and Thumb immediate word/byte
and PC-relative literal loads. PC destinations, register-offset memory, stores
and unsupported addressing remain reference boundaries. Load nodes remain
straight-line operations inside the existing 32-block / 512-instruction caps.

A unit containing loads imports `env.read(va,width,privileged) -> hostPointer`.
The runtime uses only the existing warmed read cache: matching physical host
mapping, virtual tag, privilege, generation and in-granule span. A successful
lookup credits one reference read-cache hit; generated WASM then loads the word
or zero-extended byte and applies writeback. A refused lookup returns zero with
no counter, register, memory, fault or MMIO effect. The complete original
instruction handles that refusal, including its one miss and any fault.

The helper refuses legacy unaligned accesses and alignment-fault cases. U=1/A=0
unaligned accesses within a granule are allowed. It never walks a page table or
calls the bus. User code cannot request a privileged lookup. Budget, condition
and instruction guards precede the helper; destination/writeback occurs only
after success. Literal code guards and writer/lifetime restrictions remain.

## Evidence

- `differential.json`: 33,043 actual generated-WASM comparisons against the
  retained interpreter, including 412 new load/address/refusal checks. The
  differential read stub supplies identity RAM pointers, so this alone does not
  qualify MMU or cache accounting.
- `unit-tests.log`: all 107 JavaScript tests pass, including independent expected
  values, import policy, false conditions, writeback and Thumb literal alignment.
- `runtime.json` / `build-report.json`: actual emitted code, C helper, live MMU,
  registry and scheduler: all 37 cases pass. Complete snapshot bytes and separate read-cache hit/
  miss counters must match. Successful word, byte, translated, Thumb/literal,
  nonidentity and U=1 cases require observed successful helper calls. Cold,
  device, legacy-unaligned and cross-granule cases require real calls with zero
  successes. Condition-false loads require zero calls. Existing abort, partial
  store, timer and lifetime checks remain in this suite.
- `read-cache-tests.log`: 52 native helper checks use interpreter-warmed caches.
  Every refusal preserves the complete CPU, including all counters. Every hit
  changes only its one hit counter. Width/span/generation/privilege/alignment,
  cold MMU and missing/device mappings are covered.
- `native-tests.log`: all 30 native suites pass after factoring shared cache
  lookup into `dread_peek`. Native test source compiles with `-Werror`; linking
  against the whole existing LTO core uses its normal warning policy. An initial
  `-Werror` link rejected pre-existing MBX maybe-uninitialized diagnostics.

`first-fixture-failure/` retains the integration assertion that compared a signed
JS i32 export (-19088744) to the same unsigned bits (4275878552). Full machine
snapshot comparison had already passed. Register assertions now normalize the
export with `>>> 0`; no compiler semantics changed to resolve that fixture.

The cold-RAM case records one compiled helper refusal and exactly one reference
miss. The device-load case records 42 compiled refusals and no fast-path hit;
its total 43 reference misses include the initial interpreter instruction.
Warm word-load cases record 42 successful generated reads and one initial miss.

## Reproduction / limits

```sh
node tools/jit/adaptive/test-emitter.mjs
npm run test:unit
node tools/jit/adaptive/test-machine-runtime.mjs
```

The native read-cache fixture links the ordinary native `core/libemucore.a`.
These are correctness/coverage results, not a speed benchmark. Generated stores,
broader memory forms, remaining all-writer qualification, background compilation
and frozen real-app throughput/browser acceptance remain unfinished.
