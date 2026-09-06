# Next source/observation gate: input completion versus output startup

Stage J is preserved at the third transfer request. Do not accept more input or alter IRQ semantics based solely on that stop. This contract is source-only; no implementation/run is authorized by this file.

## Established control flow

The original code is `.context/amc-first/stage-d/apple-amc-all-dis.txt`, disassembled from the pinned original kernel. The independent source identities are retained in the prior manifests.

- `c0275e70` starts the initial input DMA at `c0275f54`, disables output mask `0x400` at `c0275f70` on its existing dependency branch, polls start-enable bit 9 at `c0275f78`, reads actual shared-header offsets, obtains the output layout, and calls session configuration at `c0276028`. The first-publication trace is inside that last session setup path.
- Its sole direct BL reference in this disassembly is `c0276198`. A successful ordinary return proceeds through `619c..61b8`, sets two driver output bookkeeping words to one, and calls `c0277284` at `61c0`. That helper writes `+4=1` through `c0277fac`, performs the original two halfword SRAM reads at `c0277168`, then enables mask `0x400` through `c02771e0`. This is a real, ordered output-start path; the model must not synthesize it.
- Other callers of `7284` exist: the small `c0272224` two-output bookkeeping method, interrupt/release paths, and unsuspend. Merely finding one caller does not prove which path must be active in this execution.
- The original IRQ handler `c0273dcc` reads masked status `b1c` and input pending `101c` separately. Nonzero masked status calls the output handler; independently, nonzero input pending is acknowledged at `c0273e3c` and produces type-zero notification at `3e54`. Type zero is not evidence that the decoder needs compressed input. The two Stage J notifications follow actual DMA completion and are observed separately from output processing.
- `c0274778` selects a real input descriptor by caller index, obtains its physical address and computes `(byteCount+3)>>2` before restarting input through `c02780f0`. Index bounds permit 0..2. The saved R6 values alone do not establish which descriptor index the caller passed or prove a three-buffer preroll policy.

## What the retained run does and does not establish

Every AMC write remains traced, so the absence of actual `+4=1` and mask-enable `0x400` before the third stop is decisive. But the first-publication handler replaces the pre-step hook set with consumption targets. Thus the absent `602c`/`619c` messages after that point cannot establish an unreturned initialization call. Current artifacts do not record post-publication call/return progress or driver flags at offsets `0x24d/0x24e`.

The declared HLE policy currently sets `service=true` at actual `+0`. DMA may copy 4 KiB at each retired-instruction boundary and can signal a complete 16 KiB transfer after four such steps, independently of pending PCM output. The current timing is an explicit functional approximation. It is plausible that early input completion permits refill work before the initialization caller enables output, but this is not a measured causal conclusion. No evidence yet supports redefining input completion as decoder-needs-input, suppressing an actual interrupt, forcing the output mask or injecting a service command.

## Smallest useful observational experiment

Keep the exact current two-transfer admission and third-request stop. Extend post-first-publication observation only to record:

1. Session return `c027602c`, helper return `c0276040/604c`, caller `c027619c`, output-start `c02761bc/61c4` and wrapper `c0277284`.
2. Entry/return through `c0276098/6274`, `c0272224`, and actual refill helper `c0274778/4878`, with full CPSR, registers, LR and cycles. Record the actual caller input index at entry, rather than infer it from unrelated later registers.
3. At bounded selected points, peek the verified driver's `0xe0`, `0x15c/160`, and `0x24d/24e` through the reviewed RAM-only observer. Preserve CPU/TLB/device state; reject/unresolved on an uninspectable span. Copy only the small relevant spans, never use them as device input.

Use a small fixed hook/record budget and the same existing guest/host caps. Require exact first075 and Stage I preadmission device/input evidence. Keep second DMA normal and reject third. No changing hardware commands or scheduling in this observational arm. If the original output-start is pending in a suspended call/context, capture the actual context and source-level block rather than guess. If it never occurs before the existing stop, report that finite observation, not proof it will never occur.

Only after this evidence should a separate contract consider a functional transfer schedule or protocol correction. Any timing policy needs explicit resource/counter/IRQ conservation and baseline/candidate evidence; input completion remains derived from the actual modeled transport, with queue backpressure and ownership retained. A full 64 KiB queue is a reason to preserve backpressure, not to fabricate additional input capacity or decoder progress.
