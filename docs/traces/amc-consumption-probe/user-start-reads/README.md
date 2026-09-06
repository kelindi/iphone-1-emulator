# Real userspace startup and all first-buffer PCM reads

One isolated Stage P run corrects only the observer's user/kernel permission assumption. Real userspace keeps its original permissions; kernel owner fields are marked unavailable there, never inspected with elevated privilege or invented. The native contrast reproduces the old rejection and verifies the correction with a protected kernel mapping. Exact Stage O state and trace before its rejected user hook are preserved.

The actual original scalar-return instruction at3123f7f0 now validates against its mapped32-byte original image and retires. R0=0, scalar count4 and outputs `[0xf220,8192,0x11220,8192]`. Original initialization/output-info wrappers both return0; actual method3 BL and original kernel start2224 retire. Original code enables output processing, acknowledges its output event, requests service and sends a genuine type1/index0/count2048 IPC notification.

The original userspace reader then successfully reads every byte of the first4,096-byte PCM output through1,024 real32-bit reads at PCs3009a20c/3009a218. The frozen read-only auditor verifies actual returned bytes and full coverage exactly against independently established PCM. There are two real publications; both PCM buffers, all owned compressed input and SRAM/write masks pass their independent audits.

**The complete ownership-return gate is still not accepted.** The original0→1 owner write occurs atc02726a8, then the probe stops with `release store not retired`. The old guard assumes post-step cycles equal the MMIO callback's cycles+1; actual ARM interpreter code increments cycles before executing the store, so the callback and post-step both report9,873,752,634 while final PC is26ac. This is a concrete observer accounting problem. The run remains a preserved exit10; no guest retry or retrospective successful-run label. The next guard must bind actual instruction-start state and completed successor/mode rather than add an extra cycle after a callback. Its inherited full-consumption auditor also needs to replace an old three-input formula with the already audited actual five-input copy ledger before a future final gate.

## Evidence

- [Single5.14-second run](run.json), [complete trace](trace.log), [actual six retired startup records](ordering-audit.json), [all successful PCM reads](read-only-audit.json).
- [Actual capture/copy ownership](later-audit.json), [complete queue and both PCM buffers](queue-audit.json), [exact first075](first/functional-audit.json), [original upload](first/upload/kernel-oracle.json).
- [Correction diff](source.patch), [frozen pins](review-pins.json), [build](build.json), [original protected-kernel rejection](test_permissions_old.log) and [corrected permission checks](test_permissions.log).

Nine real artifact mutations reject through frozen auditors: third/fourth/fifth inputs, actual copied bytes, DMA current address, queue, ordering register, mapped code and fabricated available kernel fields. Complete consumption-only negatives were not run because its positive retirement gate did not pass. The separate read-only result invokes the existing frozen byte auditor against saved evidence only; it neither changes the gate nor executes the guest again.

No production/snapshot change, I2S output, sample-clock or audible playback claim. Full private media, PCM, RAM and guest binaries remain local under `.context/amc-first/stage-p/run-1`. This source/trace update has no visual recording because it is a nonvisual experimental device boundary.
