# Space for disposable browser tests

The VM's browser Blob storage needs several GiB free. To make room for the
original Safari service test, five **completed historical test exports** were
losslessly gzip-compressed, decompressed and checked against their full original
size/SHA before removing only the raw duplicate. This reclaims about 692 MB.
Their `.iphone.gz` and `.iphone.archived.json` files stay beside the original
locations. No current fixture, firmware, IndexedDB or service-worker cache changes.

[Exact original/archive paths and SHA receipts](archived-completed-exports.json),
[executed archiver](archive-completed-exports.py).
Use [materialize.py](materialize.py) from the repository root with the adjacent
`.archived.json` path to restore an old raw export if needed. It verifies every
byte before atomic creation and refuses to overwrite a different file.
The compression preserves the artifact; it is not a snapshot-format migration.

A second pass archives the six remaining completed historical `paused-phone.iphone`
exports by the same full roundtrip check, reclaiming another 830 MB. Their original
raw paths are now absent; the adjacent gzip and receipt preserve the bytes and
the same materializer restores them. [Second-pass paths and hashes](archived-completed-exports-2.json)
and [executed source](archive-completed-exports-2.py) retain the operation. This
creates headroom for the isolated compiler comparison; no current fixture or
browser saved state was removed.

A third pass archives five completed RTC picker/reload snapshots from the closed
`rtc-write/browser-preview/instrumented-run-1` session, reclaiming 735,300,170
bytes of raw-versus-compressed storage. [Paths and full raw SHA receipts](archived-completed-snapshots-3.json)
and [executed source](archive-completed-snapshots-3.py) preserve the operation.
Their original `.snap` paths are now absent; adjacent `.snap.gz` and
`.snap.archived.json` files restore through the same materializer. The active
RTC-before-manual-change, MBX, CPU and audio fixtures remain untouched. Free-space
readings also include concurrent focused build activity, so the before/after
filesystem difference is not the exact compression saving.

The fourth pass archives nine closed historical CPU replay outputs, reclaiming
1,251,299,206 bytes. [Their paths and full hashes](archived-completed-replays-4.json)
and [executed archiver](archive-completed-replays-4.py) retain exact restoration
through the same adjacent gzip/receipt scheme. The active compiler agent confirmed
these outputs were not inputs to the current LTO audit. Current replay fixtures,
firmware and saved browser profiles remain intact.

[Three closed functional browser HTTP/Code caches](older-cache-cleanup.json)
were also removed. Those download/compilation caches can be rebuilt; saved phones,
service-worker storage, recordings, traces and source remain. An earlier attempted
hardlink deduplication of the exports found their bytes differed and changed none.

A fifth operation replaces 246 duplicate durable LTO browser artifacts with atomic
hardlinks to their closed private originals, after verifying both full file hashes
and sizes. Both paths and all bytes remain; this reclaims 361,850,446 bytes.
[Per-file receipts](lto-browser-hardlinks.json) record the operation. No browser
profile changed. These historical artifacts are immutable: any future replacement
must use a new inode and atomic rename, never overwrite shared file contents.

A sixth operation archives six further closed outputs (one old Settings backup
and five old CPU replays), reclaiming approximately820MB of allocated storage.
[Exact paths and shared-materializer receipts](archived-completed-replays-6.json)
and [executed archiver](archive-closed-cpu.py) preserve the operation. The archiver's
original `.snap.archive.json` receipts use different field names and stay retained;
adjacent `.snap.archived.json` receipts supply the exact `originalPath`/`gzipPath`
schema expected by the shared materializer above. Raw duplicates are absent; all
gzip hashes were independently rechecked. Saved browser profiles are unchanged.

A further operation archives both closed Safari rendering diagnostic exports and
the separately parsed immutable 256MiB RAM image. Full raw SHA/size roundtrips
pass before removing duplicates, reclaiming456,981,453 bytes. [Exact receipts](archived-safari-rendering.json)
and [executed source](archive-safari-rendering.py) use the same adjacent
`.archived.json` materializer schema. The incomplete first launcher and corrected
second result both remain preserved; no active browser or current firmware changed.

A seventh operation deduplicates three byte-identical closed CPU gzip outputs and
archives the old `replay-speed.snap` using an independently matched compressed
witness, reclaiming560,648,192 allocated bytes. [Receipts and preserved mismatch
refusal](closed-cpu-dedup-7.json). The initial script stopped when NZCV's bytes
differed; that distinct artifact remains separate. An eighth operation similarly
archives the closed [old WASM replay](closed-wasm-replay-8.json). Adjacent standard
receipts restore both raw paths; all original logs remain.

A ninth pass archives two closed RTC browser snapshots, reclaiming294,101,389
bytes. [Exact paths](archived-closed-rtc-9.json) and [executed archiver](archive-closed-rtc-9.py)
retain full roundtrip proof. The protected canonical RTC **before** manual-change
fixture is unchanged; this archives its completed **after** state and a separate
old picker-test input. Shared restore receipts stay beside each gzip. These
operations provide the preserved six-GiB starting reserve for actual old/new
MBX runtime upgrade verification.
