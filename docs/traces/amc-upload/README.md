# Original AMC program upload — isolated verification

The original driver writes **59,622 program/data bytes** from the selected `Espico_aac` descriptor table into SRAM. An independent Python oracle reads the supplied original Mach-O file, reconstructs every segment and compares the **entire 180,224-byte SRAM** against the actual guest result. SHA-256 is `011452fdff9699442c49115354c9b0bced1b98265ba64ee5b95c1c48bbb01583` for both. No program bytes are supplied by the host device model.

This uses an explicitly approximate **synchronous idle reset**. The isolated model accepts the observed complete core/DE requests (`0xa000`, then `0x0a00`, CLOCKGEN+`0x50`) only with clocks 3/4 and power domain 4 enabled. Reset completion generates status bit 8; bit 9 always stays clear. SRAM and the separate divider are retained. Power/clock gating invalidates completion; re-enabling alone cannot complete reset. Unsupported masks, order, width, register requests and undeclared SRAM writes stop before device mutation. Exact original pre-upload configuration commands are accepted as idle setup with no inferred FIFO/IRQ/execution effects. This does not model an active decoder or measured silicon timing.

The positive boundary is the original `STR` at `0xc0277998` reaching the MMIO handler with `(0x38500340, 1)`, after all SRAM writes are committed and checked. The probe terminates **inside that callback, before the marker instruction retires**. It does not claim a completed post-upload device transition. Its interrupted machine state is not resumed or saved as a portable checkpoint. Engine execution, bit-9 start-enable semantics, transfers, PCM, sample pacing and audible playback remain unmodeled. No production source, firmware, clock, JIT or snapshot format changed.

## Evidence and independent checks

- [Original upload trace](upload.log): 48,013,483 bounded instructions from the genuine ringtone checkpoint; 17,687 SRAM writes and 5,563 SRAM RMW reads. No-model [control](control.log) instead retains the original reset assertion with zero SRAM accesses after 55.3M instructions.
- [20,265 idle-reset contracts](contracts.log) cover dependency/order/partial-mask rejection, repeated reset, status/bit9 rules and atomic rejected requests. [30 probe write-footprint contracts](bus-contracts.log) include undeclared zero writes, partial/unaligned/overrun writes, and writes before upload. Prior Stage A adds 279 storage and 14 bus checks.
- [External original-file oracle](kernel-oracle.json), [its output](kernel-oracle.log) and [source](source/kernel-oracle.py) verify original source words, lane lengths, span bounds, full byte contents and exact guest write footprint. Eight deliberate corruptions include a swapped program-memory lane and missing coverage for an expected zero byte.
- [Source, binary and fixture hashes](verification.json). The first successful prefix and the tightened replay remain separately preserved under `.context/amc-first/stage-b/`; their traces are byte-identical. No unexpected guest prefix was hidden or accepted after a failing run.

The table mapping is separately grounded in original code. At selection return `0xc02778b8`, the actual index is 1. [Original setup](aac-table-setup-dis.txt) places `r6` at table+`0x10`, with name `Espico_aac` at +`0x14`. The setup fills `r6` using [three original getters](aac-table-getters-dis.txt) returning `0xc02a4100`, `0xc029c884`, and `0xc02a2054`; these exactly match the actual program/data copy arguments. This names the uploaded table, not a demonstrated AAC decode.

| Bank | Original data copied | Guest operations |
| --- | --- | --- |
| `pmem`, SRAM+`0x1c000` | 40,272 bytes; eight interleaved lanes | 10,068 32-bit writes |
| `ram_mc2`, SRAM+`0x28000` | 11,126 bytes | 5,563 packed halfword RMW operations |
| `ram_mc1`, SRAM+`0x14000` | 8,224 bytes | 2,056 32-bit writes |

The write footprint covers 59,628 bytes, **six more than the descriptor data**. These are precisely the retained neighboring halfwords of the original 32-bit RMW:

| SRAM-relative bytes | Why the guest touches them |
| --- | --- |
| `0x29230..0x29231` | First halfword index 2329 is odd, so its full-word RMW retains the lower neighbor at index 2328. |
| `0x29bbe..0x29bbf` | The span ending at halfword index 3550 retains upper neighbor 3551. |
| `0x2ae5e..0x2ae5f` | The final span ends at halfword index 5934 and retains upper neighbor 5935. |

The original-file oracle reconstructs that full-word footprint and proves those six bytes stay zero under the stated initial SRAM policy. It also proves every descriptor byte was actually written, including bytes whose expected value is zero. Initial-policy bytes are not described as measured hardware reset contents.

## Reproduction

The complete isolated sources are in `source/`. Put the matching `web/emu_api.c` alongside them (SHA-256 `87dd9c5c3ec3047e907935a2e61fb8a70d0975ec94184b50aa97d627646aaec1`), or compile the probe with `-Iweb` to locate that same API. Build against the hashed native archive using `-O2 -Iweb/tests/include -Icore/include` and `-lm`. The storage/reset tests need only their corresponding `.c` files; the probe and probe-bus tests additionally need the native archive. Example with the preserved working tree:

```sh
cc -O2 -Iweb/tests/include -Icore/include .context/amc-first/stage-b/probe.c .context/amc-first/stage-b/amc_idle.c .context/amc-first/stage-b/amc_sram.c .context/build-native/core/libemucore.a -lm -o .context/amc-first/stage-b/probe
mkdir -p .context/amc-first/stage-b/recheck
.context/amc-first/stage-b/probe .context/audio/ringtone-page.snap 1 .context/amc-first/stage-b/recheck
```

Redirect stdout to `recheck/trace.log` when running the independent oracle against that directory. Probe mode 0 is the no-reset-model control. Raw SRAM/mask artifacts remain in `.context`; they are neither production assets nor resumable snapshots.
