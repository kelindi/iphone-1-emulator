# Real-phone adaptive workload harness

The isolated harness includes the real browser API and its machine, with ABI1
excluded at compile time. It exports an explicit ABI2 layout, RAM/CPU access
and a bind/track operation. It does not modify product build defaults.

`adaptive_capture_at` captures another aligned virtual PC in the current CPU
context without stepping, walking page tables, refilling caches or changing
counters. MMU-on requires a resident valid fetch mapping and a matching MMU
stamp before consulting the current-fetch shortcut. MMU-off proves identity
RAM directly. Physical code versions are attached when tracking is active.
The existing current-fetch capture API remains unchanged.

## Evidence and limits

- `capture-tests.log`: 287 native checks, including complete CPU preservation,
  current/noncurrent mappings, aliases, permissions/XN faults, MMIO/cold/alignment
  refusals, pending exceptions, active-call rejection and raw CP15 context changes.
- `build-report.json`: source-pinned isolated WASM build. No product outputs built.
- `smoke-report.json`: genuine frozen Home snapshot and firmware hashes checked.
  Both arms initialize identically, restore the phone, then execute 10,000
  scheduler credits through the real web API. Interpreter versus empty adaptive
  registry produces identical 244,887,522-byte saved states; zero differing bytes.
- `source/` and `source-sha256.json`: exact helper, adapter and test sources.

The initial comparison failed at snapshot offset 582 (the serialized TLB-flush
counter), plus the eight checksum bytes. First run was after boot; second was
restored over the previously executed machine. Restore intentionally clears
translation caches but retains a derived MMU stamp, so the two setups differed
in whether their first translation credited a context flush. Both arms now
initialize identically. No counter or saved byte was excluded.
`initial-failure.json` and `failure-byte-diff.json` preserve the diagnosis.

This is an empty-registry API smoke, not generated coverage, a full app replay or
a speed benchmark. Next implement bounded hot-entry collection and compilation
without changing frozen input/run boundaries, then require full checkpoint
equality before the declared ABBA throughput gate. All compilation CPU must count.

Reproduction commands are in [the tooling README](../../../tools/jit/adaptive/README.md).
