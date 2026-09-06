## Tweet idea

Typed “Hello” on the real 2007 iPhone keyboard, saved the emulated phone, refreshed the browser—and the note was still there. The browser iPhone now keeps a durable checkpoint, with exportable backups and protection against competing tabs overwriting it.

## What you're seeing

Recording: [persistent_hello_note.mp4](videos/persistent_hello_note.mp4), with [thumbnail](thumbnails/persistent_hello_note.jpg).

The 52-second excerpt covers opening Notes, typing with the original guest keyboard, tapping Done, saving, reloading the page, and returning to the same note. It is taken from the full persistence test's 81.87–133.62-second section. The restored screen below the status bar compares exactly to the saved screen, and visibly contains the title and body “Hello”. No host text injection or recreated note UI is used.

## ELI5 technical explanation

The phone's memory includes both the running apps and its writable ramdisk. We expose the core's existing snapshot serializer to the browser worker, copy a consistent machine checkpoint out, and store it transactionally in IndexedDB. A write commits the data and metadata together and retains the previous complete generation. One browser tab holds the write lock; other tabs can inspect their phone but cannot overwrite the shared profile.

Raw checkpoints initially triggered Chrome private-browsing Blob storage quota failures after repeated saves, even when the reported origin quota looked sufficient. Streaming gzip compression reduced the tested checkpoint from about 244.5 MB to 101.3 MB and passed the repeated-save regression. Old raw checkpoints remain readable, corrupt gzip is rejected, and expanded-size limits prevent uncontrolled allocation.

Snapshot capture currently pauses the guest for roughly 0.6–0.9 seconds. Compression runs asynchronously while the worker can continue emulation; the measured full save took about 6.2 seconds on this VM during concurrent development. This is functional persistence, not the final performance target.

The real-browser suite passed 22 checks: repeated saves, Export, invalid Import reaching the native validator without changing the live guest, second-tab restrictions, exact reload, retained ringer state, blur/input cleanup, and the saved note. Four additional storage helper tests cover raw compatibility, gzip roundtrip, corrupt data, and size limits.

## Why it matters

The browser iPhone can now retain something the user created instead of always resetting to a demonstration snapshot. This supplies the first durable offline-app workflow and the backup/recovery foundation required for contacts, calendar entries, settings, and future media imports.
