# Compiler lifetime barriers

Adaptive builds now invalidate the bound compiler lifetime before snapshot
payload application, machine init/free, firmware copy/decryption and boot paths
that edit RAM directly. An unsuccessful release blocks that operation. Snapshot
restore reports `SNAP_ERR_BUSY` when active execution or retirement ownership
prevents invalidation; that is a host runtime error, not corrupt guest input.

`adaptive_enable(0)` advances the bound epoch even when no unit or version table
exists. Captures queued outside the registry must be cancelled too. Disabling
releases installed units through the retirement queue; old witnesses cannot be
revived by merely changing their request's epoch. The signed native path is also
skipped while adaptive execution is selected. Arbitrary tier mixing and optional
native writers are still not qualified; callers must use the disable API before
switching to an engine whose raw stores bypass the observer.

## Evidence

- `runtime.json` / `build-report.json`: 25 actual-WASM integrated cases pass,
  with source identities checked before/after. The new restore case executes
  generated code, restores through the real snapshot API, verifies exact handle
  retirement and rejected pre-restore capture, then compares complete machine
  state after continued reference execution. The old module receives no calls.
- `lifecycle-tests.log`: 172 checks across seven native cases using an
  adaptive-enabled core. Successful restore/free invalidates ownership; corrupt
  snapshots preserve full state and live compiled ownership. Active restore/free
  cannot modify RAM/device state or free storage; fatal state blocks subsequent
  execution without replay. Disable cancels pure untracked capture-only work.
- `loader-tests.log` / `boot-tests.log`: the existing loader suite (53 checks)
  and OS1 boot suite pass linked to the adaptive-enabled native core. These use
  an unbound compiler and do not replace armed/error-path lifecycle fixtures.
- `native-tests.log`: all 30 ordinary native suites pass. Snapshot stream fields
  and version remain unchanged. `dispatch.log` preserves protocol-only scheduler
  tests; that fixture owns no registry and therefore has a no-op forget stub.
- `first-build-failure/`: the first edit mistakenly inserted the restore barrier
  into stream verification too. Compilation rejected the undefined machine
  pointer. The barrier now exists only in the applying pass; the corrupt-input
  test verifies that validation does not discard the current lifetime.

Review identified the capture-only disable gap. Its regression test tries both
the old request epoch and a fresh request epoch with an old witness; both fail.
Only a newly captured witness can install afterward.

## Reproduction and limits

```sh
node tools/jit/adaptive/test-machine-runtime.mjs
```

Native lifecycle tests build `emucore` with
`-DCMAKE_C_FLAGS=-DS5LBOX_ADAPTIVE_JIT=1` and link `tests/lifecycle.c` with
`web/adaptive-runtime.c` and `core/libemucore.a -lm`. Native LTO logs retain
existing MBX warnings.

The snapshot barrier runs before any applying read, so its placement also covers
later file I/O failures. A deliberately failing third file-read pass has not been
injected. Init/boot/decryption barriers are implemented; the lifecycle matrix
focuses on restore/free/disable. The browser host and background compiler are not
integrated yet. Literal guards remain; this is not a speed result or a complete
all-writer qualification.
