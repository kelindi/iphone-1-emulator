# Genuine guest networking: measured first gate

Status: 2026-09-05. This investigation refines [the Wi-Fi proposal](wifi.md). An isolated minimal SDIO controller/card now passes the first card-register gate below; no production network device or transport is implemented. The genuine guest still has **No Wi-Fi**, and Safari cannot make an external HTTP request.

## What exists and what does not

There is no `core/src/soc/sdio.c` or WLAN card model in the current tree. `machine.c` has no SDIO MMIO dispatch; accesses to its page follow the unmapped read/write path. The old proposal describes future files, not an existing implementation.

The device tree's `/arm-io/sdio` `reg` is `0x00d00000, 0x1000`, relative to the `arm-io` bus. Its actual physical page is **0x38d00000**, confirmed by Apple's driver log and native MMIO trace. Interrupt number is `0x2a`, clock gate `0x0b`, compatible string `sdio,s5l8900x`. The reset function refers to GPIO `0x0607`. The supplied tree has a zero local MAC address and zeroed 1024-byte calibration property; those values need a deliberate emulated-device policy before association acceptance.

The retained rootfs contains neither a separate Apple SDIO/Marvell kext nor the proposed `/usr/share/firmware` files. The genuine drivers are already in the kernel's `__PRELINK` segment. Its embedded IOKit personality identifies `AppleMRVL868x`, provider `IOSDIODevice`, bundle `com.apple.driver.AppleMRVL868x`, version `1.1.0`, and dependencies on IOSDIOFamily, IO80211Family, and IONetworkingFamily. Strings identify SD8686 helper/firmware loading; actual embedded image addresses and upload bytes still need extraction/trace. Do not start by substituting Linux firmware files.

## Exact first failure, reproduced

A cold boot with the unmodified device map writes the following offsets under `0x38d00000`:

| Offset | Written value | Meaning supported by guest code |
|---|---:|---|
| `+0x30` | `4` | Clock divider setup |
| `+0x04` | `3`, then `0` | Data/FIFO reset sequence |
| `+0x00` | `1` | Controller enabled |
| `+0x14` | `0xffffffff` | Clear status |
| `+0x38` | `0xffffffff` | Clear pending interrupt state |
| `+0x3c` | `3` | Interrupt mask setup |
| `+0x34` | `2` | SDIO interrupt/control setup |
| `+0x0c` | `0` | CMD5 argument |
| `+0x08` | `0x00040045` | CMD5 command descriptor, before execution trigger |

The next **100,001 reads of `+0x18` all return zero**, at guest PC `0xc04314cc`. The driver logs a CMDRDY timeout for command 5, then fails base initialization and bus scanning. No card command completes, so neither firmware download nor `en0` creation has begun. This rules out HTTP, DHCP, or TLS as the first failure.

Kernel SHA-256 for the addresses below: `42e9f3c18d4a4d5c956acdcded0c3f8f247fe3e7fc96d14cfa3aedcd77c292c9`.

Apple's register-pointer initialization at `0xc0430974–0xc04309fc` and the actually executed command loop at `0xc04314b0–0xc04315bc` establish:

1. Write argument `+0x0c`, then command descriptor `+0x08`. The command index occupies the low six bits. CMD5 adds `0x40` and response flags `0x40000`.
2. Poll **`+0x18` bit 0** for command readiness. This is a prerequisite before execution, not the final completion indication.
3. Read the command register, set bit 31, and write it back at PC `0xc0431524` to execute.
4. Poll `+0x18`: bit `0x8000` takes an error path; bits `0x30000` also take an error path. Bit `0x40000` is tolerated specifically for CMD5. Bit `0x10` completes the command successfully. Exact hardware error names should be confirmed before assigning public constants.
5. Write observed status to `+0x14` to acknowledge it; on success read response words at `+0x20`, `+0x24`, `+0x28`, `+0x2c`.

Do not fix this with an always-ready register constant: it would conflate readiness, execution, completion, acknowledgement, and errors, and could make the next transfer appear successful without transporting data.

## First implementation and acceptance gate

Implement an M68-only S5L8900 SDIO controller plus a minimal separately modeled card endpoint. The first acceptance gate is **a real cold-boot trace that completes CMD5 and reaches CCCR reads through CMD52**. It is not a network-interface milestone yet.

