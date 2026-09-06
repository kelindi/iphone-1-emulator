# Preserved request-observer stop — no first-command acceptance

This is a documentation update for an already executed isolated experiment. **The disabled control passed; the one positive request-observation gate failed and was not retried.** Milestone077's earlier accepted scope is unchanged. There is no milestone080, actual runtime request acceptance, new card behavior, firmware CPU execution or network-interface claim.

## What was actually retained

The [control report](control-1/report.json) and [independent audit](control-1/audit.json) reproduce41,947,204 instructions and4,581,748 SDIO events, with exact073 raw trace SHA256 `13881df363adc1770b768a2841874ab289f7377c5d58eb337e52569b2735eae9`. Its146,615,936 raw bytes remain private, with the already retained [identical durable stream](../../sdio-checkpoint/v2/actual/continuation-transactions.bin.gz) reused through the [reference record](control-1/transaction-reference.json).

The [failed positive report](positive-1/report.json), [request metadata](positive-1/request.json), [witness log](positive-1/milestones.log), [process result](positive-1/process.json) and [launcher failure](positive-launch.log) are preserved unchanged. Its complete [raw trace](positive-1/transactions.bin) has41,261 events /1,320,352 bytes, SHA256 `42c5f1e9191759e122fa1f84d20559f06b14756d2227903e8017e9df920e8d59`.

The positive observer recorded the same original077 boundary: ARM `c0380208`,235,719 added instructions and22,919 events. It then observed:

- Register6's actual R5 `0x1100` at cycle17,517,347,210: an explicit card-address error.
- The original byte-read wrapper returning **r0=1** at `c0380220`, cycle17,517,347,673. Its caller unconditionally writes1 next; no successful register6 device implementation was added.
- The original host block-size setter committing320 at `c037a1c8`, object `cbabe340`, cycle17,519,479,702.
- A different first frame than predicted, causing the observer to stop at **ARM SVC `c0380804`**, CPSR `60000113`, **r0=`cbc893c0`, r4=20**, cycle17,519,480,204 and tick256,799,308. The previous completed instruction was `c038c15c`, the original command-length setter return.

The positive used2,410,187 added instructions and hit neither its100M instruction cap nor its90-second execution cap. The native report retains504 data completions, unchanged uploaded image, empty runtime slots and **zero runtime CMD53 requests**. Its `normalOutsideRunStop` and `passed` fields remain false. The launcher therefore did not run its complete positive auditor: matching prefix witnesses are recorded, but no independent complete-positive acceptance or independently audited prefix-byte comparison is claimed here.

## The missing bytes are genuinely missing

The failed observer checked its expected later setup/builder before copying this first frame. Consequently it did **not** capture the actual frame object, r5/r6 or the320-byte packet. The zeroed request arrays in the private output are unfilled harness storage, not observed guest bytes. They are not published as a packet. The original live machine was not saved at this failed boundary, and its missing buffer cannot be recovered from these reports or filled from an assumed firmware command.

The unfinished boundary is therefore **first-frame classification/capture, before any first runtime CMD53 trigger**, not a completed request, command response, DMA transfer or driver response consumption.

## Source-only inference, not a recovered packet

Already generated [original power-on code](source-notes/original-power-on-loader-dis.txt) explains why the predicted later0093/0043 sequence was premature. Initial `powerOnSequence` loads the firmware function pointer through a literal and calls it with **BLX `c03826b0`**, which a direct-BL-only caller scan missed. After return, it calls virtual slot0x4dc with a MAC pointer and length6.

The [original `setHardwareAddress` code](source-notes/original-set-hardware-address-dis.txt) builds command **0x004d**, a16-byte command payload containing action1 and six actual caller MAC bytes, then enqueues through the [common frame builder](source-notes/original-enqueue-header-dis.txt). That would produce20 logical transport bytes, consistent with the observed r4. A static pointer occurrence also matches this virtual slot. **The live vtable, opcode, sequence, MAC and packet bytes were not captured**, so this remains source-grounded inference.

A corrected observer should retain actual bounded registers/object/virtual and physical RAM views before classifying the first frame, then stop on any unknown path or command. Private `.context/sdio-first/runtime-write-v2/` contains incomplete source preparation only: it is **unbuilt, unvalidated and not approved for execution**. No new emulator run, reverse-engineering command, build or network operation was performed during this documentation-only update.

## Files and remaining scope

The [artifact manifest](artifact-manifest.json) inventories copied existing evidence. [Frozen pins](source-pins.json) identify the executed probe (`4a1b2c5e…2407fb30`) and its inputs; their manifest SHA is `7c32565acb1bffa4ee1621131e9c70da0389a2e1ac19fb7fa08cb276132a9ce5`. The [preflight report](preflight-report.json), [canonical-load record](actual-canonical.json), observer, RAM reader and independent parser are retained alongside the failed result. Those synthetic/canonical preflights do not turn the failed actual gate into a pass.

Original firmware payload dumps, native binaries, both full private attempt directories and unfinished successor sources remain under `.context/sdio-first/`; they were neither deleted nor reconstructed. The original073 package, frozen077 result and production remain unchanged by this experiment. The next functional step remains a reviewed capture of the actual first command, followed separately by grounded request/response transport and original-driver consumption. No further guest work is authorized by this documentation.
