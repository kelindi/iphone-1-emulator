# Coarse ARM decoder partition: rejected performance prototype

> Historical recordings and publishing artifacts have been removed from Git history. The summaries below describe the original runs; remaining test reports and screenshots are retained. Historical hash receipts may predate this archival cleanup.

The current production profile's largest internal leaf, function81, reliably maps
to `arm_step` through the [binding proof](binding/README.md). Its24.37% exclusive
sampled residency includes fetch, Thumb execution, decoding and exception handling;
it is not a measurement of decoder routing alone.

The isolated [candidate patch](candidate.patch) partitions the conditional ARM
instruction chain by bits27:25. Each bucket keeps the original matching predicates,
priority and complete instruction bodies. Fetch, condition handling, Thumb and final
fault/PC handling are unchanged. No Apple binary or production source was modified.

## Verification

- Exhaustive routing equivalence:536,870,912 cases. The only routing predicate
  depending on the condition nibble is exact WFI; AL and another nibble cover both
  possibilities. ConditionF is handled outside the changed code.
- Fresh control build is byte-identical to current core3b43; candidate is6b549c12.
  Both use the original seed and identical ABI offsets.
- Four complete snapshot hashes match at25/50/75/100M instructions in both
  interpreter and seeded execution. Four400M seeded runs also match complete final
  state and JIT statistics. This is relative correctness, not full ISA conformance.
- Node fixed-work screening reports3.01% more candidate process CPU. Concurrent
  source-analysis work overlapped this screening, so this preliminary figure is
  not a decisive quiet benchmark.
- Actual browser order is control/candidate/candidate/control,11 Settings/Home
  pairs per session. All88 actions complete without page errors; actual served
  core and seed hashes are checked. There are20 warm samples/action/version.

| Warm metric | Original | Candidate |
|---|---:|---:|
| Settings median |2317.30ms|2317.65ms|
| Settings p95 |2517.40ms|2461.30ms|
| Home median |866.95ms|867.30ms|
| Home p95 |868.20ms|868.40ms|

Settings median and Home median are essentially tied. The56ms pooled Settings
p95 reduction alone does not establish a useful improvement; it is far short of
20% and remains a small sample. First Settings is4.113/3.465s for control and
3.611/3.464s for candidate. First control may overlap final source-analysis work;
these two first samples do not establish a first-launch gain.

## Decision and scope

Reject this prototype and retain production3b43. No promotion, saved-phone upgrade
or full-app acceptance was attempted for the rejected candidate. Normal browser
clock/autosave policy was retained; snapshot comparisons above are separate fixed
execution checks, not an autosave/reload product gate. Reference-Mac performance
remains unmeasured.

`browser-summary.json` retains every timing and raw frame hash, including failed
exact matches rather than hiding them. All44 Home frames match below the status
bar;23 Settings frames differ from the first control. Manual review and pixel bounds locate every difference in the right scrollbar
(x313–317,y66–339), at different fade intensities. Alpha is exact; maximum RGB
difference is63. The exact-image check remains failed; no crop or tolerance was
introduced to turn it into a pass. Complete recordings and original frames remain in
each browser session directory. historical recording (removed)
and historical thumbnail (removed) show the actual experiment.

`archive.json` hashes the retained scripts, reports, compiler patch, modules,
logs and browser artifacts. Scripts retain their original `.context/core-hotspot`
working paths; they are evidence of the executed experiment, not a new maintained
benchmark command. No reruns or unreported candidate revisions were made.
