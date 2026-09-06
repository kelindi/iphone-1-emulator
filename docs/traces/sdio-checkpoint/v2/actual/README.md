# Actual private v2 checkpoint and continuation

The one authorized fresh attempt passes every reviewed gate. The original driver reaches the frozen069 handoff at `c0379684`, instruction17,517,070,017, tick256,764,208, after785 seconds within the22B/900s boot limit. Serialization takes1 second within120s. No legacy longjmp or pre-step fake yield creates this capture: the final stop is observed outside a normally returned execution call. [Normal boundary](full-checkpoint.json), [fixed budget](full-budget.json), [attempt context](attempt.json).

Both original-file audits reproduce the entire upload:128,704 wire bytes,117,932 copied bytes,852 untouched mapped bytes,64 outside canaries,3,414 command responses and all504 sequential IRQ/status/DMA acknowledgement chains. The final physical command4 header is retained. Command completions are first observed at600 ticks3,413 times and616 ticks once, exactly matching frozen069; no silicon-time or scheduling-cause claim is added. [Combined audit](verification.json), [independent original-MachO audit](root-byte-audit.json), [complete boot trace](full-mmio.log).

## Fresh restore and actual continuation

The191,881,187-byte machine and246,287-byte private sidecar load into a freshly constructed M68. Fresh callbacks are bound before device restoration; full machine and sidecar bytes immediately re-save exactly. The original machine remains alive with its original warm caches, rather than being normalized or itself restored through the loader.

Both phones then execute41,947,204 further instructions to the actual driver failure boundary `c038a8ec`, r4=1000. Every instruction's resulting PC, instruction/timer count, modeled SDIO IRQ and routed VIC bit match. Every one of4,581,748 SDIO transactions matches in original order, including time, PC, address, value, access width and acceptance. An independent binary parser verifies exactly1,000 actual CMD52 commands with argument0x10004000 and response0x1008: the original driver really consumes the still-unready status8. [Continuation result](full-continuation-comparison.json), [independent transaction audit](continuation-independent-audit.json), [separate root chunked audit](root-review.json), [original-driver continuation log](full-continuation-mmio.log).

Complete serialized architectural/device state is compared at five prefixes: initial, then1,1024,32768 and41,947,204 instructions. After initial equality, only three known CPU translation-cache diagnostic counters and their independently verified payload checksum may differ. Standard load intentionally clears derived caches; the comparator binds the serialized counters to each live CPU and leaves all other bytes exact. The actual final differences, restored minus original, are hits−280, misses+319, flushes+1. [All five exact counter rows](full-continuation-cache-counters.jsonl), [reviewed comparator](../compare.c). This is not full-register comparison at every instruction, and no files or original machine are normalized.

The continuation remains in MAIN_HANDOFF with requested0,504 DMA completions and no new SDIO IRQ edges. It is a real polling-path continuation test, not another DMA transfer or a firmware-ready success. In-flight and completed-unacknowledged state coverage comes separately from the retained synthetic contracts.

## Published private package

Only after both wire/IRQ audits, independent continuation parsing and fresh C canonical validation does the publisher write the manifest last. It then loads the actual generation through the same immutable-file/hash/identity/C validation path and verifies exact pair bytes. [Publication and reload result](publication-report.json), [canonical validations](canonical-validation.log), [exact private manifest metadata](private-package-manifest.json). The capture metadata's `published:false` is its original pre-publication fact; the later publication report records the successful package.

Actual private generation: `.context/sdio-first/checkpoint-v2/full-attempt-1/private-package/`, generation `6ef08b30e66247338f55cb9d6de853c8`.

- Machine191,881,187 bytes, SHA256 `2cf3a7391b2972cea6e152672693b9028b4707765937b8344018d0f5929ea0d5`.
- Sidecar246,287 bytes, SHA256 `9b91cff0fd310e457df7850424260cd6307e39ec08467f4e0c935725953507d8`.
- Each original/restored transaction stream146,615,936 bytes, SHA256 `13881df363adc1770b768a2841874ab289f7377c5d58eb337e52569b2735eae9`.

The full machine blob stays private. Both immutable raw transaction files remain in the private attempt. Their byte-identical trace has one [lossless gzip copy](continuation-transactions.bin.gz) here, independently decoded and hashed back to the original146,615,936 bytes. The binary format is repeating32-byte little-endian `<QQIIII>` records: cycles,ticks,PC,address,value,flags; flags bit0write, bits1..7width, bit8accepted. [All artifact hashes and gzip roundtrip](artifact-manifest.json).

## Scope and retained failures

Production snapshot format, SDIO/machine code, browser assets and user profiles are unchanged. Original kernel, DT and work-disk inputs retain their hashes. The private loaded-DT copy uses the previously approved absent-calibration branch and emulated local MAC; those byte changes remain audited, not physical identity or RF calibration claims. The received helper selects a documented semantic protocol model; neither helper nor main firmware CPU is executed. No FEDC marker, network interface or connectivity is supplied.

The earlier v1 full attempt still fails and stays unpublished: it lacked real Z1 receive/uptime state. [Preserved diagnosis](../../attempt-1/canonical-failure.json). Version2 captures the actual original state in a new boot, rather than guessing missing bytes from that pair. The3,987 focused Z1 checks,36,578 lifecycle checks,65 real-C package checks and unchanged1,012,343 frozen SDIO contracts remain separate preflight evidence. Dirty-contact-padding and deliberately omitted receive/uptime negatives remain retained. [Implementation and tests](../README.md).

This is a nonvisual development-checkpoint milestone. The run included separately authorized short functional work and makes no speed comparison. The checkpoint can now support bounded next-protocol investigations without repeating the17.5-billion-instruction upload; a supported runtime/readiness model still needs its own source-grounded review and acceptance gates.
