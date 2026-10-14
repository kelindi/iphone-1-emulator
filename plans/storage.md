# Move filesystem storage out of guest RAM

## Recommended boundary

Run the genuine 1A543a flash stack over an emulated FMC controller and raw NAND
backing. Do not write a second host implementation of Apple's FTL. The firmware
already contains `AppleNANDFTL`, including VFL/FTL initialization, formatting
and block-device publication. The missing emulator boundary is below it.

This is larger than attaching an HFSX image to the existing raw NAND helper.
The first implementation gate should be one bank's reset and READ ID transaction,
validated against the real driver's behavior. It should not claim filesystem
support or change the default starting phone yet.

## What exists

| Component | Evidence and current limitation |
| --- | --- |
| Raw NAND | `core/src/soc/nand.c` supports geometry, page/OOB reads, program-only bit clearing, erase and bad blocks. It allocates the entire device in host memory. |
| NAND persistence | `storage.c` saves data, spare bytes and the bad-block map with transactional validation. Its full-image container is unsuitable for sparse multi-gigabyte browser devices. |
| Guest controller | No FMC entry in `machine.c`'s device windows, no NAND member in `s5l8900_t`, no FMC IRQ or snapshot state. |
| Host VFL/FTL | Absent, explicitly deferred by `nand.h`. Raw page tests are not guest storage integration tests. |
| Guest stack | Kernel prelink personalities include `AppleS5L8900XFMC`, `IOFlashStorage`, `AppleNANDFTL`, `IOBlockStorageDriver`, partition schemes and `IOMediaBSDClient`. |
| Boot path | `bringup_os1.c` copies the entire RAM disk into DRAM and places subsequent boot reservations above it. The workaround consumes wired guest memory. |

The inherited `md_bridge.c` and `md_raw_bridge.c` also provide host-backed
memory-disk transfer mechanisms, but their documented ABI requires replacing
audited guest instructions with Thumb SVC patches. They are not an eligible
shortcut under this project's unchanged-Apple-binary rule. Reusing their generic
backing abstractions does not remove the need for a genuine guest device boundary.

The stock matching chain is `AppleARMIODevice` (`fmc,s5l8900x`) → FMC →
`IOFlashStorageDevice` named `disk` → `AppleNANDFTL` → block storage/IOMedia →
partition scheme and BSD disk nodes. The device tree describes FMC at physical
`0x38a00000`, size `0x1000`, IRQ 20, DMA FIFO address `0x38a00080`, and a disk
bank mask `0x0f`. These are taken from the shipped M68 device tree, not inferred
from later phones.

The prelinked bundle contains both FMC and higher-probe-score ADM-FMC
personalities. The current real boot selects the ordinary FMC path: it prints
`AppleS5L8900XFMC::start` and executes its reset and READ ID helpers. A mandatory
ADM microcode implementation is therefore not the first blocker on this boot.

## Native probe: the first missing behavior

The unchanged current core, clean compact RAM disk and 300 million guest
instructions produce this trace:

| Instruction | Access | Meaning demonstrated by guest code |
| ---: | --- | --- |
| 131,769,952 | write `0x800` to FMC `+0x04` | Controller reset request. |
| 131,769,956 / 959 | read `+0x04` → 0 | Driver waits for reset bit `0x800` to clear. |
| 131,769,996 | write `0x779ff` to `+0x00` | Controller/bank selection setup. |
| 131,770,001 | write `0xff` to `+0x08` | NAND reset command. |
| 144,240,269 | read `+0x48` → 0 | Driver samples bank reset completion after its delay. |
| 144,240,275 | write `0xff0` to `+0x48` | Acknowledge the bank-completion bits. |

The guest reports `no NAND flash banks are ready`. Disassembly at
`0xc0420208` shows the reset helper shifting status bits 4–11 into a bank mask.
The missing status therefore prevents bank discovery before any flash page
data or FTL metadata is accessed.

A separate **diagnostic-only** bus wrapper responds with bank 0's completion
bit (`0x10`) after command `0xff`. The unchanged guest then proceeds to:

1. Select bank 0 (`+0x00 = 0x77803`).
2. Issue READ ID (`+0x08 = 0x90`).
3. Set address count/address (`+0x2c = 0`, `+0x0c = 0`).
4. Start the address cycle (`+0x04 = 1`).
5. Poll status `+0x48` for bit `0x04`, then time out because the probe does not
   implement address completion.

