# Pause remains available while saving

## Tweet idea

Our browser iPhone no longer greys out Pause for the whole autosave. It can pause and resume while its captured checkpoint is compressed and written, then stay paused when saving finishes. A small fix for a very visible “why did my click do nothing?” moment.

## What you're seeing

[Original-speed recording](videos/pause_during_save.mp4) · [thumbnail](thumbnails/pause_during_save.jpg).

The full 18.72-second recording shows a real phone loading, manual Save, Pause → Resume → Pause while that save is in progress, successful completion while paused, and a final Resume/reload. The thumbnail corresponds to the completed-save-and-paused section. The manual action exercises the same save implementation used by the periodic autosave.

## ELI5 technical explanation

Saving already copied the phone's state into separate memory, then finished checksums, compression and browser storage asynchronously. The running phone could keep working, but the page unnecessarily disabled Pause until all that work finished.

The page now permits Pause and Resume during an ordinary checkpoint. The worker still owns a fixed snapshot of the earlier moment, so changing the live phone's pause state does not change the bytes being saved. Import and Reset retain their existing serialized controls. Save completion now preserves the Paused status instead of replacing it with a normal running message.

A real-Chrome regression passes 14 checks: both pause/resume directions work during saving, paused ticks and retired instructions remain exact, the save completes, the phone stays paused, and the checkpoint reloads. Restoring the previous disabled-button rule makes that same test fail at its first check. Recorded acknowledgments during saving are about 0.30–0.37 seconds on this busy VM; this is an availability improvement, not a claim that all input now meets the 150 ms target. Ordinary Resume after saving took about 70 ms in this sample.

Reproduce with `npm run test:save-pause`. Reports and original recording are in `.context/save-pause-production/`; the negative control is `.context/save-pause-negative/`.

The separate saved-checkpoint recovery suite also passes all 24 checks against this frontend, covering corrupt/incompatible checkpoints, transaction rollback and storage-access recovery. Its report is `.context/save-pause-recovery/report.json`.

## Why it matters

A periodic background operation no longer makes a basic control unavailable for several seconds. The change preserves checkpoint integrity and helps distinguish real emulator latency from a button that was deliberately disabled by the page.
