# Stage V proposal: actual BSU lock and partial-input pause

Source-only proposal following the single preserved Stage U run. No Stage V model/build/guest exists. Preserve U's first outcome and artifact-only auditor correction; no replay solely to add missing log fields.

## What the actual request means

The stop is the original STR at c0277ce8, value1 to AMC+400, LR c0271ac4. c0277cd0 writes1, reads+400, treats nonzero as acquired, and retries with actual original delay calls until a finite timeout. c0277d58 writes0 and tests for zero. The original assertions in c0271ab0 explicitly call this `ode_bsu_lock()` / `ode_bsu_unlock()` and describe saving BSU state for AMC power gating. This establishes a lock lifecycle; it is not a PCM-ready request or an I2S FIFO.

Original current caller c0271ab0, after successful lock:

1. c0271ad0 calls c0278204, which writes0 to+1000, then0 to+a00 and returns1. The original diagnostic describes failure here as inability to lock DMA.
2. c0271ae8 calls c027804c. It reads actual +1010/current BYTE address, +1004/start and +1008/word count; stores current into driver+1c4 and remaining words into+1c8 using `count-((current-start)>>2)`.
3. c0271af4 calls c0277ddc to save BSU state: reads seven words+410..428 into caller memory, reads channel0+188 as a word count, drains+47c into the saved structure and reads+404 as an error/status result. Zero result is success; nonzero triggers the original error-recovery/restage/unlock path.
4. On success, later original control invokes provider methods to gate the engine. A resume path restores channel/FIFO/seven-word state through original MMIO. None of these later operations is already supported by the current isolated engine.

Actual U's last two c027251c entries carry r2=1, contrasting earlier r2=0 ownership returns. The original release routine marks each software buffer state3; when both states are3 and its configured mode is3, it calls c0271ab0. This connects the source to the observed save request, without claiming that every intermediate instruction or the provider mode field was separately observed.

Original userspace supplies a grounded explanation for that flag: MainMixerChannel calls getFillStatusAfterWrite at31227688, selects its boolean when the result is1/2 and other real stream conditions permit, writes samples, and passes the boolean through MainChannelBufferManager::releaseProcessBuffer, MediaManager::releaseBufferOwnership, CodecManager and an actual codec vtable dispatch. The actual ring at U stop has reader00383000, writer003bc000, fill0x39000 and high threshold0x38000. This is consistent with preroll/backpressure requesting a suspend; the exact userspace threshold branch and codec vtable target were not recorded, so that last attribution remains an inference. Do not call it an observed AudioDeviceStart.

## Smallest defensible implemented transition

Add an isolated functional BSU lock and input-pause policy, with real retained resources. Acquiring the lock at an instruction boundary freezes further decoder probes/commits/publications; it retains the actual FAAD history, owned queue, published PCM, ownership, pending events and service request. There is no asynchronous decoder work at a guest MMIO boundary in this single-thread model. Therefore a synchronous acquisition can be explicitly labeled an HLE approximation, and +400 read1 can report the lock actually acquired. It is not a ready mask without a resource transition.

The engine's input DMA remains a distinct resource. Only after the actual original +1000=0 request is accepted under that lock does its future DMA-copy work stop. Keep `dma_active`/remaining ownership distinct from a new `dma_paused` state: a pause must not manufacture transfer completion, acknowledge a latch, erase remaining bytes or restage RAM. The following actual+a00=0 disables transport while retaining the same interrupted transfer. IRQ masks/acknowledgments remain independent. +1010/+1004/+1008 report the actual paused transfer; current at the U boundary is14ae1568,346 words copied,3,750 words remaining, with1,384 bytes of input6 actually owned.

Reject wrong width/value/order, an unsupported lock lifecycle, active restaging, reset, or physical power change before mutation. A duplicate acquire may be an idempotent already-held lock, preserving every counter/event. A release is only supported when its declared lifecycle has a complete valid resource state; this first prefix does not need to unlock or invent a successful save. Unknown BSU status/register/FIFO requests still stop before their callback returns a value.

## One bounded next positive boundary

After focused tests and frozen pins, preserve all fresh U evidence through the actual +400 write, then allow this real lock and DMA-pause sequence. Stop before the first unmodeled BSU save-state read, predicted+410 at c0277dfc. Capture the actual paused engine/queue/codec counters, readback address/count and original driver save-buffer destination with a RAM-only observational peek. Capture hook availability and actual instruction retirement; never grant privileged access to a user context. The prior 53 PCM publications/51 releases/actual successful reads must remain exact, and no new copied/decoded/PCM bytes may appear after their corresponding locks.

Same64/8,155.3M total, existing5M post-input/publication/post083,240s wall180s CPU1GiB8MiB/file bounds. No publication/input cap expansion. First unexpected access, power operation, new input admission or FIFO remains the earliest stop. One run, no automatic retry. No synthesized completion or error return to steer the caller.

Focused tests: lock freezes actual decoder history/probe count/output masks and queue; DMA can remain separate until actual pause; pause conserves current/remaining/owned bytes without input completion; repeated locked/paused steps are byte/counter-identical; invalid ordering/restaging rejects atomically; readback correct for partial346/4096 transfer; independent actual artifacts reconstruct all prior generations and queue; altered pause/current/count/copy/owner receipts reject. A retained full-corpus oracle remains separate from guest proof.

## Remaining full suspend/resume contract

A successful BSU save must represent actual state, not return seven zeros and zero status to escape the caller. The seven register meanings and the exact FIFO/reservoir representation are not yet recovered. The model's65,534-byte heap queue cannot simply be reported as the BSU+188 word count: the original saved object is only64 bytes, and its restore routine also reads/writes the same seven words and a small FIFO. That would overrun the original caller and conflate decoder-input ownership with a hardware bit reservoir.

A declared HLE saved-context policy may be feasible because the driver mostly transports the register words opaquely. It would need a bounded, generation-bound retained codec/queue context and exact original save/restore sequencing, with integrity/lifetime checks, no regeneration from the media oracle, no hidden reset and no fake portable snapshot claim. Before implementing it, independently check all seven-word consumers, the save variant's first FIFO store at saved+18 versus the other variant's+1c, count bounds, reset/power order, and what the driver can legitimately modify before restore. The current decoder destroy-on+0c and blanket rejection of active power loss cannot be reused unchanged for a real suspend. This remains a separate source contract; it is not permission to bypass the observed lock with success.

## Evidence

Original kernel SHA25642e9f3c18d4a4d5c956acdcded0c3f8f247fe3e7fc96d14cfa3aedcd77c292c9; original MeCCA15f8f049239e64f88660aeb1ce99aa07273ccd19ca636e624ff19e9a68e7f4ec. Adjacent bounded disassemblies are regenerated from those files in this directory. Complete original reference remains stage-d/apple-amc-all-dis.txt. Stage U actual evidence is run-1/trace.log, stream.json, stream-audit.json, artifact-v2/report.json and transport-stop.json. No clock rate is inferred from the software48000 field; no I2S or audible output is claimed.
