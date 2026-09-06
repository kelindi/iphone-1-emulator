# The original audio driver can reach its codec

## Tweet idea

The 2007 iPhone's real audio driver now reaches its emulated WM8758: 56 traced initialization writes succeed. It packs nine-bit values into two-byte commands. This gets the control path working; carrying real audio samples to the browser is the next job.

## What you're seeing

This is a nonvisual protocol milestone. There is no playback recording and no audible-audio claim. The matching evidence is the genuine driver's cold-boot transfer trace and the command sequence preserved in [the codec test](../core/tests/test_wm8758.c). The [audio investigation](../plans/audio.md) records exact driver addresses, device-tree wiring, representative bytes and validation limits.

## ELI5 technical explanation

The operating system talks to the sound chip in two different ways. One connection sets its controls; another carries the sound samples. The original iPhone's control connection had no device answering it in this emulator, so the real driver received errors.

We inspected that original driver and watched the bytes it sent. Its sound chip uses nine-bit settings squeezed into two-byte messages, unlike the later iPhone codec already modeled in the project. A separate WM8758 endpoint now understands those messages, while old saved phones retain their existing snapshot format. Tests also pause and restore the machine between the first and second byte of a command.

The sample connection still needs FIFO timing, DMA requests and browser output. A successful volume-register write alone does not make a speaker work.

## Why it matters

It removes an observed device error on the genuine audio driver's startup path and gives sample-transport work a tested foundation. All 24 native suites pass, including 460 codec checks; three older Home, Settings and Calendar checkpoints load and resave correctly. The later iPhone's separate codec behavior remains intact.
