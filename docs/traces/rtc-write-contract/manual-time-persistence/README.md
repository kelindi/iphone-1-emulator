# Original manual time changes survive real autosave and reload

> Historical recordings and publishing artifacts have been removed from Git history. The summaries below describe the original runs; remaining test reports and screenshots are retained. Historical hash receipts may predate this archival cleanup.

The isolated complete-RTC-write candidate passes the single instrumented browser workflow: **19 checks**, two real picker changes, two natural autosaves, two reloads and final unpaused generated execution. The full historical recording (removed) runs 123.68 seconds at25fps (historical thumbnail (removed)). The test's elapsed time, including setup/close, is125.650seconds. This is a candidate result; production promotion is separate.

## Actual interaction and persistence

The current production frame, hidden companion and startup spinner were copied unchanged to a private localhost8201 site. Only its private runtime dist was replaced. The hidden Import input loaded the existing before-picker fixture, and hidden Export controls captured diagnostic states. Those are explicit test instrumentation, not newly visible product controls. The Save button was never invoked; no test storage writer or forced interval substituted for autosave.

The imported historical fixture initially has selected wheel13 but header4:14. The first real pointer press selects visible15 at guest(160,460); the next selects16 at(160,416). Both use real180ms press/release. Actual post-edit screens display4:15 and4:16. Each edited state is exported **before** persistence, avoiding the earlier missing-evidence boundary.

The unmodified application's natural30-second autosave must commit a timestamp newer than a recorded post-edit baseline. The actual committed gzip records are retained, then the page reloads. Both timestamps/blob sizes remain the committed values. The harness compares36,000 picker pixels per reload, and a separate offline comparison expands this to **all147,200 RGB pixels below the status bar for each pair**,294,400total: zero differing channels, maximum difference0. The actual PNGs were also visually reviewed. The status bar is excluded; no whole-screen timing-independent equality is claimed.

At each reload, generated instructions and calls must actually increase while unpaused. The final separate interval retires35,722,175→46,930,846generated instructions. The report records no page errors or failed requests; its three console warnings concern diagnostic canvas readback performance, not product failures.

## Actual hardware-state audit

`export-audit.json` independently loads all seven actual snapshots through the native snapshot39 decoder, executing **zero guest instructions**. The two compressed autosaves are expanded into anonymous Linux memory files, preserving source bytes and avoiding extra filesystem pressure. Original source hashes remain unchanged.

| Actual state | PMU time, UTC | Pending date flags | Unsupported writes | Existing unsupported reads |
|---|---|---|---:|---:|
| Before picker |00:14:20|all zero|0|51|
| First edit |00:15:28|all zero|0|52|
| First real autosave |00:15:42|all zero|0|52|
| First reload |00:15:47|all zero|0|53|
| Second edit |00:16:30|all zero|0|53|
| Second real autosave |00:16:43|all zero|0|54|
| Second reload |00:16:48|all zero|0|54|

All values are2010-01-01UTC, corresponding to the actual previous-day local picker. The preexisting unknown-read counter is retained explicitly; this is not a claim that all PMU registers are supported. Restored ticks/instructions advance and RTC advancement agrees with elapsed ticks within one-second quantization. Complete bytes, fractions, write counters, date registers and hashes are in the report.

## Compatibility and promotion boundary

The private WASM SHA256 is `7a72add6d886440f64a853511375ba51cbbb23f2263831b6a05c51fde7969088`. Seed SHA256 remains `d9c661556ab17bb8433ed9b0c3ecfd52e2fcafe58221b4cb84a920e4976b3881`. ABI revision1 and all16 offsets match; actual browser JIT installs511regions/12,341,313module bytes. All127 served file hashes remained unchanged. Public snapshot schema remains39; no Apple binary changed.

The supported model admits a complete, calendar-consistent seven-register date update for2000–2099 and applies it at the next real1Hz pulse. Partial/invalid writes remain explicitly unsupported; alarm behavior, arbitrary hardware date-write sequences and universal date ranges are not included. Existing native evidence includes1,089focused checks, seven exact pending-state snapshot roundtrips and the original seven-write callback trace ([native evidence](../complete-update-native/README.md)). Broader maintained lifecycle/update validation and production integration remain the parent's separate gates.

## Preserved failure and resource correction

The first recorded preview failed at natural autosave with “Failed to fetch”; it did not reach the second edit/reload and was not relabeled successful. [Original failure](../complete-update-browser/README.md). A no-emulator reproduction then isolated the failure to streamed Blob creation, with all HTTP dependencies present and zero origin storage use. After inventoried cache cleanup raised disk headroom above Chromium's documented reserve, the **unchanged** storage path completed an exact255,983,586-byte save/restore. [Shared storage-pressure evidence and primary source](../complete-update-browser/storage-diagnosis/README.md).

Additional frozen snapshots were sparsified with every SHA256 unchanged before this separately named workflow. No production storage correction was needed. The successful run has an explicit7.2GB initial free-space guard and preserved console/network diagnostics; it is one new evidence-backed attempt, not an automatic retry.

The private `.context/rtc-write/browser-preview/instrumented-run-1/` retains the profile, all raw exports/gzip records, original WebM and full raw frames. Durable evidence here includes the executed harness, native read-only inspector/auditor, reports, hashes, actual PNGs and the four compared raw frames. Large Apple-containing snapshots are not redistributed. `artifact-manifest.json` pins the retained small evidence.

The separate candidate lifecycle gate has now passed all12 maintained assertions, including exact hidden/frozen and paused restore state plus JIT generation1→2. Its initial export timeout was an adaptation regression: hidden DOM click omitted the enabled wait while visibility autosave was still active. [Preserved failure and instrumented correction](../clock-lifecycle/instrumented/README.md). Production integration remains separate.
