# Original audio driver suspends a real saved decoder generation

> Historical recordings and publishing artifacts have been removed from Git history. The summaries below describe the original runs; remaining test reports and screenshots are retained. Historical hash receipts may predate this archival cleanup.

One isolated Stage Y guest retires the original power and clock writes and the original driver's state3→5 store. All 180,224 SRAM bytes and the 65,534-byte compressed queue remain owned by one real retained decoder generation; the active engine relinquishes its allocations, service, DMA and IRQ state. The normal guest then reaches PC `31236650`, where an existing observer refuses its binding. This is **not an accepted HAL call or audible output**.

This is a nonvisual follow-up to [083](../README.md). No production core, firmware, snapshot format or browser sound output changed. The experimental GPL FAAD decoder and its allocations remain private to this probe.

## Actual transitions

The [frozen trace](run-1/trace.log), [actual final state](run-1/suspend.json) and [artifact-only corrected audit](run-1/suspend-audit-corrected.json) establish:

| Original operation | Actual before → after | Retired guest cycle |
|---|---|---:|
| ONCTRL `39a0000c=3` | Power `12ec→12ec`; domains0/1 already ungated | 9,874,735,590 |
| OFFCTRL `39a00010=10` | Power `12ec→12fc`; AMC bit4 gated, allocations transferred | 9,874,735,608 |
| CLOCK `3c500048` bit3 | `4fffe267→4fffe26f` | 9,874,735,774 |
| CLOCK bit4 | `4fffe26f→4fffe27f` | 9,874,735,957 |
| Original `c0272640` STR | Driver `ccaa2ce0`: state3→5 | 9,874,736,040 |

Each MMIO store has actual mapped original opcode, full CPSR, pre-step cycle, in-instruction callback, post-step PC/CPSR and returned `emu_run`. Clock stores execute in SVC with IRQ disabled (`80000093`), preserved rather than normalized away. The state store uses original `e58430e0`; its successor is `c0272644`, CPSR `60000013`, with the actual RAM field read back as5 before further service.

The last verified kernel store is5. At the final stop the CPU is in USR (`60000010`), where that kernel field is unavailable; `driverState:0` with availability0 is an empty observer field, **not a guest state0 value or proof of the final kernel value**.

The old “selector” description of `39a0000c=3` was incorrect. It is an idempotent ONCTRL write here; the later OFFCTRL bit4 is the actual dependency loss. Existing synchronous power/clock controller behavior is reused. SRAM retention and ownership of the live decoder history while gated are declared functional-model policies, not measured silicon behavior or a portable codec snapshot.

## Conservation and scope

The complete actual X prefix before the first forwarded ONCTRL is retained in `power-prefix`, along with all original083/R/S/T/U/V/W/X audits. No earlier file supplies the current resource output: the probe independently dumps the live retained queue, SRAM and controls at its final boundary. These match the current run's actual pre-suspension captures byte for byte.

The resources still record53 published/read4KiB buffers,51 original owner returns,83,304 copied input bytes,17,770 decoded input bytes,65,534 queue bytes and346 of4,096 words copied from input6. The final two buffers remain owner0. No release, completion, reset, input byte, decoder call or PCM publication is manufactured while suspended. Active initialized/configured/decoder/queue/IRQ/events/mask/service/DMA flags are all zero; saved historical events are reported separately as retained state. Cleanup releases the unique saved decoder/queue exactly once.

The single guest ends at cycle9,874,747,384 after6.141 wall seconds. It keeps the64-publication/eight-input,155.3M total/five-million continuation bounds,240-second wall/180-second CPU/1GiB address-space/8MiB file caps. No retry was run. Both I2S models remain untouched; the final transport census records no audio FIFO activity.

## Tests and preserved audit failures

The [native build](build.json) passes21 targets, including [700 new suspension/atomic controller/ownership/retirement checks](test_suspend.log), the retained [7,164 full-corpus/partial-input adapter boundary checks](test_boundary.log), and saved-context/chunked-read tests. Ten original synthetic auditor controls pass before the guest.

The original [launcher report](run-1/run.json) remains `guestPass:false`. Its first suspension auditor failure expected synthetic `service=1`; the actual engine correctly clears service on publication53, with no later+4. A second overly strict assertion would have required the final USR context to read the kernel state field. These were auditor assumptions, not failed original stores. The [original failed auditor/log](run-1/suspend-audit.log) and [failed launcher output](launch-run-1.log) are preserved unchanged.

A [separately retained artifact-only auditor](postprocess-1/suspend-audit.py) checks actual last-publication/+4 history for service0 and proves the kernel store at its actual successful receipt. It explicitly reports the final field unavailable/null. [Eleven corrected synthetic controls](postprocess-1/test.log) pass. [Ten new actual artifact mutations](postprocess-1/actual-negatives.json) all reject: queue, SRAM, active-state fabrication, missing retained owner, unrelated clock bit, fabricated final kernel state, missing state-store receipt, wrong successor, wrong power transition and wrong retained service. Alongside47 earlier actual controls, this is57 rejected mutations on one guest's evidence. No guest or frozen run source changed to obtain the corrected result.

The older save-audit JSON describes its own pre-forward X boundary. Its “no power transition” scope must not be read as a claim about the later Y continuation; the final suspension result is the separate corrected report above.

## Unaccepted observation boundary and next step

The first stop is `post observer binding/bounds`, PC/address `31236650`, LR `3124dc50`, USR CPSR `60000010`, before the instruction retires. This PC is a static target for the original HAL-start wrapper, but `post_begin` never records an accepted mapped-code witness. There are zero post records. Current code combines RAM/translation/allocation failure and original32-byte-window mismatch into one return value. Object/stack availability is optional at this entry and cannot cause this particular rejection; mode and record-count bounds are known satisfied. The artifacts do not distinguish failed code peek from mismatched live bytes. No bound HAL entry/return is inferred from PC alone.

The next useful change is observer diagnostics that record these distinct failures and actual attempted code/mapping availability without privileged reads, fake instruction retirement or device changes. Reset/ungate/restore and further audio transport still require their own actual contracts; none was admitted here.

[Provenance](provenance.json), [770-file review pins](review-pins.json), [private binary hashes](private-artifact-hashes.json), [reviewed contract](next-suspension-contract.md) and [isolated implementation](run-1/source/suspend-probe.inc) retain the exact boundaries. Original media/PCM/RAM and executables stay in `.context/amc-first/stage-y/run-1`; this durable archive retains source, traces, reports and hashes.
