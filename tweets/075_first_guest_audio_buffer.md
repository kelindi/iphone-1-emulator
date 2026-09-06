# The original iPhone driver reaches its first decoded audio buffer

## Tweet idea

The original iPhone audio driver now supplies real ringtone data to our isolated audio engine and gets its first 4 KiB decoded buffer. The bytes match an independent decoder reference exactly. Next: let the guest consume and release that buffer, then work toward actual playback.

## What you're seeing

This is a nonvisual hardware trace milestone; no screen recording or audible playback is claimed. The matching evidence is the [actual run trace](../docs/traces/amc-first-pcm/trace.log), from `ACCEPT engine initialization` through `HOST layout`, the four session writes, and `PASS first PCM publication`. The [report and audits](../docs/traces/amc-first-pcm/README.md) record the exact boundary and deliberate input/PCM corruption rejections.

## ELI5 technical explanation

The original driver expects a little audio computer inside the phone. That computer needs memory buffers, a way to receive compressed bytes, and rules saying who owns each finished sound buffer. Simply announcing that it is ready would leave those resources missing.

Our isolated model allocates real bounded resources at the driver's actual start command. The original code accepts two distinct buffers and writes its real session settings. The engine reads only bytes delivered through the device's actual transfer registers. A real decoder finds the raw frame boundaries; the app's private packet table remains an external test oracle.

After the decoder's first startup frame produces no samples, the second produces 2,048 signed 16-bit sample values: 1,024 stereo frames, or 4,096 bytes. Every byte matches the independently packetized reference. Checks also verify the original uploaded program, actual input and every memory change. Two separate corrupted artifact sets are rejected by the same frozen auditor. A [second independent review](../docs/traces/amc-first-pcm/root-review.json) confirms the actual input, PCM, output header and unchanged memory.

## Why it matters

This connects the original driver and genuine input to actual decoded output. The probe stops before the next guest instruction or output-interrupt refresh, so it proves the first buffer's creation while leaving guest consumption, ownership return, continuous timing and speaker playback for subsequent gates. Production and browser audio remain unchanged.
