# A shorter pause when Save captures the phone

## Tweet idea

Saving the browser iPhone now captures its memory faster: the first memory snapshot fell from 225–273 ms to 136–143 ms in the actual browser. It produces exactly the same backup bytes. A smaller stall, with more work still needed to meet our input-latency target.

## What you're seeing

[Original-speed recording](videos/snapshot_page_scan.mp4) · [thumbnail](thumbnails/snapshot_page_scan.jpg).

- **0:00–0:17.72:** original snapshot capture, with genuine Save, Pause, Resume and saved-phone reload.
- **0:17.72–0:35.40:** candidate capture using the same workflow at original playback speed. At **0:29.72**, the actual phone is paused while Save remains in progress.

This is the first accepted pair from a quiet baseline/candidate/candidate/baseline comparison. The recording shows the real phone and surrounding controls; the small latency difference is established by the accompanying measurements. All four sessions pass 14 checks each, including no guest-time advance while paused, continued execution after Resume, successful Save while paused, and reload of the saved phone. Generated CPU execution is confirmed before every run.

## ELI5 technical explanation

Before a backup can be compressed, the emulator captures the phone's memory. It checks each memory page for a simple shortcut: is every byte the same? Such a page can be described compactly. The old check looked at one byte at a time. The integrated change safely compares eight bytes at once and checks the remaining tail, preserving the same page classification and file format.

The direct WebAssembly capture benchmark in Node falls from about 280–283 ms to 97–98 ms. The actual Chrome workflow shows a smaller but repeated improvement: capture is **272.6 → 135.6 ms** in the first pair and **224.9 → 142.8 ms** in the reversed pair. The first Pause acknowledgement falls from **328.3 → 180.9 ms** and **250.2 → 206.2 ms**.

The faster comparison must not change a save. More than 1.1 million sanitizer-backed page comparisons cover every byte value, first differing position, alignment and page-tail length. Full snapshot bytes match across two real phone fixtures, including captures finalized while the guest keeps executing.

## Why it matters

The unavoidable first step of Save occupies the emulator for less time. This complements the earlier compression-worker change, which freed the browser page from encoding work. It does **not** establish a faster total Save: finalization and compression remain separate costs, and the measured first Pause acknowledgement still exceeds the 150 ms target.

[Durable evidence, exact results and limitations](../docs/traces/snapshot-page-scan/README.md). The recording preserves the tested candidate. That exact runtime is now integrated: all 26 native suites, 34 JavaScript checks, the WASM smoke check, 12 production clock checks, 23 persistence checks and 24 old-to-new offline upgrade checks pass. The maintained snapshot suite now has 602 checks, including partial-page wire-format and restore cases. All eight recorded Home stages were independently reviewed and match exactly below the status bar. The original instruction seed and snapshot format remain unchanged.
