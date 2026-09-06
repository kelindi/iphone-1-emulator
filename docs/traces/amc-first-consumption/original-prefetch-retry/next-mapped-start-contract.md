# Stage AB proposal: execute original mapped StartIO and observe the real HAL callback

Source-only proposal after accepted Stage AA. No implementation, tests, guest or HAL admission is authorized by this document. Existing production, snapshot and isolated device models remain unchanged.

## Correct original function identities

The AA retry at `31236650` is `MeccaCoreAudioDevice::StartIO`, not the output callback itself. Original MeCCA SHA256 `15f8f049239e64f88660aeb1ce99aa07273ccd19ca636e624ff19e9a68e7f4ec`; disassembly `.context/amc-first/downstream/coreaudio-start-dis.txt` and independently parsed original Mach-O `.context/amc-first/stage-m/MeCCA.original` establish:

- StartIO's ARM entry pushes `{r4,r5,r7,lr}`. It uses actual object `r0`, device ID at object+8 and installed flag at object+0x58. When needed, it registers actual HALOutputProc `31236734` and clientData=this using `AudioDeviceAddIOProc` at `3123667c`.
- It calls `AudioDeviceStart` at `3123669c`. The existing hook at `312366a0` observes its real returned status R0 before `SUBS r5,r0,#0`; zero branches toward the common StartIO epilogue. That is an AudioDeviceStart return, not proof StartIO or an output callback has returned.
- HALOutputProc is `31236734`. Actual entry stack arguments are output AudioBufferList at SP+0, output timestamp pointer at SP+4 and clientData at SP+8. Its prologue later loads these from adjusted SP+0x18/0x1c/0x20. The output callback is the real function pointer at clientData+0x10. Output count derives from actual AudioBufferList byteSize+8 shifted right two, and actual data pointer is at+0x0c. This does not independently prove hardware channels, sample type/rate or clock.
- HALOutputProc's actual epilogue at `312367f8` pops `{r4,r5,r6,r7,pc}`. Saved PC is current SP+16. A successful completed successor/ISA must match that actual return word. This is the existing narrow positive stop before subsequent engine service.

No new hook is necessary. Keep the existing six transport sites and all ten consumption sites; do not displace old coverage just to add StartIO's separate return at `312366e0`. Report the available call/return evidence accurately rather than implying that unobserved return.

## Preserve AA before new execution

Start from the same original fixture, not a claimed resume of a host-pointer-containing decoder. Repeat the exact original083/Z/AA path under their immutable engine, queue, service, DMA, ownership, suspend and fetch rules. At the actual mapped retry, independently match all16 original registers, full CPSR, translation identity, actual fault receipt, current mapping and original32-byte code. Emit an `aa-prefix` using actual current resources/code/context at cycle9,874,749,020 before any new instruction; preserve the entire original AA trace before its stop and its full binary/resource artifacts. Do not copy old artifacts as current evidence.

Only after that prefix validates, change the observer's stop decision: stage the existing StartIO entry receipt and return normally so the unmodified CPU executes it. Do not advance PC/cycles, force an exception/return, clear caches, alter page tables, enable the device or install a service pointer. The earlier real ABT remains evidence, not a shortcut to a preload.

AA's live fast-fetch fields remain observed. At any newly admitted watched instruction, obtain the actual32-byte READ/FETCH code under current and private fresh translations. If a live fast-fetch cache hit could select different bytes, require its pointer to correspond to the same bounded normal-RAM span before releasing that watched step. Checking uses pointer equality/range derived from actual RAM without exporting/dereferencing an unvalidated host pointer, and never clears the cache. Otherwise stop before the instruction. Copied/fresh walk success alone cannot bind an unrelated stale live fetch cache.

## Actual context, faults and instruction receipts

The accepted StartIO retry fixes an invocation identity: actual translation tuple SCTLR/TTBR0/TTBR1/TTBCR/DACR/contextID, original this pointer, initial SP/LR and full entry registers/CPSR. Every watched instruction must be ARM/USR with actual original bytes and the same relevant address-space identity; capture the complete current registers and CP15/fault context. A matching library PC in another task is not this invocation. Refuse before executing any watched mapped instruction with wrong code/address-space/ISA.

For the existing AudioDeviceStart return, correlate actual R4=this and the original StartIO frame/SP/R7 against the retired entry, using mapped user stack only. The entry stack push must actually complete before declaring StartIO entered. Record real R0 and the SUBS successor/derived flags; never assign a successful status. If the first actual status is nonzero, stop after its returned instruction receipt and report the real error boundary. Do not infer API completion from merely reaching its pre-step PC.

