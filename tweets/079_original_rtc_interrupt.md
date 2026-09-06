# 079 — The missing tick behind the original clock setter

## Tweet idea

The2007 iPhone clock setter was waiting for a hardware tick we hadn't emulated. Adding that event let the unmodified Apple driver send its real seven-byte date update. One more missing piece found between the original OS and its virtual hardware.

## What you're seeing

This is a native device milestone. The [actual trace](../docs/traces/rtc-write-contract/second-interrupt/causal-replay/run-1/trace.log) follows the queued date, real callback, seven I2C writes and retired epilogue. It stops after25,249,108 instructions; it is not a visual clock-change or speed recording.

For UI context, the existing [uncut manual-time diagnostic](videos/manual_time_diagnostic.mp4) shows the original date picker around02:56 ([thumbnail](thumbnails/manual_time_diagnostic.jpg)). That earlier recording is separate from this native replay and retains its failed final unpaused check. It does not prove the new interrupt behavior or persistence.

## ELI5 technical explanation

The original clock driver writes down the requested date and asks the power chip to wake it on the next second tick. Our virtual clock counted seconds, but never rang that bell. Original kernel code and two actual saved states showed which interrupt it enabled.

An isolated change now records that event whenever the emulated calendar advances a second. The existing interrupt wiring wakes Apple's unchanged code, which converts its pending date into the seven real chip-register bytes. The same replay without that event never reaches the callback within its declared bound.441 focused checks and six altered-trace negatives support the result.

## Why it matters

We now know why the original hardware setter never ran. The next step is implementing the chip's actual date-write behavior and verifying retained time through the original UI and Save/reload. Those writes are still ignored by the device model; production clock behavior remains unchanged. [Exact evidence and limitations](../docs/traces/rtc-write-contract/second-interrupt/causal-replay/README.md).
