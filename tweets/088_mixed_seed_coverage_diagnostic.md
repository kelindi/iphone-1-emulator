# 088 — More generated code, modest gains and real tradeoffs

## Tweet idea

We taught the iPhone emulator more of the code its real Settings app actually uses. Warm launches improved a little, but cold launch and Home results were mixed. Every checked screen and CPU checkpoint matched. The bigger engine stays experimental: correctness alone is not a reason to ship it.

## What you're seeing

[Full original-speed candidate recording](videos/mixed_seed_coverage_diagnostic.mp4) · [Thumbnail](thumbnails/mixed_seed_coverage_diagnostic.jpg)

The complete 117.28-second third session shows the genuine OS loading, a controlled paused backup import, and eleven Settings/Home rounds. The thumbnail at 00:46 shows actual Settings. No cuts, speed changes or reconstructed phone screens. This uses the frozen comparison UI from before milestone 085; the current production UI is preserved.

Across 20 warm samples per action/configuration, Settings median is **3.167→3.067s** and p95 **3.467→3.261s**. The forward/reverse median gains are **6.86%  / 2.35%**. First Settings is mixed: **6.364→5.506s** forward, **5.201→5.588s** reverse. Home median stays about **0.867s**; pooled p95 improves **0.966→0.917s**, but the reverse pair worsens **0.867→0.967s**. These are fresh-profile cloud-VM measurements with compilation tracing, not the cached return-visit result in milestone 080.

The original series stopped at its disk floor before the fourth browser launched. We preserved that failure and all three completed observations, cleared only their disposable HTTP/code caches, and ran the previously unstarted fourth control separately with the same limits. The cleanup gap is a limitation; no guest was repeated or sample discarded. [Full results, methodology and receipts](../docs/traces/jit-mixed-coverage/README.md).

## ELI5 technical explanation

The browser normally translates each old ARM instruction. Our faster engine already has safely compiled chunks of the original phone's code. A new census showed exactly where the real Settings app still fell back to translation, rather than guessing from how compiled chunks ended.

We added 64 measured pages and 2,048 positions while keeping every old region and safety check. The seed grew 12.34→14.36MB. Its unchanged rules still stop for changed code, memory faults, interrupts and device deadlines. After 100 million genuine instructions, all four complete backups and the final screen match the original engine exactly. The candidate executes 5.88 million additional instructions through generated code, but also makes more generated calls and needs more compilation work.

All 88 settled screenshots and eight manually reviewed first screens pass. Compilation stays under its declared bound, but summed foreground/background compilation CPU rises: original **19.284/17.105s**, candidate **20.662/20.626s**. More compiled coverage is therefore not free, and the browser results do not support a broad speed improvement.

## Why it matters

We now have actual fallback evidence and a reproducible correctness-tested expansion, plus a concrete reason to leave it parked. The production engine is unchanged. Two-second navigation and 150ms input remain goals; this experiment does not meet them.
