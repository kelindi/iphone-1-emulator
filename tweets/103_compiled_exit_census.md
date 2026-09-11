# We measured the compiler's missing coverage

## Tweet idea

The original iPhone's browser emulator has an accelerator that already understands many instructions it still interprets. Two warm Settings runs show74% of rejected compiled entries hit opcodes the compiler supports. The fixed seed simply doesn't cover those positions. That's a concrete next target—not a claimed74% speedup.

## What you're seeing

[Full recording](videos/compiled_exit_census.mp4) · [Thumbnail](thumbnails/compiled_exit_census.jpg). The complete instrumented browser session shows startup and three Settings/Home pairs. The second and third pairs supply the warm census. Final Settings and Home screens are reviewed; six actions complete. This recording has diagnostic overhead and is not a speed comparison.

## ELI5 technical explanation

Think of the compiler as a translator with a prepared phrasebook. Knowing how to translate a phrase doesn't mean that phrase is in the prepared book. Our fixed seed contains512 code regions and selected positions, so it repeatedly falls back on instructions the translator already understands.

We counted the exact reasons for stopping, rather than guessing from the slowdown. Of compiled exits in two warm Settings windows,40.1% reach an unprepared position,38.6% cross a code-region boundary,18.2% hit memory checks and3.1% exhaust the supplied budget. Separately,74.3% of rejected entry attempts have an observed opcode supported by the emitter. These counts use different denominators and do not measure time saved.

Instrumentation preserves all512 original execution bodies after removing observations. Seven observer checks pass; complete fixed-work phone states and compiled counts match. Browser raw rows reconcile with all aggregate counts and no table overflows. The census includes the screen-confirmation interval and does not count scheduler paths that skip JIT entirely.

## Why it matters

We have a specific next experiment: fill the hottest supported missing positions within the existing region budget, then test the uninstrumented result against production. This narrows the work without promising a faster phone before measuring it. The default engine remains unchanged. [Evidence, limitations and next test](../docs/traces/compiled-exit-census/README.md).
