# Complete main-container receive/storage contract

Source-first extension of066, 2026-09-05. Work remains isolated; the original helper is represented by its explicitly selected semantic model, not by running a Marvell CPU. The next gate ends at the received command4 handoff request. No downloaded main code executes and no firmware-ready marker or interface is produced.

## Original destinations and protected state

The unchanged kernel's main container is at VA `c0392d1c`,122608 bytes, SHA256 `06a5d9d1f5cc4c5209cd072bb2434058be3d52b8bebb2b022153c325f3821150`. `../helper-protocol/main-records.json` contains233 command1 records:231 lengths512, one504 at container offset24816/destinationc0006d44, and one88 at offset122488/destination00016d20. The final16-byte command4 header is at offset122592 and is `[4,0,0,0x188cdb1f]`.

The actual233 copies produce117932 bytes in two disjoint contiguous intervals:

| Actual writes | Bytes | Proposed bounded mapping | Untouched mapped guard |
| --- | --- | --- | --- |
| `[c0001000,c0006f38)` |24376 | `[c0001000,c0007000)` |200 bytes at the end |
| `[00000000,00016d74)` |93556 | `[00000000,00017000)` |652 bytes at the end |

The model checks the entire destination interval and unsigned subtraction before any copy. Anything outside these two declared mappings stops with an explicit model-admission diagnostic after preserving received physical bytes. A copy cannot cross a bank boundary or wrap an address. Map buffers include additional outside canaries for tests. They are bounded emulated memory, not host pointers or a claim that the entire physical Marvell RAM map is known.

The high mapping excludes the relocated helper text atc0000000..c000063b, callback tablesc0000800..c0000877, globalsc0000878..c00008e7, and RX stagingc0000a00..c0000bff. The entire `[c0000000,c0001000)` interval remains unmapped for main writes. The original startup's stack04002000 is also outside the mappings. Low-memory download does overwrite address0000fffc, the startup mode-word location; the reached receiver loaded its mode once before entering the loop and retains dispatch index `r4`. That earlier absolute word is not a relocated-helper code/table dependency reread per packet. Protecting it from a real container write would contradict the original byte stream. This is static control-flow evidence, not observed second-CPU execution.

The retained2140-byte helper receive buffer is the received-program record used for compatibility selection, not an executing physical address-zero alias. The main low bank legitimately replaces address-zero contents; retaining the original receipt does not claim those bytes still occupy address0. This distinction is explicit in the high-level model.

## Logical bytes, wire bytes and padding

Every header request is16 logical bytes but32 physical bytes. Body length comes from actual header word2; the physical transfer is rounded to the next multiple of32, as original host `c038a79c` and `c038a8c8` show. The mode2 callback copies logical length minus4, regardless of physical padding. The only newly necessary sizes are:

- 504 logical →512 physical →500 copied; physical tail8 bytes retain the preceding buffer contents.
- 88 logical →96 physical →84 copied; physical tail8 bytes likewise retain prior contents.

All 231 other bodies are512 physical/logical and508 copied. There are234 header transfers (including command4) and233 body transfers, **467 main transfers**, totaling **126368 physical main bytes**. Adding064's2336 helper bytes gives **128704 complete wire bytes** and **504 actual DMA completions**.

The original host reuses one source buffer. For each logical request it overwrites only that many bytes before sending the rounded physical length. Header padding therefore commonly contains bytes16..31 of the preceding body; it is not generally zero. Before the first main header, only the first32 buffer bytes are known zero from the helper terminator. An independent source oracle tracks known bytes and refuses to predict any transmitted unknown byte. The first512-byte body overwrites the entire future observable buffer, making all subsequent padding exactly derivable. The oracle must compare every original request buffer and complete mapped DMA source, not just deframed logical data.

The main phase uses no helper length-prefix framing. Header word3 and each body's final four logical bytes remain ignored by the reached software copy callback; no CRC algorithm or hardware checksum behavior is invented.

## Extension and lifecycle invariants

