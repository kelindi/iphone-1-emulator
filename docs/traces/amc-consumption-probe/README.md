# Consumption probe stops at the next original input request

One fresh isolated continuation reproduced075's first decoded buffer and its exact byte/counter boundary, then stopped at a second16KiB input request before accepting its transfer-enable write. **No PCM read, output acknowledgement, ownership release or second publication occurred.** This is a preserved earlier protocol boundary, not a consumption milestone or audio playback.

The actual pending input IRQ was delivered normally. The original driver read pending1, acknowledged it, and completed a type0 input notification through the original IPC path. It then staged physical`14ac1000`,4,096 words, through helper`c02780f0`. The guard stopped the final `AMC+100c=1` at PC`c0278130`, LR`c0274874`, cycles9,873,247,421:12,836 retired instructions after075. The store was not committed or retired. The helper/caller differ from the first transfer; no second-buffer media offset can be inferred from the address alone.

The frozen probe did not copy the newly requested bytes into an artifact. Consequently this result establishes the real request and its bounds, not its contents or whether it continues the same media. A separately reviewed capture-only follow-up is needed before further input admission. No automatic retry occurred.

## Evidence

- [Actual trace](trace.log), especially `CONTINUE`, the input pending/acknowledgement, type0 notification, five setup writes and the final `STOP`.
- [Run report](run.json): one5.03s functional run, exit10 at the intentional unsupported boundary; first-boundary audits passed, consumption audit did not run/pass. Fixed240s wall/180s CPU/1GiB/8MiB and retirement/publication limits were preserved.
- [Reproduced075 external audit](first/functional-audit.json) and [original-kernel program/coverage audit](first/upload/kernel-oracle.json), executed by frozen copied auditors.
- [Boundary audit](boundary-audit.json): all180,224 final SRAM bytes, original write mask, all three intentional write masks, the16,036-byte owned queue and first4,096-byte PCM remain byte-identical to the first-publication capture. Read coverage and the actual read ledger are empty.
- [Source/report hashes](provenance.json) and [build manifest](build.json). Original media/program/PCM buffers remain private local artifacts and are not copied into this documentation bundle.

## Prepared instrumentation and focused checks

The source retains full CPSR, including Thumb state, in successful read, copy and release records. Partial unaligned reads contribute only returned bytes overlapping the first PCM range. Destination inspection uses a copied CPU/TLB and a RAM-only page-table bus, respecting1KiB legacy permission boundaries; it cannot call live devices or modify guest counters.

The release guard checks the actual admitted owner0→1 write, its retirement, full read coverage and original IPC/copy/event evidence before allowing further engine work. It stops before potentially producing a third buffer. The auditor independently checks actual owner-write records and their order, rather than trusting a final summary alone.

All [4,229 read-observer checks](test_observer.log), [20 observational peek checks](test_peek.log), [4,122 retirement/refill checks](test_service.log), and [nine auditor predicate checks](audit-test.log) pass. These include synthetic Thumb/alignment, wrong release-PC/value, omitted coverage and corrupt read-data cases. They are unit fixtures. The actual consumption-artifact corruption controls remain unexecuted because no positive consumption result exists.

There is no production, snapshot, browser, I2S or speaker change.075 remains the accepted first-buffer result; original guest consumption is still pending.


## Capture-only follow-up

The separately reviewed one-shot [second-input observation](second-input/README.md) now proves the requested bytes are exactly the next 16 KiB of the original stream. The complete prior trace and state remain unchanged. The original failure above is preserved; second-transfer admission and PCM consumption remain pending.


The [subsequent one-shot admission](second-admitted/README.md) copied that second input through the existing engine and stopped at a third request before any PCM read. Its exact queued bytes and unchanged output state are audited; the next gate is service/interrupt ordering, not additional blind input admission.


The [ordering-only probe](ordering/README.md) now records a successful original session return and actual refill indices 1 and 2, with the entire previous trace and state unchanged. The caller/IRQ path to output startup remains under source investigation; no timing or input admission change was made.


The [actual saved-return observation](caller-return/README.md) resolves that call: configuration returns through the original kernel scalar I/O shim, with output start exposed as a separate method. Exact prior state/trace remain unchanged. A post-run launcher error and its artifact-only recovery are preserved explicitly.


The [third-input admission](third-admitted/README.md) verifies the next actual 16 KiB and its entire owned queue. The original driver wraps its refill index to 0 and requests a fourth transfer before enabling or reading output. The fixed cap preserves that earlier boundary; no further admission or playback claim.


The [MeCCA-return observation](mecca-return/README.md) confirms a successful original kernel configuration return with exact unchanged state. Selected userspace returns were not observed; their actual mapping/execution remains unresolved. No user error, startup or consumption is inferred from absence.


The [input-backpressure continuation](input-backpressure/README.md) naturally fills the bounded queue and reaches the proposed userspace scalar-return PC. An observer permission check stops before validated retirement; all actual copied bytes and prior state remain audited. No PCM consumption.


The [permission-corrected observer](user-start-reads/README.md) verifies successful real userspace returns/startup and every first-output PCM byte read. A later owner-store cycle assumption stops the probe; full handoff is not yet accepted. All device/admission behavior remains unchanged.
