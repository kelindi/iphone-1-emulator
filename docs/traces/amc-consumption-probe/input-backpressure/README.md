# Bounded input queue fills; original userspace return address reached

One fresh Stage O run preserves the exact Stage N prefix, accepts the fourth and fifth actual RAM transfers, and lets the unchanged engine apply its existing64KiB backpressure. The first publication-to-Stage N stop interval was only0.588ms of emulated timer time, so earlier absence of user-return hooks did not establish blocked initialization.

The fourth16KiB capture matches original file offset100392/raw stream49152; the fifth matches file116776/raw65536. Both actual wrapped-ring captures are uniquely located and contiguous. The fifth DMA copies only348bytes/87words before the owned queue reaches65,536bytes. It remains active, its current physical byte address is14ac115c, and completion is clear. The rest of the fifth capture is available evidence, not owned/copied input.

Original execution subsequently reaches PC3123f7f0, the proposed MeCCA scalar-return site, with saved R0=0. **The ordering observer rejects before validating its full record or retiring the instruction.** Therefore this is an observed PC/register boundary, not a verified user-return/count or PCM-consumption result. Source inspection identifies a likely observer permission bug: user-mode hooks attempt to inspect kernel driver fields under user permissions; old synthetic tests made the driver readable. The exact failed predicate was not recorded, so this remains a source-backed hypothesis pending the corrected observer gate. The preserved stop is an instrumentation limit, not an established audio-engine error.

The only guest ran5.03s, exit10, no timeout or retry. Exact075/original-upload/second/third/Stage N state and trace-prefix audits pass. All actual copied bytes, the full65,536-byte queue, retained SRAM/write masks and first PCM match their independent oracles. Eight actual artifact mutations reject: third/fourth/fifth captured input, copied DMA bytes, current-address state, queue, ordering register and mapped code. No read/ownership/start/output acknowledgement or audible playback occurred.

## Evidence

- [Run and bounds](run.json), [full trace](trace.log), [later input/copy provenance](later-audit.json), [full queue/PCM](queue-audit.json), [recorded ordering](ordering-audit.json).
- [Source diff](source.patch), [frozen pins](review-pins.json), [build](build.json), [first075](first/functional-audit.json), [original upload](first/upload/kernel-oracle.json).
- New native admission/backpressure/copy246checks and external-auditor24synthetic checks pass. All retained observation/read/retirement tests pass. Source/launch/oracles and logs are retained here; full private media, PCM, RAM and runtime binaries remain local under `.context/amc-first/stage-o/run-1`.

No production or snapshot changes. Next: correct only the observer's user/kernel permission assumptions, retain real user code/object/stack validation and replay this same bounded engine path. No new device admission or ready event.
