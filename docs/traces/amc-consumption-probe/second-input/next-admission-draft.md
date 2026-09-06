# Conditional next gate: admit only the actual second input

This draft depends on the pending capture-only run. It does not authorize a guest run or presume the second transfer's bytes continue the original stream.

If that run establishes actual original-file/raw-stream provenance and preserves the exact earlier boundary, the existing engine already supports the observed second staging sequence: idle pending acknowledgement, +1000=1, physical byte address at +1004, word count at +1008, then +100c=1. The current probe's first-transfer-only guard is the barrier; no new engine register semantics appear necessary.

An isolated follow-up could count accepted transfers, preserve the first 075 audits, and accept at most two actual public DMA requests through the unchanged engine. At the second request the engine should be idle with input pending acknowledged, 16,036 bytes already owned, 348 bytes decoded, and first PCM still owned by the guest. Another 16,384 bytes fit the existing 65,536-byte queue. Admission resets current-transfer copiedWords to zero and sets dmaActive; cumulative copiedBytes stays unchanged until each real copy occurs. +1010 remains the actual byte address plus four times current copiedWords.

Normal per-retired-instruction service must copy at most 4,096 bytes per step from actual physical RAM. The pending output event blocks further decode/publication but does not block real input copying; completion must arise only after all requested words are copied. No oracle bytes may enter this path. Queue audit should compare captured DMA bytes in actual transfer order, minus independently validated decoded bytes. A gap or overlap in source media must remain visible, not repaired by host software.

Keep Stage H's genuine IRQ/ack, successful PCM-read coverage, copy/IPC handoff and owner-store-retirement gates. Stop before any third transfer admission or potentially third publication work, preserving the first unsupported witness. Preserve the 5 million post-publication instruction cap and fixed host resource limits. Do not suppress normal input IRQs to make output handling occur first.

Focused contracts before a new guest run: second admission has no queue-copy side effect; each bounded copy advances physical byte/current-word/cumulative-byte counters correctly; a full queue backpressures without input completion or ownership overwrite; pending output prevents decode while allowing bounded DMA; active restaging/disable rejects without mutation; final ack is independent. Confirm first and second output remain valid if the original host releases or requests more data before consuming PCM.

This would test original input refill and possible PCM consumption. It would not establish I2S, a speaker clock, audio continuity or production compatibility. New source and final bounds require root review before execution.
