# Why the ringtone preview is still silent

## Tweet idea

We traced the real 2007 iPhone's ringtone preview to two missing hardware behaviors: an audio engine that cannot finish reset and a frame-clock interrupt that never arrives. A diagnostic experiment starts DMA—but every captured sample is zero. Moving bytes is not yet making sound.

## What you're seeing

This is a **nonvisual hardware-investigation milestone**, with no playback recording or audible-audio claim. The matching evidence is the original driver's register trace and the all-zero capture analysis documented in [the audio investigation](../plans/audio.md#measured-playback-blocker-ringtone-preview-2026-09-05). No unrelated footage is used.

## ELI5 technical explanation

The sound chip's configuration connection now works, but the phone has several audio paths. Its Alarm ringtone preview uses the baseband audio connection. Before sending samples, the real driver waits for two clock events that the emulator does not generate. Another audio engine, AppleAMC, also fails its reset sequence.

In a separate diagnostic run, we supplied test edges to learn what the driver would do next. It started its own DMA transfers, without changing Apple's code. But the current emulator empties buffers without audio timing, and all 241 million captured 16-bit values were zero. That experiment identifies missing hardware; its artificial timing has not been added to the product.

The next bounded step is to trace the audio engine's reset and buffer-production path, while finding a genuine media workflow that uses the separate WM8758 codec connection. A readiness bit alone cannot substitute for the missing engine.

## Why it matters

We now have exact failing registers, driver calls and a reproducible sound request. That keeps browser audio work focused on real guest samples and prevents a host sound effect—or a buffer full of zeros—from being mistaken for working iPhone audio. Existing production behavior and snapshot compatibility remain unchanged by this investigation.
