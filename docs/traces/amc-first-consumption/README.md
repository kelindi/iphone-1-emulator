# First original-guest PCM consumption and ownership return

One isolated Stage Q run verifies the whole first-buffer handoff: original userspace scalar initialization succeeds, the original app explicitly starts AMC, the original driver enables/acknowledges output and reports a real IPC notification, userspace reads every decoded PCM byte, and the original driver returns buffer ownership. The probe stops after that store retires, before the next guest instruction or engine service. **No continuous sample-clock, I2S, browser or speaker playback is claimed.**

## Actual boundary

- All4,096 first-output PCM bytes were returned through1,024 real32-bit reads under original user CPSR60000010, at PCs3009a20c/3009a218. Read values and full coverage exactly match the independently decoded first PCM (SHA2562ce90c8f574188134ae90dc6a79e045c8e75c99c05ed28a466669d381f7ec372).
- The original user scalar call returns0/count4 with buffer offsets/capacities[f220,8192,11220,8192]. Initialization and output-info wrappers return0. Original method3 call and kernel2224 execute; the device sees actual output mask/ack/service writes and genuine type1/index0/count2048 notification.
- The actual owner0→1 store is original ARM `STRH r3,[r0]` opcodee1c030b0 atc02726a8. Recorded instruction start cycles9,873,752,633/CPSR20000013; callback and completed cycles9,873,752,634; actual completed PCc02726ac and CPSR unchanged, after emu_run returns outside guest. A separate original-kernel parser verifies the opcode. This avoids the previous wrong assumption that retirement must add another cycle after the callback.
- Two PCM buffers were published and match the full independent reference. The fifth input is still partial:169words/676bytes copied, physical current14ac12a4, active1/pending0. Total actual copied66,212bytes minus679decodedbytes equals the exact65,533-byte owned queue. Available but uncopied capture bytes are never counted as device-owned input.

## Proof and preserved failures

[Run](run.json), [full trace](trace.log), [complete consumption audit](consumption-audit.json), [actual instruction receipt and exact Stage P state](retirement-audit.json), [actual startup records](ordering-audit.json), [input/copy provenance](later-audit.json), [queue and both PCM buffers](queue-audit.json), [original first075](first/functional-audit.json), [original uploaded program](first/upload/kernel-oracle.json).

All17 final state artifacts and the entire original trace through the owner store remain byte-identical to the preceding Stage P run. Stage P's failed callback+1 gate remains [preserved](../amc-consumption-probe/user-start-reads/README.md), as do Stage O's observer permission stop and earlier caps. Stage Q adds actual missing pre/post receipt fields; it does not relabel those old runs successful. One guest replay5.48s/exit0, no timeout or retry; this is untimed functional validation, not a speed measurement.

All18 actual artifact mutations reject through copied frozen auditors: third/fourth/fifth captured input, actual copied bytes, current DMA address, queue, ordering register, mapped code, fabricated available kernel fields, missing read coverage, wrong returned bytes, owner PC/value, instruction start, completed successor, final mode, returned-call flag and instruction opcode. Full original program/coverage/explicit header/PCM/host-write masks and memory outside those masks remain checked. The read/write ledgers are built only from successfully returned source bytes and actual stores; no forced host notification, read, release or PCM.

## Source, bounds and tests

[Isolated diff](source.patch), [frozen pins](review-pins.json), [build/source hashes](build.json), and retained source/launcher/auditors. The engine, decoder, DMA cadence, queue size, IRQ masks/events, actual five admissions and two-publication cap remain unchanged. Same155.3M total/5M continuation/240s wall/180s CPU/1GiB/8MiB-per-file limits, original fresh fixture, no fake decoder snapshot/resume.

New retirement55 focused checks, actual wrapper4,126, external receipt/read15 and full-auditor5 integration checks pass alongside all retained code/permission/DMA/read tests. The5 integration checks used retained P bytes plus an explicitly synthetic receipt to test auditor coverage only; final acceptance is solely the actual Stage Q receipt and artifact controls.

This is a nonvisual hardware milestone. Full private media, PCM, RAM and runtime binaries remain in `.context/amc-first/stage-q/run-1`; this durable archive retains source, structured metadata, hashes and traces. No production/snapshot or browser audio change. Continuous sample timing, downstream PCM transport and audible output remain the next separate gates.

Root independently inspected the actual coverage, PCM equality, IPC notification and original mapped STRH/start/callback/completed context, and accepted this narrow first-buffer result. The decoder remains an isolated GPL-2.0-or-later FAAD2 experiment; [the pinned version, license and full-corpus feasibility](../aac-framing/README.md) are separate from any production integration.

The [source-only next transport plan](downstream/next-transport-contract.md) identifies the original memcpy/MeCCA queue path and current I2S/PL080 limits. Actual destination, output format and sample-clock evidence are still needed; no downstream run or direct host playback occurred.


**Actual copy destination verified:** the unchanged083 handoff now binds the original MeCCA writeSamples caller and exact4,096-byte userspace destination at physical160e4000. [Evidence, original-source oracle and explicit TTBR correction](copy-destination/README.md). This is not the hardware output ring; manager fields and downstream transport remain open.


**Actual transport census:** the original256KiB software ring and48000 format field are captured, while I2S/DMAC0 remain untouched through the same first-consumption boundary. Earlier codec writes are retained explicitly. [Evidence and27 actual artifact negatives](transport-census/README.md). No downstream DMA or sound claim.


**Next real service request:**12 instructions after the unchanged083 release, the guest issues+4 and reaches the diagnostic third-publication cap. [Fresh-prefix proof and28 corruption controls](next-service/README.md). This supports a bounded continuous-ownership extension; no HAL/FIFO/sound yet.


**Audio083 follow-up — real suspension:** one Stage Y guest retires all four original power/clock writes and the driver's actual state3→5 store. One retained decoder generation owns the unchanged180,224 SRAM bytes and65,534-byte queue; active allocations/service/DMA/IRQ are cleared. All21 native targets and57 actual artifact negatives pass across the original audits and a separately preserved artifact-only correction. The first auditor's synthetic service1 assumption and final-USR field assumption remain documented; no guest retry. Execution stops at an unaccepted observer binding at31236650, so no HAL/sound claim. [Actual transitions, limits and failed/corrected evidence](power-suspension/README.md). Reset/restore and production integration remain open.


**Audio083 follow-up — observer stopped before an unmapped page:** the single Stage Z diagnostic preserves every Stage Y guest/resource byte and stop cycle. Actual L1`15c8ac48=08511001` and L2`085110d8=0` explain all four cloned READ/FETCH fault7 results; no actual guest fetch or HAL call occurred. All22 native targets and65 actual artifact negatives pass across the original proofs and an artifact-only correction using actual snapshot RAM geometry. The original auditor's synthetic-aperture failure is retained; no guest retry. [Evidence and limits](hal-binding-diagnostic/README.md). Next is the original kernel's normal fetch-fault path, not a fabricated mapping or audio event.


**Audio083 follow-up — original prefetch abort and mapped retry:** the single Stage AA run lets the unmodified CPU take the actual missing-code fault, then returns 1,636 core steps later to the same full user context with original MeCCA bytes mapped. It stops before the callback instruction. Exact fresh Z/083 prefixes, all retained SRAM/queue/ownership and suspension proofs pass; 23 native targets and 70 actual artifact negatives pass. No HAL, reset/restore or sound claim. [Evidence and bounds](original-prefetch-retry/README.md).
