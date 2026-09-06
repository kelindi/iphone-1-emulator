# Private SDIO checkpoint: verified v2 reload and retained v1 failure

**Version2 now passes the reusable private checkpoint gate.** A fresh785-second cold boot reproduces the exact069 upload, captures complete Z1 state, and immediately reloads byte-for-byte. The original warm machine and fresh reload then match through41,947,204 instructions and4,581,748 SDIO transactions, ending at the genuine1,000-poll still-unready failure. Both original-file audits and manifest-last package reload pass. [Actual result, exact limits and artifacts](v2/actual/README.md). This remains isolated development infrastructure; production and networking readiness are unchanged.

## Preserved version1 result

The original v1 experiment below is a retained failed acceptance gate. Production machine/snapshot/SDIO code and user profiles are unchanged. Frozen milestone069 remains intact. A private v1 sidecar captures every106 non-pointer SDIO leaves alongside an unchanged v39 machine snapshot, using manifest-last publication, fresh M68 reconstruction and explicit source/input pins. The original v1 gate pair remains unpublished in `.context/sdio-first/checkpoint/full-attempt-1/`; its191,881,187-byte machine blob and244,765-byte sidecar are not committed here. Exact hashes and lost-state diagnosis are in [canonical-failure.json](attempt-1/canonical-failure.json).

## Actual result

One genuine cold boot reached the original handoff stop **outside a normal run return** at PC`c0379684`, instruction17,517,070,017 and tick256,764,208. This exactly matches069's original gate instant. It completed in766seconds under the unchanged22-billion-instruction/900-second bound, then serialized in2seconds under a separate120-second limit. This was a bounded correctness run, not a speed comparison. [Gate report](attempt-1/full-report.json), [normal-exit metadata](attempt-1/full-checkpoint.json), [budget](attempt-1/full-budget.json), [complete trace](attempt-1/full-mmio.log).

Both retained independent original-MachO audits pass unchanged:128,704 actual wire bytes,117,932 actual copied bytes, full two-bank/write-mask identity,852 untouched mapped bytes,64 outer canaries,3,414 command responses and all504 sequential consumed IRQ/status/DMA acknowledgements. The original observation histogram also matches exactly (3,413 at600ticks and one at616). [Combined audit](attempt-1/verification.json), [separate root audit](attempt-1/root-byte-audit.json). Earlier no-model controls remain the frozen069 evidence, explicitly referenced by the adapter; they were not rerun as part of this full attempt.

The original in-memory machine was retained through serialization for a planned one-instruction-at-a-time comparison against a fresh reload. **Initial canonical equality failed before any continuation instruction ran.** The process ended without publication. [Comparison](attempt-1/full-continuation-comparison.json). No1,000-poll failure or reusable continuation was established, and no firmware-ready/runtime/networking behavior was added.

## Exact failure, not a comparison exception

Fresh load/re-save changes one payload byte: zero-based offset16,818,035, `mtz1.rx_len`, from8 to0. The other changed bytes are its checksum. SDIO sidecar bytes, file lengths and initial TLB statistics are exact. An isolated annotated serializer identifies the field directly. [Diagnostic log](attempt-1/debug-layout.log), [field/state report](attempt-1/canonical-failure.json).

The actual saved Z1 is present, powered and in firmware mode, with a completed deselected transaction: selected0, pkt_len0, rx_len8; raw_stream, aspeed_stream, transaction_overflow, stage_b and atn are0. Current `snap_mtz1` writes packet lengths but clears them, raw_stream and overflow on load; it does not save the1024-byte receive buffer. `s5l_mtz1_select_pin` examines retained rx[0] on the next select for its C2-prefix exception, so the missing original receive bytes cannot simply be declared irrelevant. The same standard visitor also omits uptime_ms/uptime_tick_acc, which later affect touch-frame timestamps and velocity. Those absent original bytes/values were not captured and cannot be claimed recovered from this pair.

No machine/file normalization, extra comparison exclusion or repaired publication is applied. The next reviewed design should preserve complete non-host Z1 state in an explicitly versioned private extension, test actual byte/selection/timestamp continuation, and account for the missing original data before any further full boot. Standard snapshot integration is a separate decision.

## Preflight and retained negative evidence

- 36,578 synthetic checks pass with ASan/UBSan on the private source; the frozen linked core archive itself is not sanitizer-built.65 package checks use the actual fresh C loader. All1,012,343 frozen069 contracts pass. [Preflight](preflight-report.json), [native log](warm-sanitizer-test.log), [package log](package.log), [retained contracts](retained-contracts.log).
- Ten synthetic M68 in-flight/unacknowledged states retain every SDIO field and canonical pair bytes through bounded arithmetic instruction prefixes and acknowledgements/reset. A real N82 snapshot with matching RAM geometry is rejected without changing the M68 source.
- A warmed-MMU synthetic loop and real SDIO read reproduce raw continued-statistic differences. The reviewed comparator preserves untouched snapshots and permits only three live-bound CPU cache-stat fields plus independently verified checksums to differ after execution. All other machine/sidecar bytes remain exact. Rehashed CPU, all three counter windows, device, RAM and section mutations fail. **This cache exception does not include the Z1 failure.**
- A deliberately late DMA callback rebind produces69 failures, including destroyed active DMA/pending deadlines; [negative source/log](negative-rebind/). A premature attempt to execute a binary while it was still linking failed with Permission denied; [retained harness failure](premature-exec-failure.log), unrelated to guest behavior.

`source-pins.json` binds the actual compiled sources/binaries/archive and original kernel/DT/disk. The approved loaded-DT calibration/MAC policy is audited in the actual wire verifier; original files remain unchanged. Private codec/wrapper/normal-boundary probe/strict comparator/package sources and all audit adapters are retained alongside this README. Paths in reproduction scripts intentionally refer to the private workspace experiment, not a production Import facility. No screen recording is applicable to this nonvisual failed acceptance gate.


## Reviewed source follow-up: complete private Z1 state

A separate version2 implementation now explicitly carries118 non-host Z1 leaves/1490 bytes and requires complete raw machine/sidecar canonical binding. Its3,987 focused sanitizer checks,36,578 broader lifecycle checks and65 manifest checks pass, including actual SPI/select/C2/timestamp/velocity continuation and deliberate receive/uptime omission negatives. [Source, design and exact scope](v2/README.md), [focused report](v2/focused-report.json). The new genuine capture and continuation now pass in the [separate actual v2 attempt](v2/actual/README.md). That result does not change the failed original attempt1 or recover its missing bytes.
