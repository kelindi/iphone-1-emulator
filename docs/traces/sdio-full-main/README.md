# Complete main-container receive evidence

> Historical recordings and publishing artifacts have been removed from Git history. The summaries below describe the original runs; remaining test reports and screenshots are retained. Historical hash receipts may predate this archival cleanup.

Milestone 069 describes the actual completed transfer and its limits. The original helper is represented by a supported semantic model; neither helper nor main CPU code executes.

- [Full trace](full-mmio.log), [report](full-report.json), [model state](full-main-model.json), [data state](full-data.json), and [fixed budget](full-budget.json):504 original DMA completions,234 headers/233 bodies, final command4 at committed completeDMA return0xc0379684. The host's outer main-loader return is not claimed.
- [Actual source wire](full-source-wire.bin) and [request wire](full-request-wire.bin):128704 bytes, SHA256 `445167098fb17ac5376f1910578459f7a6bc5ac4ed29fcd09cf9ac8c69eac727`.
- [Low bank](full-low.bin)/[coverage](full-low-mask.bin) and [high bank](full-high.bin)/[coverage](full-high-mask.bin):117932 actual writes,852 mapped guards and64 outside canaries verified. [Final handoff frame](full-handoff-frame.bin) retains original header and padding.
- [Combined verification](verification.json), [independent root audit](root-byte-audit.json) and [its source](root-byte-audit.py). Root independently extracts original Mach-O segments and checks complete wire, banks/masks/canaries and504 sequential IRQ→status→DMA-return chains.
- [Contracts](contracts.log):1012343 pass, including all549969 retained checks. [Controls](controls-verification.json), [first-payload trace](positive-mmio.log) and [no-model trace](main-negative-mmio.log) preserve exact earlier instruction stops.
- [Source oracle](source-oracle.json)/[code](source_oracle.py) and [separate static oracle](root-independent-oracle.json)/[code](root-independent-oracle.py) were prepared before the actual run. The [reviewed contract](protocol-contract.md) preserves the pre-run proposal and its original workspace-relative references.
- [Delayed observation](completion-observation-review.json), [initial verifier source](attempt-observation-assumption/verify_full.py), [inherited parser](attempt-observation-assumption/verify_controls.py), and [failure record](attempt-observation-assumption/failure.txt):one first-observed completion at616 ticks,3413 at600. No early completion or ordering exception is accepted; exact cause of the additional16 observed ticks is unproven. Device source did not change for this verifier correction.

The fixed22B-instruction/900-wall-second run completed at17,517,070,017 instructions/780 seconds, with concurrent untimed browser functional checks. These durations are execution-budget evidence, not a throughput benchmark.

The two actual written intervals are `[0,0x16d74)` and `[0xc0001000,0xc0006f38)`. Page-rounded banks extend to0x17000 and0xc0007000, preserving652 and200 tail bytes. Relocated-helper code/tables/RX remain outside the admitted destinations. The received2140-byte helper record is compatibility/provenance storage, not an executing address-zero alias after main writes.

The isolated source is preserved in [source/](source/). It never reads the expected main-image fixtures. Actual headers choose bounded destinations and lengths, and actual bodies drive copies/coverage. Early command4 records a handoff without satisfying external full-image acceptance. Firmware-ready status remains zero; reception is unavailable after handoff. No production machine or snapshot format is changed.

To reproduce, restore `source/` into `.context/sdio-first/main-full/` and the oracle scripts into `.context/sdio-first/main-full-plan/`. Original kernel, work disk and native archive hashes are recorded in verification.json. Tests requiring fixtures use the independently extracted original bytes; those fixtures never populate device memory directly. Run from repository root:

```sh
python3 .context/sdio-first/main-full-plan/source_oracle.py
python3 .context/sdio-first/main-full-plan/root-independent-oracle.py
cc -std=c11 -O2 -Wall -Wextra -Werror .context/sdio-first/main-full/test_full.c .context/sdio-first/main-full/sdio.c -o .context/sdio-first/main-full/test_full
.context/sdio-first/main-full/test_full
cc -std=c11 -O2 -Icore/include .context/sdio-first/main-full/probe.c .context/sdio-first/main-full/sdio.c .context/build-native/core/libemucore.a -lm -o .context/sdio-first/main-full/probe
.context/sdio-first/main-full/probe --main-first
.context/sdio-first/main-full/probe --main-negative
.context/sdio-first/main-full/probe --full-main
```

The verifier also needs the original earlier helper-wire fixture and the saved input/source hash lists, as identified in its retained source. Its paths intentionally preserve the original workspace investigation layout. The full probe is always bounded; preserve failed or partial outputs before any deliberate rerun.
