# RTC and streaming-loader integration

The reviewed complete-date RTC model and streaming seed loader are integrated in the default local web build. The exact final unmarked bundle passes21 browser checks and an independent artifact audit. This is local integration, not a public deployment or completed product speed gates.

The supported clock operation is the original driver's complete valid seven-register date update in2000–2099, applied at the next pulse. Pending updates roundtrip through snapshot39. Partial/invalid dates and independently altered weekdays remain unsupported. [Native source and checks](../rtc-write-contract/complete-update-native/README.md), [two actual edits and autosave/reload](../rtc-write-contract/manual-time-persistence/README.md), and [all12 lifecycle checks](../rtc-write-contract/clock-lifecycle/instrumented/README.md) retain the evidence.

The optional seed loader verifies bytes before compiling the original fetched Response, allowing Chrome to reuse native code. Its runtime class and512-region seed are unchanged; cancellation, size limits, MIME fallback and corrupt-input rejection remain checked. [Actual physically disconnected cache reuse](../jit-stream-cache/sw-cache-offline/README.md) and [the controlled speed comparison](../jit-stream-cache/return-visit-v3/README.md) describe separate gates. First Settings improved about30% in the latter experiment, while Home p95 worsened49.8ms. It does not establish the two-second navigation target or performance on the user's Mac.

## Integrated bytes and checks

- WASM SHA256: `7a72add6d886440f64a853511375ba51cbbb23f2263831b6a05c51fde7969088`.
- Loader SHA256: `3c2cc4122a9b32ab22db44d253e26920650c885c1c019b170a2951002ce597e9`.
- Unchanged seed SHA256: `d9c661556ab17bb8433ed9b0c3ecfd52e2fcafe58221b4cb84a920e4976b3881`.
- Offline generation: `9c31daade3d7df58b6e2624a`; SW SHA256 `60b00dc89d60f5d5f7babdc0e4fb3a88b5d5228eacf50241e3a73dd58ebd8abd`.
- All28 native CTest suites pass against the integrated source; all58 maintained JavaScript tests and the WASM initialization smoke pass.

Only core date/SECOND behavior, its three relevant C test files/CMake targets, the loader/tests and WASM change. The generated service worker matches the exact current UI, worker, manifest and seed. `emu.js` is byte-identical. Files were staged and replaced atomically so historical hardlinked private sites retain their original bytes. [Integration receipt](integration-applied.json) and [release projection](release-projection.json) identify every source and asset.

The combined upgrade workflow uses the actual oldb323 runtime and new7a72 runtime, preserving the existing saved phone. It passes30 named checks, including interpreter fallback after optional-seed corruption, rejection of a corrupt update and recovery of a corrupt firmware cache entry. Its independent transport audit excludes a malformed redundant relative-URL probe, distinguishes46 service-worker HTTP responses from two local Blob worker scripts, and verifies six actual remote-disconnection receipts plus two physically closed local listeners. The preserved report and corrected diagnostic helper remain separate evidence; no guest retry was used to erase the malformed probe.

The [final default-release recording and evidence](../rtc-release-upgrade/final-release/README.md) verify all28 frozen/current source files and actual served core, loader and SW hashes. A fresh phone opens genuine Settings and returns through the physical Home button, performs a natural autosave, and restores the identical saved bytes online and with the private origin physically stopped. Both Home reloads match all147,200 compared RGB pixels below the status bar (294,400 total). All16 offline HTTP responses come from the SW with zero network responses; actual local/remote nonce errors and TCP refusal are retained. Generated execution advances after each reload, with no uncaught errors. Offline installation uses an explicitly labeled hidden diagnostic action; ordinary first-visit auto-install is not claimed. The full57.32-second recording is uncut at original speed.


**Later frontend follow-up:** [visible recovery](../visible-recovery/README.md) moves existing retry/recovery actions into the phone error overlay and passes its own 12-check real-browser session. It changes the shell generation to `6fc3927f86a86269a30dc811` while retaining this exact core, worker, loader, seed, manifest and storage. The 21-check combined bundle evidence above remains scoped to its frozen earlier UI hashes.
