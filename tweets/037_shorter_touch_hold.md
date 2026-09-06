# A modest improvement to real iPhone typing

## Tweet idea

Typing in the real iPhone OS 1.0 now feels a little quicker: a shorter touch-release hold reduced median visible-letter latency from 438 to 338 ms in our test. All six ten-letter notes were correct. Physical buttons keep their longer debounce; the big app-navigation delays still need separate work.

## What you're seeing

[Original-speed comparison](videos/shorter_touch_hold.mp4) · [thumbnail](thumbnails/shorter_touch_hold.jpg).

- **0:00–0:26.92:** the first 100 ms baseline run, including loading Notes and typing `Qwertyuiop` on the original keyboard.
- **0:26.92–0:50.16:** the first 20 ms candidate run, performing the same action.

These are complete sequential recordings at original speed, with labels added. Their differing startup time is not the claimed typing improvement. The actual comparison used three alternating pairs, 30 characters per build, and measured the first frame containing each committed character separately from the key popup. The video shows one representative pair; the [input timing report](../plans/input-latency.md) contains all run medians, limitations and reproduction details.

Every host click lasts 30 ms. The test waits at least 400 ms per key and another 200 ms after detecting commitment, so its total typing duration is a guarded test sequence, not maximum typing speed. The candidate recording uses the isolated experimental build; production adopts its touch timing while retaining the original 100 ms physical-button floor.

## ELI5 technical explanation

A browser click must become a press and a release that the original touchscreen driver actually reads. We previously held every press for at least 100 milliseconds of guest time. The improved touch controller already preserves pending press/release reports, so we tested a shorter 20 ms touch hold instead.

Both native traces and real Chrome runs delivered all tested letters correctly. The browser's median committed-letter time fell by about 100 ms. A further check used all keyboard rows, repeated letters, backspace and retyping, ending with the exact expected `Codexhello` note. New tests also prove that an unread press cannot be replaced by its release, while Home keeps its separate 100 ms floor and a real release interrupt.

All 24 native suites pass, including 10,149 browser-API checks. No Apple binary or snapshot format changed.

## Why it matters

It removes some avoidable waiting while typing into the genuine apps. It does not fix the multi-second Settings/Home delays, and both typing builds still had occasional commits around two seconds. The larger performance work remains necessary.
