# Existing software ring and audio transport state at first PCM consumption

One isolated Stage S run passes the entire Stage R trace and every prior final binary byte-for-byte, with all083/input/PCM/retirement/destination/original caller audits and27 actual artifact negative controls. It executes no extra guest instructions and stops after the same first owner-release instruction. No new hooks, DMA behavior, admissions, publications, FIFO acceptance or sound.

## Actual captured state

The original MeCCA manager at39254d38 is read through the retained actual user address space and original caller/code binding. Its first0x40 bytes establish allocation base00383000, logical end003c3000 and capacity0x40000 (256KiB). The first PCM copy destination00387000 is base+0x4000, not allocation base. Reader pointer remains00383000; writer advances00387000→00388000 and fill bytes0x4000→0x5000 between the existing copy observation and owner return. Actual software-format field+2c is0xbb80=48000, consistent with original setOutputFormat's store of its rate argument. This is **not** a measured hardware clock or I2S rate.

Both I2S models have zero stored registers and zero reads/writes/unknown FIFO counts at fixture load, first PCM publication, copy and release. DMAC0 is disabled with all registers and historical access/transfer counters zero. DMAC1 is unchanged from the fixture:9 completed items/90,528bytes on channel3, retained destination3d200010, controller/channel disabled; this is not an I2S destination. No immediate LLI qualifies, so no LLI memory is inspected. Thus the current modeled I2S path has not been configured or accessed by this boundary; absence of prior trace lines alone was not used to conclude this.

WM8758 is different:29 registers have written flags and the retained register-write count is63 at all four stages. Raw clock/power values change during original AMC initialization and remain unchanged thereafter. The [interpretation](interpretation.json) retains every written codec register and nonzero clock offset; no unsupported bit/rate decoding is inferred. Neither a software allocation nor stored codec settings establish an output DMA ring, continuous transport or speaker readiness.

## Evidence and limits

[Run](run.json), [full immutable guest trace](trace.log), [transport audit and raw ordered values](transport-audit.json), [fixture](transport-fixture.json), [first publication](transport-first-pcm.json), [actual copy](transport-copy.json), [actual release](transport-release.json), [original caller audit](caller-audit.json). All binary receipts and private memory artifacts remain at.context/amc-first/stage-s/run-1; hashes, source, JSON and logs are retained here.

[Implementation contract](implementation.md), [source diff](source.patch), [pins](review-pins.json), [build](build.json),41 native observation/permission/state-immutability/LLI checks and6 external auditor controls pass, alongside all14 native targets and prior auditors. The four added actual artifact negatives change a scalar receipt, manager bounds, manager availability and LLI availability; all reject. Two pre-guest setup/test mistakes remain documented in implementation.md, without any guest retry.

Exactly one guest run5.283492seconds, exit0; no speed claim. Same five DMA admissions/two publications,155.3M total/5M subcaps,240s wall180s CPU1GiB8MiB-per-file. Manager fields are explicitly unavailable before their actual binding is captured. The copy-stage artifact includes a uniform contemporaneous hardware report but adds no guest/device access. Plain model storage is copied directly, optional RAM peeks respect bounds/actual user permissions, and no live bus callback is used.

This is nonvisual follow-up evidence to083. No separate visual milestone/recording, production source, snapshot ABI or browser audio change. A future source-reviewed gate must locate actual software output-start/preroll and CoreAudio/DMA configuration; it cannot treat48000 or zero I2S state as permission to synthesize playback. The isolated GPL decoder remains outside production.
