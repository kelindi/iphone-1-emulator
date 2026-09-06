# Giving the original apps room to work

## Tweet idea

Contacts editing now survives save, reload, and another app launch on our browser iPhone. The real OS was asking apps to quit because our temporary RAM disk consumed too much memory. Compacting a clean disk copy recovered 16.6 MiB without changing any Apple application.

## What you're seeing

[Recording](videos/contacts_memory_recovery.mp4) · [thumbnail](thumbnails/contacts_memory_recovery.jpg).

The 88-second recording joins original-speed sections from one Chrome session using the verified compact starting phone:

- 0–11s: enter Ada in the real Contacts app.
- 11–23s: save and reload the created contact.
- 23–39s: open the name editor, which failed on the older phone after this sequence.
- 39–48s: change Ada to Adam.
- 48–62s: save and reload the edited contact.
- 62–88s: launch Settings and reach its real settings rows.

Source sections are 131–142s, 165–177s, 186–202s, 221–230s, 251–265s and 330–356s of the compact Contacts validation session. Gaps are omitted; playback is not accelerated. Both saved contact reload comparisons match exactly. The validation also opens Clock between Contacts and Settings.

## ELI5 technical explanation

Our temporary storage device lives inside the phone's RAM. The old disk used about 184.6 MiB of a 256 MiB emulated machine, leaving too little room for apps. The genuine operating system reported urgent memory pressure and asked Settings to quit. The screen looked like a broken app launch even though touch input still worked.

We made a separate, clean disk copy and packed its allocated blocks together. The new disk is 168 MiB. All 5,120 ordinary data and resource forks retain identical SHA-256 hashes. The tool checks block ownership and refuses dirty or unsupported filesystems; seven synthetic tests cover content preservation and rejection cases. No Apple binary or memory-warning policy was patched.

That frees roughly 16.6 MiB of RAM at the cost of leaving only about 6 MiB free on disk. The genuine phone now completes create → reload → rename → reload → launch another app. The fresh Home checkpoint also includes successful original audio-codec initialization; that is not yet audible sound.

## Why it matters

It fixes a measured organizer workflow and makes the default starting phone more useful. Existing checkpoints remain intact, with their original disk layout. Export before Reset to keep an old phone while opting into the new starting state. This is a bounded improvement: separate persistent storage is still needed for large media libraries, and full-speed execution and online apps remain unfinished. See the [memory investigation](../plans/guest-memory.md) and [app audit](../plans/app-coverage.md).
