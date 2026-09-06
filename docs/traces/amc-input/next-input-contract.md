# After067: capture the original input request before start-enable

2026-09-06. Source-only investigation; no new model, guest execution, production change or milestone. Milestones065/067 and their pre-retirement, non-resumable probe stops remain unchanged. This contract proposes the next isolated experiment; it does not report that experiment as run.

## Concrete result

The input-start caller programs its genuine input descriptor **before** polling AMC status bit9. That creates a useful next observational boundary without manufacturing readiness or PCM. Readiness has an observable buffer prerequisite: after bit9, the original driver consumes output addresses from a shared SRAM header that is still entirely zero in067. Setting bit9 now would produce two identical output addresses and zero capacity.

## Command roles: established use versus unresolved effects

All offsets below are relative to physical AMC base `0x38500000`.

| Register | Original evidence | Safe conclusion and remaining uncertainty |
|---|---|---|
| +0 | `c0277f94`, store at `c0277fa0`; actual task-manager method `c02782fc`, argument0, after captured task configuration | Initial engine command request is observed in067. No proof yet of instruction execution, completion, or a change in +10. |
| +4 | Helper `c0277fac`; wrapper `c0277284` then reads two SRAM halfwords and writes +b0c=400. Called when output header+8/+a indicates work and from `c0272224` after setting driver buffer states | Used during buffer service/continuation. Calling it STOP would contradict its repeated service context. Exact instruction/control semantics remain unknown. |
| +8 | Helper `c0277fc4` writes1; bounded aligned literal-reference scan found no caller | A distinct command port exists. No defensible STEP/STOP/START label from the offset alone; scan is not proof of unreachability. |
| +0c | Helper writes1; reset path uses it when +10 reads1 | Reset-related quiescing request is a hypothesis; exact active-engine transition is unresolved. |
| +10 | Read helper and reset comparison against1 | An engine-state observation is used by reset. Neither all possible states nor a generic running-bit definition is established. |
| +b18 bit8 | Existing reset prefix and model065 | Completion of the explicitly modeled synchronous idle reset, with real clock/power dependencies. This is a labeled timing approximation, not measured silicon latency. |
| +b18 bit9 | `c027721c` polling, assertion names `ode_waitForStartEnable()`; actual input caller below | Start-enable handshake. Its downstream buffer contract is established; its hardware/firmware producer and timing are not. It remains0 in the next experiment. |

See retained `../engine-controls-dis.txt`, `../command-controls-dis.txt`, `../control-xrefs.json`, `command4-callers-dis.txt` and `output-completion-dis.txt`. These names refer to original code usage, not an inferred generic OptimoDE register map.

## Input request and shared output contract

`start-input-caller-dis.txt` shows the original caller checking input index<3 and required output arguments. It selects a descriptor from driver+110+4*index or +11c+4*index according to driver+d9. The allocation assertions in `output-layout-dis.txt` name these shared/unshared input descriptors and a third packet-description array at +128+4*index. The descriptor accessor at `c0149b09` supplies the transfer address; the original byte count is retained in r8. At `c0275f48..5f54`, it passes `(bytes+3)>>2` to helper `c0278084`.

That helper writes `101c=1, 100c=0, 1000=1, 1004=address, 1008=wordCount, a00=1, 100c=1`. Query helper `c027804c` reads current address+1010 and computes `remainingWords = count - ((current-start)>>2)`. Thus start/count/current units are grounded. No evidence here licenses advancing the address or producing the +101c interrupt. +101c bit0's write1 acknowledgement is already modeled; its event producer remains absent. +1018 is written1 after configure and0 on other lifecycle paths, but its full mask/IRQ semantics remain unresolved.

Before programming the transfer, the caller invokes a workloop action: `c0275da8`, member-pointer record `c02824c8 = {ffff0000,c027225c}`. That action calls the actual event source/provider. It is **not a shared-header clear**. Any newly encountered device interaction there must be retained as the next unsupported witness, not bypassed to reach the desired transfer.

After the transfer setup, the caller polls bit9 at `c0275f74`. Only after success does it read the normal codec header's 16-bit +10/+12 offsets, each measured in **halfwords relative to the header base**. It stores the resulting buffer addresses at driver+140/+144; driver+148/+14c points to header+8/+a, and +150/+154 to header+c/+e. `c0271680` returns both addresses relative to SRAM and equal capacities computed as buffer2−buffer1. The separate codec3 path uses a different layout and is excluded.

Original setup `c0271220` maps normal header=SRAM+f200 and session-control=SRAM+2a000. Actual type2 selects these at configure. Consequently the AAC header is physical `2200f200`, session control `2202a000`. `retained-header-state.json` independently extracts both 24-byte regions from067: allzero. Full SRAM SHA256 remains `011452fdff9699442c49115354c9b0bced1b98265ba64ee5b95c1c48bbb01583`. A ready response at this point would make both output addresses2200f200 and capacity0. This is a concrete invalid contract, not merely an unknown delay.