Keep066 mode unchanged behind an explicit `complete_main` experimental configuration. The full mode permits block32 fixed-address function1 writes of any1..16 blocks only when the rounded physical length equals the current requested length, the original descriptor/card/block/source constraints hold, and bounded staging is empty. Generalization derives from the original host's rounding and the same receive callback; it is not an exception for96 or512.

Full-mode header command1 accepts logical lengths4..512 and a complete destination interval in one declared bank. That is the model's explicit admission limit; the original helper itself has no such software bounds check. Repeated records and arbitrary bodies drive actual copies. The device must not know the expected233 records or inject source-oracle data. External coverage/byte verification decides whether the original full image arrived.

Odd logical lengths are codec-level tests only: the original Apple caller treats the requested low bit as an error and would abort. The actual original container uses even lengths, so arbitrary codec acceptance is not claimed as host support for every length.

Retain separate physical-transfer and card-semantic deadlines. `sdio_endpoint_receive` transfers completed bytes into independent pending storage exactly once, including public direct API calls. Host/FIFO reset and reader rebind cannot discard those bytes. Pending processing quiesces/reanchors across function/power/time discontinuities, while uncommitted DMA staging cancels. True card reset clears downloaded helper, main memory and coverage. Committed writes happen once. Preserve completed controller latches until original W1C or an explicitly defined controller/FIFO reset.

Requested low/high bytes remain independent CMD52 completion samples. Publish the next request only after parsing a complete header or committing a complete body, with receive availability derived from state. Full acceptance must trace all actual host request pairs and compare them with original container order. The same deadline-boundary tests remain: mixed/stale values cannot bypass current-request, full-source, capacity or destination checks. No atomic latch, read-count advance or ready shortcut is added.

## Command4 and genuine stop

When an actual32-byte physical header contains command4, retain all16 header bytes and physical padding, publish requested0 as the original helper does, and record a distinct `MAIN_HANDOFF` state with its actual entry address. Do not report an error merely because this is the intended supported stopping boundary. Also do not call it full download based on the command alone: an early/arbitrary command4 must record the request with incomplete coverage, and the external gate must reject a complete-image claim.

The original helper next rearms and branches to header word1 (final original entry0). That branch/main execution is unsupported. The model must not invent data-ready status after handoff just to let Apple's loader return true. Stop on the host instruction hook **c0379684** after the final command's physical transfer has received the original IRQ/status acknowledgements and `completeDMA` has returned1. If semantic parsing is not yet complete at that hook, continue only until a subsequent safe host instruction boundary after parsing, retaining the already verified final completion; do not stop mid-MMIO read. Current066 timing has parsing complete long before the IRQ handler, so the earlier hook should suffice.

Required external acceptance:37 helper +467 main transfers; all504 responses/data completions/IRQ acknowledgements/return values; exact128704 wire bytes; original122608 logical container; all117932 written RAM bytes and their coverage,852 mapped guard bytes untouched; exact command4 header/entry0; and no firmware marker/CPU execution. Preserve the066 first508-byte stop as a mode in the new probe and rerun its small contract suite plus the no-model original failure.

## Bounded execution and tests

`budget-evidence.json` preserves actual066 response-boundary instruction samples. Its first two main transfers cost35954685 and35931067 instructions (about523k guest timer ticks each). Extrapolating467 transfers gives about16.79 billion instructions after main-start1.670 billion, or **about18.46 billion total**. Set a fixed **22-billion-instruction limit and900-second wall limit**. The expected native job is roughly8–10 minutes at the previous probe throughput; this is scheduling, not a performance claim. No unlimited retry. Preserve partial memory/wire/progress on budget failure, diagnose it, and coordinate any rerun.

Before a full guest run, retain all549969066 checks and add: both banks/range edges, guard/copy coverage, all logical lengths4..512 with rounded forms, arbitrary multi-record data/overwrites,504/88 tails, unknown command17, early/late command4, unavailable mapping, full512 physical source failures, physical/semantic deadline cancellation at both phases, completed pending bytes across reset/rebind, card reset, and low/high boundary reads. Independently predict the full original wire and memory from Mach-O segments. Send this contract and actual contract results to root before starting the bounded full boot; coordinate with the Save-isolation CPU diagnostic.
