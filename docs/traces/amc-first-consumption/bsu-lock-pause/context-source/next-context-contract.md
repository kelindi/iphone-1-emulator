# Stage W proposal: owned BSU context, beginning with a real save

Status: source analysis and proposal only. No register implementation, native build, guest execution, new input admission or PCM publication occurred for W. Stage V remains the last actual boundary: the original instruction requests `+0x410`, and no value is returned.

## Original consumers and corrected layout

The primary source is the original kernel SHA-256 `42e9f3c18d4a4d5c956acdcded0c3f8f247fe3e7fc96d14cfa3aedcd77c292c9`, disassembled in `../stage-d/apple-amc-all-dis.txt`. `context-source-audit.py/.json` pins the relevant instruction windows and inventories literal references; the inventory is not a proof of global unreachability.

| Device or saved field | Original software operation | Bounded interpretation |
|---|---|---|
| AMC `410,414,418,41c,420,424,428` | Seven consecutive reads at `c0277dec..7e08`; stores unchanged to saved offsets `0..18`. Restore `c0277a64..7a7c` writes those seven unchanged words back. Swap and alternate save/restore helpers perform the same copies. | Opaque within these reviewed host consumers; individual silicon meanings remain unknown. No arithmetic or field decoding of these seven values was found in the reviewed AMC driver. |
| Channel 0 `+188` | `c0277e10..7e20` gets word count; count saved at `+3c`. Alternate helpers poll count while draining. | Small hardware bitstream FIFO count, separate from our 64 KiB compressed-input queue. Software storage permits at most eight words. |
| `+47c` | Save loops pop FIFO data. | Only an actually queued FIFO word could be returned. Empty pop must reject, not invent zero. |
| Channel 0 `+100` | Restore pushes saved FIFO words through `c0277150`. | Bounded actual word queue; reject overflow before mutation. |
| Channel 0 `+180=1` | `c027711c` resets FIFO before restore. | Clears only the modeled small FIFO; does not discard the retained compressed queue or decoder history. |
| `+404` | Every save/restore/swap helper returns error status; caller branches on zero/nonzero. | Success must derive from a completed valid context transaction. Invalid order/content stops before acceptance; no unconditional success register. |
| Saved `+1c..38`, `+3c` | Eight FIFO words plus count, following the seven words. | Entire software context is 64 bytes. |

**Correction to the frozen U/V source proposals:** the instruction operand `[pointer,#0x18]` does not imply an effective first FIFO word at saved `+18`. Save and restore branch to the condition block first; that block increments the pointer before the first body execution. Both effective first accesses are `+1c`, then `+20..38`. No seventh-word/FIFO overlap exists. The static arithmetic audit checks counts 0 through 9; the ninth word would reach and overwrite count `+3c`. Frozen earlier proposals remain unchanged as provenance.

## Actual resources and lifetime paths

`fixture-provider.py/.json` extracts selected bytes from the original pre-ringtone checkpoint without loading or running the emulator. Its page-table walk is a bounded static RAM extractor, not proof of a successful guest access or current object lifetime. Original driver initialization `c027575c..5770` independently proves the SRAM-relative offsets below, so they are not inferred only from checkpoint pointers.

* The driver saves 16 KiB from SRAM `+28000` (`22028000`) to kernel `c02b6334`, and 32 KiB from SRAM `+14000` (`22014000`) to `c02ae334`, at `c0271b00..1b20`. These are actual SRAM copies, not the host queue or FAAD heap. Restore `c0271bf4..1c14` reverses them.
* Both final output buffers are still owned by the guest at V. The original pause branch `c02725c0` saves state, then requests power off at `25d4`, clocks off at `25ec`, and enters decoding state 5. Neither final buffer has an observed owner release; do not synthesize one.
* Actual checkpoint provider `cc9efa00` maps clock IDs 3 and 4, power ID `0x10004`. Original dispatch resolves through `c015b7dc/c015b80c`, `c040b53c/c040b5d0`, and `c0410870/c0412170` into `c0410270/c0411c50`. Clock disable sets bits 3/4 in the gate shadow and writes offset `48`; power disable requests bit 4 via offset `10`, then polls offset `14`. The power ID high half is a client-ownership mask: hardware disable also depends on other clients, so a provider call alone is not proof the gate changed. Current probe must observe actual MMIO.
* Resume `c0276448` accepts suspended/power-gated states, calls `c02746a8` (clocks on before power on if needed), and for state 5 calls **`c027628c` before `c0271be8`**. That routine invokes `c0271870(1,1)` (core and DE reset functions), platform configure, task-manager slot 8 start, masks/transport setup, startup-status poll and session setup. Only then are SRAM and the BSU context restored. This precludes a simplistic rule that every reset destroys the only saved generation.
* Restore then restages the actual saved physical byte address and remaining word count through `c0278084`, unlocks BSU via `c0277d58`, and finally writes both owner halfwords to 1 at `c02764cc/64d4`, sets driver state 3, and issues normal service `+4` via `c0277284`. Those writes must remain genuine guest operations.
* Other lifetimes are explicit exclusions until supported: full stop/close around `c02743cc..4410`, provider/interrupt teardown `c027443c..4538`, non-power-gated suspension, another codec/program selection, arbitrary FIFO clients, context swap between distinct sessions, process exit, machine reset, and portable checkpoint reload. The reviewed alternate helpers are accounted for as rejecting paths, not claimed absent.

