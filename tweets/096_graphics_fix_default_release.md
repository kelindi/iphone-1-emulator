# Safari graphics correction reaches the default web phone

## Tweet idea

The real iPhone's Safari graphics fix is now in our default browser build. Existing saved phones survive the update, and the phone restores offline. This closes a rendering bug; slow app launches are still the next problem to solve.

## What you're seeing

[Full recording](videos/mbx_stride_default_release.mp4) · [Settings thumbnail](thumbnails/mbx_stride_default_release.jpg).

The uncut final-package check shows genuine Home around 0:10, Settings around
0:17, physical Home return around 0:21, ordinary autosave around 0:39, and
restoration with the origin server stopped around 0:55. Offline caching is
explicitly enrolled through the hidden diagnostic control; a normal first visit
does not enroll automatically. This is a functional check with observation waits,
not an app-launch benchmark.

## ELI5 technical explanation

Safari copies text from an image whose rows can be wider than the screen. Our
graphics model rejected those valid rows. The bounded correction from
[095](095_safari_wide_source_recovery.md) now ships in the local WASM build.
Apple's apps, the CPU engine and compiled instruction seed are unchanged.

All 30 native suites, 58 JavaScript tests and the WASM smoke pass. Updating an
actual previous-runtime saved phone passes 30 checks; the final unmarked bundle
passes another 21, including identical saved bytes after ordinary and physically
disconnected reloads. All observed browser processes close without forced cleanup.

The first upgrade attempt passed its functional checks but failed an added
response-body audit. Its evidence remains intact. A separately frozen successor
declares UTF-8 text correctly and distinguishes proven worker cancellation during
intentional startup failure from successful byte delivery. It verifies 180 complete
asset bodies; two unavailable negative-test bodies are never counted as byte proof.
[Release evidence](../docs/traces/mbx-stride-release/README.md).

## Why it matters

This puts the verified rendering correction into the default local phone while
preserving saved state. It is not a public deployment, Safari server recovery,
Photos acceptance or a speed improvement. App launches remain above target;
measured responsiveness is now the priority over further audio/network expansion.
