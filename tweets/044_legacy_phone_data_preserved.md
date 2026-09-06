# An older saved phone keeps its contact through a fresh boot

## Tweet idea

Can an old saved browser iPhone get the faster boot without losing its data? A first proof: let the real OS shut down cleanly, reboot its same disk with USB enabled, and Adam is still in Contacts. An experimental step toward upgrades that keep your data.

## What you're seeing

[Original-speed recording](videos/legacy_phone_data_preserved.mp4) · [thumbnail](thumbnails/legacy_phone_data_preserved.jpg).

The complete 48.64-second recording first loads a disposable browser and imports a checkpoint
from the experimental new boot. Around 0:25–0:29 the original Repair Needed alert
is dismissed; 0:30–0:42 opens Phone and Contacts; 0:42–0:46 shows the retained
Adam contact. The final section pauses and exports that phone. The thumbnail is
at 0:45. No contact is created in this recording.

Shutdown and the fresh native boot happened beforehand using a disposable old
Contacts fixture. This is a recorded verification of their result, not a
migration flow already available in the browser. [The investigation](../plans/legacy-upgrade.md)
records the exact source/clean-disk/checkpoint hashes and shutdown trace.

## ELI5 technical explanation

A saved phone is a photograph of its entire running computer, including the old
USB configuration. A new emulator alone cannot restart that old computer's
missing service. Copying its disk while it is running is also unsafe: the OS may
still hold some file changes in memory.

We held the actual Power button and dragged Apple's original power-off slider.
The real OS flushed the disk and marked it clean before shutting down. Its disk
had no missing or overlapping file blocks, and the Contacts database passed its
integrity check. The unchanged disk then booted with the original USB device
tree. The real `lockdownd` service stays alive, the interactive-clock check
accepts the new boot, and Apple's Contacts app still displays Adam.

The OS had also created an empty attributes-tree file that our earlier disk
compactor did not support. A narrowly checked extension preserves that entire
file and every ordinary file fork byte for byte. Ten synthetic tests and an
independent real-disk audit pass. Its test output is only one block smaller;
this is preservation evidence, not a meaningful memory saving.

## Why it matters

One old 168 MiB saved phone now has a demonstrated route to the newer boot while
retaining its contact. A safe browser upgrade still needs rollback, failure
handling, enough space and wider app/profile validation. Larger old disks and
unsupported filesystem layouts remain explicit gates; no existing user's
profile was reset or migrated by this experiment.