## Proposed functional representation

This is a declared same-process HLE policy, not recovered silicon register meanings. A saved resource owns the actual decoder, compressed queue and counters, pending transfer and address, output ownership/events, session identity and actual SRAM state. Its identifier is a monotonic engine-lifetime generation, never a host pointer. At most one retained generation is permitted; no unbounded registry or lookup through guest memory.

A candidate seven-word encoding is `(policy-version, generation-low, generation-high, decoded-bytes-low, decoded-bytes-high, copied-bytes-low, copied-bytes-high)`. Each value derives from this resource and its identity. High words may legitimately be zero; these are not placeholder hardware-register zeros. Full queue/counter/header/PCM/decoder ownership still stays in the actual resource; the seven words cannot serialize it. Restore must compare all seven words and the full retained resource invariants before binding it. Generation exhaustion rejects, rather than reusing a stale identifier.

The adapter decodes one complete raw AAC block synchronously. Pinned FAAD `libfaad/syntax.c` ends the supported non-DRM raw block with `faad_byte_align`, and `decoder.c` computes consumed bytes and ends the local bit reader before returning. A supported lock is therefore between complete frame calls, with no in-flight host bit reader. The proposed small BSU FIFO can be *derived empty at this particular boundary*; that does not mean compressed input is empty. A future adapter contract must expose and test this boundary explicitly, including failed/incomplete probe paths, before returning count zero. Nonempty small-FIFO semantics remain bounded and unsupported in the first save gate.

Control lifetime and saved-resource lifetime must be separate. A later, reviewed suspension transition may retain this owned generation while actual power/reset clears live hardware control. It must not permit decode, DMA progress, output IRQ generation, or automatic ownership changes while gated. Fresh program initialization may create valid temporary initialization resources, but must neither overwrite the saved generation nor claim it already restored. Old tokens from a different engine/session/process cannot bind. This is deliberately **not yet a complete reset/restart implementation contract**: the exact task-manager restart writes, new header overlap and temporary decoder allocation bounds must be resolved before accepting that phase. No generic reset exemption or reusable token is authorized by this proposal.

## Smallest next experiment proposed for review

Implement only a transactional save view of the current locked, paused, initialized resource. Admit the seven bounded reads, the independently derived empty small-FIFO count, and success only after all seven words and the count were genuinely returned in the original transaction. No restore, reset, unlock, dependency loss or new FIFO data behavior is admitted. Then let the original driver copy its two actual SRAM spans and **stop before the first actual unsupported clock/power dependency change**. This establishes an honest saved-generation identity and actual Apple SRAM save, while preserving the unresolved restart boundary.

Require the fresh V prefix byte/counter/ownership evidence unchanged before its original `+410` request. Preserve all 083/R/S/T/U/V auditors and bounds: 64 publications, eight actual inputs, 5M post-083 /155.3M total instructions, 240s wall/180s CPU/1GiB memory/8MiB log cap. No additional admission solely to reach an expected call. Stop any unexpected register, SRAM write, context transaction, third-party session, empty FIFO pop, resource drift, output overwrite or earlier existing limit before mutation.

Positive audit: original mapped save opcodes and actual read ledger match all seven resource-derived values; actual kernel saved block contains those exact bytes and FIFO count zero; actual 48 KiB saved copies equal the frozen live SRAM spans; actual pause/current/remaining and full queue/PCM/owners/events remain unchanged; final dependency request records full CPSR/PC/source instruction and is not accepted. A missing SRAM copy is an earlier/unresolved result, not patched by the auditor.

Focused negatives before a guest: wrong state/order/width/register; stale generation/overflow; mutated counter/queue/header; incomplete adapter frame; count/read omission; empty FIFO pop; save followed by unrelated reset or power loss; failed transaction must leave all resources untouched. Actual artifact negatives must run the same frozen auditor on modified seven-word, missing-read, changed SRAM-copy and ownership files and reject them. Freeze source/pins, one run only if root accepts this contract; preserve first outcome.

## Remaining work before a complete suspension cycle

1. Resolve actual task-manager slot 8 restart effects and whether the selected upload/session is identical on resume. The source invokes it, but V has not executed it.
2. Define temporary initialization resources, saved-generation ownership across both real resets, and exact restored SRAM/owner-header interaction without allowing a new program to reuse the old codec state.
3. Account for transport restaging while the old DMA owns its unfinished 3,750 words: same actual current byte address, no duplicate 346 words, no premature completion/IRQ, and conservation across resume.
4. Verify or explicitly stop alternate save/swap/close consumers. The reviewed host treats the seven words opaquely; whole-system computed access absence is not proven.
5. Only after those checks, propose a real resumed decode/ownership cycle and next HAL/I2S boundary. Neither this document nor an eventual save-only result is audible playback or a portable codec snapshot.
