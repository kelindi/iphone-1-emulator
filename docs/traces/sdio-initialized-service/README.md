# Original-driver initialized-service verification: accepted narrow gate

The actual original driver now exits its complete-image loader and consumes the initialized private model's verification marker. **The disabled control and positive gate pass; the full marker-disabled timeout gate is incomplete.** This is isolated device-protocol progress, not firmware CPU execution, runtime command success, a network interface or a production change. [Accepted scope](accepted-scope.json).

## The exact control and positive

All 84 source/input/dependency pins match. Each attempt starts by loading the unchanged published073 package into a fresh M68 and requiring full canonical machine/sidecar equality before executing an instruction. Preparation leaves the loaded device bytes unchanged; its new buffers/state live outside the old schema. No full cold boot or modified checkpoint publication occurs.

| Gate | Added instructions | Raw SDIO transactions | Actual stop | Result |
|---|---:|---:|---|---|
| Disabled control | 41,947,204 | 4,581,748 | ARM SVC `c038a8ec`, r4=1000 | Exact073 failure and trace SHA reproduced |
| Initialized service | 235,719 | 22,919 | ARM SVC `c0380208` | Actual successful verification comparison branch |
| Marker disabled | 100,000,000, fixed cap | 41,254 | Thumb SVC `c014782c` | Incomplete negative; three zero pairs, no verification acceptance |

The [control report](control-1/report.json) and [independent full parser](control-1/audit.json) reproduce all146,615,936 trace bytes by hash, including exactly1,000 successful status8 responses. The raw control remains private; its byte-identical [durable lossless stream](../sdio-checkpoint/v2/actual/continuation-transactions.bin.gz) is reused instead of creating another large copy. [Exact reference](control-1/transaction-reference.json).

The [positive witness log](positive-1/milestones.log) records these original-driver steps:

1. A completed function1 status read at cycle17,517,137,155 returns R5 `0x1009`.
2. Original requested-length reads at0x10 and0x11 both return zero.
3. At `c038a968`, the real source cursor is122608 and next requested length is0. The original loader returns1 at `c038aaa4` and enters verification at `c03801b4`.
4. Successful original R5 reads return low `0xdc` at register0x34 and high `0xfe` at0x35, at cycles17,517,263,365 and17,517,305,259.
5. The original comparison branch commits, stopping outside a normally returned instruction at `c0380208`, cycle17,517,305,736, tick256,767,641. CPSR is `0x60000113`, ARM SVC. **The following register6 operation has not run.**

Both the [independent parser](positive-1/audit.json) and [separate root review](positive-1/root-review.json) verify all22,919 events, monotonic cycle/tick fields, accepted widths, the exact five ordered commands/responses and no new data start. The [complete raw trace](positive-1/transactions.bin) is733,408 bytes, SHA256 `c92482f0414171909c7f453e322e8015263f5e6038eb42769eb6c03b794eac82`. All actual bank bytes, write masks, helper receipt and retained handoff frame still match independently verified073. DMA completions remain504.

## What initialization actually means here

The model validates the received2140-byte helper, exact actual main-bank bytes and write masks, complete command4 handoff and its32-byte physical frame, canaries and clean transfer/acknowledgement state. Counts alone cannot admit it. In particular, the original host's zero-length arithmetic can return loader success prematurely; independent image/coverage validation prevents using that permissive branch as a substitute for a complete upload. [39 exact original instructions and two literals](source-contract.json), [direct source extraction](source_contract.py).

It then allocates two independent guarded320-byte slots and verifies capacity, canaries, ownership and the image again before committing preparation. Real APIs reserve, commit arbitrary bytes, read bounded subranges, release and cancel ownership, with tokens rejecting stale operations. Completed bytes survive host reset/rebind; card-only unavailability cancels uncommitted reservations and deasserts service, while full card reset clears both slots. Allocation failure, duplicate ownership and corrupted guards fail transactionally.

After a declared600-guest-tick preparation delay, the model rechecks image and empty storage before advertising the initialized service. **600 ticks is emulated-device policy, not an observed hardware startup duration.** Status and scratch values derive from the current enabled service and storage ownership. Independent low/high reads sample current values; no atomic pair latch or read-count readiness is invented.

