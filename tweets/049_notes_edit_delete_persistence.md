# Notes keeps creations, edits, and deletions

## Tweet idea

The original iPhone Notes app now has a complete checked workflow in the browser: type Hello, reload, edit it to Hello web, reload, delete it, and reload again. Every saved screen returns with identical note-area pixels. The keyboard and Notes are Apple's real 2007 software.

## What you're seeing

[Original-speed recording](videos/notes_edit_delete_persistence.mp4) · [thumbnail](thumbnails/notes_edit_delete_persistence.jpg).

The recording shows the whole browser run, including loading and saves. Around 00:26 the note reads **Hello**; 00:50 shows it after reload. Around 01:04 it reads **Hello web**, and 01:23 shows the retained edit. The genuine Delete Note confirmation appears around 01:25. The final section shows **No Notes** after another reload, then Home. The thumbnail is the edited note at 01:04. Fixed observation waits are part of this functional test; the recording is not a speed benchmark.

## ELI5 technical explanation

Notes stores its text inside the emulated phone. Browser Save captures that phone's memory, devices, and disk state, then writes a compressed checkpoint. Reload must restore the edited disk as well as the screen; a deletion must also stay deleted.

The maintained `npm run test:notes` uses the original on-screen keyboard and real app controls in a disposable browser profile. It compares every RGB pixel in the native 320×450 area below the status bar after each reload: **144,000 pixels, zero differences**, three times. Independent visual review confirms Hello, Hello web, the deletion confirmation, No Notes, and Home. With `REQUIRE_JIT=1`, it also requires generated CPU execution before and after the workflow; both gates pass.

The first automated replay exposed a test synchronization problem: it sent the edit tap before Resume had actually re-enabled the phone surface, then subsequent taps opened Mail. Matching saved pixels alone could not detect the wrong app. The harness now waits for acknowledged browser controls and the genuine Notes keyboard before typing. The failed run is preserved as failed evidence; the recording here is the successful fresh replay. Content still requires visual review—the pixel comparison establishes restore identity, not text recognition.

## Why it matters

Notes now has evidence for its complete basic create/edit/delete workflow and three reloads, extending the earlier single-note save milestone. This strengthens useful offline app coverage. Interrupted-save recovery, offline upgrade, and general Export/Import have separate tests; this run does not claim every combination of those operations with note edits or full-speed acceptance.

Reproduce with a built browser server:

```sh
REQUIRE_JIT=1 ARTIFACT_DIR=.context/notes-workflow npm run test:notes
```

Reviewed evidence: `.context/notes-verified-ready/`. The earlier rejected replay is `.context/notes-verified/`.
