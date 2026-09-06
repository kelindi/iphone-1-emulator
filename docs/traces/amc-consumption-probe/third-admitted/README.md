# Third real input admitted; original driver requests a fourth before consuming PCM

One fresh isolated run admits the exact already witnessed third transfer through the unchanged engine. The actual 16,384 bytes at physical `14ae1000` uniquely match original media file offset **84,008** and raw AAC offset **32,768**: contiguous with the first two captures. The driver then wraps its refill index from 2 to **0**, requests physical `14a8b000` again, and stops at the fixed fourth-transfer guard. The fourth buffer's current contents were not captured or admitted; its reused address does not prove repeated media bytes.

This is an earlier protocol boundary, **not PCM consumption or playback**. At cycle 9,873,476,740, the fourth enable store has reached the observer but is neither committed nor retired. Total actual copied bytes are 49,152; the owned queue is exactly 48,804 original captured bytes after 348 decoded bytes. The first output remains unchanged: one PCM publication, zero successful output reads, zero coverage, no output acknowledgement, release, explicit-start entry, service wrapper, +4 write or 0x400 mask enable before the stop.

## Evidence and limits

- [One-run report](run.json): 5.34 seconds functional execution, native exit 10 at the planned cap, no timeout or retry. The 075/second/third/queue/ordering gates pass; the consumption gate remains false.
- [Actual trace](trace.log): exact first-publication counters; second/third live input admissions; real completion acknowledgements and type-0 input notifications; refill sequence 1, 2, 0; final unretired fourth request. There are seven genuinely retired ordering records and no 2224 or 7284 record.
- [Third-input provenance](third-request-audit.json): unique original-file/raw-stream search, exact complete non-observer Stage J prefix, and all eleven pre-admission artifacts equal the earlier stop. Captured SHA-256 is `d37d70971aeb96a7a26c014be3e855ca9d2aa9cc9b17430bd9323c84021cb8f5`.
- [Final ownership audit](queue-audit.json): exact queue hash `b67a7afe3dc1a7003a3293223a5828cde0aea6df1ded1a86fa63135441b1a914`, original PCM, and complete SRAM/write-mask checks. Original program/source metadata remain external oracles, never engine input.
- [Original first publication](first/functional-audit.json), [original-kernel upload](first/upload/kernel-oracle.json), [ordering records](ordering-audit.json), frozen [source/launcher pins](review-pins.json) and [build manifest](build.json).
- Three actual artifact mutations reject through those same frozen auditors: changed third-input byte, changed owned-queue byte, changed observation CPSR. [Input](negative-third-input.log), [queue](negative-queue.log), [record](negative-record.log). The four consumption mutations were not run because no consumption occurred.

Focused synthetic checks pass: 68 ordering, 53 retained second transfer, 67 third transfer, 12 capture bounds, 4,229 read observer, 20 safe peeks, 4,126 retirement/fourth-cap wrapper, 9 consumption auditor, 21 ordering auditor and 2 exact command-vector checks. The earlier Stage L negative-launcher error remains preserved; this frozen launcher completes its three reached-boundary negatives without replay.

The engine, decoder, DMA steps, IRQ timing, masks and ownership rules are unchanged. The observer permits refill index 3 to reach a potential fourth-request guard, but this real driver instead wraps to index 0. No additional admission follows this result. The next investigation must explain actual explicit-start scheduling or protocol backpressure without assuming more input is sufficient. No production, snapshot, browser, I2S or speaker change. Original program/media/PCM binaries remain private local artifacts and are excluded from this bundle.


## Startup source resolved

The [original MeCCA source path](startup-source/README.md) now identifies direct initialization → output information → explicit start. IOAudio2 selector 1 is a separate interface. The proposed next observation follows actual user/kernel returns while keeping this same fourth-request stop and all device behavior unchanged.