No opcode semantics are implemented. The command API explicitly rejects execution without generating a success response. No guest runtime CMD53, response interrupt, register6 behavior, helper CPU or main CPU is supplied. Tests prove that status9 cannot reopen the old HANDOFF download parser, and that the320-byte runtime transfer form still returns an error. This accepted gate establishes initialized finite storage plus the original verification branch; later commands and interface behavior remain unsupported.

## The incomplete negative remains a failure of its full gate

One separately authorized marker-disabled run retains initialized status9 but leaves scratch zero. It passes the loader and enters the distinct verification loop, then records **three successful low/high zero pairs**. The high reads occur at cycles17,517,305,259;17,553,001,140;17,588,722,155. The two observed gaps are35,695,881 and35,721,015 instructions; no scheduler cause is inferred from those intervals.

It reaches the unchanged100-million-instruction cap at kernel PC`c014782c`, CPSR`0xa0000133` (Thumb SVC), before the intended100-attempt failure return. It does not hit its90-second wall cap, execute register6, start new DMA or change the retained image. **No full100-attempt timeout acceptance is claimed, and there is no cap expansion or repeat.** [Failed report](no-marker-1/report.json), [original launcher failure](no-marker-launch.log), [complete raw prefix](no-marker-1/transactions.bin), [prefix-only audit](no-marker-1/prefix-audit.json).

The prefix auditor verifies every41,254 raw event and the nine successful R5 reads, but explicitly sets `fullNegativeGatePassed:false`. Its first assumption incorrectly required an ARM stop even at an arbitrary exhausted-budget instruction; that source/failure is [retained](prefix-audit-first-assumption/README.md). The correction records the actual Thumb mode and leaves all named target-mode requirements and the failed full-negative result unchanged.

## Tests, source review and bounds

The retained [preflight report](preflight-report.json) precedes guest execution:

- **48,451** focused ASan/UBSan checks pass for image/bounds/admission, arbitrary-byte storage, deadline/read boundaries, cancellation, reset and unsupported transfer behavior.
- **1,012,343** unchanged frozen SDIO contracts and **36,578** lifecycle checks pass with hooks unattached. Old073 phone, device schema, image models and codecs remain byte-identical;13 unchanged sources are listed in the report.
- **16,396** mode/ordered-pair checks reject Thumb/IRQ/user address aliases and failed, reversed, interrupted, nonconsecutive or stale marker pairs. Every real verifier iteration resets the probe witness, because the original verifier does not inspect the pair helper's boolean return.
- **13** explicitly synthetic auditor tests reject malformed evidence. They are not guest traces.
- The deliberately omitted image validation fails **110 assertions**, preserved in [negative-image](negative-image/test.log).
- The first harness incorrectly rejected changes beyond the32-byte committed frame and left failed stack fixtures attached; its resulting sanitizer cascade and defensive teardown correction are [preserved](first-test-failure/README.md). Runtime admission was unchanged by that harness correction.

The native system OpenSSL3 SHA256 implementation and frozen native archive are pinned linked dependencies, not newly sanitizer-instrumented source. The original kernel/DT/disk and complete073 package stay unchanged. [Source/input pins](source-pins.json), [actual canonical loader check](actual-canonical.json), [probe](probe.c), [launcher](run.py), [auditor](audit.py).

Each native probe retains120 seconds for canonical load,100M instructions/90 seconds for execution and a240-second outer bound. Actual native control load/run were2.049/2.867 seconds; positive1.871/0.028 seconds; incomplete negative1.998/10.967 seconds. These are correctness-budget observations from restored checkpoints, not an end-user speed comparison or full boot time.

No user profiles or production assets are changed. This is a nonvisual original-driver milestone; no screen recording is invented. Next is source-grounded register/command/interrupt work, with this still-incomplete negative preserved and any further guest run separately bounded and reviewed.

## Later request-observer stop (no new milestone)

A subsequent isolated control again reproduces the exact073 trace. Its one positive continues beyond the077 marker branch, records actual register6 R5 address error and the original wrapper's ignored return1, then stops at an unexpected20-byte first-frame boundary before any runtime CMD53. The observer classified too early and did not capture that packet's actual bytes. An already completed source inspection identifies initial power-on's indirect MAC-address command as a plausible explanation; it is not a recovered packet or accepted runtime operation. [Preserved reports, full partial trace, missing-data limits and unfinished boundary](request-observer-stop/README.md). No retry, card semantics change or milestone080 is claimed; corrected observer source remains unbuilt and unvalidated.
