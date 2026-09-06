# The downstream audio observer stopped before an unmapped code page

One observer-only Stage Z run resolves the earlier `31236650` binding failure: the actual second-level page-table descriptor is zero. All four copied-MMU READ/FETCH diagnoses return page-translation fault7. No actual code bytes are available, and **the guest has not fetched or executed this instruction**. This is not a code mismatch or accepted HAL call.

The entire [Stage Y suspension](../power-suspension/README.md) trace is unchanged after removing the single diagnostic line; every prior binary, transport capture and live suspended-resource report matches exactly. Device behavior, ownership, input admissions and stop/caps are unchanged. No reset, ungate, restore, I2S operation or audible output is admitted.

## Actual evidence

The [diagnostic receipt](run-1/binding.json) records USR/ARM CPSR`60000010`, PC`31236650`, LR`3124dc50`, cycle9,874,747,384, zero prior post records and no pending observer record. `emu_run` is active, but its current pre-step PC/cycle is unchanged and no instruction has been attempted at this target.

Actual CP15 translation context is SCTLR`00c0380d`, TTBR0`15c8a000`, TTBR1`147d8000`, TTBCR2, DACR1, context ID`11`. Each cached-copy READ/FETCH and each separately flushed-copy READ/FETCH observes:

| Actual RAM page-table access | Returned descriptor | Meaning |
|---|---:|---|
| L1`15c8ac48` | `08511001` | Coarse table at`08511000`, domain0 |
| L2`085110d8` | `00000000` | Unmapped target4KiB page |

An [independent descriptor parser](postprocess-1/binding-audit.py) derives the table addresses from the captured CP15/VA and checks the resulting fault7. Private fresh-walk results are explicitly not live instruction fetches. All clone faults match; no cached-versus-fresh discrepancy is seen here. `binding-code.bin` is empty, rather than a zero-filled pretend code window. The expected32-byte window is independently matched to the original MeCCA Mach-O, but not attributed to current guest memory.

Live preexisting fault registers are DFSR`807`, DFAR`00788be8`, IFSR7 and IFAR`3121e5f4`. That IFAR is a different address. It does not show that the guest has already taken a prefetch abort for`31236650`. A normal subsequent guest fetch may raise a genuine fault that the original kernel can handle; this run deliberately stops before testing that possibility.

## Guard and state preservation

The diagnostic copies CPU state and installs a RAM-only page-table bus. Fresh variants flush only their private CPU copy. No live TLB, cache, fault register, interrupt, device callback or RAM byte changes. Bounded diagnostics retain at most four walks, two1KiB spans and eight table words per walk. Failed/partial reads never publish actual code bytes.

Before the guest run, source review found that an unexpectedly successful binding could have continued into the target instruction. The [old build/pins](build-before-stop-fix.json) and [changed source copies](source-before-stop-fix/) remain preserved. The accepted correction stages against a private observer and **unconditionally stops before the instruction**, including an explicitly reported unexpected-success outcome. No guest ran with the earlier version.

The one native guest took6.033 wall seconds under the unchanged240-second wall/180-second CPU/1GiB memory/8MiB file,155.3M total/five-million continuation,64-publication/eight-input caps. This was functional testing alongside other work, not a speed comparison. It stopped at exactly the Y PC/cycle; no retry occurred.

## Validation and original auditor failure

All [22 native targets](build.json) pass, including [52 binding/permission/XN/cached-versus-fresh/atomic-output/CPU-RAM immutability checks](test_binding.log), retained700 suspension checks and the full7,164 AAC corpus/boundary checks. The independent table/fault parser has12 synthetic controls. The original55 actual input/PCM/read/ownership/suspension artifact mutations reject on this same run.

The [original launcher report](run-1/run.json) remains `guestPass:false`: its final auditor used the synthetic test aperture`10000000..20000000` instead of the actual phone's RAM. It incorrectly rejected the real L2 table at`085110d8`. The [failure](run-1/binding-audit.log) is preserved; the diagnostic probe used the correct live bounds throughout.

The [artifact-only corrected audit](run-1/binding-audit-corrected.json) reads the exact already-used snapshot's GEOM header: base`08000000`, size`10000000`, ending`18000000`. Snapshot loading had already checked that geometry against the live machine. No guest, code bytes, descriptor, source model or diagnostic record changed. Five geometry-parser controls pass; their first synthetic header-construction typo is separately retained in `postprocess-1/test-attempt-1.log`.

[Ten additional actual artifact mutations](postprocess-1/actual-negatives.json) all reject: fabricated live fetch, CP15 context, code availability/bytes, and both fresh walks' fault kind, page-table address and descriptor value. This is65 actual rejected controls for this one guest, with the initial failed result and corrected result both retained.

## Next boundary

The observer is currently preventing the original kernel from seeing the target's real fetch failure. A separately reviewed next experiment can allow the normal instruction-fetch/exception path, record actual IFSR/IFAR/LR/mode/vector and distinguish IRQ diversion, then observe whether the original kernel maps this page and returns. Code binding and HAL execution should be accepted only from an actually available original window and actual instruction/return receipts. No page may be mapped, instruction skipped, return forced or device enabled by the observer.

This is a nonvisual083 follow-up. [Provenance](provenance.json), [review pins](review-pins.json), [original contract](next-binding-diagnostic-contract.md), [private binary hashes](private-artifact-hashes.json) and [actual source](run-1/source/binding.c) retain the complete scope. Private RAM/PCM/media and executable remain under `.context/amc-first/stage-z/run-1`. Production and sound output are unchanged.
