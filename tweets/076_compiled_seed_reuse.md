# 076 — Chrome remembers the compiled CPU seed

## Tweet idea

The web iPhone still runs the original 2007 software. In an isolated experiment, Chrome now remembers compiled emulator code after the browser closes. We verified actual native-code reuse and matching phone screens. Next: prove a repeatable return-visit benefit before shipping it.

## What you're seeing

[Complete second-session recording](videos/seed_native_cache_return.mp4) · [thumbnail](thumbnails/seed_native_cache_return.jpg), **00:00–01:02.20**. This is a fresh Chrome process using the first session's persistent browser profile. The uncut recording includes the normal phone import, two Settings/Home cycles, and the final paused wait. It runs at the original speed.

[Complete first-session recording](videos/seed_native_cache_first.mp4) · [thumbnail](thumbnails/seed_native_cache_first.jpg), **00:00–01:08.48**, shows the same sequence with the initially empty profile. Neither recording is a promoted speed benchmark. [Source/output hashes and duration proof](../docs/traces/jit-stream-cache/recordings.json).

## ELI5 technical explanation

Our guarded CPU seed translates supported original ARM instructions into WebAssembly. Chrome then translates that WebAssembly into instructions for the host computer. Until now, we gave Chrome a verified byte buffer, which did not use its URL-based persistent native-code cache.

The experimental loader keeps the real downloaded response. It checks a clone's size and fingerprints first, then gives the untouched original response to Chrome's streaming compiler. That preserves the browser's cache metadata while retaining the existing safety checks, fallback interpreter and restore lifecycle. Apple's binaries and the generated CPU instructions are unchanged.

We closed the first browser process tree completely and started a new one with the same profile. The second trace identifies the exact seed URL, checks its digest, and completes native-code deserialization successfully. It consumes 21,104,920 bytes of cache metadata. This is stronger evidence than simply finding the downloaded file in an HTTP cache. All eight phone frames match independent original references within the existing strict tolerance, and every screen was manually reviewed.

The loader/HTTP/lifecycle suite passes 49 checks. Twelve deliberately broken trace examples fail the native-reuse proof, including missing success/digest events and invalid cache data. [Full evidence and independent review](../docs/traces/jit-stream-cache/README.md).

## Why it matters

This offers a concrete way to avoid repeating some browser compilation on return visits. One diagnostic first-Settings observation fell from 5.665 to 3.466 seconds, but warm Settings rose from 2.568 to 2.867 seconds; two sessions do not establish a repeatable speed improvement.

The experiment uses explicit HTTP caching and bypasses the service worker. Production is unchanged. A fair return-visit comparison, realistic validation headers and actual offline-cache behavior remain required. First-ever launch and full-speed app functionality are still open.


Follow-up header gate: [full no-cache/ETag return diagnostic](videos/seed_cache_revalidation_diagnostic.mp4) · [thumbnail](thumbnails/seed_cache_revalidation_diagnostic.jpg), **00:00–01:49.52**; actual Settings at00:36. Both returned candidate sessions accept native-cache deserialization after real304 revalidation, and all88 strict phone stages pass. A separate workspace runs browsers/encoding during the series, so timings are contaminated and no speed acceptance follows. Warm regressions remain recorded. [All orders, retained failures and full evidence](../docs/traces/jit-stream-cache/return-visit-v2/README.md). Production remains unchanged; a new independently trained comparison is being prepared.
