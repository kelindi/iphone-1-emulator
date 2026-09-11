# YouTube audio startup failure — 2026-09-10

The original player fails because `AudioDeviceStart` returns `0xe00002d8` (device not ready). Registering the audio callback succeeds. This is measured with the silent owned H264 clip, after exact HTTP delivery; it does not prove successful decoding.

## Observed chain

| Original call/return | PC | Result |
|---|---|---|
| Telephony-supported CoreAudio start | `3124db54` | actual route entered |
| AudioDeviceAddIOProc return | `31236680` | `0` |
| AudioDeviceStart return | `312366a0` | `e00002d8` |
| Mixer device start return | `31227e5c` | `ffffffff` |
| Mixer start return | `31238368` | `1` |
| MeCCA play return | `302830e0` | `1` |

Immediately before the audio return, the original serial output says `AppleBasebandOutput: could not start DMA: device is not ready`. These are qualified instruction-byte-pinned observations in media process TTBR `14a8f000`; the YouTube positive control runs in TTBR `16ff0000`.

[points.log](points.log) retains the bounded register observations; [serial.log](serial.log) retains the original diagnostic. [comparison.json](comparison.json) records the frozen inputs, executable hashes and full control/probe comparison. Both runs use identical observer scheduling; the control omits observation. All 252,624,866 saved bytes and the final frame match. This is a correctness check, not a speed benchmark. No original instruction, return value, device completion or movie data was patched.

## What remains unknown

A second qualified run directly reaches the original I2S configure and start routines. It writes `00=1, 40=0, 04=01100301, 30=0, 08=0, 34=0, 3c=1`. The controller object's synchronization state at `+0x8c` is 0 on entry and 4 (timeout) on return. At the original return instruction `c040bcec` (`mov r0,r6`), r6 is `e00002d8`. No callback at `c040be44` occurs in this replay. The original driver only takes this return with state 4 after its synchronization wait times out, before submitting DMA or writing TX enable 6.

[kernel-comparison.json](kernel-comparison.json) confirms every state byte and frame match the control again. [kernel-pins.json](kernel-pins.json) contains the exact original instruction pins, and [kernel-points.log](kernel-points.log) records the observed registers and bounded controller state. The earlier ringtone result is therefore reproduced at the I2S synchronization gate for the actual YouTube attempt. GPIO170 and the I2S1 physical mapping are established by earlier driver/device-tree work; this observer does not separately trace GPIO MMIO or translate the controller's MMIO virtual base.

A correct fix still needs external clock source/frequency/gating, FIFO capacity and DMA pacing, including enable/disable and saved-state semantics. Arbitrarily pulsing GPIO or forcing success is not a device implementation. Playback, sound and live YouTube remain unavailable.

## Prior work checked

[openiBoot I2S definitions](https://github.com/iDroid-Project/openiBoot/blob/master/plat-s5l8900/includes/hardware/i2s.h) identify the same I2S1 baseband path and register offsets. Its [PMB8876 radio implementation](https://github.com/iDroid-Project/openiBoot/blob/master/radio-pmb8876/radio.c) configures speaker audio through baseband commands, but labels clock and I2S meanings as uncertain. This supports investigating external baseband clock control; it does not establish a frequency, FIFO depth or safe production implementation.
