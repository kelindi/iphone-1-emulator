# Calendar edits and contact data survive browser reload

## Tweet idea

The original iPhone Calendar now has a real browser workflow: type an event, save it, reload, rename it, reload, delete it, reload. Contacts can create and delete Ada too. These are the original apps and keyboard, with their real data preserved in the emulator checkpoint.

## What you're seeing

[Screen recording](videos/calendar_contacts_persistence.mp4) · [thumbnail](thumbnails/calendar_contacts_persistence.jpg)

Selected original-speed excerpts from real browser sessions, with intermediate navigation and observation waits trimmed:

- **0:00–0:33:** type `Meet` in Calendar, create the event, and save/reload the browser.
- **0:33–0:58:** reopen the title editor, append `up` to make `Meetup`, then save/reload again.
- **0:58–1:26:** confirm Delete Event and retain the empty day after save/reload.
- **1:26–1:56:** create `Ada` in Contacts with the original keyboard and retain the contact after save/reload.
- **1:56–2:27:** a separate fresh-profile Contacts session confirms deletion and retains the empty contacts list after save/reload.

Calendar's guest clock still reads **December 31, 2009**. These clips do not establish current-date correctness, alarms, carrier service, or full speed. Contacts name editing after reload failed in a separate original-snapshot test; native investigation identified guest memory pressure. A later fresh compact-rootfs candidate passes that edit/reload plus Clock/Settings launches; see [the memory investigation](../plans/guest-memory.md). That follow-up is separate from this original-snapshot recording and does not migrate existing user saves.

## ELI5 technical explanation

Clicking an app icon proves that its screen can open. We need stronger evidence before calling the phone useful: someone must enter real data, change it, and find the right result after closing and reopening the browser.

The browser test taps the emulated touchscreen and types on Apple's original onscreen keyboard. Calendar and Contacts write their own data inside the guest. The browser's Save control captures the emulator state into its local checkpoint, and page reload restores it. No JavaScript replacement calendar or injected contact database is involved.

Three Calendar checkpoints preserved the created, renamed, and deleted states. Separate Contacts tests preserved creation and deletion. Each tested reload had zero changed samples in the guest framebuffer below the status bar, and the resulting event/contact content was checked visually. Continuing to edit Calendar after reload additionally shows that the restored screen remains interactive.

The failed Contacts name-edit test matters too: delivering a tap correctly does not ensure the old operating system has enough memory to finish an app transition. Its original broken checkpoint was exported for diagnosis rather than hiding the failure with a replacement UI.

## Why it matters

The browser iPhone now has verified offline organizer workflows beyond app launches. This provides a repeatable acceptance test for future emulator and storage changes while keeping the remaining guest stability and timing problems visible.

Reproduce the verified Calendar sequence against the built web frontend:

```sh
BASE_URL=http://localhost:8080 ARTIFACT_DIR=.context/calendar-report node web/tests/organizer.mjs
```

The sequence is tracked in the script, so it needs no previous `.context` files. The harness creates an isolated disposable browser profile, records video, saves screenshots and action logs, and compares each explicit Save/reload. An unattended replay of this exact tracked command completed successfully in 225.5 seconds, with all three checkpoint comparisons equal and screenshots reviewed. Review new screenshots for semantic correctness; framebuffer equality alone cannot recognize event text. See [the app coverage audit](../plans/app-coverage.md) for remaining app gates.
