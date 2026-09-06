# Finding the original audio engine's real reset request

## Tweet idea

The original iPhone's audio failure was pointing us at the wrong register. Its audio driver first sets a divider, then sends two separate reset commands through the clock controller. We now trace the real request—but we are not calling silence working audio.

## What you're seeing

This is a **nonvisual original-driver trace**, not a sound or screen demonstration. [The captured bus/caller sequence](../docs/traces/amc-first/baseline.log) and [provenance hashes](../docs/traces/amc-first/provenance.json) show 55.3 million bounded instructions from the genuine ringtone-page checkpoint, without changing register replies or Apple code. No recording is claimed for this nonvisual milestone.

The driver writes divider value 3 at `0x38502000`, then actual core/engine reset masks `0xa000` and `0x0a00` at clock-controller register `0x3c500050`. It subsequently reads engine status eleven times, receives zero, and reports the original reset failure. No SRAM upload or audio output is observed.

## ELI5 technical explanation

A device can be reset by another device. Our first trace watched the audio engine's own registers, so it saw a setup write followed by failed status checks. Watching the clock controller too reveals the missing reset commands. Disassembly connects those writes to the real device-tree reset functions and distinguishes them from the divider-setting helper.

The original driver also contains firmware loaders for program and data memory, but the failed reset prevents this observed path from reaching them. Simply returning the expected ready bit would skip over the missing hardware. We still need to establish reset completion, state clearing and the engine's behavior before modeling that transition.

## Why it matters

The next audio implementation can now begin at the correct hardware boundary. This corrects the engineering plan without fabricating readiness or sound. It does not establish working decoding, sample transport, audible playback or microphone support. [The audio plan](../plans/audio.md#actual-amc-reset-request-identified-readiness-remains-unimplemented) records the exact remaining uncertainties.

### Storage follow-up, same nonvisual investigation

An isolated 180,224-byte SRAM model now passes **279 storage checks and 14 machine-bus checks**. Two identical 55.3M-instruction replays with storage disabled/enabled still reach the same original reset failure and make zero SRAM accesses. Their traces differ only in the declared mode, and their RAM checksum matches. [The follow-up evidence](../docs/traces/amc-first/stage-a/verification.json) includes both traces and hashes. This is verified storage in a disposable probe, not a production snapshot change, firmware upload or audio output. No screen recording is claimed for this additional nonvisual work.
