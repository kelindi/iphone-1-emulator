# Why compiled execution stops

> Historical recordings and publishing artifacts have been removed from Git history. The summaries below describe the original runs; remaining test reports and screenshots are retained. Historical hash receipts may predate this archival cleanup.

**Decision:** prioritize measured seed-coverage gaps before another connected-code architecture. This diagnosis finds frequent missed opportunities, not a demonstrated speedup. Production is unchanged.

## Warm Settings result

One isolated Chrome session performs Settings/Home three times. Exclude the first pair from the pooled result; the remaining two Settings samples agree closely. The denominator below is 18,640,516 generated-function exits:

| Exit | Share |
|---|---:|
| No emitted position at destination | 40.14% |
| Left the 1KiB region | 38.59% |
| Data cache host/tag/generation miss | 16.15% |
| Data alignment or crossing a 1KiB span | 2.02% |
| Supplied execution budget exhausted | 3.11% |

There are separately **41,112,723 rejected attempts to enter compiled code**: 69.17% have no active region, 21.88% lack an admitted position, and 8.90% lack a matching current instruction-fetch region. Other first-rejection reasons total about0.05%.

Of rejected entries, **74.27% have a safely observed opcode that today's emitter supports syntactically**. Most are on pages absent from the fixed512-region seed; some are omitted positions within its pages. This does not establish that their memory/state checks would succeed or that compiling them saves proportional time.

Across the two warm Settings windows, supported omitted positions within existing regions account for 6,469,737 admission rejections plus1,295,526 generated exits. These are concrete targets without increasing the region count. The hottest observed missing page is Thumb0x3001d800 (860,294 rejections); two existing ARM regions0x306fdc00/0x306fe000 repeatedly transition between one another, about305,000 times per window combined. See exact addresses in the raw analysis rather than treating page edges as individual branch sites.

The seed contains previously observed positions; it does not automatically learn all currently compilable code. A compiler's ability to translate an opcode is distinct from that position actually being installed.

## Recommended next experiment

Fill only the highest-frequency already-supported missing positions within the existing512 regions, retaining all current raw-code, memory and timing guards. Rank candidates using both warm Settings and Home; do not increase the page cap or add privileged opcodes in this first test. Compare complete guest states, then an uninstrumented browser comparison against production. Require a meaningful improvement in Settings with no Home/tail regression before installation. Prior expanded576-region work was mixed; counting more coverage alone is not an acceptance gate.

If this fails, use the retained page/edge census to assess adaptive hot-page selection and remaining memory fallback. Do not promise2×, remove timer checks, or infer a physics ceiling from the result. The observer does not measure exclusive CPU cost per reason.

## Instrumentation and correctness

The original emitter already records an internal exit reason. Add one isolated C observer after register spill and before stale retirement, and split memory reason6 into alignment/span8, cache9 and write-policy10 without changing branches. All512 emitted functions project byte-for-byte back to the original WAT after removing the observer and mapping diagnostic labels back. Runtime admission preserves its original first-failure order. No additional region lookup, guest memory translation, device access or code execution is performed for observation.

Read raw opcodes only through a valid, matching, aligned instruction-fetch RAM mapping; otherwise mark unavailable. Record source region index, destination PC/ISA, reason, observed opcode validity, call count, retired prefix and zero-progress count in a bounded table. All windows report zero table overflow. Source is a region, not the original entry PC or exact branch instruction.

Seven focused observer checks pass. Original and diagnostic seed runs match complete guest-state hashes at25/50/75/100M instructions and at400M for actual Settings/Home fixed input. Compiled calls and retired instructions also match exactly. These hashes equal the previous unmodified baseline trajectory. This is instrumentation validation, not a performance comparison; report times include observer overhead.

All six browser windows conserve generated exits against the exact JIT-call delta and summed prefixes against the exact retired delta. An independent review also recomputed raw-row count/prefix/zero sums against all40 totals, with exact matches. Both final app screens were visually reviewed. The test verifies actual fetched runtime and seed hashes. It does not qualify saves, every app, or native/Mac performance.

## Interpretation limits

- Browser collection starts in the worker shortly before pointer-down and ends after the observer's500ms stable-screen confirmation plus message delay. This is a navigation-and-confirmation window, not precisely release-to-ready. Table-copy time affects diagnostic end timestamps; guest execution is synchronous and cannot advance during that copy. No speed claim uses these timestamps.
- A positive compiled prefix with budget remaining is followed by a mandatory interpreter step. That step is not an admission rejection; the two denominators must stay separate. Reason4 can be an omitted supported position or a genuinely unsupported opcode.
- Reason0 includes VFP access/rounding refusal and ARM BX invalid target low bits. Reason5 can detect a changed instruction elsewhere in the guarded block, not necessarily at the reported PC. Memory misses are not proof of MMIO or faults.
- Budget exhaustion takes priority over page/ISA checks. It does not identify a specific hardware deadline. Scheduler paths that bypass JIT entirely are not counted. Source inspection confirms the active-clock256-instruction ceiling and other boundaries, but this census cannot apportion all device-time overhead.
- Opcode support is tested with the pinned original emitter, independently of installation masks. Unavailable observations remain unknown. Existing baseline WASM VFP defects remain unresolved.

## Evidence and reproduction

[Analysis](analysis.json), [pooled warm Settings](pooled-warm-settings.json), [state replay](screen-result.json), [browser report](browser/report.json), [observer checks](counter-test.json), and [seed projection checks](seed/build-report.json). Raw row files are gzip-compressed JSON; each row is `[used, sourceIndex, pc, thumb, reason, raw, rawValid, count, retiredPrefix, zeroCount]`. Aggregate columns are calls, retired prefix, zero calls, full-budget calls. Reasons are defined in classify.cjs and jit-runtime.c.

Scripts retain workspace-relative paths. Regeneration requires original firmware/fixture and the pinned current core archive recorded in runtime-build.json; those large inputs are not duplicated. Compiled observer runtime and seed are retained. Decompress raw rows into original paths before rerunning analysis. This archive adds no production code.

historical recording (removed) and historical thumbnail (removed) show startup followed by all three Settings/Home pairs. The later two pairs correspond to the pooled warm census. Video is a demonstration of the instrumented phone, not comparative speed evidence.
