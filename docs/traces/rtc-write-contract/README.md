# The original OS 1.0 RTC setter is implemented

> Historical recordings and publishing artifacts have been removed from Git history. The summaries below describe the original runs; remaining test reports and screenshots are retained. Historical hash receipts may predate this archival cleanup.

2026-09-06. Static inspection of the unchanged `1A543a` kernel disproves the current emulator comment that this kernel's date setter is a panic stub. **No guest clock behavior changes here.** Calendar still needs a genuine date-setting and persistence gate.

The [read-only extractor](extract.py) checks the original kernel SHA-256, maps file-backed Mach-O segments, verifies the class name and vtable, and records eight complete function bodies in [report.json](report.json) and the adjacent text files. It uses Capstone only to decode original bytes. There is no Apple binary patch, guest execution, host-time injection or firmware change.

## Exact call path

- The original `ApplePCF50635PMURTC` constructor stores vtable `c0402348`. Its read and write entries at offsets `4a8` and `4ac` point to `c03ff8a0` and `c03ff978`.
- Base `AppleARMRTC::setGMTTimeOfDay` at `c015dec4` converts seconds to the date structure, then calls virtual slot `4ac`. The subclass GMT setter at `c03ff964` forwards to the base with one second added. This adjustment is part of the original driver, not a proposed emulator correction.
- The write method at `c03ff978` invokes a gated member action. Its immutable member-pointer words are `ffff0000,c03ffa20`: zero low-halfword adjustment and the direct action address. The nearby panic handles unsupported multiple inheritance; that branch is not selected by these original constant words. It is not an unconditional date-setting panic.
- The action at `c03ffa20` copies ten date bytes into the object's pending structure, sets the pending flag and enables its provider interrupt through the event source. (The earlier wording “signals” was incorrect; see the independently checked construction and IRQ mapping below.) The callback at `c03ffa60` converts second/minute/hour/day/month and year modulo 100 to BCD, retains the binary weekday, and calls the PMU write method with **start register `59`, length 7**. It then clears the pending flag.
- The getter reads the same seven registers, rereads seconds, and retries if the two seconds bytes differ. It converts BCD fields, leaves weekday binary and adds 2000 to the two-digit year.

This establishes the original software protocol, not every PCF50635 hardware behavior. The existing emulator deliberately ignores writes to `59..5f` and increments `unknown_writes`, so the source evidence identifies a real missing device capability.

## Next bounded implementation gate

1. Observe one date change through the genuine Settings UI and capture the complete I²C transaction and original callback boundary. Confirm byte order, weekday, success/error behavior and software calendar update; retain the original state before the change.
2. Define the device's partial-write, transaction-boundary, invalid-BCD/date and tick-rollover rules from hardware evidence before implementing writes. Preserve deterministic tests, monotonic timer accounting and existing snapshot compatibility. A final seven-byte shape alone does not establish intermediate hardware behavior.
3. Verify leap years/month boundaries, read-during-rollover behavior, Save/reload and a second manual date change through the original UI. Correct host synchronization is a separate policy: changing the emulated RTC after restoring an already running kernel does not itself prove that its cached software calendar changes.

No production implementation or visual milestone is claimed. Initial extractor preflights used a one-byte-short class-name length and a vtable-literal address four bytes too early; both assertions stopped before any report was accepted. Correcting the source-range bookkeeping produced the retained complete extraction, without changing the original binary or interpreting either preflight as a guest failure.

## Actual UI diagnostic — acceptance remains incomplete

The original Settings → General → Date & Time UI allows automatic time to be switched off and reveals the original picker. A real tap moves its minute selection from 13 to 14. The subsequent Save/reload image sampler matches all 18,000 sampled RGB pixels, but the final generated-execution check times out with the phone left paused. **This run fails its complete acceptance gate.** Picker state and a matching image do not establish a committed RTC change or active execution after restore.

Native read-only inspection of both genuine exported snapshots reports zero unsupported PMU writes in each. Total register writes rise from 222 to 231, which does not identify any RTC transaction. No guest instruction is executed by this inspector. The first inspector assumed 128 MiB and failed geometry; the actual exported phone has 256 MiB. Corrected geometry and 64-bit counter formatting produce the retained observations. This does not alter either immutable export.

[Complete reports, screenshots, actions, inspector source and hashes](browser-diagnostic/) preserve the result. The first noninteractive launch collected only Home because stdin was closed; the separate interactive run also retains an initial mistaken Wallpaper navigation and its correction. historical recording (removed) covers the interactive run, including manual review pauses and the final timeout; historical thumbnail (removed). It is a diagnostic recording, not a speed or successful persistence milestone. Next, leave the picker through the original UI, observe the actual transaction, and finish an unpaused execution gate before making a clock-change claim.

## Bounded attempt to leave the picker

A single native continuation loaded the genuine post-picker export, applied normal restored-input cleanup and issued a Back-coordinate touch/release. A delegating observer watched only actual PMU I²C writes to registers 59–5f. It stopped after the declared 100 million instruction bound with **zero RTC writes and zero new unknown writes** (exit 10); RTC seconds advanced 1262304883→1262304889. [Probe source, log and identities](bounded-commit-probe/) preserve this negative result. No screenshot establishes that Back was acknowledged, so this does not disprove the original setter or prove a committed date change. No date, instruction, return value or device register was injected. The probe was not repeated with an enlarged bound. The next useful investigation is the original app's commit path and actual callback scheduling.


## Follow-up: software calendar adjustment is visible

The original app's checked class/method metadata now identifies an immediate `datePickerChanged:` → `settimeofday` path. Read-only inspection of both actual exports finds a20.757616-second change in the kernel's calendar offset, while hardware RTC unsupported-write counts remain zero. This supports software-time adjustment during the interval, and explains why another Back-only attempt is not the next useful test. [Exact original code, decoded fields and limits](app-commit/README.md). It does not override the failed final browser gate or establish RTC persistence.


A subsequent, separately bounded replay from the before-picker export now records two actual original setter paths and matching date structures. All18 observed entry instructions retire correctly, including the queued-action entries. No asynchronous callback or RTC I²C write occurs before the400million-instruction bound; the hardware-write gate remains failed. [Actual setter observations and limits](setter-observer/README.md). The next unresolved boundary is queue completion/event-source dispatch, rather than whether Settings has a setter.


## Pending date waits for the hardware second interrupt

[Original construction, vtable and actual exported state](second-interrupt/README.md) identify the pending date source as provider index1, original PMU IRQ7. Its enable method unmasks INT1 bit7: the genuine before/after exports change INT1MASK from f0 to70 while the pending flag changes0→1 and INT1 remains zero. The driver enables an interrupt; it does not software-signal its callback. The current device advances the clock without generating this event. This identifies a missing hardware dependency, but an isolated causal replay is still required before attributing the failed callback to it.
