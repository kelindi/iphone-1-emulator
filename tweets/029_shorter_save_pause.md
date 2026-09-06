## Tweet idea

Saving an entire iPhone meant pausing it to checksum hundreds of megabytes. Now we freeze a consistent copy, let the phone continue, and finish the checksum in small pieces. Same backup bytes, much shorter interruption: about 438 ms → 168 ms in the paired WASM check.

## What you're seeing

Recording: [shorter_save_pause.mp4](videos/shorter_save_pause.mp4), with [thumbnail](thumbnails/shorter_save_pause.jpg).

The complete Chrome recording shows the real guest responding to Volume and Silent controls while the page says Saving your iPhone, then the saved status and capture timing in Diagnostics. The middle section contains the volume overlay; the final section shows completion. No guest UI is recreated or video accelerated. The recorded save had a 198.5 ms initial capture and 9.42 s full completion; measured checksum/copy chunks peaked at 1.7 ms. The historical synchronous serializer is compared separately by `npm run test:capture`, not by this video.

## ELI5 technical explanation

A backup must describe one instant. Copying a little live RAM, running the phone, then copying more would mix different instants and could corrupt its disk or app state. We still serialize the complete machine synchronously into an owned copy. Once that copy exists, the guest can move on while we checksum the frozen bytes and copy them out in 1 MiB turns.

The final stream remains byte-for-byte the existing version 39 format. Three real-firmware Node/WASM comparisons each ran 11.65 million guest instructions during finalization and produced exactly the same backup as the blocking serializer captured beforehand. Median initial capture fell from 437.5 ms to 167.7 ms; this measures serializer work, not the full browser save or total app latency. The browser's full save is now longer because checksum and transfer work yield to the emulator before asynchronous gzip and IndexedDB commit.

Native tests also destroy the source machine before finalizing its copy and verify the original CPU, devices, RAM, and checksum restore correctly. Unfinished bytes remain inaccessible; API tests cover cancellation and repeated cleanup. All 23 native suites, WASM smoke, and 24 private-browser persistence checks pass, including valid/invalid Import and exact reload. An independent agent reviewed the copy lifetime and worker interleaving.

## Why it matters

Periodic saving interrupts the guest for less time, and the phone can respond while its backup is being completed. The measured initial capture still exceeds the plan's 150 ms input target, so this is a concrete improvement with remaining work, not full performance acceptance.
