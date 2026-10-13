# Guarded continuation between existing generated regions

> Historical recordings and publishing artifacts have been removed from Git history. The summaries below describe the original runs; remaining test reports and screenshots are retained. Historical hash receipts may predate this archival cleanup.

**Decision: parked after the actual browser gate.** This isolated runtime candidate passes focused and full guest-state equivalence and reduces fixed-workload execution time by4.20%, but browser p95/Home are effectively unchanged and four strict screen comparisons fail. **Production is unchanged.** The original 512-region seed, ABI, CPU instructions and device deadlines remain intact; this is not a browser speed milestone.

## Mechanism and boundaries

The actual MBX census has 3,209,650 cross-region exits, of which 2,601,329 destinations (81.05%) have an admitted original seed entry. The existing runtime always returns to the machine loop after one generated region, which then executes an interpreter instruction if budget remains. The [candidate patch](runtime.patch) permits another admitted generated region after a positive prefix crosses the one-KiB region or ARM/Thumb mode boundary.

Each iteration rechecks pending abort, valid mode, IRQ/FIQ, PC alignment, original admission masks and fetch mapping. It retains the original remaining device budget and existing generated raw-code and physical-code-alias guards. Same-region and zero-progress exits return immediately. A saved source base/mode remains valid even when a callback retires the source region. There is no recursion, guest patch, new cache, snapshot field or seed ABI change.

The already existing `arm_fetch_cache_try_refill` in `core/src/mmu.c` may reuse a valid fetch mapping, map MMU-off RAM or use a successful existing fetch TLB entry with matching tag, generation, privilege and MMU stamp. It never walks page tables or speculatively admits MMIO/faults. Full one-KiB host RAM availability is checked before a new mapping commits. Only a positive generated prefix can invoke it here; first-entry behavior is unchanged. Parent independently reviewed this source boundary; that review is not speed acceptance.

## Exact artifacts

- Production core: `b3237ef3843412aea164d3db111299650ea8433bf16311417116799959ab6606`.
- Candidate core: `9220380b7a2a45b33ed965796d252af33b65ec6cae5129be34111c5d18ebcce3`.
- Shared original seed: `d9c661556ab17bb8433ed9b0c3ecfd52e2fcafe58221b4cb84a920e4976b3881`.
- Shared production archive: `765a902a384823ab97b9fb7be6655247144269e391ccb80e1daee33d0245d3fb`.

[Build provenance](build-provenance.json) retains the exact compile/link arguments. Only the runtime object is replaced over the production archive; both arms include the previously promoted snapshot classifier. [Artifact hashes](artifact-hashes.json) also bind the tested oracle module/glue, without duplicating large generated binaries. Build scripts retain their original repository-root `.context/jit-continuation` paths; copy the retained sources there to reproduce. The genuine firmware/checkpoints remain local prerequisites, not newly distributed files.

## Correctness

[Full replay](correctness-600m.json) passes **24 complete raw snapshot checkpoints**: genuine MBX and software fixtures, candidate interpreter and candidate original-seed execution, six checkpoints every 100M instructions through 600M. All SHA-256 values equal the previously verified original runtime references. TLB hit/miss/flush counters remain exact. No snapshot bytes or architectural fields are normalized. The original Settings touch and Home transition at 300M are retained.

Generated retirement and call counters intentionally change because fewer original guest instructions fall back. They are recorded explicitly, not falsely required to match the original classification:

| Fixture | Original generated / calls at 600M | Continuation generated / calls at 600M |
|---|---:|---:|
| MBX | 460,306,256 / 64,437,078 | 482,201,736 / 66,928,334 |
| Software | 496,605,837 / 56,932,774 | 516,338,709 / 58,751,725 |

At 100M, MBX generated retirement rises from 73,093,840 to 75,581,442; software rises from 87,140,487 to 90,051,305. These are execution-classification counts, not predicted time savings. Final MBX raw SHA is `642bd79aa34ec60617b745f2eceeff4c5f7c9e64ccf694cbf4b1f439adc958b4`; software is `bfcde2adfff879343c437ef0d0b55fb689525a2178e080c16b95f7956c77c7f5`.

The [actual-emitter oracle](oracle.c), [build script](build-oracle.sh) and [frozen passing test](test-continuation-frozen.cjs) pass **404 focused three-engine cases**. The actual original compiler generates instructions and the same C core executes interpreter/original/continuation paths. Exact CPU/status/read/write/TLB bytes and complete one-MiB RAM hashes match. Ninety-six cases additionally compare full serialized machine bytes against literal one-instruction ticking at phase/deadline/IRQ boundaries. Coverage includes cross-region loops and exact budgets, unsupported destination masks, stale source/current/tail/target code, invalidated source callback, no progress after refill, mapping/stamp/privilege/host-memory faults, actual XN prefetch abort, BX low bits 00/01/10/11 and physical code-write aliases.

[Current test](test.cjs) adds isolated compiler-path, original-runtime-only and output-directory seams for another agent's independent grouped-function experiment. With `TEST_ORIGINAL_RUNTIME=1`, both generated comparison rows route through the original runtime; continuation is not combined. The frozen file identifies exactly the 404-case continuation run. Small programs here do not prove 32-block grouping; that experiment has separate large-group cases.

Both deliberate controls fail architecturally: [wrong remaining budget](negative-budget-failure.json) retires four instructions for a three-instruction limit; [missing target refill](negative-refill-failure.json) executes stale source mapping rather than changed destination code. Sources, build scripts and errors are retained.

