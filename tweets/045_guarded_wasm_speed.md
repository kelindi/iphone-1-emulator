# Faster repeated app launches with guarded WebAssembly

## Tweet idea

The real 2007 iPhone OS now opens Settings 29–34% faster on repeat visits in our browser test. Home returns improve 16–30%. We translate hot ARM code into guarded WebAssembly; 600 million instructions still produce exactly the interpreter's saved phone state.

## What you're seeing

[Original-speed recording](videos/guarded_wasm_speed.mp4) · [thumbnail](thumbnails/guarded_wasm_speed.jpg).

The complete 39.52-second recording shows the maintained accelerator loading the genuine phone, then four Settings → Home rounds. The first Settings view is visible around 00:11, and the later section shows the repeat visits through the final Home return. The thumbnail is the real Settings frame at 00:11. Playback has not been sped up, and the loading and transition frames remain visible.

This is the accelerator arm of a quiet paired Chrome run. The separate control rebuild uses the same current source, phone snapshot, firmware, and release settings, with the optional JIT hook and runtime entirely excluded. The accelerator ran first, reversing the earlier comparison's ordering. Both four-round runs passed.

| Round | Settings: interpreter → accelerator | Home: interpreter → accelerator |
| --- | --- | --- |
| First | 4.40 → 4.60 seconds | 1.60 → 1.20 seconds |
| Repeat 1 | 4.90 → 3.25 seconds | 1.25 → 1.05 seconds |
| Repeat 2 | 4.80 → 3.40 seconds | 1.51 → 1.05 seconds |
| Repeat 3 | 4.90 → 3.35 seconds | 1.50 → 1.15 seconds |

Repeated Settings launches are 29–34% faster and Home returns are 16–30% faster in this pair. **First Settings is 0.20 seconds slower**, at 4.60 versus 4.40 seconds. Readiness is sampled from genuine canvas pixels every 50 ms and must remain stable for 500 ms; the reported time starts that stable interval. Four rounds on this VM are a bounded comparison, not the product's p95 acceptance result or a full-speed claim.

The original reports, screenshots, metrics, and both recordings are in `.context/jit-maintained/chrome-{seed,control}/`. [The experiment log](../plans/speed-experiments.md) retains the earlier measurements and rejected approaches.

## ELI5 technical explanation

The interpreter normally reads one ARM instruction, works out what it means, and performs it. The phone repeats many of the same little stretches of code. We translate 512 observed hot regions into WebAssembly ahead of time, so the browser can execute those stretches with less repeated decoding and register handling.

Skipping the decoding must not skip the phone's rules. Generated code checks that the original instruction bytes still match, that memory mappings and permissions are valid, and that it stays within the next timer or interrupt boundary. Unsupported instructions and changed code return to the interpreter. The generated code handled roughly 74–80% of instructions during the sampled browser actions; three stale regions retired safely, leaving 509.

Earlier live-compilation experiments spent too much time discovering and compiling code. This version loads a bounded, reproducible seed alongside normal startup. It does not synchronously warm every function or make the phone wait for optional compilation. Missing assets, incompatible runtime layouts, failed allocation, or bad metadata leave the interpreter available. Restore discards old derived code before changing phone memory; ordinary Save preserves valid code. Apple's binaries and the checkpoint format remain unchanged.

The maintained runtime passed a 600-million-instruction Settings/Home replay with identical complete interpreter snapshots at all six checkpoints. It executed 82.77% of those instructions through generated code. Twenty-five focused controller/worker lifecycle checks pass, including restore generations, partial-install cleanup, and initialization order. Real Chrome also reaches the ready phone when acceleration is explicitly disabled or the seed download returns 404. The reusable `npm run test:jit-replay` checks both full state identity and positive generated execution; CI runs its 100-million-instruction variant.

## Why it matters

This moves a measured CPU improvement into the default browser build while preserving the real operating system, input, clocks, and saved phone. Repeat visits are noticeably quicker in the verified run. First-launch latency, broader app workflows, and the full-speed product goal still need work.

Final production compatibility checks also pass: 24 persistence checks, seven offline/cache recovery checks, 18 offline upgrade checks, six physical-control checks, and 12 clock/lifecycle checks. These runs require positive generated execution where relevant, so an accidental interpreter fallback cannot stand in for the accelerated path. The offline upgrade retains the identical compressed checkpoint hash and all 153,600 native Settings pixels. Browser hidden/frozen transitions and Export/Import preserve the expected clock and snapshot state. These are separate functional runs, not additional speed samples.