This is a positive discovery gate, not a functioning NAND model. The wrapper is
kept under `.context/storage/fmc-probe.c`; it is not compiled into production.
`probe.err` and `probe-serial.log` contain the unchanged-core run;
`reset-probe.log` contains the one-bank diagnostic response.

Further static evidence in `_fmcReadID` (`0xc0420d74`) gives the next bounded
transaction: address completion is W1C bit `0x04`; `+0x30 = 3` requests four
bytes; `+0x04 = 0x7e2` starts the read; status bit `0x08` marks data ready; the
driver reads the word from `+0x80` and acknowledges completion. Its bundled
`device-info-list` contains supported IDs, including `0x9551d3ec`. Geometry
must be confirmed from the selected entry and the driver's published properties;
the raw helper's default page/block constants are not sufficient evidence.

## Implementation gates

1. **Real reset → READ ID.** Add a small FMC controller with explicit command,
   address, FIFO, status, W1C and completion state. Use one actual supported bank
   identity. Test reset clearing, absent-bank behavior, address completion,
   FIFO width/count, status acknowledgment and reset during a transaction.
   Run the guest past bank discovery and record the first page-read request.
   Coordinate `soc.h`, machine bus/IRQ integration and snapshot versioning before
   adding controller state. No HFS image or host FTL mapping belongs in this gate.
2. **One real page transaction.** Connect data and OOB reads to raw NAND backing,
   including the actual DMA, ECC result and interrupt behavior exercised by the
   selected driver. Return erased media and let the guest identify it as blank.
   A successful blank-page read with correct buffer contents and completion is
   the acceptance test; reporting generic success without transfers is not.
3. **Guest creates its own metadata.** The binary contains `nand-erase` and
   `nand-enable-reformat` boot-argument paths and a warning to use PurpleRestore
   for blank NAND. On a separate disposable device, investigate whether the
   genuine formatting path can construct VFL/FTL metadata and publish block
   media. Presence of these strings does not prove automatic formatting works.
   If it requires restore software, that becomes an explicit dependency instead
   of guessing on-flash metadata. Implement erase/program only as this gate
   exercises them, retaining NAND bit-clearing rules and failure semantics.
4. **Filesystem on disk.** After real block I/O and formatting work, create the
   partition/filesystem layout expected by the actual driver and stock fstab.
   First mount a secondary filesystem while booting the known RAM disk; write,
   flush, reboot and verify a file. Then cold boot root from disk and remove the
   large RAM-disk reservation. Measure guest free/wired memory and repeat real
   app workflows. Do not infer `/dev/disk0s1` readiness merely from an FMC ID.

## Backing and persistence constraints

Before provisioning a supported multi-gigabyte geometry, replace the raw helper's
eager full-device allocation with sparse page backing: erased pages are implicit,
written data/OOB pages are materialized, and block erase removes their entries.
The backing remains outside guest DRAM; host allocation and guest physical memory
must not be conflated. Preserve the existing small-device tests and add sparse
read/program/erase and allocation-failure tests. Browser storage needs its own
bounded page cache and durable chunk format; the current whole-NAND stdio
container is not that implementation.

A saved CPU/device checkpoint must identify the exact immutable NAND generation
it accompanies. Saving CPU state and newer disk pages independently can corrupt
a filesystem on restore. Include in-flight controller/DMA state and commit the
checkpoint-to-disk-generation reference atomically, retaining the previous valid
pair. Existing v39 RAM-disk profiles must remain loadable and must not silently
acquire a different disk generation or geometry.

Compaction is the verified current mitigation. This storage work removes its
6.125 MiB free-space constraint only after real block I/O and persistence gates
pass. It is independent of CPU throughput work and should not delay current
browser speed experiments.

## Newly reproduced Z1 checkpoint limitation

The isolated Wi-Fi checkpoint test exposed a limitation in the existing v39 touchscreen serializer: it writes receive/packet lengths but clears them on load, and omits the receive buffer and device uptime. The captured, deselected controller had `rx_len=8`; fresh loading changed it to zero before execution. Actual next-byte and touch timestamp controls demonstrate that the omitted fields can affect subsequent behavior. Existing Home/persistence gates do not cover every mid-transaction save boundary.

[The failed checkpoint and diagnosis](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/sdio-checkpoint/README.md) remain preserved. A private, versioned Wi-Fi research package is being extended with complete Z1 state and exact fresh-load comparison; this does not repair the production snapshot format. Before release claims cover arbitrary save instants, add a versioned production representation for the missing state, retain old-save compatibility, and test real selected/deselected SPI transactions plus touch timestamps/velocity after restore. Do not hide this limitation by normalizing compared bytes.
