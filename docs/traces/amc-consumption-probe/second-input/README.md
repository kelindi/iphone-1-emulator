# Original second input bytes, observed before admission

The one reviewed capture-only run passes. The actual staged physical address `14ac1000` contains **16,384 bytes matching original media file offset 67,624 and independently reconstructed AAC stream offset 16,384**. Both matches are unique. Thus the requested data immediately follows the first transfer, with zero gap or overlap; this is measured provenance, not an assumed offset.

Captured SHA-256: `7205c99a864329de5e1f14308d6aed889f7617535d6385220ce1d5c939c871ae`.

Removing the two new `CAPTURE` lines reproduces the entire preserved Stage H trace byte for byte. All eleven checked final artifacts—including SRAM, write masks, queued input, PCM, registers and empty read observations—match Stage H exactly. The original first-publication kernel/input/PCM oracles pass again. A cloned artifact set with one actual captured byte corrupted is rejected by the same frozen external auditor.

The stop remains the unaccepted, unretired second transfer-enable store at `c0278130`, cycle 9,873,247,421, exactly 12,836 retired instructions after first publication. The engine is idle for input, pending input is acknowledged, and 16,036 previously owned bytes remain queued. The first PCM event is still pending. **No second DMA admission, PCM consumption, ownership return or speaker output occurred.** Exit 10 is the expected observational rejection; the run report separately marks capture success and consumption failure.

## Evidence

- [Capture audit](capture-audit.json), [actual trace](trace.log), [single-run report](run.json), and [actual corrupted-input rejection](negative-captured-byte.log).
- [Frozen external auditor](audit.py) and [independent original-file MP4 parser](mp4_oracle.py). Neither supplies bytes to the device.
- [Tiny probe patch](capture.patch), [source/build manifest](build.json), [reviewed pins](review-pins.json), and [local provenance inventory](provenance.json).
- [Original kernel/program audit](first/upload/kernel-oracle.json) and [first input/PCM audit](first/functional-audit.json).

The single run took 4.98 seconds under fixed 240-second wall and 180-second CPU caps. This is functional evidence, not a speed measurement. The bounds helper passes 12 checks; all 4,229 observer, 20 peek, and 4,122 retirement/refill wrapper checks remain passing. No repeat occurred. Original media, program and decoded PCM binaries remain private local evidence, not part of this documentation bundle.

The [next admission proposal](next-admission-draft.md) retains existing queue capacity, actual physical DMA, pending-event and ownership rules. It needs separate source review before another guest run. Production and snapshot formats remain unchanged.

Root independently confirmed unique original-file offset, exact continuity, all eleven unchanged state artifacts and the complete trace match: [independent review](root-review.json).