`header-update-dis.txt` also grounds one codec parameter: the type2 path maps the third captured word48000 to a session-sample-rate enum3; the assertion explicitly names the sample rate. `aac-session-fields-dis.txt` writes session halfwords +0=1b, +2=2, +4=caller argument, +a=rate enum. The first input caller invokes this with argument0 **after** acquiring the output layout. Do not move these writes earlier or label the other raw codec parameters without evidence.

## Ownership and completion still needed for a functional engine

`output-completion-dis.txt` reads normal session+12 bit4 and optional session+16 information, reads an output size from header+c/e, and consults header+8/a before setting driver buffer state to1 or2. It calls the original callback with flag1, that size and information; nonzero size toggles the selected output index. Remaining available buffer state triggers the +4 wrapper; the empty case writes +b10=400 instead. The handler acknowledges handled status through +b04. These establish that output sizes, buffer ownership, service commands and interrupt acknowledgements cooperate. They do not establish the initial output offsets/capacity, exact ownership bit definitions, PCM packing, interrupt producer or consumption clock.

BSU code adds another real resource dependency: channel0 writes+100, reads+184/+188, saves seven registers410..428 and drains+47c while+188 is nonzero; restoration writes saved words through+100. The original assertions name BSU lock/unlock/RestoreState. +400 is written1/0 and read until matching lock state. A successful lock/read response cannot stand in for absent bitstream/FIFO state. See `bsu-controls-dis.txt`.

## Proposed bounded experiment

1. Preserve067. Start from the same original checkpoint and rerun reset/upload/19-write configuration with all existing guards and independent original-kernel/coverage oracle. Never resume the longjmp state.
2. At the exact already witnessed +0=1, an isolated diagnostic may record a new **armed, unserviced request** state and return. This is explicitly an observational continuation, not execution or start-enable. Initialize only model-owned empty pending-input bookkeeping. Do not alter SRAM, +10, bit9, progress address, pending event or IRQ. Reject a repeated command, wrong width/value/order, inactive dependency, or unexpected lifecycle access before mutation.
3. Preserve the first following unsupported MMIO witness. Static prediction is +204=1, +184=7, +b0c=2000, +1018=1. These are not yet runtime-verified after067. A subsequent frozen candidate may retain exactly witnessed configuration with no event producer; do not accept arbitrary values or readback semantics. +4/+8/+0c and new BSU activity remain separate review gates.
4. Positive target: reach the original nonzero input-transfer setup. Capture caller/index, descriptor and packet-description objects, byte count, physical address, rounded word count, page mapping and exact bounded input bytes. Stop at the final +100c=1 **before accepting transfer/retiring that store**. No data is consumed. Address/count may be retained as idle staged configuration only after their preceding real witness and all source bounds pass. Earlier unsupported behavior is an honest narrower result, not grounds to synthesize readiness.
5. Bound to5M additional retired instructions after067, plus the unchanged upload prefix. Stop sooner on unknown access, unsupported configuration or command, repeated/reset/power event, attempted SRAM mutation, invalid descriptor/span, bit9 poll before the intended boundary, or input request requiring unmodeled encryption/transport. Preserve every failed prefix before extending it.

Required positive/negative checks: full original SRAM bytes **and write coverage** unchanged; no event/IRQ/bit9/+10 mutation; exact request/order/width admission; atomic unknown/malformed rejection; duplicate command rejection; address addition/rounded-size overflow and span/page validation; distinguish original byte length from padding; absent/unmapped/mismatched packet metadata rejection; lifecycle changes invalidate staged state rather than complete it. Each disposable stop reports PC and non-retirement; no snapshot/resume claim. The original-file upload oracle remains external to the in-process guest-descriptor check.

Input payload must be verified against its genuine source/container and packet metadata before choosing a functional decoder. AAC program identity and a plausible sync word are insufficient: raw packets, ADTS, configuration records, encryption, padding and packet boundaries are separate questions. Capturing the transfer may expose initialization bytes rather than compressed audio; report what the bytes prove.

A later high-level engine is possible without full DSP ISA emulation, but must implement real bounded input queues, independently grounded output layout/ownership, validated codec configuration, actual packet consumption, and completion/IRQ/sample-clock behavior. A synchronous initialization approximation can be considered once those resources and transitions are specified; missing silicon latency alone is not a blocker. No output offset, ready mask or PCM is proposed here merely to satisfy an assertion.

## Primary-source scope

ARM's [2004 vendor announcement](https://www.globenewswire.com/news-release/2004/05/17/310529/2693/en/ARM-Announces-Ground-Breaking-Embedded-Signal-Processing-Core.html) describes configurable data engines and variable instruction widths. The [ESPICO/ARM 2005 announcement](https://www.design-reuse.com/news/202509167-espico-to-offer-audio-codecs-that-run-on-arm-optimode-embedded-signal-processing-data-engines/) supplies a concrete relationship between that vendor and codec software. These support a research lead; neither identifies this AMC register map, program ISA or output ABI. The AudioDE whitepaper fetch failed and is not used as reviewed evidence. No generic architecture claim supplies missing device behavior.
