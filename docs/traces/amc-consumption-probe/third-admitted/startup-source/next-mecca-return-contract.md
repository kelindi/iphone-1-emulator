# Next observation: the actual MeCCA initialization return and explicit start

Source-only proposal. No fourth admission, new device semantics, build or guest run is authorized by this file. Stage M's one-shot result remains frozen.

## Resolved interface distinction

The actual original driver-prefix capture contains `mUserClientType=1` at +94. Original Apple assertions at c0275b38..5b44 explicitly identify 1 as `kAppleAMCDriverUserClientTypeMeCCA`. MeCCA.framework contains the matching named AMC_IOKit_DecoderDriver/AMCDeviceInterface methods and source-path strings.

The separate IOAudio2 transformer selector 1 is now resolved too: its original runtime method-table constructor c022d6f4..d710 copies member descriptor c02300d0 (`014c0000,00000000`) into entry 1. The ARM C++ virtual member encoding selects slot `(0x14c*4)-4 = 0x52c`; the actual driver-prefix vtable c0281d78 has c02732a0 there. That method prepares the free input buffers, sets +24e=1/+24d=0 and returns through the base transformer. It is not AMC UserClient method 3. The observed +24e remains zero, consistent with the distinct MeCCA path. Do not substitute an IOAudio2 start or alter that flag.

## Original MeCCA path to AMC method 3

Original MeCCA SHA256 `15f8f049239e64f88660aeb1ce99aa07273ccd19ca636e624ff19e9a68e7f4ec`, read from retained HFS rootfs:

1. `AMC_IOKit_DecoderDriver::restartCodec` (3123cd18) prebuffers at 3123cd60.
2. It calls `AMCDeviceInterface::initializeDecoderWithDMABuffer` at 3123cdc4, with the real interface at decoder+50, input index +6c and byte size +64.
3. That method sends scalar selector 2 at 3123f7c8/3123f7ec; actual return is 3123f7f0. It requires return zero and output count four, then populates the interface's actual output pointers/capacities from the scalar results at 3123f868..f890.
4. The caller checks init success at 3123cdc8; it gets output buffer info at 3123ce14 and checks success at 3123ce18. This getter returns pointers into the interface (+64/+74/+7c/+6c); it has no input-refill loop or additional decode wait.
5. At 3123ce50 it calls `startDecoder`. That method acquires driver access, then sends literal selector 3 at 3123f958→3123f95c→3123f2f8→3123f35c (`IOConnectCallScalarMethod`). The existing original AMC table maps method 3 to c0272224, then c0277284 writes actual +4 and enables output mask 400.

A bounded raw ARM direct-call scan finds only 3123cdc4 calling initializeDecoderWithDMABuffer and only 3123ce50 calling startDecoder in this original file. These are static code facts, not proof the latest guest retired those sites.

`handleInputDMADone` (3123c920) is a separate input task. It increments +6c modulo three, starts the next actual buffer when it differs from its final-buffer index (+74), then refills the freed buffer. This explains why address index 0 can contain new data. It does not establish that explicit start waits for three/four/five transfers. The current bounded stop occurs only 242,155 retired instructions after first PCM, so thread scheduling remains a plausible reason the setup caller has not returned to user mode; it is not yet a diagnosed bug.

## Proposed six observation slots, ten consumption hooks unchanged

Replace the six previous ordering-only hooks with:

- c01533c2: original kernel scalar-dispatch return, Thumb/SVC; expected retired successor c01533c4.
- 3123f7f0: actual MeCCA scalar initialization return, ARM/user; record R0, interface R4, output count and all four scalar results from the real stack before the existing checks.
- 3123cdc8: restart caller's initialization return, ARM/user; record R0 and decoder R4.
- 3123ce18: output-info return, ARM/user; record R0 and relevant actual decoder/interface fields.
- 3123f95c: actual no-argument method-3 call, ARM/user; require observed R1=3 and record actual branch retirement to 3123f2f8 with LR3123f960.
- c0272224: actual kernel start method entry, ARM/SVC; record genuine driver argument/retirement. Existing global MMIO traces independently retain +4/400 evidence.

First verify the original instruction bytes at the user hook VAs with RAM-only observational translation. Do not assume a file VA is currently mapped or that it executes. Any failed map/byte check is a preserved observer-boundary failure, not permission to patch code or weaken the gate. Source-byte comparison uses only a clone of CPU/TLB and RAM page tables, not live guest reads. Register/stack/interface reads are observation-only and must never supply the engine.

At the user returns, copy bounded real stack/interface/decoder data only: initialization output count at SP+38 and four 64-bit scalar results at SP+8; interface fields +8 connection/+64..80 output bookkeeping/+84 SRAM; decoder +50 interface/+64..78 input indices/counts/+8c..98 output info, as needed for the actual site. Validate pointer arithmetic/mapped RAM before reading. Derive the exact captured ranges and interpretation from original instructions; do not guess an object address or fill absent fields.

All records retain full CPSR and before/after cycles/PC; only successfully observed successors count as retired. IRQ diversion and changed mode are separate unretired observations. At most 128 fixed records. The one control-flow call needs target/LR validation, not the ordinary PC+4 predicate. No record alone replaces actual start, event/mask, successful PCM reads or retired ownership return.

## Unchanged run and audit boundary

Retain Stage M's three exact live input admissions and stop before fourth transfer enable. Engine/decoder/DMA/IRQ/service/ownership timing remains byte-identical source. Preserve first075, second/third captures, queue and all binary final state; require full Stage M trace equality after stripping only old/new observer lines. All ten existing consumption hooks and two-publication/third-work cap stay in place.

A single future run uses the same 155.3M total, existing 5M caps, 240s wall/180s CPU/1GiB/8MiB limits. No retry, fourth input, or change in completion timing. An init error/count mismatch is evidence; a thread not returning before the fourth stop is also evidence, not permission to force a start. Focused mode/Thumb/branch/IRQ/peek/record negative tests, original-file symbol/stub/table oracle and frozen auditor artifact negatives must be reviewed before execution.
