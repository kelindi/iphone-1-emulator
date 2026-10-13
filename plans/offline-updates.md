# Updating an installed offline phone

> Historical recordings and publishing artifacts have been removed from Git history. The summaries below describe the original runs; remaining test reports and screenshots are retained. Historical hash receipts may predate this archival cleanup.

An existing offline installation now checks for an update in the background at startup. A first-time visitor is not registered until they choose **Keep offline**. A waiting update gets a separate notice: **“Phone update ready. Save, close all phone tabs, then reopen to update.”** Saving or pausing does not replace this notice.

The service worker still never calls `skipWaiting`. Every open phone keeps its active content-hashed shell, manifest and WASM together. Normal browser activation happens after all phone tabs close. The upgrade neither resets the guest nor rewrites its IndexedDB checkpoints. Failed update checks while disconnected are quiet; phone startup and saving remain independent.

## What this does not migrate

A shell already cached before the new startup check cannot execute code it has not downloaded. Its browser's normal service-worker update discovery, or choosing Keep offline again while connected, must first discover the updated bundle. Save and close all phone tabs, then reopen online to complete that initial transition.

Fresh-phone firmware changes are separate from saved-profile migration. Existing v39 profiles retain their genuine saved boot state, including an old USB-unmatched configuration. Export a backup before explicitly choosing Reset if the new starting phone is wanted. No upgrade automatically opts a saved phone into that reset.

Firmware cache URLs are currently stable across releases. When firmware hashes change, the first updated visit may need a connection to replace old firmware cache entries. This test deliberately holds the firmware bytes constant and proves the shell/manifest/WASM upgrade and save preservation; it does not claim first-visit offline migration of changed firmware.

## Real Chrome regression

Run `node web/tests/offline-update.mjs --record` after building the browser bundle. `PORT` selects an unused local port (default 8091); `ARTIFACT_DIR` selects a disposable artifact/web root. The test copies shell files and links read-only firmware into that root, generates two independent content-hashed service workers, and uses an isolated Chrome context. It never publishes test versions to the live web root.

Both versions run the genuine guest. Test-only shell markers, a manifest metadata field, and a valid inert WASM custom section make their bytes distinguishable without inventing an emulator behavior change. The test opens real Settings and saves an actual native checkpoint through the browser UI.

The completed 2026-09-05 run in `.context/offline-update-verified/report.json` passed all 16 checks:

- A new visitor has no service-worker registration until Keep offline is chosen.
- After publishing B, reloading the controlled A page discovers B without another Keep offline click. A's shell marker, manifest marker and WASM SHA-256 remain consistent while B waits.
- A second phone tab lacks write ownership. Closing only one tab leaves A active; closing both and reopening activates matching B.
- The same genuine Settings guest restores with profile ownership. The 248,807,394-byte raw checkpoint remains stored as the exact same 103,630,618-byte gzip Blob, SHA-256 `2c95612ddaabdab8c5936d2a3a304cf39d64b5f0b11b6aba34918b0ef2f85915`. Saved time, format version and size are unchanged.
- The waiting notice disappears after activation. B and the saved guest reopen with network requests blocked, and the failed background update check does not replace normal phone status. There are no uncaught page errors.

The before/after Settings screenshots were also inspected and decoded to RGB: all 153,600 pixels in the displayed 320×480 guest area match. This supplements the byte-exact saved-record comparison; it does not substitute a host-rendered screen for the guest.

The first run's offline assertion incorrectly relied on `navigator.onLine`, which remained true in this Chrome setup even after Playwright blocked network access. The maintained check instead requires an uncached, non-service-worker request to reject. The rerun passes with that actual network probe, and records both observations.

Milestone 039 contains the matching original-speed tab recordings. This is an update safety gate, not a speed measurement.


## Validation with the compiled CPU engine

The maintained upgrade fixture now copies and content-hashes `seed-jit.js`, `jit/seed.wasm`, `jit/regions.json` and `jit/abi.json` along with the shell and main WASM. Omitting those new dependencies would make the isolated upgrade fixture incomplete. Two additional checks observe the installed engine after version B activates and after its disconnected reopen. For a restored paused phone, the harness briefly resumes the live guest to receive worker metrics, then restores Pause; it leaves the durable checkpoint untouched.

The final production-bundle regression passes **all 18 checks** in `.context/jit-production-offline-update-verified/report.json`. The saved record remains exactly identical: raw size 248,836,066 bytes, gzip size 103,631,768 bytes, SHA-256 `babb41bc27b292b5cd8d160311498f3d107ae64a59b6352f0996bfa2506b3c5c`, with unchanged timestamp/version/metadata. All 153,600 RGB pixels in the displayed 320×480 Settings area match exactly before and after upgrade (`upgrade-pixel-comparison.json`). Version B installs 511 seed regions after disconnected reopen and actually retires 5,584,644 instructions through 711,548 generated calls. The network probe fails as intended even though this headless browser reports `navigator.onLine=true`.

Separate actual-production checks also pass: **24 persistence checks**, including generated-instruction retirement during the real Settings launch, repeated saves and previous generations, raw export, invalid/valid imports, exact reload pixels, ringer/paused state and released input (`.context/jit-production-persistence/report.json`); and **7 offline checks**, covering disconnected engine installation, corrupt-firmware rejection/removal and recovery after reconnect (`.context/jit-production-offline/report.json`). These are functional compatibility checks, not speed measurements. The first upgrade attempt exited during isolated server startup on port 8101 before any browser test; the complete verified replay uses unused port 8112.
