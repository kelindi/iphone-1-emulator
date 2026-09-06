# Narrow MBX source-stride integration — in progress

Only the independently qualified BGRA source stride limit changes, from320 to472
pixels. Original destinations, source format/row mappings, validation and atomic
commit remain unchanged. Safari rendering-only candidate095 passes; HTTP daemon
and Photos qualification are still separate failures.

Maintained `core/src/soc/mbx.c` and `core/tests/test_mbx.c` now exactly match the
reviewed isolated candidate. A fresh native build passes all30 CTest suites.
All58 JavaScript tests and the staged actual WASM smoke pass. Candidate WASM
3b43ccf4a8292a4da776491aa92185b0ea88e2fc38a3002a6e1fe1d5a6aa4463 is
built from exact released RTC inputs with only mbx.c.o replaced; glue, seed,
public exports/imports and JIT ABI remain unchanged.

Immutable previous and candidate web projections are retained here. Their only
runtime asset difference is `dist/emu.wasm`; generated service worker changes
6fc3927f86a86269a30dc811 to c145cb60d9fe4ab0e1460ed8. Both use the same
R2 firmware manifest and16-app starting snapshot. Production `web/dist/emu.wasm`
and generated service worker remain unchanged until actual previous-to-candidate
saved-phone upgrade, offline recovery/fallback, and exact final package checks.
All shared old asset inodes are immutable; integration must stage and atomically
replace changed production paths, never overwrite a hardlinked file in place.

The CPU agent is preparing an adapted copy of the retained actual old/new RTC
upgrade harness with these exact hashes and meaningful final/served-byte checks.
Initial reserve remains6GiB; no quota override, profile deletion, fake WASM tag
or old-state reset is permitted. Test shell/manifest markers are diagnostic;
after acceptance run the exact unmarked final default for fresh navigation and
ordinary autosave/reload. No public deployment is implied by local integration.
