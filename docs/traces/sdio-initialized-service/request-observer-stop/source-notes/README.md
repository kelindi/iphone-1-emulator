> Historical source-analysis note retained from before the documentation-only hold. References to a corrected successor describe a proposal: its private source is unbuilt and unvalidated. “Prefix passed” below refers to recorded native boundary witnesses; the failed launcher never ran the complete positive auditor or an independent prefix-byte comparison. The parent README states the accepted scope.

# Preserved first-request prediction failure and corrected source path

The frozen `.context/sdio-first/runtime-write` control passed exactly:41,947,204 instructions,4,581,748 transactions, unchanged073 trace SHA13881df…eae9. Its one authorized positive stopped at the first unexpected observer condition. It was not retried or relabeled successful.

The positive's exact077 prefix passed:235,719 instructions and22,919 events, original c0380208. It then consumed register6 R5=0x1100; atc0380220 the original wrapper returned **r0=1**, which its caller unconditionally overwrote with1. It committed the actual host block-size320 setter atc037a1c8.

The stop was ARM SVC **c0380804**, CPSR60000113, original r0=cbc893c0,r4=20, cycles17,519,480,204, ticks256,799,308 after2,410,187 added instructions. The previous completed instruction wasc038c15c, the original physical-length setter's return. All41,261 raw events are retained at `../runtime-write/runs/positive-1/transactions.bin`. Controller data completions stayed504; no runtime CMD53, DMA or buffer receive occurred. Its failed `report.json`, request metadata, process/launcher error and image/slot dumps remain unchanged.

The harness checked a predicted later setup/builder before copying this first frame, so it did **not** capture r5/r6, the first frame object or its source bytes. The emitted zeroed request arrays are unfilled harness storage, not observed guest packets. Those missing bytes cannot be recovered from the report and must not be manufactured from a guessed command or prior checkpoint.

## Source-grounded explanation

The earlier direct-BL scan found the wake/resume-style `c038ac1c → c038aaf4` caller, followed by optional0093 and0043 commands. It missed the original **indirect** initial power-on path:

- `powerOnSequence` atc0382640 obtains the actual local MAC property.
- c03826a8 loads literalc03828c4 = c038aaf4; **BLXc03826b0** calls the same firmware loader.
- After its return atc03826b4, c03826bc obtains driver virtual slot0x4dc. c03826dc calls it with the actual MAC pointer and length6.
- The unchanged kernel has pointerc038ac94 atc03923b8, consistent with vtablebasec0391edc/slot4dc. The next experiment must observe the live vtable and function pointer; the static pointer alone is not a captured live dispatch.
- `setHardwareAddress` atc038ac94 builds command **0x004d**, with a16-byte command payload, action1 and the six actual caller MAC bytes. It enqueues throughc038add4→c03822e4; c038232c calls the common frame builderc038075c. A16-byte payload yields a **20-byte logical transport packet**, matching the failed run's observed r4. This is a strong source inference, not recovered actual packet bytes.

The predicted logical shape is `14 00 01 00 4d 00 10 00 SS SS 00 00 01 00 MM MM MM MM MM MM`; SS is the real driver sequence and MM the real original-call input. Physical length remains320, leaving300 original padding bytes. No fixture MAC or source bytes may be injected by the observer.

## Minimal corrected observer contract

A new `.context/sdio-first/runtime-write-v2` successor retains077/reg6/host-setter proofs and the actual completed CCCR/function interrupt-mask writes. It witnesses the real initial-power-on return, live vtable slot4dc, setHardwareAddress entry/caller/sequence/MAC and original command object. It removes the incorrect requirement to visitc038ac20 or0093/0043 before the first command.

Most importantly, **the first bounded frame is captured before classification**. Atc0380804 retain all sixteen registers/CPSR, object bytes, safe bounded stack bytes, declared lengths, and full virtual and physical RAM views whenever mapping/bounds allow. Only then classify expected004d/body/sequence/MAC/setup. An unknown opcode, path or length stops with genuine captured evidence rather than zero placeholders. Unsafe/unmapped ranges are recorded as missing; no MMIO reads or synthetic bytes stand in for them.

If the actual004d path and320-byte source match, the same final gate observes and retires the original CMD53 execute store atc0431524, stopping at ARM SVCc0431528 before response/data completion. Actual controller/card320-byte fields and all image/latch/ownership checks remain. No card semantics change is required. The prior accepted control can remain the pinned baseline; root reviews final corrected sources/tests/pins before authorizing one new bounded positive. No new marker variant, cap increase, full boot or hidden retry is implied.
