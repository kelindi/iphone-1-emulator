# Second input received; original consumption still pending

One reviewed isolated run admits the original driver's exact second input request through the unchanged engine. All 16,384 actual bytes are copied from live physical RAM. The original driver receives and acknowledges input completion, then requests a third transfer before reading the pending PCM buffer. The two-transfer guard preserves that earlier stop; **this is not a PCM-consumption or audio-playback pass**.

The stop is the unretired third `+100c=1` store at PC `c0278130`, LR `c0274874`, cycle 9,873,362,515: 127,930 retired instructions after first publication, and 115,094 after second admission. The third staged address is `14ae1000`, count 4,096 words. Its bytes are not captured; no stream offset is assumed.

The original first-publication audit passes. The complete trace before second admission, all eleven pre-admission artifacts and the actual second input match Stage I exactly. An additional independent final audit verifies the two real input captures equal the original stream's first 32,768 bytes, and the entire owned queue equals those bytes after the already decoded 348 bytes: 32,420 bytes, SHA-256 `68435b731a9f5c66e02369e6926ad7da26263e63bf0f2fb758ca16e57d8e14f6`.

All 180,224 SRAM bytes, the original write mask, all intentional write masks and first PCM remain unchanged. There are no recorded PCM reads, output acknowledgement, service request, ownership release or second publication. Raw events remain `0x600`, mask `0x2000`, input pending is acknowledged. Further input admission would not resolve why output processing has not started; the next investigation is original driver setup and interrupt/service ordering.

## Evidence

- [Actual trace](trace.log), [one-run report](run.json), [exact second-request audit](second-request-audit.json), and [final queue/state audit](boundary-audit.json).
- [Frozen source](source/probe.c), [probe diff](capture-admission.patch), [reviewed manifest](build.json), [pins](review-pins.json), and [durable inventory](provenance.json).
- [Original first kernel audit](first/upload/kernel-oracle.json) and [first input/PCM audit](first/functional-audit.json).
- [53 second-input contracts](test_second.log), [12 span checks](test_capture.log), [4,229 observer checks](test_observer.log), [20 peek checks](test_peek.log), [4,122 wrapper checks](test_service.log), and [nine auditor predicates](audit-test.log).

The single run took 4.98 seconds with fixed resource and instruction caps. No retry occurred. The four actual consumption-artifact negatives were not run because the positive consumption boundary was not reached; the listed negatives are synthetic contracts. No production, snapshot, I2S or speaker changes. No numbered milestone is claimed for this intermediate input-only boundary.

Root independently confirmed the complete final queue, SRAM/masks/PCM and zero read observations: [independent review](root-review.json). The next [source-only ordering contract](next-ordering-contract.md) identifies the original +4/mask-enable sequence and missing call/return observations, without admitting more input.