Two initial harness setup failures are also retained. A synthetic one-Hz timer fixture left PMU frequency at its incompatible default, so snapshot serialization correctly refused it; the fixture now sets both frequencies coherently. A later test expected generated Thumb BX although the unchanged compiler refuses that instruction; the corrected admission includes the supported prefix and exercises the existing interpreter fallback. Neither was hidden as a passing run, and neither required a production change.

## Timing protocol

[Quiet harness](quiet.mjs) restores the identical MBX snapshot for each arm and shares one compiled original seed. Each core first runs the complete 600M workload outside the measured sequence, avoiding a cold Home segment after only 100M warmup. It then waits for explicit all-agent quiet START, runs baseline/candidate/candidate/baseline, and requires all six raw/TLB checkpoints plus the frozen engine-specific JIT counters in every arm. Synchronous `emu_run` elapsed time and process CPU intervals exclude restore, installation, input, capture and hashing; total elapsed including checks is separate. First compilation, browser launch latency and p95 are outside this experiment's scope.


## Quiet result: repeatable CPU improvement, browser gate open

All other agents explicitly ended heavy work before measured START. Both cores first completed their full 600M warmups. [Every arm and checkpoint](quiet-report.json), [warmup state](quiet-ready.json), [summary](quiet-summary.json), and original logs are retained. No run was discarded or substituted.

| Order | Core | Synchronous emu_run elapsed | Process CPU in those intervals |
|---|---|---:|---:|
| A | Production baseline | 22,261.389 ms | 22,279.548 ms |
| B | Continuation | 21,273.364 ms | 21,285.651 ms |
| B | Continuation | 21,422.782 ms | 21,428.005 ms |
| A | Production baseline | 22,308.597 ms | 22,313.524 ms |

The reduction is **4.438% forward and 3.971% reverse, 4.204% combined elapsed**; process CPU independently agrees at **4.215% combined**. All24 complete raw snapshots and TLB counters pass. Generated counters match each engine's frozen policy exactly. The seed, ABI, fixture and core hashes are asserted, with no instruction-emitter or parked candidate combined.

This one ABBA workload is a bounded CPU result, not p95, first-launch latency, animation speed or universal gain. It justifies an actual browser gate before deciding promotion. Production remains unchanged. Warmup process CPU is deliberately excluded: shared first-use compilation makes the first warmup more expensive and is not the measured question here.

## Browser decision: parked

The subsequent quiet browser ABBA used four fresh Chrome sessions, normal Import of the identical MBX checkpoint, original seed and byte-identical production frontend. Only runtime/glue and the derived offline generation differ. Each session has one first Settings/Home pair and ten warm pairs:20 warm observations per action/configuration. The unchanged maintained30ms pointer pulse, actual release timestamp,50ms sampler,500ms stability observer and normal30-second automatic Save cadence remain. Actual browser response receipts verify every core/glue/seed hash. Every action has positive generated execution and no reported engine or page error. [Frozen design](browser-design.md), [harness receipt-only patch](performance-receipts.patch), [preflight](browser/preflight.json), and [complete reports](browser/quiet-navigation/comparison.json) are retained.

| Diagnostic release-to-ready time | Production | Continuation |
|---|---:|---:|
| First Settings, two sessions | 4,116.3 / 4,055.0 ms | 4,313.5 / 3,916.5 ms |
| Warm Settings median | 2,967.1 ms | 2,917.2 ms |
| Warm Settings p95 | 3,167.6 ms | 3,167.7 ms |
| First Home, two sessions | 816.2 / 866.7 ms | 966.4 / 866.3 ms |
| Warm Home median | 867.05 ms | 867.30 ms |
| Warm Home p95 | 917.9 ms | 917.4 ms |

Per-order warm Settings medians are2,941.95 /2,892.90 /2,917.35 /2,992.15ms in baseline/candidate/candidate/baseline order. All underlying observations and first samples remain in the [review](browser/quiet-navigation/review.json). The small median change does not establish a meaningful p95 or Home improvement; two first launches per configuration are insufficient to resolve the mixed first-use result.

The strict screenshot gate also **fails4 of88**. Every screenshot is compared both with its same-session first reference and the frozen first baseline Settings/Home references, below20CSSpixels with the unchanged maximum RGB delta3. Three failures are baseline and one is candidate: baseline run1 rounds7/10 and run4 round1, candidate run3 round4. The differing region is the Settings scrollbar at x294–297,y63–318, maximum delta5/6. All eight first references and these four failures were manually reviewed: genuine Settings content and complete Home remain visible, but that does not waive the numeric failure. [Exact comparisons and bounds](browser/quiet-navigation/screen-comparison.json), [human reference review](browser/quiet-navigation/human-reference-review.json), and all88PNGs are retained. No sample or threshold was replaced.

**Park the candidate; production is unchanged.** The4.204% fixed-workload CPU result is real within its bounded experiment, but it did not establish an accepted browser benefit. The diagnostic p95 values above do not replace the latest fully verified navigation result in milestone055. Full-speed app functionality remains open. Four original-speed recordings are retained with [local paths and hashes](browser/quiet-navigation/recordings.json); the historical recording (removed) now preserves the entire106.36-second session without cuts or speed changes, with an historical thumbnail (removed) at18 seconds. [Media provenance](browser/video.json) verifies matching source/output duration.
