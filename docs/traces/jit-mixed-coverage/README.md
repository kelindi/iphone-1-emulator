# Measured mixed seed coverage candidate — parked

> Historical recordings and publishing artifacts have been removed from Git history. The summaries below describe the original runs; remaining test reports and screenshots are retained. Historical hash receipts may predate this archival cleanup.

The browser comparison does not justify promotion: warm Settings improves modestly, but first use is mixed and Home tail latency worsens in the reverse pair. Production remains unchanged. Milestone 088 and the complete actual recording.

The candidate adds 64 measured hot pages and 2,048 measured positions within 71 existing regions. It preserves all 512 original definitions, captured opcodes, and admission bits. The instruction compiler and its raw-code, memory, exception, mode, alias and budget guards are unchanged. This is separate from the parked continuation, POP, lazy-group and earlier coverage experiments.

Selection comes from the zero-loss [actual fallback census](../jit-fallback-census/README.md), with positive-prefix mandatory interpreter steps excluded from the direct ranking credit. The selected positions are new to the parked 959-position experiment. Pages with observed opcode changes across either Settings or Home are excluded. The 7,857,848 selected direct interpreted steps are a syntactic upper bound, not a speed forecast: runtime guards and instruction sequencing still matter. Full selected PCs/opcodes/ranks, provenance and frozen input hashes are in [selection.json](evidence/selection.json).

The generated seed has 576 regions and 14,358,444 bytes, under the predeclared 576 / 16 MiB limits. Its largest body remains 67,111 bytes, under 80 KiB. Original seed regeneration is byte-identical (`d9c661…`). All 441 unaffected original function WAT and WASM bodies remain byte-identical; 71 bodies change to include the new positions. Candidate seed SHA-256 is `43532cd26c7367058087f8baa9ecf9b3a88fa706cfd1c467e983954c1de28164`. ABI revision and all 16 offsets are unchanged. [Generation evidence](evidence/generation.json).

Only the isolated runtime's region capacity changes from 512 to 576; its bounded retired-function queue grows with that constant. The loader accepts at most 576 definitions and 16 MiB, retaining the existing eight-second deadline and streaming/integrity policy. The core links the exact released RTC archive and API, without the census observer. Control core is `7a72add6…`; candidate is `4752729764601d23b8f073ec895f0d426a9cae9bb8de39fd1a33e5930bc21b59`. [Build commands and archive identity](evidence/build.json).

Focused validation passes 53 loader/lifecycle checks, including 576 complete installations, 577-definition rejection before function allocation, cleanup of all 576 slots after final-admission failure, reset and full retirement reclamation. Actual runtime WASM passes 1,744 capacity assertions, including output canaries, duplicate invalidation, full queue drain, reset and replacement saturation. Running the same capacity oracle against the original 512 runtime fails at the 513th installation, as required. The existing actual-emitter boundary suite passes 404 scenarios / 1,212 engine cases with architectural, read/TLB, RAM and literal device-tick equality. Its two generated-engine rows deliberately use the same original dispatch policy; it does not claim a continuation comparison.

One 100-million-instruction candidate run matches all four complete raw snapshots and the final framebuffer of the retained exact 7a72/original-seed control. Both use the same pinned genuine snapshot, firmware, Settings press/release at 0/5 million, one-million-step chunks and snapshots at 25/50/75/100 million. No snapshot fields or TLB counters are normalized. Full candidate snapshots are retained privately and pinned in [archive.json](archive.json). The final 248,274,914-byte snapshot hash remains `e8bd93554ee39f08092da5f99f52652d57e626fca67cc0b81eb5c63a437f5d58`; the final 320×480 framebuffer remains `4d9e9478…`.

Generated retirement increases from 73,093,840 to 78,975,457 (+5,881,617), while generated calls increase from 9,273,308 to 9,878,554. Remaining active regions are 509 versus 573 after genuine invalidations. These are instruction counts, not CPU-time savings. The functional run may overlap the independent private-program cold boot; no timing is reported. [Candidate report](evidence/candidate-100m.json), [retained control](evidence/control-100m.json).

