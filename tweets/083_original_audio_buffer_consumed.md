# The original iPhone software consumes its first decoded audio buffer

## Tweet idea

The original iPhone software now starts our experimental audio engine, reads every byte of its first decoded ringtone buffer, and returns that buffer through the real driver. The complete handoff is verified. Next: follow the genuine output path to timed speaker playback.

## What you're seeing

This is a nonvisual hardware trace milestone; no screen recording or audible playback is claimed. The matching [actual trace](../docs/traces/amc-first-consumption/trace.log) runs from the original userspace return records through `HANDOFF IPC`, the `PCM READ` records and `RELEASE RETIRED`, ending at `PASS first PCM read and ownership returned`. The [run, source and independent byte audits](../docs/traces/amc-first-consumption/README.md) retain its exact boundary and all eighteen corrupted-artifact rejections.

## ELI5 technical explanation

Making decoded sound bytes is only half the job. The original software must learn that a buffer is ready, read it, then tell the audio engine that the buffer can be reused.

We let the original driver fill the engine's bounded input queue. When that queue becomes full, its next transfer pauses naturally instead of inventing another completion interrupt. The original app then finishes initialization and explicitly starts output processing. Its real driver sends the notification, and userspace reads all 4,096 bytes of the first sound buffer. Every returned byte matches the independently decoded reference.

Two checks in our observer needed correction: userspace cannot read privileged kernel bookkeeping, and an interpreter's counter can advance before the instruction performs its memory write. The final probe records actual instruction-start and completed state. It verifies the original ownership-return store, its exact next instruction address and unchanged processor status, then stops immediately. Earlier failed probes remain documented.

## Why it matters

This verifies a complete first-buffer handoff through the original Apple software, including notification, sample reads and ownership return. Continuous timing, downstream PCM transport and audible output remain open. The engine and its GPL-licensed decoder are still isolated experiments; production and browser audio are unchanged.
