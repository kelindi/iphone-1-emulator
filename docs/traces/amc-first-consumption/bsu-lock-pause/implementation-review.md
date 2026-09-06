# Source-only implementation review notes

No source model changes, compilation or guest runs are included here. These notes refine the reviewed proposal while the CPU benchmark is quiet.

## Minimal state and placement

Two private engine booleans are sufficient for this prefix: `bsu_locked` and `dma_paused`. They are not production SoC state and do not enter snapshot serialization. Initialization/destruction resets them; active hardware reset and power loss remain rejected by the probe before mutation. Do not interpret `dma_paused` as `dma_active=false`: the latter currently controls ownership and completion.

Admission precedes the existing active-DMA staging rejection, but follows actual dependency/resource validation. Proposed exact values are +400 write1, +1000 write0 while locked, then+a00 write0 while both locked and paused. Reject all other newly encountered values/widths/order, including unlock0 in this first scope. Duplicate1/0 requests within the admitted phase are idempotent. Independent existing interrupt masks/acks must retain their normal behavior.

Acquisition must validate the actual initialized/configured decoder, queue, SRAM/header ownership, current/count bounds and queue conservation. Store only `bsu_locked=true`; do not clear service, events, pending input, masks, samples, queue or FAAD history. The helper owns no asynchronous decoder work, so synchronous lock acquisition is an explicit functional policy. Subsequent +400 read returns the actual lock state, with active-resource checks. No standalone fabricated success value.

In the engine step, execute the original DMA-copy block when active **and not paused**. After that block, return WAIT before decoder probe/decode whenever BSU is locked. This ordering preserves the independently controlled input DMA until the real lock helper's later writes pause it. +1000=0 sets only `dma_paused`; +a00=0 sets only existing `transport_enabled=false`. No current or remaining word count changes. DMA readbacks retain their existing physical-byte semantics.

The actual-copy observer also needs a narrow matching change: `copy_prepare` returns a zero-byte prospective record for a paused DMA. Otherwise its old `dma_active` predicate could predict a copy that the modeled pause correctly prevents. The old Stage U prefix has both flags false, so every previous byte/counter/trace remains unchanged. This observer update does not silently delete a copy: focused tests and the final cumulative-counter/queue oracle must verify that no bytes were copied while paused.

## Receipts and earliest stop

Use the existing actual one-instruction `guest_step` receipt for the three admitted stores, not callback+1 guesses. Save pre-step PC/CPSR/cycles, actual mapped instruction bytes, callback address/width/value/cycles, returned `emu_run`, next PC/CPSR and after-state. The first lock is at c0277ce8 and predicted pause writes at c0278218/c0278220; reads at c0277cf0 and c0278060/64/68 remain ordinary original execution.

Freeze the actual Stage U prefix **before accepting the lock**. This means its last request line and pre-mutation SRAM/queue/register/input/counter state, not a forged `STOP` line during the continuation. Existing U's STOP was observational and its STR was unretired. Compare newly emitted prefix artifacts and trace up to that request; retain a separate report of new actual store retirement.

The first unsupported save-register read (+410 at c0277dfc predicted) stops before returning a value. At that point safely inspect the actual driver destination (`r0` initially driver+184) and original saved current/remaining fields, using live SVC permissions and RAM-only reads on a CPU copy. Keep absent fields explicitly unavailable. No guest code or extra hook is needed to manufacture a caller path. Unexpected earlier register/power/IRQ/control accesses remain the preserved first outcome.

## Focused test matrix to run only after release

1. With free owned input capacity and a serviceable output slot, acquire BSU lock; repeated steps may copy genuine RAM but never call codec probe/decode or publish. Verify all actual copied-byte ledger entries and unchanged output bytes/ownership.
2. Pause active partial DMA, disable transport, then repeat steps: entire engine/queue/SRAM/codec test-state and pending-event counters stay fixed. Existing physical current/start/count reads equal exact prior values; remaining count conserves the untransferred bytes.
3. Show the two controls are independent: lock alone cannot hide real DMA copies; pause alone is rejected without lock. A historical `service=true` does not make locked decode progress.
4. Reject malformed width/value/order, unlock in this bounded scope, active restaging, bad source span/count, missing resource, invalid owner/header and inconsistent queue totals before mutation. Retain existing masks/acks as independent operations.
5. Duplicate acquisition/pause/transport-off writes do not consume bytes, regenerate PCM, signal completion, reset epoch, or mutate history.
6. Wrapper retirement negatives: wrong next PC, CPSR/Thumb change, IRQ diversion, still-active callback and wrong opcode. Check actual mapped original instruction independently.
7. External actual-artifact negatives: changed lock/pause receipt, current/remaining word count, queue/input bytes, PCM generation or original owner receipt. Reuse the frozen U successor auditor plus the exact U prefix comparison. No guest retry if a new path is encountered.

## Concrete later-state hazard

The original saved BSU object spans driver+184 to before driver+1c4:64 bytes. Seven register words occupy offsets0..18; +3c carries a count. The actual save variant c0277ddc resets its output pointer and begins FIFO stores at+18, while another state helper starts at+1c. Do not generalize one layout over the other or invent a7-word identity token without checking this overlap and the real restore consumer. The large65,534-byte input queue is not the same object and cannot become +188's reported word count.

Original power-save success also copies16KiB and32KiB through driver fields+1d0/+1d4 before provider gating. The driver restore reverses those copies, then locks BSU/DMA, restores the small state, restages the remaining current/count, and unlocks. Those48KiB copies are source-proven; their live source mappings were not captured at U. They are not evidence that the current host decoder/queue already has a portable snapshot. Full lifecycle retention, reset distinction, original FIFO/register encoding and resumption remain separate work.
