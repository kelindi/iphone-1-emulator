# Stage Z proposal: identify the rejected original HAL window

Source-only after Stage Y. No new guest, build or admission is authorized by this document. Root is currently running a quiet LTO browser series.

## What the retained failure proves

Stage Y completes genuine state suspension and then calls its existing pre-step observer at PC31236650, LR3124dc50, CPSR60000010, cycle9874747384. The stop's value0 is the actual `post.count`; `post-records.bin` is empty. The original selected window is the static MeCCA HAL-start wrapper at31236650. It is not an accepted actual HAL call, because the observer rejects before committing the mapped-code record or allowing the instruction to execute.

`post_begin` in `stage-y/run-1/source/post.c` checks arguments, pending/count, USR/ARM mode and target, then calls permission-respecting `amc_peek_ram(..., PC, code, 32)` and compares the32 bytes with `post-original.h`. At this entry, stack/object availability is optional and cannot reject. Count0 and actual mode satisfy those gates. A pending record cannot remain from an earlier normal instruction without the probe's mandatory post_finish/stop, and none was recorded. Thus the remaining ambiguity is failed mapped-code peek versus mismatched live32-byte code. The current function does not preserve either partial diagnostic. An allocation failure is one possible cause within peek; neither a permission fault nor code corruption is established.

The helper uses an independent CPU copy and RAM-only page-table bus, preserving the copied TLB contents. It does not flush the live or copied TLB. Instruction observers are invoked before `arm_step` fetch/IRQ processing in the pinned machine loop. Reaching PC alone therefore does not prove that the target instruction is currently fetchable or will retire. The later failure must not be “fixed” by accepting this address or overriding user permissions.

## Smallest observational change proposed

Keep the exact Stage Y device model, admissions, suspended ownership, all previous observers, execution schedule and stop. Instrument only the first existing rejected `post_begin` attempt, with a bounded independent diagnostic record:

- Actual original argument/target/mode/count/pending predicates and a separate reason enum for allocation, RAM-only translation, physical bounds, or complete code mismatch. Record full CPSR, all16 registers, cycle, current pre-step state and CP15 SCTLR/TTBR0/TTBR1/TTBCR/DACR; unavailable fields remain explicit.
- Save the actual32-byte window only when all reads succeed. Do not commit a partial/zero-filled window as actual code. Retain the original expected32-byte window and its independently pinned original MeCCA source offset separately.
- On failed code observation, perform at most four **copied-CPU** translation diagnoses: current copied-TLB READ and FETCH, plus separately labeled fresh copied-TLB READ/FETCH. Flush only each private copy used for the fresh-walk variant; no claim that these are actual live fetches. Report returned fault code, actual normal-RAM physical span and availability. Page-table callbacks must be RAM-only; no live bus/MMIO or fault-state/counter writes. At most two1KiB spans cover the32-byte test window, and at most a small fixed number of page-table words are retained per walk. Failure/overflow stops the diagnostic itself with explicit unavailable fields.
- Keep the original stop immediately afterward, at the same cycle/PC before guest execution. Do not allow the instruction to retry/fault naturally in this experiment, because the current goal is to distinguish the failure without changing the continuation. Do not create a successful post record, fabricate LR/return values, or admit reset/ungate/restore/FIFO work.

This yields one positive *diagnostic* gate: a specific independently supported rejection reason and actual available mapping/code bytes. It is not HAL start acceptance. A subsequent observer change may be justified once actual fault versus relocated/mismatched image bytes are known; that is a separate reviewed decision.

## Tests and one-run bounds if reviewed

Focused synthetic cases must cover exact original window, mismatched bytes, missing current translation, user permission denial, non-RAM/out-of-range page tables and output physical spans, bad mode/target, record cap/pending, allocation failure and late second-span failure. Unavailable code must leave caller output unchanged. Verify whole live CPU bytes/TLB/counters/faults and RAM are unchanged; trap if any live bus callback is called. Explicitly distinguish cloned cached and fresh-walk results with a deliberately stale copied mapping rather than silently changing the helper used by old witnesses.

Freeze source/launcher/original-image pins after tests. One bounded run only, unchanged155.3M total/five-million continuation,64publications/eight guest inputs,240s wall/180s CPU/1GiB memory/8MiB perfile. No automatic retry. The full Stage Y trace must match after stripping only new diagnostic lines; every prior current resource/input/PCM/control/copy/transport binary must match. Run all existing first083/X/Y and corrected suspension audits. The new frozen auditor must reject at least a corrupted actual code byte (if available), fabricated availability, mismatched live CP15 context, incorrect fault kind and altered cloned page-table witness. Keep the original Stage Y launcher failure and initial suspension-auditor assumptions intact.

Only after this diagnostic can we decide whether the next guest step should be allowed to resolve a real fetch fault, whether the observer bound to the wrong original image/context, or whether a different narrow defect exists. None of these hypotheses is established by the existing address-only stop.

## Pre-run source review correction

The first focused build passed, but review found that its diagnostic success path could have staged the real post observer and executed the target if the window unexpectedly became readable. No guest had run. Root approved a correction: diagnose against a private observer copy and stop unconditionally at this target before the original instruction. A successful binding is reported as an unexpected diagnostic outcome; it is never admitted as HAL execution. Old build/pins and changed source files are preserved in `build-before-stop-fix.json`, `review-pins-before-stop-fix.json` and `source-before-stop-fix/`. The correction requires a focused rebuild/refreeze after the browser quiet window.