The separately coordinated actual-phone comparison is complete, with unchanged readiness and settled-frame rules, complete recordings and exact served hashes. Its original disk-stop and separately authorized fourth-only continuation are preserved below. The predeclared compilation CPU limit is matched control × 1.25 + 100 ms, accounting for foreground and background compilation consistently. No loader fallback, timeout, trace loss or failed strict frame gate is accepted. Production remains unchanged. The two-second navigation and 150 ms input targets remain open.


## Actual browser result and decision

The original frozen series completed three sessions, then stopped before the fourth browser at the declared 4.5 GiB per-arm free-space floor. Each completed arm retained 696–716MB, more than the startup headroom estimate allowed for four arms. The first three sessions, original failed series and all observations remain immutable. After clearing only their disposable HTTP/Code Cache directories, a separately authorized controller ran the unstarted fourth original control with the unchanged 240-second limit. Free space was 5.49 GB before that arm and 4.79 GB afterward. IndexedDB, CacheStorage, raw frames, traces and recordings were preserved. This is an alternating four-arm comparison with a disclosed cleanup gap, not an uninterrupted accepted original series. [Disk growth](browser/disk-stop.json), [original stop](browser/original-incomplete-series.log), [fourth-only receipt](browser/fourth-continuation.json).

| Metric | Original | Candidate |
|---|---:|---:|
| Warm Settings median, 20/config |3.167s|3.067s|
| Warm Settings p95 |3.467s|3.261s|
| Warm Home median |0.8665s|0.8669s|
| Warm Home p95 |0.966s|0.917s|
| First Settings, forward |6.364s|5.506s|
| First Settings, reverse |5.201s|5.588s|
| Summed compilation thread CPU, forward |19.284s|20.662s|
| Summed compilation thread CPU, reverse |17.105s|20.626s|

Warm Settings median gains are 6.86% forward and 2.35% reverse; p95 gains 15.90% and 3.00%. Home median is unchanged, and reverse Home p95 regresses 0.867→0.967s despite the favorable pooled value. The two first Settings pairs disagree. All 88 settled raw frame pairs/reference comparisons and eight manual first screen reviews pass; exact served hashes, scoped cleanup, complete traces and both compilation CPU budgets pass. Summed CPU includes foreground/background compilation, unioned per thread without nested duplication; it is not elapsed launch time and not seed-only attribution. [All observations and per-order statistics](browser/browser-summary.json), [88-frame audit](browser/screen-audit.json), [frozen method](browser-contract.md).

**Decision: park, no production promotion.** The modest warm improvement does not justify the extra compilation/size and mixed first/Home outcome. These fresh-profile controlled-import measurements are distinct from the accepted cached return-visit finding in 080. The comparison uses frozen previous UI, normal clock/autosave and the genuine guest; the current recovery UI is unchanged. All 4 raw WebMs remain private; the entire 117.28-second third candidate arm is available as the original-speed historical recording (removed), with actual 46s historical thumbnail (removed).

## Register-transfer follow-up: already measured, no repeat

A source-only recheck reconstructs every current original 512 WAT body and proves exact identity with the prior register-layout census. All 512 register read/write sets match. The original compiler, input and shipped seed hashes are unchanged. Static counts remain 1,980 of 8,192 spills aimed at never-written registers and 1,580 loads of never-referenced registers. Historical fixed 100M call weighting gives 25.90% and 20.56% respectively. These historical weights are not asserted to equal the current warm browser: the new fallback census records interpreter PCs, not generated per-region calls.

This is exactly the opportunity already tested by the [parked register-omission experiment](../jit-register-layout/README.md): 2,335 architectural cases, deliberate bad-load/bad-spill negatives, complete 600M proofs on both fixtures, then opposite timing orders (+1.067%/−1.162%, aggregate −0.042%). V8 may already eliminate nominal transfers; fewer emitted loads/stores did not show repeatable gain. No new prototype or timing is justified by unchanged bodies and no new dynamic evidence. [Current body/set identity proof](browser/register-recheck.json), [source](source/recheck-registers.mjs).
