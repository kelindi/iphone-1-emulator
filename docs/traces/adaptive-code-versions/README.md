# Physical code versions and committed-write notifications

The isolated runtime now records physical 1 KiB RAM versions in captured code
witnesses. It rejects stale captures at installation, retires stale installed
entries before invocation, and rejects a changed version from an active guard.
Every write advances versions even if the bytes return to their original value.
Overflow and invalid spans fail closed. Binding releases the table and advances
the existing machine epoch. Literal guards are retained.

`runtime.json` / `build-report.json` qualify actual emitted WASM through the
registry/MMU/scheduler: 24 cases pass. Tracked cached stores must produce exactly
43 notifications (version 44), rather than merely any nonzero count. A guest code
store advances its version once and retires the affected unit once, including
a store through VA 0x100000 aliasing code fetched at VA 0 (physical RAM 0x10000). Host-loader
and bus ABA tests change an instruction and restore its original bytes: old
captured work is rejected, the installed unit retires before any generated guard,
and the complete saved-machine state still matches the reference execution.

`registry.log` records 8,511 checks, including active-unit version refusal,
invalid observer context/span fatal behavior, retirement ownership and bind
recovery. A read-only review identified a missing null-bus check in the write
callback; the guard and a fatal-state regression test are included.
`code-versions.c` tests crossing ranges, physical identity, partial last
granules, invalid spans and explicit UINT32_MAX exhaustion without wrap.

`device-writes.log` records 652 checks across eight CPU, six DMA and four MBX
cases. Exact callback counts, committed bytes and spans are asserted. CPU tests
verify warm-cache hits under direct-write consent; unaligned word stores and DMA
cross 1 KiB boundaries. MBX direct and bus paths are tested, including refusal
before any commit for a missing destination mapping. The log retains existing
native MBX LTO compiler warnings; they are not test failures.

`native-tests.log` records all 30 native suites passing. The added host callback
increases native machine size from 127496 to 127504; the compile-time snapshot
size guard caught the change. A native sizeof probe confirmed 127504. The guard
and explanatory comment were updated; no serialized field or format version
changed. Existing snapshot tests and integrated raw-byte equality still pass.

The [writer audit](../../adaptive-ram-writers.md) names remaining bypasses. In
particular, existing ABI1 WASM stores do not notify, and boot/decrypt/restore paths
need explicit lifetime barriers. This work does not qualify version-only guards,
product tier mixing, real-app performance or the user's Mac.

Reproduce the integrated build and range tests:

```sh
node tools/jit/adaptive/test-machine-runtime.mjs
```

Native qualification used a Release build in
`.context/adaptive-compiler/native-core`, followed by `ctest --output-on-failure`.
The retained standalone test sources link that build's `core/libemucore.a` with
`-lm`; registry tests also compile `web/adaptive-runtime.c`.
