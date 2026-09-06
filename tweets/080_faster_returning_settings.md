# 080 — Settings opens faster when you return

## Tweet idea

An experimental web iPhone now opens Settings about1.5seconds faster after you return to the browser. Chrome reuses verified compiled emulator code. The original2007 software is unchanged. We also measured a small Home tail regression, so this is not shipping yet.

## What you're seeing

[Complete actual returning-session recording](videos/seed_cache_return_settings.mp4) · [thumbnail](thumbnails/seed_cache_return_settings.jpg), **00:00–01:49.08**. First Settings is visible around00:27. The uncut recording contains the normal import, eleven Settings/Home cycles and ordinary automatic saving, at original speed. It is candidate arm2 from the frozen four-session comparison; the tested shell predates the latest separate UI update. [Source/output hashes and duration](../docs/traces/jit-stream-cache/return-visit-v3/recording.json).

**Production remains unchanged. The later real service-worker/offline mechanism now passes [milestone 084](084_offline_compiled_seed_reuse.md); the combined runtime upgrade and fallback gates still precede promotion.**

## ELI5 technical explanation

Our CPU seed turns supported original ARM instructions into WebAssembly. Chrome then compiles that again for this computer. The experimental loader lets Chrome remember some of those compiled instructions between browser processes, while still checking the original downloaded bytes and keeping all emulator guards.

Two fresh profiles received equal training. Four new browser processes then ran original→candidate→candidate→original with real no-cache/ETag validation. Both candidate returns identify the exact seed URL and successfully deserialize21,104,920bytes of native-cache metadata. HTTP caching alone would not establish this. All88 strict paired/reference screens pass, all eight first-stage references were manually reviewed, and all48 scoped Chrome process identities close.

| Order | Loader | First Settings | Warm Settings median / p95 | Warm Home median / p95 |
|---|---|---:|---:|---:|
|1|Original|5.112s|3.292 /3.667s|.868 /.917s|
|2|Streaming|3.566s|3.217 /3.567s|.866 /.917s|
|3|Streaming|3.417s|3.092 /3.267s|.868 /1.016s|
|4|Original|4.909s|3.339 /3.916s|.867 /.916s|

First Settings improves30.24% and30.40% in the paired directions. With20 warm observations per action/configuration, Settings median improves3.314→3.117s and p953.667→3.417s. Home median stays near.867s, but its pooled p95 rises.9167→.9665s: **49.8ms worse**, driven by candidate arm3. The blanket no-warm-regression gate therefore does not pass. Two first-return samples/configuration are not a p95 estimate; these are measurements on this VM.

The coordinated window has no unrelated browser/encoder/compiler in retained boundary receipts. Conductor git housekeeping appears during setup; exact exit times were not captured. Boundary observations are not continuous system-idle proof. The earlier time-limit failure and externally contaminated comparison remain preserved, with no excluded samples or automatic retries. [All values, source, process receipts and limitations](../docs/traces/jit-stream-cache/return-visit-v3/README.md).

## Why it matters

This removes a measurable part of returning Settings latency without modifying Apple's binaries or the generated CPU instructions. It does not improve a first-ever visit, prove every app faster, or meet the full-speed goal.

The narrower Settings result justified the real offline-cache and current UI autosave tests, now passed in milestone 084. A product decision must retain the Home tail tradeoff and complete the combined runtime upgrade and fallback gates before enabling the loader in production.
