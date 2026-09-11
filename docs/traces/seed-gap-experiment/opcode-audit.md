# Seed-gap failed browser attempt: source audit

2026-09-07. Read-only opcode/source inspection found **no demonstrated compiler defect**. Parent diagnostic established the candidate was already on SpringBoard's lock screen before the Settings tap; subsequent black screen followed normal sleep, while guest execution continued. The first failed browser attempt is not a valid app-launch comparison and remains failed evidence.

## Concrete cause of the invalid precondition

`web/emu-worker.js` now contains `openLocked()` (line103) and invokes it after snapshot load and before exposing ready (line189). This deliberately uses physical Sleep/Home inputs to enter the original lock screen. It is an explicit startup-policy change, not evidence of spontaneous auto-lock caused by additional generated coverage.

At inspection, production and BOTH staged sites had identical worker SHA-256:

`6d341e735e7d62ed167e0aed0406f1d6647f2458773e7cd8c71660543b3706b8`

All three paths shared inode `78021020`, link count17:

- `web/emu-worker.js`
- `.context/seed-gap/control-site/emu-worker.js`
- `.context/seed-gap/candidate-site/emu-worker.js`

Worker modification/change time: **2026-09-07 02:31:29.490558245 UTC**.
Control report start: **02:30:09.263 UTC**, passed22 actions.
Candidate report start: **02:31:31.231 UTC**, failed initial Settings readiness.

`prepare-browser.py` staged web files with `shutil.copytree(..., copy_function=os.link)`. Therefore an unrelated in-place source edit could propagate into both supposedly frozen sites between sessions. The first harness verified core and seed response hashes, not an actual served worker-body hash, so it did not detect the changed startup policy. The timing/inode evidence establishes the contamination mechanism; the exact old worker response was not independently captured by this audit.

## Opcode review and remaining qualification limits

Reviewed selected ARM/Thumb operations against interpreter source, prioritizing the high-frequency post-index LDR, CMP/SUB carry/overflow, immediate shifts, Thumb high-register PC+4, ARM PC+8 and multi-register addressing. No concrete semantic discrepancy established. This is a source review, not exhaustive opcode proof.

The272 focused tests remain valid within their declared scope: all256 selected literal-opcode entries compare in normalized-address full-region fixtures, plus16 added-word/old-tail stale guard cases. Only175 selected entries execute generated code in that fixture;81 safely use interpreter fallback, mainly because their memory fast paths are not warmed. Thus those tests alone do not qualify every data-dependent generated path. Parent's real-address100M/400M equality checks cover their actual fixed trajectory, not all browser startup states.

## Disposition

Do not change compiler semantics or exclude arbitrary added opcodes based on this failed browser run. Preserve the unrelated product changes. Any separately named successor comparison needs independently copied site files, identical complete frontend/worker/core assets except candidate seed files, fresh controls, explicit genuine Home precondition and actual served worker identity. Never reinterpret or splice the failed original comparison into a passing timing result.

No product edits, compiler variants, bisects or new guest runs were performed by this audit.
