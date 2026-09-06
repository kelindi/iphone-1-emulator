# The original kernel handles the audio callback's missing code page

The single isolated Stage AA run lets the original CPU attempt the unmapped instruction observed in [Stage Z](../hal-binding-diagnostic/README.md). It takes a genuine prefetch abort, the original guest installs a mapping, and execution returns to the same original user context. The probe stops **before executing the mapped original StartIO entry instruction at `0x31236650`**. No HAL, I2S or audible playback is claimed.

## Actual fault and retry

At cycle 9,874,747,384, the real fetch-block cache has a pointer present but its block is `3124dc00`, which does not match the target block `31236400`. Generation and privilege match, so observing only those fields would have been insufficient. The observer evaluates the complete unmodified interpreter predicate and requires a miss; it never clears the live cache.

The original `emu_run(1)` returns at cycle 9,874,747,385 with PC `ffff000c`, ABT CPSR `60000197`, IFSR 7, IFAR `31236650`, LR `31236654`, and SPSR `60000010`. Actual IRQ and FIQ lines were clear. This is an exception step, not retirement of the inaccessible target instruction. [Before/after receipt](run-1/fetch.json) and [independent audit](run-1/prefetch-audit.json) distinguish both facts explicitly.

After 1,636 total core steps beyond the earlier Z stop, the original guest returns to `31236650` at cycle 9,874,749,020. All 16 registers, full user CPSR and translation-context fields match the original request. The actual L1 remains `15c8ac48=08511001`; actual L2 `085110d8` changes from zero to `15fbe02e`. All four cached/fresh cloned READ/FETCH walks resolve to physical `15fbe650`, where the actual 32 bytes match the independently parsed original MeCCA Mach-O window. [Actual retry mapping and bytes](run-1/fetch-attempt-1/binding.json).

The mapped retry hook stops before the original instruction. No page-table write, fault, return, instruction advancement or interrupt was supplied by the observer. The page-in follows ordinary guest execution. No claim is made here about which individual kernel store installed the mapping; before/after actual descriptors and the unchanged original continuation establish the observed result.

## Preserved audio state and acceptance

The [fresh prefetch prefix](run-1/fetch-prefix/) matches Stage Z's exact trace through its diagnostic and all earlier binary, transport and suspended-resource captures. This run independently retains every original 083 input/PCM/read/owner-release proof, 53 genuine PCM publications, 51 ownership returns, 83,304 actual copied input bytes, 17,770 decoded bytes, and the 65,534-byte owned queue. All 180,224 SRAM bytes and retained resource state remain unchanged through the retry. Active decoder allocations, service, DMA and IRQ remain cleared; the suspended generation owns the retained resources. No reset, ungate, restore, extra input or publication is admitted.

[All acceptance results](run-1/run.json) pass on the first and only run. The native diagnostic returns 10 because the intentional stop uses the existing probe stop convention; the launcher returns zero only after the actual boundary and all independent audits pass. Guest execution took 6.449 wall seconds under functional overlap, not a speed measurement. Bounds remain 155.3M total/five-million earlier continuation, 64 publications/eight actual input requests, 240 seconds wall, 180 seconds CPU, 1 GiB memory and 8 MiB per file, plus at most eight target attempts and one million core steps after Z.

All [23 native targets](build.json) pass, including [176 checks using the real pinned interpreter](test_prefetch.log), [52 read-only binding checks](test_binding.log), 12 independent descriptor controls, retained 700 suspension checks and [7,164 AAC corpus/boundary checks](test_boundary.log). Tests cover high/low ABT vectors, IRQ/FIQ priority, wrong returned state, live-cache refusal and an explicitly test-owned mapping used solely to verify stopping behavior.

All 70 actual artifact negatives reject through the frozen auditors: 66 retained input/PCM/ownership/save/suspension/binding and new exception/cache controls, plus [four mapped-retry controls](run-1/prefetch-negatives.json) corrupting context, availability, physical mapping and code. These invoke the actual auditor on modified artifact sets; they are not mere byte-inequality checks. No guest retry or post-run source correction was needed.

## Why this matters and what remains

The prior stop was an observer preventing a normal demand-page fault, not proof of a blocked HAL implementation. The original kernel resolves it within a small bounded continuation. Actual StartIO execution, its real return and downstream HAL/hardware requests can now be investigated from a mapped, original-code-bound boundary; they are still separate gates.

This is a nonvisual follow-up to milestone 083. [Source and scripts](run-1/source/), [review pins](review-pins.json), [provenance](provenance.json) and [private artifact hashes](private-artifact-hashes.json) preserve the result. Original media, RAM, PCM and binaries stay private under `.context/amc-first/stage-aa/run-1`. Production and snapshot formats are unchanged; the FAAD decoder remains an isolated GPL experiment.

The [source-only next contract](next-mapped-start-contract.md) distinguishes actual `StartIO` at `31236650` from HALOutputProc at `31236734`. It proposes the existing six transport observations and ten consumption hooks, actual stack/return receipts, and the unchanged lifecycle limits. No subsequent guest or StartIO/HAL execution has occurred.
