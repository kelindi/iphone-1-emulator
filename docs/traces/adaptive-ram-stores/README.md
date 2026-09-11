# Compiled warm RAM stores

ARM immediate STR/STRB (including translated and pre/post-index forms) and
Thumb immediate word/byte stores now use the qualified warm write cache.
Refusal has no effects and hands off the complete instruction. Committed
stores notify physical versions; overlap with any active code dependency
forces an immediate exit and retirement, including aliases and same-value
writes. Literal guards remain required. Product activation is pending.

## Evidence

- `differential.json`: 33,455 generated-WASM/interpreter comparisons, including
  412 added store cases with complete 1 MiB RAM equality as well as CPU state.
  Identity-memory stubs alone do not qualify MMU behavior.
- `unit-tests.log`: all 115 JavaScript tests pass.
- `runtime.json` and `build-report.json`: all 51 actual WASM/scheduler cases pass
  with complete saved-machine equality and exact read/write-cache accounting.
  Successful warm word/byte/translated/unaligned stores require observed helper
  hits; cold/device/revoked/legacy-unaligned/cross-granule refusals require none.
  Direct and virtual-alias code writes each commit once, force one boundary
  and retire one function handle.
- `write-cache-tests.log`: 150 native checks for committed byte/halfword/word
  stores, notifications and full CPU/RAM preservation on refusal.
- `store-runtime-tests.log`: 529 native checks across 14 cases, with and without
  physical tracking. Includes same-value code writes, forbidden disable during
  commit, malformed exit reasons and zero-retirement stale exits.
- `native-tests.log`: all 30 native core suites pass.
- `source/` and `source-sha256.json`: retained implementation and focused tests.

## Failures retained and fixed

`initial-failure/`: fixtures omitted direct-write consent, so expected warm
store hits correctly refused. Both reference and candidate now explicitly opt in.
`alias-install-failure/`: warming an aliased write evicted the code TLB entry
while leaving the fetch shortcut populated. The fixture now clears that shortcut
before its explicit real-interpreter fetch warmups; install still requires real
current mappings. Production mapping checks were not relaxed.
`zero-stale-failure.log`: a committed code write followed by a malformed
zero-retirement STALE result was accepted. Runtime validation now rejects that
result before cycle credit, preventing replay of the committed instruction.

Re-run actual WASM qualification in a fresh directory:

```sh
ARTIFACT_DIR=.context/adaptive-compiler/store-machine-new \
node tools/jit/adaptive/test-machine-runtime.mjs
```

These are correctness results, not a real-app throughput trial. Remaining writer
restrictions are in the [audit](../../adaptive-ram-writers.md). Next is integration
with the frozen Settings/Contacts/Notes workloads and their declared gate.
