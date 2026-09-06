# After the actual PCM software copy: bounded transport observation proposal

Source-only proposal; no new guest, model, FIFO acceptance or production change has been run. Stage R remains the latest executed boundary. Existing five input admissions/two publications and all engine/IRQ/ownership behavior remain fixed.

## What original code establishes

The original MeCCA SHA256 is 15f8f049239e64f88660aeb1ce99aa07273ccd19ca636e624ff19e9a68e7f4ec. Stage R's actual r5=39254d38 matches the original AudioOutputBufferManager singleton. Its exact destination00387000/physical160e4000 and original caller window are established, but the object's fields were not captured. Do not call this physical address a DMA ring.

`AudioOutputBufferManager::init` at3122be50 allocates0x40000 bytes through operator new[] at3122be90 if its base pointer is null; it zeroes that allocation. Static fields are +0 allocation base, +4 logical end (base+field20), +8 reader pointer, +0c writer pointer, +14 fill bytes, +20 logical capacity. The first writer position is base+4*field18. This last offset means even the actual00387000 write destination must not be assumed to equal allocation base without reading fields. `setOutputFormat` at3122bf80 selects a logical capacity from its actual arguments, stores sample-rate argument at+2c (3122c03c), and may reinitialize. Init writes44100 as a default; it is not evidence of the active output rate. Decoder48000 is a separate supported AAC policy.

`writeSamples` copies the original decoded buffer into this ring and updates metadata. `readSamples` at3122c174 copies out through +8, handles wrap against+4 and decrements+14. The direct copy call at3122c464 is one of several conditional paths; no runtime selection has yet been observed.

`Mixer::AudioOutputCallback`312288f0 calls this readSamples at31228990, then `Mixer::mix`31228190 at31228a08. It returns the output pointer and may copy the mixed result into the callback-provided destination. It can also produce silence when the main channel is not playing. Thus a later sample stream need not remain byte-identical to the first raw decoder PCM.

`MeccaCoreAudioDevice::StartIO`31236650 registers HALOutputProc31236734 via AudioDeviceAddIOProc at3123667c, then calls AudioDeviceStart at3123669c. HALOutputProc obtains actual AudioBufferList output bytes at+8 and data at+0c; it calls the configured output callback through object+10 at312367ec using count=bytes>>2. This establishes a CoreAudio bridge, not its actual execution, active buffer layout or kernel DMA ownership. Stereo/signedness/rate cannot be inferred merely from that shift.

See output-init-dis.txt, output-read-dis.txt, mixer-callback-dis.txt, coreaudio-start-dis.txt, coreaudio-output-proc-dis.txt and coreaudio-bridge-dis.txt. All are bounded original-file disassembly, with embedded source hashes. Data-pool lines are not executed instructions.

## Existing hardware boundary and unanswered runtime questions

The M68 device tree names wm8758 on i2s0; older common soc.h comments describe N82 WM8991 and must not override the actual M68 tree. I2S0 physical3ca00000, DMAC0 physical38200000. Current core stores seven I2S configuration slots and counts unknown FIFO accesses at+10/+38, without audio clock/queue. PL080 can perform RAM-to-peripheral transfers; its ready callback gates SPI capacity and does not pace I2S. A first actual DMA write to3ca00010 therefore presently reaches the unknown-write stub, not a speaker.

Stage R artifacts do not expose current I2S/DMAC/codec registers or actual AudioOutputBufferManager fields. Absence of their trace lines does not imply no earlier configuration. Whether a hardware ring/channel was already configured before083 is unresolved. No rate, clock-enable polarity, channel ownership or first FIFO word is invented from defaults.

## Smallest next isolated observation (review before implementation/run)

Keep the same fresh fixture, engine, caps, retirement receipts and final first-owner-release stop. Do not extend instruction execution merely to wait for a callback.

1. At the existing actual first-copy observation, use captured USR context and actual r5 to read only the first0x40 manager bytes. Bind original writeSamples caller/prologue and singleton symbol as already verified. Record availability explicitly; do not privilege-override, substitute defaults or follow arbitrary pointers. Reinspect those same fields at the verified owner release using retained context. Validate pointers/order/capacity against actual fields before any optional bounded ring read; the independently verified4096 destination remains the byte oracle.
2. Copy current model I2S0/1, DMAC0/1 channel/global registers and their existing counters directly into observer-owned artifacts at fixture load, first PCM publication and owner release. Capture current clock/power and WM8758 stored registers with defined raw field names, no MMIO reads/writes or guessed semantic decode. Record which values were present at fixture load versus changed during this run. Copy plain state only: no serializing decoder snapshots or exposing host pointers.
3. Add at most six image-bound, mode-checked observational sites for actual setOutputFormat entry/return, StartIO call/return and HAL output callback entry/return **if these naturally occur before the existing stop**. Every new pre-step record has separate committed successor/IRQ-diversion status. Do not infer absent calls as failures. Retain all consumption and current ordering hooks; bounded total records (128) and32-byte code windows. If the current hook table cannot accommodate this without changing old coverage, first do only items1–2; avoid replacing required old witnesses.
4. No DMA list walk during device steps. At final stop, inspect at most the captured active channels' immediate16-byte LLI records only if the physical range is normal RAM; optional unavailable status otherwise. Do not run channels, clear IRQs, probe FIFOs or advance the guest to populate missing data.

Positive: every prior083/Stage R artifact and trace equals baseline after removing only new observer records; actual manager fields/device configuration and their availability are retained, even if no HAL call is reached. Negative: changed manager bytes/code/addressspace, out-of-range optional LLI, fabricated availability, observation mutating RAM/TLB/device counters, wrong successor/IRQ-diversion must reject the same frozen auditor. No full-buffer or actual hardware allocation claim from one LLI record.

The run retains155.3M total and all5M subcaps,240s wall/180s CPU/1GiB/8MiB file bounds, five DMA admissions, two publications, and stop immediately after verified first ownership return. One fresh run only after final source/tests/pins review. No unsupported admission, extra publication or artificial playback event.

## Later continuation gate remains separate

After these actual fields are reviewed, a later bounded continuation may trace the actual HAL callback and first hardware FIFO request. It must stop before an unsupported third publication or sixth DMA, and before unmodeled FIFO acceptance if reached. If those existing caps prevent the callback, preserve that earliest boundary and propose an explicit ownership extension based on its real state; do not auto-increase caps. A real format/clock/PL080 source/destination witness is required before pacing FIFO or forwarding samples to the browser. The isolated GPL decoder limitation remains separate from transport feasibility. No oracle-PCM playback shortcut.