The guest IOSDIOFamily initializer at `0xc03798bc` calls this exact sequence: CMD5 with argument `0`; CMD5 with `0x00060000`; CMD3 with `0`; CMD7 with the high 16 bits of the CMD3 response. Its next routine reads function-0 bytes at addresses `0`, `8`, `9`, `10`, and `11`. Thus a first card model needs coherent operating-condition responses, an assigned relative card address, selection state, and CMD52 register access. The full CIS walk, identity matching, function enable, and interrupts follow and must be traced rather than guessed.

A subtle unresolved detail: Apple's initial CIS-address assembly at `0xc0379814/0xc037983c/0xc0379860` appears to combine addresses 9/10/11 in the opposite order to openiBoot's implementation. Confirm the command wrapper and actual returned bytes before copying a CIS image layout. This is one reason the genuine driver is the acceptance oracle.

Implementation work should include:

- Controller registers and explicit ready/busy/completion state, command trigger, status acknowledgement, interrupt masking, and reset behavior. CMD descriptors written without bit 31 must not execute twice when the bit is later set.
- Card state for reset, power negotiation, RCA selection, function-0 register storage, and errors for unsupported commands/addresses. No fabricated firmware success flag at this stage.
- Tests that replay the actual trace and check no response before execution, one completion per trigger, acknowledged events staying cleared, reset during a pending operation, and rejected invalid access.
- Machine MMIO routing, M68 board attachment, VIC line 42, and snapshot serialization/version handling. Coordinate the shared machine/snapshot changes with audio work. An old Home snapshot already contains a failed SDIO probe and is not enumeration evidence.
- A cold-boot trace showing the actual transition through command trigger, completion, response, next CMD5/CMD3/CMD7, and CMD52. Keep both the negative baseline and the first successful trace.

There is no small production enumeration patch in this investigation because controller dispatch, card state, IRQ semantics, and serialized state are all absent; adding a response constant would not establish a usable interface.

## Isolated first card-register gate passed

A fresh genuine cold boot now completes **CMD5(0), CMD5(0x60000), CMD3(0), CMD7(0x10000), CMD52(function 0, address 0), and CMD52(function 0, address 8)** in the isolated model. The six actual response words are respectively `10060000`, `90060000`, `00010000`, `00001e00`, `00001000`, and `00001000`; each command also reads three zero high response words after acknowledgement. The original caller regains control at `0xc03797dc` with R0=1 after the second register read. It has already extracted/stored both revision nibbles from the first byte. This is consumed-response evidence, not merely an issued request.

