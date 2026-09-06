# Autosave failure isolated to VM disk headroom

The failure was not a missing asset or an RTC write. Relevant scripts loaded with200 responses and exact hashes. The no-emulator first owned write failed before IndexedDB committed anything; the origin reported zero usage against10GiB quota. The direct maintained codec also failed converting its compressed stream into a Blob, while manually draining that identical gzip stream succeeded with105327978bytes.

Chromium documents a4GiB minimum free-disk reserve for desktop64 Blob storage ([primary source](https://chromium.googlesource.com/chromium/src.git/+/refs/heads/main/storage/browser/blob/README.md)). The VM was below that reserve. Another agent removed only inventoried obsolete browser caches, preserving failures and guest data; its private receipt is `.context/jit-sw-cache/obsolete-cache-cleanup.json`. Settled free space rose to4771352576bytes.

The unchanged first workercompression/IDB/Blob/decompression operation then passed in10.482seconds: all255983586original bytes restored with SHA256 `d478cf35dbebaf45319c75175df29c743e9f21929eea36b3b63827de454bfb5d`. This controlled before/after result supports the filesystem-resource cause. Neither product storage code nor candidate RTC code was changed. The original failed guest attempt remains retained. A separately instrumented guest workflow is prepared but not yet executed; it will collect console/network errors and post-edit exports before persistence.

The preserved reports/scripts prove scope and phases. Private netlog is omitted because it includes incidental browser background traffic; app asset receipts and zero failed requests are retained in the storage report. The storage-only profile contains the immutable test fixture, not user data.
