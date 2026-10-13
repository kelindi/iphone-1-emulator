# Make the verified local Safari demo available in new phones

> Historical recordings and publishing artifacts have been removed from Git history. The summaries below describe the original runs; remaining test reports and screenshots are retained. Historical hash receipts may predate this archival cleanup.

**2026-09-06 qualification result:** the maintained quiet daemon boots from the actual pristine accelerated-default image and original Safari renders its landing page and Next. The first form stalls with guest graphics hangs; the final process list also lacks the daemon, with exit cause unobserved. Bookmark/reload/final Home remain unfinished. No default promotion or retry. [093 failure evidence](../docs/traces/guest-http-default/README.md).

This is the concrete next integration after [091](../docs/traces/guest-safari/README.md). The isolated test already proves original launchd/dyld execution, loopback HTTP, Safari URL entry/link/form/bookmark behavior, and real autosave/reload of the running service. It does not yet provision the default phone. No production changes or new guest run are part of this proposal.

Implementation preparation is now authorized and in progress in
[`tools/legacy_services/guest_http/`](../tools/legacy_services/guest_http/README.md).
The persistent service has passed its first parser and quiet/diagnostic lifecycle
checks, with identical socket response digests. The reusable builder and
copy-only installer are being qualified; the remaining permission/installer
checks, final source pins and pristine-image native/browser run are pending.
No default asset, remote object or production manifest has changed. The final
quiet executable will be tested with fresh form content, the genuine bookmark
and uncached post-reload rendering; raw HTTP wire evidence remains the explicitly
verbose 091 qualification.

## 1. Maintain the independently authored program

Move the reviewed `server.c`, `http.c`, `http.h`, own launchd plist and host tests into `tools/legacy_services/guest_http/`. Make the build take explicit ARM-capable Clang, compatible classic ld64, and extracted original libSystem paths; remove `.context` paths. Retain the exact compiler/linker version and public ABI provenance from 086/087. A checked-in, independently authored small executable can accompany its reproducible source/build hash so normal users need no toolchain. No Apple library or SDK implementation is added to the repository.

Promote only the already tested executable mode/owner arguments to `rootfs_work.py`, retaining its five catalog/roundtrip tests and admission before mutation. A dedicated installer must add just the own program and plist to a **copy** of a qualified clean image, reject an existing path, check catalog capacity/first keys, and compare every prior fork and file record afterward. It must not edit mounted images, Apple executables, application resources or original bookmark templates.

The qualification daemon currently stops after 64 connections or 3,600 listener polls and does not request respawn. For a default service, remove that test-only lifetime ceiling while keeping finite work per connection, fixed 2 KiB/4 KiB buffers, one accepted client at a time, socket ownership checks, loopback-only admission and ordinary blocking/poll waits. It must never busy-loop on an error. Keep a bounded host-test stop seam and explicit clean shutdown/error behavior. Do not assume a modern launchd `KeepAlive` policy applies to OS 1.0; no respawn behavior is required for a process that remains in its normal service loop.

Use quiet production logging for startup and failures. Full hex request/response logging remains a diagnostic build option; exact response-byte tests must prove this option changes logging only. This avoids paying dozens of console writes for each small page. Test sustained requests and failed/partial I/O for fixed descriptor/memory ownership, then perform one real Safari workflow on the final shipping executable rather than treating the verbose qualification binary as identical.

## 2. Prepare a pristine starting phone

Start from the clean compact work-image generation underlying the **current default rootfs**, verifying its multipart hashes and filesystem state. Do not ship the 091 fixture: it came from a prior data-retention qualification, contains test data, and still had the original Repair Needed alert. Keep the existing 168 MiB disk/256 MiB machine geometry unless the checked installer demonstrates a real capacity issue.

Add the two own files, preserve the current activation/no-autolock work-image policy and all existing app data, and boot once with the current unmodified kernel and original USB-enabled device tree. Require actual listener success, real Home, expected guest memory level, lockdownd/service health and current MBX policy. Dismiss the original alert through genuine touch as in the existing starting-phone preparation. No USB-unmatch workaround, clock-policy change, different seed/ABI or replacement renderer belongs in this change.

Create a bookmark named **Local demo** through the actual original Safari UI, targeting `http://127.0.0.1:18081/`, then return Home and capture the new starting phone at a normal boundary. This uses the bookmark format the original app itself writes and preserves its existing bookmarks. Do not edit `MobileSafari.app`'s built-in bookmark plist or copy the qualification form/history state into production. Compare default Contacts/Notes/Calendar/Photos data with the pristine source and record the small, intentional Safari history/bookmark difference.

The rootfs and matching Home snapshot must both contain the daemon so cold-start fallback and fast startup offer the same feature. Stream/verify compression and use the existing multipart format; retain source/fork/own-binary/snapshot hashes. Existing large fixtures remain untouched. The next actual boot/browser run should use explicit bounds and disk reserve, not a series of repeated hello/loopback qualification boots.

## 3. Discover it through the original app

The native **Local demo** bookmark is the primary entry. The page already says that it is fixed content served inside the emulated phone. Preserve that visible label on the landing page; extend the subsequent pages' label if needed in the final reviewed source so shared screenshots cannot be mistaken for an internet service.

Add a small product help entry: “Open Safari → Bookmarks → Local demo.” It should explain that new phones include this local service. A host-page link to `127.0.0.1:18081` would target the user's browser environment, not the guest, so do not present that as a working host navigation button. No automatic touch sequence, guest-memory text injection or replacement HTML Safari view is needed.

Older saved/imported phones keep their original state and may lack the daemon/bookmark. The feature must not overwrite or reset them. Help should state that a new phone includes the demo and that users should export before choosing Reset. There is no automatic in-place migration in this change. If later UI wants to claim availability for a specific saved phone, it needs explicit backward-compatible provisioning metadata or a real guest check; a newer shell alone is insufficient evidence.

## 4. Review and publish matching assets

Before any default change, verify the final own binary/imports and host contracts, all prior filesystem forks, genuine new-phone Safari page/link/form/bookmark and real autosave/reload, plus unpaused positive JIT. Check Home/Settings/Contacts and current MBX/USB behavior. The 091 recording is the qualification reference, not a substitute for testing the changed final program and pristine image.

Prepare new immutable rootfs/Home asset objects, hashes, manifest and generated service-worker generation together. Keep the current UI, core and seed bytes unless an explicitly reviewed change requires otherwise. Root controls the existing R2 publication/release flow; upload verified immutable objects before switching the manifest. Do not silently replace a remote object behind an old hash or publish only one half of the rootfs/snapshot pair.

Run the maintained persistence/offline/actual old-runtime upgrade gates against this concrete release. Require that old browser saves remain exact and usable, fresh offline phones have the demo, and an already saved demo phone can load its bookmark and issue a new guest request after reload. Retain physically stopped-origin receipts for any offline claim. Only then promote the matching default assets and record a new visual integration milestone.

The shortest useful result is a discoverable local Safari bookmark on a pristine new phone. Weather protocol delivery, outside networking, Photos fixtures and full-speed targets remain independent work; this integration does not resume the parked wireless-device investigation.
