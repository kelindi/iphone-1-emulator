# 084 — The offline phone remembers compiled code

## Tweet idea

The web iPhone now passes an experimental offline return test: Chrome reuses verified compiled emulator code, restores the exact autosaved phone, and opens the real Settings app with the server stopped. The original 2007 software is unchanged.

## What you're seeing

[Full actual offline-return recording](videos/seed_cache_offline_return.mp4) · [thumbnail](thumbnails/seed_cache_offline_return.jpg), **00:00–00:15.28**. A fresh Chrome process restores Settings, returns Home and opens Settings again. The isolated origin server stays stopped throughout.

[Full setup, navigation and autosave recording](videos/seed_cache_offline_training.mp4) · [thumbnail](thumbnails/seed_cache_offline_training.jpg), **00:00–00:53.56**. This first process explicitly installs verified offline assets before starting the phone, performs two Settings/Home cycles, and waits for the real automatic save. Both recordings are uncut at original speed; [source hashes and duration proof](../docs/traces/jit-stream-cache/sw-cache-offline/recordings.json) are retained.

**The loader is now integrated in the default local build. These original recordings remain a mechanism test with deliberate cache preparation, not a first-visit or speed claim.**

## ELI5 technical explanation

The browser saves two different things: the phone's state and compiled instructions that help run it. We checked both. After closing every Chrome process, the next process restored the exact saved metadata and compressed bytes. Chrome also accepted **20,677,964 bytes** of native-code cache belonging to the exact verified CPU seed. The real seed and interpreter responses came from the service worker's offline cache.

Offline testing needed more than an “offline” switch. Chrome reported a stale online indicator, and its background service-worker updater could still contact our server even when ordinary requests failed. We preserved those failures, then physically stopped the isolated server and closed its connections. New TCP connections were refused; page, emulator-worker and service-worker requests failed; the server received no further requests. The remote firmware assets had already been hash-checked and cached during setup.

The completed pair passes **71 functional checks and ten full guest screen comparisons**, with all ten screens manually reviewed and all 25 scoped Chrome process identities closed. A genuine ordinary autosave survives the return exactly. No hidden control is used to simulate saving, and the current phone-only UI is preserved. [Evidence, source and earlier failures](../docs/traces/jit-stream-cache/sw-cache-offline/README.md).

## Why it matters

The compiled-code reuse measured in [milestone 080](080_faster_returning_settings.md) can also work through the real offline cache while preserving the saved phone. That separate speed comparison improved returning Settings by about 1.5 seconds, but retained a **49.8 ms worse pooled Home p95**; this functional test does not erase that tradeoff.

The combined actual old-runtime upgrade and fallback gate now passes30 checks. A separate unmarked default-release smoke passes21 checks with exact source hashes, real autosave, positive generated execution and disconnected restore. [Full final-release recording](videos/rtc_final_release.mp4) · [thumbnail](thumbnails/rtc_final_release.jpg) · [evidence and explicit offline-install scope](../docs/traces/rtc-release-upgrade/final-release/README.md). The release is integrated locally; full speed and complete app functionality remain active work.
