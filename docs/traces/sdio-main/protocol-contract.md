# Proposed first main-header and payload gate

Source-only contract, 2026-09-05. This is a proposed **high-level semantic model of one original helper program**, not execution of its ARM CPU. No implementation, production integration, main-firmware success, or network interface is claimed. Milestone064 and its receiver remain frozen in `../helper/`.

## Provenance and control flow

The original 2140-byte helper is `../enable-plan/original-helper.bin`, kernel VA `0xc03b0c0c`, SHA-256 `f1404eaae9b12a4858ac7440567de187de78dc0b8704bd693ee7de94dec74211`. Its first branch enters file offset0x14. The startup reads the word at absolute4 (the image's word4 is2) into0x0000fffc. This assumes the startup's intended address-zero placement; actual card CPU execution/load placement has not been observed.

The actual scatter records at file0x16c..0x18f copy file0x1a8..0x7e3 to `0xc0000000`, and file0x7e4..0x85b to `0xc0000800`. The zero table clears `0xc0000878..0xc00008e7`. Startup's entry literal is `0xc0000180`, therefore **file0x328**, not file0x180. The ARM entry branches through the Thumb pointer `0xc0000559` (file0x701), whose main calls receiver `0xc000035c` (file0x504). `arm-entry-dis.txt`, `arm-copy-dis.txt`, and `thumb-receive-dis.txt` retain runtime addresses and original file offsets; `decode_helper.py` annotates PC-relative literal values. The larger Thumb listing also contains literal pools and unrelated veneers; only the identified reached functions below are treated as code. The parent's earlier offset-zero linear ARM listing is a starting artifact, not a reliable CFG.

The mode word2 selects index2 in five-entry callback tables. The receiver sets `r4=mode*4`; these are actual table values, not names inferred from an SDK:

| Table address | Index2 Thumb pointer | File offset | Observed operation |
| --- | --- | --- | --- |
| `c0000800` | `c00005b9` | `760` | Initialize: halfword stores13,0,1 to80000120,8000012c,80000124 |
| `c0000814` | `c00005d9` | `780` | Store RX buffer address to80000114 |
| `c0000828` | `c00005a9` | `750` | Poll bit0 of halfword80000128 |
| `c000083c` | `c0000599` | `740` | Store0 to80000128, then13 to80000120 |
| `c0000850` | `c000053d` | `6e4` | Copy header.length minus4 bytes, return0 |
| `c0000864` | `c00005cd` | `774` | Store requested length to80000110 |

The receiver chooses buffer `0xc0000a00` and header globals `0xc0000878`. The proposed connection from the helper's card-local requested-length register to host function1 registers0x10/0x11 is an explicit modeled register alias: it is supported by both programs' matching flow and the existing Marvell host-driver protocol, not a claim that an unpublished hardware register manual was recovered. Likewise, rearming the receive endpoint is the semantic interpretation of the actual0/13 stores; unrelated meanings of their bits remain unspecified.

## Exact first exchanges

The host code is retained in `../helper-plan/original-main-loader-dis.txt`. At `c038a724..c038a77c` it initially polls the requested-length pair up to100 times. `c038a79c..c038a7bc` rounds the physical transfer length up to a multiple of32 and chooses block size32. `c038a80c..c038a820` copies the **logical requested length** from the original main container; `c038a848` advances the container offset by that same logical count. The ordinary accepted logical range in its loop is1..2048. The host's source-offset check is not a substitute for a bounds check in our model.

| Exchange | Requested/logical bytes | Physical SDIO bytes | Meaning/consumption |
| --- | --- | --- | --- |
| First header |16 |32 (one block) | Four little-endian words; ignore the other16 physical bytes |
| First body |512 |512 (sixteen blocks) | Copy first508 bytes to card RAM; ignore final4 bytes |
| Following header |16 |32 if allowed | Proposed probe stops before its CMD53 request |

The first header is `[1, 0xc0001000, 512, 0x26486be9]`. It comes from the original main image at kernel VA `0xc0392d1c`; full122608-byte image SHA-256 is `06a5d9d1f5cc4c5209cd072bb2434058be3d52b8bebb2b022153c325f3821150`. The first header's extra16 physical bytes are **predicted zero from the actual reused buffer**: the preceding helper terminator zeroed its first32 bytes at `c0380380..c0380390`, then this header overwrites16. This must be verified against the actual mapped DMA source; zero padding must not be synthesized by the oracle/model. Later short transfers can retain old padding.

The first body is container bytes16..527. The helper copies bytes16..523 to `[0xc0001000,0xc00011fc)`; body bytes524..527 are `94 6e 44 e6` and are not copied. The model must retain the complete physical wire separately from its logical header and copied-memory audits. Existing helper upload framing (a length word plus60 payload bytes) does **not** apply to this main-image phase.

`main-records.json` statically parses the entire unchanged container:233 command1 records (231 lengths512, one504, one88) and one final command4 at offset122592, ending exactly at122608. Data destinations cover contiguous intervals `[c0001000,c0006f38)` and `[0,16d74)`. This is provenance and a future mapping constraint, **not authorization to download all records**. `expected-main-header-wire.bin`, `expected-main-first-payload.bin`, and `expected-main-first-copied.bin` are source-derived audit fixtures, not data sources for the endpoint.

## State and publication contract

Proposed supported-program admission: retain all received helper bytes exactly as064 does. Only an explicitly enabled semantic model for this exact helper may begin the main protocol after its complete valid terminator and program-identity check. A mismatched or arbitrary helper remains received but unmodeled, with requested length0. The identity check selects an implementation of known code; it does not establish that a CPU ran or that any firmware succeeded. This selection rule is subject to parent review.

1. `INACTIVE`: no announced main request. Initialize a separate bounded receive staging area (capacity at least512) and card RAM before scheduling `WAIT_HEADER`. Preserve the downloaded2140 bytes. Use guest-clock deadlines, not read counts; modeled processing latency is declared, not measured card timing.
2. `WAIT_HEADER`: publish16 only when the endpoint is selected, enabled, powered, initialized, empty, and able to accept its32-byte physical frame. The source instruction is file0x566 followed by requested-length callback0x56e and rearm0x57a. Host status bits reflect actual receive availability; firmware status remains unready.
3. `HEADER_RECEIVED`: a successfully completed physical32-byte transfer stores all wire bytes, then a separate scheduled semantic step copies only its first16 to header globals. Completion/IRQ latches remain independent and require original guest acknowledgements. Command1 selects header word2 as the next request. After validating the bounded model's complete destination and capacity, publish512 for this first header and rearm; never publish it merely because the host read a register.
4. `WAIT_BODY`: accept only the described fixed-address function1 write, block32/count16, complete mapped physical source, and512-byte storage reservation. Preserve all existing command response, DCTRL start, deadline, cancellation, DMA, and latched-event contracts. Do not extend other CMD53 forms opportunistically.
5. `BODY_RECEIVED`: after actual physical completion, the semantic callback at file0x6e4 copies508 bytes from stored RX bytes into modeled card RAM. Commit atomically after every range/state/generation check. It returns0 in the original mode2 implementation; clearing four header words and rearming produces next requested16 (file0x624→0x5e4→0x566). No next request before the complete copy. Do not use source fixtures to populate memory.

For this bounded experiment declare a512-byte card-RAM window `[0xc0001000,0xc0001200)`, initially zero, with508 expected writes and four untouched canary bytes. It represents only the literal first destination, not the complete Marvell memory map. Check `length>=4`, receive capacity, unsigned subtraction/overflow, and the entire `length-4` destination interval before mutation. The original helper lacks those software bounds checks: unsupported addresses or malformed lengths must produce a separate **model-admission diagnostic and bounded stop**, not an invented helper CRC result or SDIO wire failure. The transfer must remain physically successful if already completed. Header/body mutation negatives must prove byte conservation, rather than requiring golden payload contents.

## Commands, error behavior, and termination

Command1 means receive `header[2]` bytes then copy `header[2]-4` bytes to `header[1]`. Word3 is copied into the header global but is not used along this reached mode2 path. The last four body bytes are skipped by the copy callback. No CRC algorithm or check appears in this callback; it always returns0 after memcpy. Hardware CRC behavior and the meaning of the skipped/header words are therefore **unknown**, not assumed absent from the real chip.

An unknown header command follows file0x5a0..0x5ba: publish `16|1=17`, rearm, and wait for another header. The generic receiver has a body-error retry branch that publishes `length|1`, but its actual mode2 callback cannot take that branch by returning nonzero. The Apple host checks requested-length bit0 at `c038a8e0`; an odd value takes its CRC-error message at `c038a914` and returns failure. Thus the host can label a malformed command a CRC error; that label alone does not prove a checksum algorithm. Do not mask the odd bit or invent a host retransmission: this original caller aborts. A unit-only unknown-command case should verify17 and unchanged destination bytes; future actual invalid-header testing may observe that original failure branch.

Command4 publishes0 (file0x5ce), rearms, and branches to header word1 through the BX veneer (file0x5de→0x500). The final original record is `[4,0,0,0x188cdb1f]`, so its intended handoff is address0. It is **not a firmware-ready marker**. This first-gate model must stop with an explicit unsupported-handoff diagnostic if command4 is encountered; it must not claim a branch executed, upload completion, main execution, or `0xfedc` readiness.

## Reset, cancellation, and latch policy

Carry forward064's declared retention: committed helper bytes and main-RAM bytes survive controller/FIFO reset, function disable, host-power changes, reader rebind, and host-time rollback; true card reset clears them. Preserve062's completed-but-unacknowledged controller status0x20 and IRQ0 across card-only changes/rebinding. Only guest W1C or explicit controller/FIFO-reset semantics clear those latches.

Maintain independent transfer and semantic generations. Cancel an uncommitted DMA/staging operation on controller/FIFO reset, disable/power loss, reader rebind, or invalid/backward time. No delayed callback may later publish a request or copy cancelled bytes. A completed physical transfer whose semantic step is pending must retain its bytes; host-only reset/rebind must not silently erase that card-side completion. Quiesce processing while unavailable, preserving pending committed work for a fresh valid deadline after availability returns. This is a declared pause/reanchor policy, not a claim about measured card timing. True card reset cancels both generations and returns to helper-download state. Explicit model restart requires a fresh program admission; repeated enable/read/EOF events must not replay an already consumed header or body. Committed copy must happen at most once, even when controller acknowledgement arrives later.

## Required next experiment and negative gates

Retain all310197 existing contracts and original064 no-model/zero-request failure. Add focused contracts for identity mismatch, arbitrary stored body contents, unknown command17, lengths0..3 and overflow, short/oversized physical input, full/dirty FIFO, destination edges/canaries, unsupported command4, delayed publication, stationary polling, cancellation at each phase, completed-but-unacknowledged latches, stale generations, reset/rebind/rollback, repeated enable and terminator, and no ready marker. Include independently changed header word3/trailing4 bytes to show the modeled software copy ignores them without claiming actual hardware CRC acceptance. A body outside the supported model must not partially change RAM or fabricate transport errors.

Only after review, build in a new isolated directory and run: (a) unchanged064 negative without the semantic model; (b) supported-model cold boot preserving all original input hashes/private DT audit; (c) actual first-header32 and first-body512 transfers. Require command responses, data completion, IRQ/status acknowledgements, original DMA return values, exact source/request/stored bytes, and actual host consumption of requested16→512→16. Stop at the **host instruction hook `c038a8c8`** after the body iteration has executed `c038a8c4` (combined requested pair in `sl`), with container offset `fp=528`, requested `sl=16`, and two new main transfers completed. This is after the actual register-read result is committed, before the next header's CMD53. Also require the original body call returned nonzero at `c038a840`, and all508 card-RAM bytes plus canaries independently match. If that exact committed boundary is not reached, report the narrower observed boundary and preserve the trace.

The resulting claim would be: the unmodified Apple host consumed one modeled original-helper header/body cycle and copied the original main image's first508 bytes into bounded card memory. It would not prove helper CPU execution, full main download, firmware readiness, an interface, association, or networking.
