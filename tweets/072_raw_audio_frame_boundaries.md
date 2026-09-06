# Raw audio frames survive a partial input buffer

## Tweet idea

The original iPhone ringtone bytes now pass a real decoder test: 1,069 raw AAC frame boundaries match the original file, and a frame split across buffers resumes without changing a single decoded sample. This is an offline building block for the audio engine—not guest playback yet.

## What you're seeing

This is a nonvisual decoder milestone; no screen recording or audible playback is claimed. The matching evidence is the [complete report and per-frame records](../docs/traces/aac-framing/README.md), especially the [16 KiB cutoff/52-byte continuation log](../docs/traces/aac-framing/cut.log) and [all 372 truncation probes](../docs/traces/aac-framing/truncate.csv).

## ELI5 technical explanation

The real guest supplied 16 KiB of ringtone data, ending partway through an audio frame. An emulated device cannot secretly consult the app's private packet table to decide where frames end. We tested whether a real decoder can find those boundaries itself.

A disposable decoder tries the available bytes. Only a successful complete frame reaches the persistent decoder, which retains the history needed for the next sound. At the cutoff, 49 frames were complete and the 50th had only 319 of its 371 bytes. It stayed uncommitted until the test supplied the missing 52 original bytes. All incomplete prefixes were rejected. The raw, split-input and independently packetized control paths produced the same 4,374,528 PCM bytes, including the decoder's real startup behavior.

The test uses the original file's decoder configuration explicitly as an offline fixture. That does not supply configuration to the emulated hardware. The pinned decoder version, source/license, resource limits and exact hashes are retained with the evidence.

## Why it matters

A second independently written parser also verifies all actual frame records, the original partial-input bytes and full PCM equality. Its [source and result](../docs/traces/aac-framing/root-audit.json) are retained beside the first oracle.

This establishes a practical route to consuming genuine raw compressed input without a hidden packet-table shortcut. The emulator still needs a complete device-facing configuration, buffer ownership, interrupt/reset and sample-clock model before the guest can use it or produce sound.