The old gate is preserved and reproduced with the current native core: **100,011 SDIO accesses, including 100,001 zero status reads**, ending at the genuine timeout return `0xc0431504`. The candidate has six triggers/six responses, two CCCR reads, zero rejected writes/transport errors, and no invented VIC interrupt. [The durable negative and positive reports and full transition traces](../tweets/057_sdio_first_card_registers.md#what-youre-seeing) include exact instruction sections. A separate trace verifier checks response ordering, acknowledgement, 600-tick completion delays and unchanged kernel/device-tree/rootfs/archive SHA-256 hashes.

The standalone model/contracts/probe live in `.context/sdio-first/`; they are deliberately outside production. Contracts pass **101,258 checks, zero failures**, including untriggered and busy polling at stationary guest time, independent W1C events, response latching, invalid access, absence, voltage/RCA/selection order, reset during pending commands, backwards time and deadline overflow. A trigger does not clear earlier unacknowledged events. The controller and card have separate reset/state handling. All six non-SDIO bus operations forward to the original handlers and context; direct RAM write consent is explicitly revoked before wrapping. No guest binary is patched and no snapshot is saved with unrepresented SDIO state.

The endpoint is an explicit minimal emulated-card configuration: one I/O function, voltage mask `0x60000`, assigned RCA 1, CCCR0=0 and capabilities8=0. Its 600-tick delay uses the unchanged 6 MHz device timebase (100 microseconds), while CPU clock is 412 MHz. Neither delay nor register defaults claim measured Marvell behavior. [The SD Association I/O-only R1 definition](https://www.sdcard.org/cms/wp-content/themes/sdcard-org/dl.php?f=PartE1_SDIO_Simplified_Specification_Ver1.10.pdf), table 4-6, supplies fixed CURRENT_STATE=15 and unused READY_FOR_DATA=0 for CMD7; it must not borrow a memory-card state value. Unsupported CIS/function bytes, writes and CMD53 return errors. No firmware, enabled data function, identity match, interface or packet exists yet.

Before production integration, add M68 attachment and observed IRQ42/clock-gate11/GPIO reset wiring, then an explicitly versioned portable state representation for controller registers, separate card state, pending command/deadline and latched events. Preserve rawv39 loading and guest bytes; old failed-probe saved phones must retain the old absent-device policy unless an explicit migration is proven. Never serialize callback pointers or host diagnostic counters. Tests must prove pending/acknowledged restore without duplicate completion or stale IRQ, callback rebinding, card/controller reset separation, unchanged capture quiescence and legacy clock behavior. If machine layout changes, recheck all compiled-engine offsets and ABI contracts. The isolated probe changes none of these production contracts.

The next bounded investigation derives the genuine CIS parser and identity matching, with declared standard tuples and compatible IDs supported by primary evidence. Its acceptance target is actual AppleMRVL868x matching and observed function-enable/CMD53 requests; firmware success remains a later transport gate.

## Isolated driver matching and enable request

The next genuine cold-boot gate now reaches **AppleMRVL868x**. Source and trace show that controller scan `0xc0379b54` reads function-0 FBR interface bytes at `0x100…0x700`, creates nubs for nonzero low nibbles, and registers them. The prelinked personality names `IOSDIODevice` as provider without manufacturer/device matching keys. The actual provider cast returns a nonzero pointer at `0xc0381bac`; this path does not establish a CIS tuple parser or MANFID comparison. The candidate declares one standard WLAN interface (7), absent interfaces for functions 2–7, and a bounded common tuple chain. Compatible Marvell/Libertas IDs are declared configuration, not a captured card dump or proof of how Apple matched.

Three retained boots distinguish the real prerequisites. The unchanged DT reaches matching, then fails its all-zero calibration validation at `0xc0381bdc` with message `0xc038e3c4`. The original validator checks the first 128 bytes against all-zero/all-FF data. The original missing-property branch explicitly accepts absent calibration; a separately reviewed private loaded copy removes that property and reaches the genuine zero-MAC rejection `0xc0381cdc`. A third copy adds deterministic locally administered unicast MAC **02:00:00:00:01:01**, then reaches the original function-enable request. This is an explicit **no RF calibration / emulated MAC policy**, not invented factory data or physical radio success. No original input file changes.

The final driver writes CMD52 argument `0x80000402` (function 0, IOEx address 2, function-1 bit set) and then execute descriptor `0x80050074`. The probe stops at `0xc0431528`, after the MMIO write returns and before completion: **22 triggers, 21 responses**, last command busy. Each final stage has zero R5 card errors, transport errors and rejected MMIO writes. The controller extension's **101,589 contracts pass**, including the previously missing CTRL bit-2 four-bit width and its readback/rejection checks. An earlier failed full-budget run remains recorded; it predicted the wrong immediate failure and exposed that missing width bit.

[Milestone 059](../tweets/059_sdio_driver_function_request.md) links all three durable traces/reports, the failed attempt, original calibration disassembly, and an independent byte/hash audit. The audit exactly reconstructs removal of the 1,060-byte DT record, node count 12→11, and the later six-byte MAC change. Source/probe/contract work stays under `.context/sdio-first/identity/`. The original first-gate sources and all production machine/snapshot contracts are unchanged.

The endpoint still reports no ready data function. An IOEx disable-to-zero is supported; actual enabling and CMD53 data operations remain unimplemented, and the positive probe stops before pretending otherwise. Next derive the driver's enable/readiness polling and first CMD53 request, provide a real bounded register/data endpoint with explicit completion, and validate requested transfer bytes before any firmware-ready or network claim. A possible interface object created during driver start is not evidence that firmware or packets work.

## Isolated function readiness and first helper request

The next isolated cold boot passes the enable/readiness gate with the same reviewed missing-calibration/local-MAC policy. IOEx completes at guest tick **5,274,444**; the actual IORx loop reads **0, 0, 2**, crossing the independent initialization deadline **5,276,244**. The endpoint initializes real zeroed 64-byte storage before that deadline. Selection, enabled/ready state, host power and available capacity determine its download-ready status; no firmware marker is supplied. These are declared model timings: commands take 600 ticks and function initialization 1,800 ticks at the unchanged 6 MHz device timebase.

The genuine driver reads its port bytes and pre-helper registers, sets host power, programs physical buffer **0x13c87004**, block length **32** and count **2**, then commits CMD53 argument **0x9a000002** / descriptor **0x501f5**. The next instruction hook stops at **0xc0431528**, instruction **351,499,195**, after that MMIO write returns. **38 triggers / 37 responses** prove the final request remains pending. All **64 physical source bytes** match Apple's original helper frame (little-endian length 60 plus the first 60 helper bytes), SHA-256 `90f8e97b50ecbbc45c68f4fa07379ea46d02e157aa2bd0ba86aa5f3d0a72057c`. The endpoint has received zero bytes; no CMD53 response, DMA completion or helper execution is claimed.

[Milestone 060](../tweets/060_sdio_first_helper_request.md) links the durable trace, reports, original-helper provenance and full input/source hash and DT-byte audit. **114,312 contracts pass**: all 101,589 earlier checks plus 12,723 endpoint checks covering finite storage, actual bytes/backpressure, separate initialization, no progress from stationary-time reads, disable/reset/cancellation, power, invalid/unknown registers and unsupported CMD53 completion. Independent review found that controller reset must retain the card time anchor; new tests fail the old source in exactly six assertions, and the corrected candidate passed a fresh cold boot with the same actual request/frame. Earlier successful pre-review evidence remains separately retained in `.context/sdio-first/enable/before-reset-review/`.

Implementation and reproduction commands remain isolated in `.context/sdio-first/enable/README.md`; milestone 059 sources are preserved. Production machine and snapshot layouts remain unchanged. Future integration must serialize bounded endpoint contents/used length, readiness state and pending deadlines alongside the separate controller/card state, with reset/restore and old absent-device snapshot policies verified. No host pointers or diagnostic counters belong in portable state. The immediate next gate is genuine CMD53 response and data-start semantics, a checked bounded transfer into endpoint storage, and actual interrupt/acknowledgement progression; helper termination, execution and main firmware remain separate work.

## Isolated first helper-frame completion

A new isolated candidate now receives the first 64-byte helper frame and follows the original driver through consumed DMA completion. After CMD53 response, the genuine driver writes DCTRL0x10 at guest tick **5,314,820**. The model validates the entire physical source range and stages its bytes. A separate data deadline is due at **5,315,420**, serviced at **5,315,426**; the six-tick service lateness is recorded, not concealed. Only then do all64 bytes enter the real bounded endpoint and data status/IRQ latch. The endpoint is full and never consumes or executes the helper.

The original registered interrupt method resolves to **0xc0432108**. IRQ bit0 dispatches virtual slot0x4d4 to **0xc037963c**, which invokes **0xc0431b4c** (completeDMA) before waking the command waiter. The actual cold boot uses original DT IRQ42 via isolated VIC1 line10 wiring; Apple's handler reads/acknowledges IRQ1, completeDMA reads DSTA0x21 and acknowledges0x21, then restores mask3. At instruction **352,602,757**, **0xc0379684** receives **R0=1**. No CPU jump or shortcut injects that success.

All **38 command responses** complete, with **one** whole-source lookup/start/copy, zero card/transport/MMIO/data errors and separately acknowledged IRQ/status. Every captured source byte and every received byte equals the original helper frame SHA-256 `90f8e97b50ecbbc45c68f4fa07379ea46d02e157aa2bd0ba86aa5f3d0a72057c`. **168,062 contracts pass**, retaining all114,312 earlier checks with the feature disabled and adding53,750 transfer tests. [Milestone 062](../tweets/062_sdio_first_helper_completion.md) links durable reports, full trace, original IRQ/completion disassembly, helper provenance and the unchanged-input/source-hash audit.

The opt-in command decoder checks descriptor and actual standard write/function1/block/fixed-address/count2 fields, configured port,32-byte block size, aligned bounded physical range, selected/powered/ready state and finite capacity. Unsupported count0 (standard512), other forms, bad mappings and changed prerequisites cannot produce partial payload or completion. Command response, data start, data deadline and acknowledgement remain separate. Cancellation covers controller/card/FIFO reset, disable/power, backwards time and overflow; status and IRQ masks/acknowledgement are independent. Original Apple code plus the primary openiBoot SUCCESS_DATA definition support the status/IRQ semantics.

Independent review separated in-flight cancellation from controller-event acknowledgement. Reader rebinding, card powerdown/disable/reset and time rollback now preserve an already completed data status0x20/IRQ1; explicit controller/FIFO reset can clear them, while guest status/IRQ W1C remain separate.647 additional checks retain the initial167,415, and the old candidate fails30 new assertions. A fresh corrected boot reproduces the entire original MMIO trace and both run/data reports byte-for-byte; milestone062 retains initial and corrected source audits plus the negative regression.

Sources and reproduction are isolated in `.context/sdio-first/data/README.md`;060 remains frozen. The same reviewed private missing-calibration/local-MAC policy applies, with no original DT or Apple executable mutation. No production machine/snapshot change has landed. The next gate must model the remaining helper stream, termination and execution handshake before any requested main-firmware bytes or ready marker. A full first frame is not a loaded helper, operational interface or working network.

## Isolated full-helper receive and next failure

The new opt-in receiver now stores the complete **2,140-byte original helper**, with the entire **2,336-byte wire stream** verified: 36 data transfers of 64 bytes, followed by one 32-byte zero terminator. Both actual request buffers and mapped DMA source buffers equal the original-code prediction; independent deframing equals the actual retained download memory and original helper. The final short payload correctly leaves 20 previous-frame bytes in its transported padding, which are excluded from executable payload. Wire SHA-256 `2580e629b83e3c101f0826dacfb2182db01e6d36a547bfa4433da5c0a110c33e`; payload SHA-256 `f1404eaae9b12a4858ac7440567de187de78dc0b8704bd693ee7de94dec74211`.

The unchanged helper uploader immediately submits each next synchronous frame after a successful return; it has no per-frame readiness poll or delay. It also **ignores the final terminator's return**. The positive gate therefore requires all **37 actual DMA returns and separate IRQ/status acknowledgements**, including that final transfer. It completes 146 command responses with zero errors at **0xc0379684**, instruction **1,239,997,594**. The original frozen endpoint is separately continued to its truthful second-frame no-space R5 response, preserving its first 64 bytes.

A separate continuation reaches the actual next dependency: main loader **0xc038a788** after exactly **100 zero requested-length polls** of registers 0x10/0x11 (200 additional CMD52 reads, 346 responses total). It stops before retry at instruction **1,678,065,205**, retaining the complete received helper. This is a deliberate negative execution gate: no helper CPU runs, no requested main-image size or firmware marker is fabricated, and no main firmware or network exists.

The model uses real bounded download memory, separate from the transfer FIFO; FIFO space returns only after actual payload append. Helper mode admits a new frame only into an empty FIFO, including when configured larger, so preexisting bytes cannot silently disappear. Malformed framing preserves physically received bytes and records a distinct receiver diagnostic rather than inventing a CRC failure. A golden-image hash is only an external acceptance oracle. Reset retention is declared emulated policy; the reviewed 062 controller-latch rules remain intact.

**310,197 contracts pass** (168,062 retained +142,135 new), covering arbitrary payload conservation, exact capacity, padding, malformed and early termination, finite FIFO, cancellation, committed-memory retention and stale events. [Milestone 064](../tweets/064_sdio_helper_received.md) links all three durable traces/reports, combined verifier, root's separate byte audit and the initial 600M-budget attempt that correctly received 20 frames before stopping. Successful positive/negative runs used explicit 1.5B/2B instruction limits. All original hashes and the unchanged reviewed private-DT policy are audited. Production machine/snapshot state is untouched; sources and reproduction remain isolated under `.context/sdio-first/helper/`.

The subsequent source investigation and first bounded semantic-model experiment are documented below. The064 receive-only candidate remains unchanged; its negative continuation is retained as a control.

## Isolated first main header and payload

**066 passes a new bounded gate using a semantic model of the original helper, not execution of its CPU.** Actual relocated ARM/Thumb code selects the mode2 SDIO callbacks and requests a16-byte header. The original Apple loader sends32 physical bytes, including its actual preserved padding; the parsed original header requests512 bytes, of which the helper callback copies508 to card memory. The unchanged host consumes the next16-byte request at **0xc038a8c8**, source offset528, after both original transfer returns have committed. The probe stops before another header, at instruction **1,741,652,858**.

All **2,880 actual wire bytes** (the earlier helper stream plus32+512) match original Mach-O contents and the original buffer-padding behavior. **39 actual DMA returns and IRQ/status acknowledgements**,162 successful command responses,508 stored bytes and four untouched memory bytes pass independent verification. Main-stage requested-length pairs are exactly **16→512→16**; a separate earlier zero pair is checked. The disabled-model control still reaches the original100-zero-poll failure at0xc038a788, instruction1,678,065,205. Root's independent audit extracts bytes directly from the original Mach-O segments, without the implementation's fixtures or logic.

**549,969 contracts pass**, preserving310,197 earlier checks and adding239,772. The exact received helper bytes select the explicitly supported semantic implementation; arbitrary body bytes still drive the actual copy. Completed card-side data moves into separate retained storage directly at the public endpoint-receive API, including calls outside the DMA wrapper. The DMA path does not enqueue it twice. Host/FIFO reset or callback rebind preserves completed pending bytes; unfinished transfers cancel. True card reset clears card state. This is declared retention/reanchor policy.

The requested-length bytes remain independent CMD52 samples. Deadline-boundary tests expose mixed values; they fail the already bounded transfer admission rather than a special mixed-value blacklist. This is not proof that real hardware rejects the same malformed host requests, and no ungrounded atomic latch is introduced. Actual guest acceptance separately requires coherent reads. The reached mode2 copy ignores the header's fourth word and skips four body bytes; no software CRC check is implemented or claimed from those fields. Unknown header commands produce error indication17, which the original host aborts on. Command4's downloaded-code handoff remains unsupported, with no firmware-ready marker.

[Milestone066](../tweets/066_sdio_first_main_payload.md) and its [durable evidence/source](../docs/traces/sdio-main/README.md) preserve the original disassembly, both cold boots, all hashes, separate audits and the initial hook-capacity setup failure. Work remains isolated under `.context/sdio-first/main-first/`; production machine/snapshot layouts and browser assets are unchanged. Full main download, its additional memory mappings, handoff/execution, interface and network backend are later gates.

## Isolated complete main container and handoff request

**069 completes the entire original main-container transfer/storage gate.** The unchanged host sends234 headers and233 bodies:231 lengths512, one504 and one88. Rounded physical transfers include actual reused padding. With the earlier helper this totals **504 transfers and128704 wire bytes**. All **117932 copied bytes**, exact write masks, **852 untouched mapped bytes** and64 outside canaries match original-MachO-derived oracles. The two actual write intervals are `[0,0x16d74)` and `[0xc0001000,0xc0006f38)`; bounded mappings exclude relocated helper code/tables/RX. The retained2140-byte helper receipt is compatibility/provenance storage, not an address-zero alias after main writes.

The final actual command4 header names entry0. Its504th original IRQ/status/DMA acknowledgement and successful return are committed before the probe stops at **0xc0379684**, instruction **17,517,070,017**. The semantic model records `MAIN_HANDOFF` and requested0, stops accepting receives, and does not execute a branch or advertise firmware readiness. The host has not yet consumed that zero request or returned from its overall main loader. Early command4 cannot satisfy the external complete-image acceptance gate.

**1,012,343 contracts pass** (549,969 retained +462,374 new). The actual audit verifies3414 command responses,504 sequential completion chains and467 coherent host length pairs. All original source/request wire, complete banks/coverage/guards and the final physical header pass both the implementation-independent verifier and root's separate original-file reconstruction. The earlier first508-byte and no-model100-zero-poll gates reproduce their exact original instruction counts in the new build.

The inherited verifier's exact600-tick first-observation assumption fails once:3413 commands are first observed complete at600 ticks and one at616. The failed assertion and exact event remain preserved. Reviewed accounting requires no early observation and unchanged acknowledgement/response ordering, with the full histogram explicit; the source of the16-tick observation delay is not proven by the filtered trace. Device code and deadline contracts were unchanged. The fixed22B-instruction/900-wall-second run finishes in780 seconds amid untimed functional browser checks; this is budget evidence, not a speed claim.

[Milestone069](../tweets/069_sdio_complete_main_received.md) links the [durable actual traces, bytes, independent audits and isolated source](../docs/traces/sdio-full-main/README.md). Production SDIO/machine/snapshot code and all original firmware/work-disk inputs remain unchanged. Next is a source-grounded contract for the original host continuation and command/RX/TX/interface protocol. Full ARM execution of the card firmware is not assumed mandatory: a supported high-level device/firmware model is possible, but eventual readiness must follow actual initialized protocol state and verified received-image compatibility, not a forced marker after byte counts match.

## Gates after enumeration

1. **Card identity and firmware transport:** derive the genuine driver's CIS matching, function enable and interrupt behavior, CMD53 byte/block transfer semantics, buffer address/length registers, and exact helper/main upload handshake. Confirm transfer bytes and checksums before declaring firmware ready. The controller exposes buffer address `+0x44`, block length `+0x48`, block count `+0x4c`; the isolated request gate above records their actual programming; the isolated helper and first-main transfer gates above now verify its narrow DMA/completion forms; additional forms remain unimplemented.
2. **Genuine interface:** process the driver's real Marvell command/response/event protocol sufficiently for hardware specification, MAC address, scan, open association and link state. Require an actual `en0` and Wi-Fi Settings network before claiming connectivity.
3. **Ethernet and native backend:** add host-independent packet TX/RX callbacks to the core. Validate frames with captures; use a native user-mode networking backend for ARP/DHCP/DNS/TCP. The host backend belongs outside `core/`.
4. **Browser backend:** a browser worker cannot open arbitrary raw TCP/UDP sockets. Plan an explicit supported gateway transport or a controlled in-process service for guest-generated Ethernet/IP traffic. A JavaScript `fetch` does not, by itself, supply the guest's TCP/IP interface. Choose deployment/authentication policy when implementing that gateway.
5. **Real Safari acceptance:** load a controlled plain-HTTP page through the guest stack, then test its actual request/response and rendering. Old TLS and discontinued app services remain separate compatibility work even after the network interface works.

The [openiBoot controller register definitions](https://raw.githubusercontent.com/planetbeing/iphonelinux/master/openiboot/includes/hardware/sdio.h) independently agree with the register page and offsets. Its [SDIO implementation](https://raw.githubusercontent.com/planetbeing/iphonelinux/master/openiboot/sdio.c) is a useful cross-check for card setup and CMD52/53 layout, but Apple's observed sequence takes precedence. Its [WLAN upload code](https://raw.githubusercontent.com/planetbeing/iphonelinux/master/openiboot/wlan.c) and Linux's [Marvell SDIO register definitions](https://raw.githubusercontent.com/torvalds/linux/master/drivers/net/wireless/marvell/libertas/if_sdio.h) provide later-stage hypotheses: function-1 I/O port bytes at 0–2, readiness at `0x20`, requested upload size at `0x10`, and firmware marker `0xfedc`. The isolated readiness gate above now observes the port and pre-helper status reads; firmware upload completion and the ready marker remain unproven.

## Reproduce the negative gate

Build the native emulator, use an extracted work rootfs, and cold boot. Replace `ROOTFS.img` with the local work image path; no saved Home state is used:

```sh
mkdir -p .context/networking
build/tools/iphone1 --board m68 \
  --kernel web/firmware/kernel_1A543a.macho \
  --devicetree web/firmware/devicetree_m68ap.bin \
  --ramdisk ROOTFS.img --ram-mb 256 --steps 1500000000 \
  --debug-enabled --unmatch arm-io/usb-otg --mmio-watch 0x38d00000 \
  --serial .context/networking/cold-serial.log \
  > .context/networking/cold-boot.log 2> .context/networking/cold-mmio.log
python3 tools/firmware/ksym.py web/firmware/kernel_1A543a.macho dis c0431480 88
python3 tools/firmware/ksym.py web/firmware/kernel_1A543a.macho dis c0430920 110
python3 tools/firmware/ksym.py web/firmware/kernel_1A543a.macho dis c03798bc 75
```

This workspace's baseline used `.context/build-audio/tools/iphone1` and `.context/organizer-debug/rootfs-compact168-checked.img`; generated evidence is in `.context/networking/`, including `report.json` with register-access counts. The durable facts and exact command gate are recorded above so a new workspace does not depend on those gitignored files.


## Private checkpoint investigation — retained restore failure

The first safe-boundary checkpoint attempt repeats the complete069 upload at its exact17,517,070,017-instruction gate; both original-file wire/IRQ audits pass. The private SDIO sidecar is exact after reload, but initial machine canonical equality fails before continuation: the standard v39 loader clears Z1 rx_len8→0 and does not retain its receive buffer/uptime fields. The pair remains unpublished; no reusable checkpoint or later firmware readiness is claimed. [Exact field diagnosis, source and evidence](../docs/traces/sdio-checkpoint/README.md). The complete private v2 follow-up below now passes; the original failed pair remains unpublished and is not reconstructed. Production remains unchanged.


## Verified private checkpoint and original-driver continuation

**073 establishes a private reusable restart point after the complete069 upload.** One new boot reaches its exact original PC/instruction/tick gate in785 seconds under the unchanged22B/900s bound. The version2 sidecar adds every118 non-host Z1 leaf to the106-field SDIO record, with explicit portable encoding, full bounds/checksum checks and complete machine/sidecar canonical binding. All36 source/binary/input pins are rechecked. Both original-file byte/IRQ audits pass unchanged.

Initial fresh-load/re-save is fully byte-identical. The original warm machine remains alive and matches the fresh phone through41,947,204 further instructions and4,581,748 actual SDIO transactions. Five full architectural/device comparisons pass; only the independently checked three CPU cache statistics and their verified checksum differ after execution, never device/RAM state. Both actual drivers consume1,000 status8 responses and reach `c038a8ec`, r4=1000. There are no new DMA completions or SDIO IRQ edges in this polling continuation; the separate synthetic gates cover in-flight and completed-unacknowledged device state.

The manifest-last publisher and actual immutable-file package reload pass only after these independent audits. The local191,881,187-byte machine and246,287-byte sidecar stay isolated; exact manifest/hash metadata, full text logs and a lossless copy of the identical binary transaction streams are durable.3,987 focused Z1 checks,36,578 lifecycle checks and65 real-C package checks pass, alongside retained1,012,343 unchanged SDIO contracts. V1's actual failure and the earlier padding/omission negatives stay preserved.

[Milestone073](../tweets/073_reusable_wifi_checkpoint.md) · [actual result and exact limits](../docs/traces/sdio-checkpoint/v2/actual/README.md) · [separate root review](../docs/traces/sdio-checkpoint/v2/actual/root-review.json). No production machine, snapshot, browser asset, firmware input or user profile changes. This checkpoint does not execute helper/main firmware, advertiseFEDC, create an interface or provide connectivity. Next is a source-first supported handoff/readiness contract using this proven checkpoint, with the current1,000-poll failure retained as a negative control. No further full cold boot is needed merely to inspect the next path.


## Original-driver initialized-service verification branch

**077 passes a narrow isolated service/host-verification gate.** The exact073 package loads canonically; its disabled continuation reproduces all41,947,204 instructions and4,581,748 transactions of the1,000-status8 failure. With the separately initialized finite service, the actual driver consumes status9, requested0, returns from its complete-image loader, reads successful0x34=dc/0x35=fe and commits the original ARM-SVC branch to `c0380208`. All22,919 events, ordered responses and retained image/coverage bytes pass both independent parsing and root review. The next unsupported register6 request has not executed.

The service requires the actual supported helper/image/masks/handoff plus clean transfer state and successful transactional allocation of two guarded320-byte buffers. Real reserve/commit/read/release/reset operations are tested; opcode execution explicitly remains unsupported. A600-guest-tick initialization delay is declared device policy. No runtime CMD53 path, card interrupt or downloaded firmware CPU is implemented, and status9 cannot reopen the old HANDOFF parser.

The separately authorized marker-disabled negative enters the real verification loop and consumes three zero-valued pairs, then stops at its unchanged100M instruction cap inside Thumb kernel code. It does not reach the intended100-attempt timeout; **that full negative gate remains incomplete**, with no raised cap or repeat. Its complete41,254-event prefix and failed report remain separate from the accepted control/positive result.

48,451 focused checks,1,012,343 retained SDIO checks,36,578 lifecycle checks,16,396 probe-mode/pair cases and13 parser fixtures pass. Deliberately removed image validation fails110 checks; original harness and prefix-audit assumption failures are preserved. [Milestone077](../tweets/077_original_wifi_verification_branch.md) · [actual traces, scope and tests](../docs/traces/sdio-initialized-service/README.md). Production, original inputs,073 package and user profiles remain unchanged. Register/command/interrupt behavior and a genuine interface remain later source-grounded gates; this is not network connectivity.

### First runtime request remains unobserved (retained follow-up to077)

The next isolated disabled control passes with exact073 raw trace equality. Its one positive records the original077 boundary, actual register6 R5 `0x1100` and ignored wrapper return1, but fails the observer's predicted command path at ARM-SVC `c0380804`, r4=20, after2,410,187 instructions. No runtime CMD53 occurs. The harness rejected before capturing the first packet, so its actual opcode/sequence/MAC/padding are missing; no complete positive audit ran. Prior source inspection suggests the initial power-on path first enqueues a MAC-address command004d, but that is an inference, not observed packet bytes. [Preserved evidence and precise boundary](../docs/traces/sdio-initialized-service/request-observer-stop/README.md).

The frozen failure is retained without retry or raised limits. An incomplete corrected observer exists privately but has not been built or validated. Further command transport, response and network-interface work remain unfinished; no milestone080 or production change is claimed.
