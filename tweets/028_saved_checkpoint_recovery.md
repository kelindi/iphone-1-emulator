## Tweet idea

A damaged browser save should not mean losing your phone. We corrupted real iPhone checkpoints, exported the broken bytes unchanged, recovered the earlier Settings screen, and interrupted a save midway through its database transaction. Both stored generations survived the failed write.

## What you're seeing

Recording: [saved_checkpoint_recovery.mp4](videos/saved_checkpoint_recovery.mp4), with [thumbnail](thumbnails/saved_checkpoint_recovery.jpg).

Sections: **0:11–0:22 corrupt gzip and recovery**, **0:23–0:33 truncated native save and recovery**, **0:34–0:45 incompatible version**, **0:45–1:02 interrupted save and retry**, and **1:02–1:21 storage access denial and restoration**. The recording runs at its original speed.

This is a deliberate recovery test, using the genuine Settings checkpoint exported by the persistence suite as its fixture. The test seeds corrupt compressed data, a truncated native snapshot, and incompatible version metadata in an isolated browser profile. It then uses the visible Export and earlier-save recovery buttons. Later, it aborts an actual IndexedDB write transaction and simulates denied storage access. These failures are injected test conditions, not spontaneous corruption during normal use.

The recovered Settings screen matches the saved guest pixels below the status bar after the transient ringer overlay clears. The test reads both database generations and compares their metadata and SHA-256 hashes; the displayed recovery message alone is insufficient to pass.

## ELI5 technical explanation

The browser stores a current checkpoint and its previous complete generation. Starting from a damaged save stops visibly while keeping its bytes available for export. Recovering an earlier checkpoint starts that genuine guest state without silently replacing the broken record. A save transaction changes both generations together, so aborting halfway through rolls everything back.

The 24-check browser run covers compressed and native corruption, exact damaged-backup export, earlier-generation recovery, incompatible profile metadata, failed writes, successful retry, unavailable storage, and restored storage access. The page can run an unsaved guest while storage is inaccessible, but Save and Reset stay disabled. When access returns, the original saved phone is still available.

This is a controlled Chrome test with a real exported checkpoint. It does not yet establish every browser's quota behavior, power-loss recovery, or migration between actual released snapshot versions. The incompatible-version notice now correctly says a different emulator version may be needed, rather than assuming it must be older.

## Why it matters

Persistence becomes useful only when failures leave an understandable way back. These checks show that a damaged current save or interrupted browser write does not automatically erase the earlier phone, and users can keep the exact backup bytes for later recovery.