A HAL callback may run on a different legitimate thread in the same address space. Do not require its entire register/SP context to equal the earlier StartIO invocation. Bind original code plus the same actual clientData object, and retain that callback's own full CPSR/registers/stack/context. Preserve nullable input buffers. Optional object/output/timestamp fields must have individual availability; failed peeks do not become zero-valued guest fields. No privileged reads or relative-pointer guesses. Retain actual byteSize/data/channel fields as data, without claiming an I2S format from them.

Before each watched instruction, retain a real active `guest_step` and explicit IRQ/FIQ/fault state. After `emu_run(1)` returns outside the guest, validate actual cycle+1, opcode, full CPSR, successor and side effects required by the original instruction. A push needs its original SP decrement and actual saved words; a POP needs its actual read frame, final SP, register restores, return PC/Thumb bit and flags. Instruction-fetch/data-abort or IRQ/FIQ diversion must be recorded separately with the original real exception context and must not count as entry/return. Never turn a faulted POP into a successful return based on saved stack bytes alone.

Allow ordinary original CPU/kernel fault and context-switch handling between observed instructions. Do not add broad handling for new unbound target pages: the AA allowance remains limited to its exact original target and context. At another watched site with unavailable or conflicting code, stop before admission and retain the actual diagnosis. In particular, no synthetic fault or page mapping is introduced to reach a callback. A natural retry of an interrupted watched instruction can produce another bounded staged record; it becomes retired only when its real successor/ISA and required effects match.

## Earliest stop and unchanged resources

Stop at the earliest of:

1. First verified HALOutputProc return, immediately after `emu_run(1)` and before any next engine service/instruction. Capture actual returned R0, mapped saved frame, full context, output metadata/availability and current plain I2S/DMAC/clock/codec state. No DMA-ring/PCM ownership is inferred from an AudioBufferList pointer alone.
2. Actual nonzero AudioDeviceStart status after its instruction receipt, or watched code/context/retirement mismatch. Preserve the precise status or exception; no automatic retry.
3. Existing unsupported suspended AMC/SRAM access, clock/power operation, reset/ungate/restore request or FIFO access. Reject before forwarding/mutation. Existing I2S FIFO callback-origin uncertainty remains explicit; no CPU-versus-DMA origin is fabricated.
4. Existing64-publication/eight-input, original155.3M total/five-million earlier continuation and AA one-million-after-Z/eight-target-attempt/128-observer-record caps. Same240s wall/180s CPU/1GiB/8MiB-file limits. No cap expansion, fabricated owner return or new input admission.

Preserve actual suspended resource ownership and unchanged queue/PCM/SRAM throughout, even if normal unrelated devices advance. Do not assert every hardware register is constant while allowing real guest work. Captured controller dependency/resource invariants are the acceptance gate; any actual unsupported change stops first.

## Focused tests and independent audit before any one-run authorization

- Entry PUSH and HAL return POP fixtures exercise the real pinned interpreter, actual user permissions and normal RAM; verify pre/post cycles, saved/restored words, SP, returned full CPSR/ISA and actual `emu_run` completion. Failed/late stack translations, changed mode, wrong saved frame/return, IRQ/FIQ diversion and wrong successor cannot be mislabeled retired.
- Actual code+translation binding rejects wrong address space, mismatched original window, stale live fetch hit pointing elsewhere and partially unavailable diagnostics without CPU/RAM/cache/device mutation.
- Original expected windows/return opcodes and saved-slot offsets derive independently from the pinned Mach-O; no expected field comes from what the candidate happened to produce.
- Run all retained native/decoder/resource/observer contracts. Freeze exact source/launcher/auditors/library and fixture pins. Only after root review may one separately named guest run occur; preserve its first outcome and do not replay automatically.
- The frozen external auditor must validate current `aa-prefix` through all earlier083/AA proofs, distinguish actual StartIO entry and AudioDeviceStart status from actual HAL callback/return, and check callback identity, returned stack/register effects and resource conservation. Actual artifact negatives must corrupt code, address space, available field, saved return/SP, completed CPSR/successor and suspended queue, invoking the same frozen auditor rather than testing simple inequality.

A verified callback return would establish original userspace output flow only. Actual hardware buffer/DMA transport, codec timing, sample clock and audible browser output remain separate. The isolated GPL decoder limitation remains unchanged.
