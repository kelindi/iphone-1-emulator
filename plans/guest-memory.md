# Guest memory pressure and the RAM-disk workaround

## Failure identified

The Contacts experiment created Ada, saved and restored successfully, then
opening the name editor returned to Home. Subsequent Settings launches displayed
the launch background and closed. Touch reports still reached the genuine
multitouch parser; the display workloop and SPI transport were responsive.

A native replay of `.context/organizer-report/broken-input.snap` shows the actual
termination path. At instruction 37,687,375,382, UIKit enters
`-[UIApplication didReceiveUrgentMemoryWarning]`, calls `terminate`, then
`_terminateWithStatus:` and `exit(1)`. SpringBoard's captured syslog says:

> Memory level is urgent (10). Asking Settings to quit.

The later abnormal exit status is 256, consistent with that explicit exit.
Evidence is in `.context/organizer-debug/{termination,syscalls,why-terminate}.log`.
This is a guest memory warning, not evidence of an input failure or a watchdog
timeout. The earlier Contacts editor failure has the same user-visible shape;
the traced terminating process is Settings.

## Why the current boot is under pressure

The emulator substitutes an approximately 184.64 MiB RAM disk for NAND storage.
It occupies wired guest memory in a 256 MiB machine. The original phone kept
these filesystem bytes on flash, so this substitution imposes a large extra
memory cost even though the emulated machine already has more RAM than the
original hardware.

Stock XNU computes its memory level from active, inactive, speculative and free
pages as a percentage of total physical pages. Its five-point update hysteresis
can keep the reported level at 10 after an application exits. SpringBoard then
asks subsequent applications to quit. Changing this policy would hide the
storage model's cost, so the experiment leaves XNU and UIKit unchanged.

| Snapshot | Reported level | Initially free pages | Wired pages | RAM |
| --- | ---: | ---: | ---: | ---: |
| Shipped Home | 15 | 13,278 | 53,876 | 256 MiB |
| Broken organizer profile | 10 | 13,278 | 53,744 | 256 MiB |
| Clean 168 MiB compact-image boot | 20 | 17,470 | 49,286 | 256 MiB |

Pages are 4 KiB. These snapshots are at different points in guest execution, so
the wired-page differences include ordinary runtime allocation differences.
The initial-free-page increase is 16.375 MiB. The image itself shrinks by
16.6445 MiB. Relevant kernel symbols are `kern_memorystatus_level` at
`0xc01c234c`, `vm_page_free_count_init` at `0xc01dfba8`, `vm_page_wire_count`
at `0xc01dfeb0`, and `max_mem` at `0xc01d9678` in build 1A543a.

## Bounded compaction experiment

The clean work image contains 161.875 MiB of allocated HFSX blocks. Its files
have inline extents, the extents-overflow tree is empty, and it has no journal,
attributes fork, or startup fork. A copy-only prototype moves allocated blocks
together, rewrites extent starts and both volume headers, and rebuilds the
allocation bitmap. It deletes no files and verifies SHA-256 equality for all
5,120 ordinary data and resource forks before writing the new image.

The 168 MiB candidate passes an independent allocation-ownership audit and
cold boots to the genuine Home screen. Native validation creates Ada in
Contacts, saves a full checkpoint, reloads it, opens Edit → Name, and remains
in the functioning name editor after another billion guest instructions. Its
reported memory level remains 20 throughout. A separate Chrome validation
imports the fresh checkpoint through the real UI, creates Ada, saves/reloads,
edits the name to Adam, and saves/reloads again with exact framebuffer equality.
The subsequent Clock and Settings launches also succeed; the real Settings
rows appear in `20-settings-correct-guest.png`. Evidence is under
`.context/organizer-debug/compact-*` and `.context/contacts-compact-report/`.
This verifies the previously failing workflow on a fresh profile. It does not
establish that every application fits the remaining RAM or disk-space budget.

The retained [copy-only tool](../tools/firmware/hfsx_compact.py) produces output
byte-for-byte identical to the booted prototype:

```sh
python3 tools/firmware/hfsx_compact.py INPUT.img OUTPUT.img --size-mib 168
python3 -m unittest discover -s tools/firmware -p test_hfsx_compact.py -v
```

Seven synthetic tests verify fragmented data/resource content and extent
relocation, both headers, allocation accounting, and rejection of dirty
volumes, allocation mismatches, overflow trees, overlaps, leaf cycles, and
insufficient free space. Existing destination paths are refused. The dirty
organizer RAM-disk export is also rejected in a real-image check.

Other size budgets, calculated from the same clean image, are:

| Total image | Filesystem free | Guest RAM recovered |
| --- | ---: | ---: |
| 166 MiB | 4.125 MiB | 18.6445 MiB |
| 168 MiB (booted candidate) | 6.125 MiB | 16.6445 MiB |
| 170 MiB | 8.125 MiB | 14.6445 MiB |
| 174 MiB | 12.125 MiB | 10.6445 MiB |

Only 168 MiB has been boot-tested. Free disk space is a real tradeoff: this
approach cannot provide substantial imported music, photos, or other user data.
Separating persistent storage from guest RAM remains the durable fix.

## Pristine starting-phone artifact

The separately prepared `clean-ready-home.snap` contains no test Contacts or
other app activity. The genuine Repair Needed alert was dismissed by a touch at
instruction 10,010,000,000, and Home was saved at 10,500,000,000. It reports memory
level 20 and preserves 63 successful WM8758 packed writes from cold boot, with
zero refused writes or unknown reads.

The original compact cold boot used the 12:40 native build, including WM8758,
MPL and touch fixes. The codec owner's subsequent 12:50 change was formatting
only. A fresh repeat was interrupted by host SIGTERM before creating output;
the pristine artifact therefore resumes that equivalent original cold boot
with the current rebuilt core. Board, touch, codec and snapshot tests all pass.
Build hashes are recorded in `.context/organizer-debug/pristine-provenance.txt`.

| Artifact | SHA-256 |
| --- | --- |
| Source `.context/fw/rootfs.img` | `b414e48efd3ce5c436f08981c2a73b818ba0b457e78a171f3f55aa7bc18e87d8` |
| `.context/organizer-debug/rootfs-compact168-checked.img` | `0b544d47fb67603167099e658d50e3c284c28233d489263b269682c4f9ac53b0` |
| `.context/organizer-debug/clean-ready-home.snap` | `094a6d53d0df60f435b04a6871e50842414d855e9fdeaf441a09a908083a8410` |

The matching screenshot is `.context/organizer-debug/clean-ready-home.png`.

The compact disk remains the default multipart asset in `web/manifest.json`.
The first compact checkpoint described above used a 100,894,841-byte gzip
expanding to 245,747,682 bytes. It has since been replaced by the
[USB-enabled starting phone](../tweets/036_home_service_recovery.md), which
keeps the same disk and passes the Contacts regression again. The manifest
verifies every current compressed part.
[Milestone 032](../tweets/032_contacts_memory_recovery.md) retains the browser
workflow recording and thumbnail. Existing saved phones still take precedence
over these starting assets; Export before Reset to keep an older phone.

## Existing profiles

Existing full checkpoints remain untouched. Extracting their mounted RAM disk
does not create a safe migration image: the broken organizer image has dirty
volume metadata and a fork-owned block absent from the on-disk allocation
bitmap. The filesystem may still have metadata cached elsewhere in guest RAM.
The prototype rejects it. A clean compact-image boot is a separate fresh
profile, not a migration of Ada or any other saved user data.

Increasing RAM also needs hardware-map work. RAM starts at `0x08000000`, and
256 MiB ends at the documented EDRAM aperture `0x18000000`. The current overlap
validator covers modeled device windows, so its accepting a larger allocation
would not prove that larger geometry faithfully matches the platform.


## Clean shutdown disks with initialized empty attributes

A genuine OS1 shutdown can leave an initialized but empty HFSX attributes fork,
even when the initial work image had none. The compactor now accepts only the
verified empty form: a standalone header/map, zero depth/root/leaf records,
consistent node size/count/free accounting, and a bitmap marking only the header
node allocated. Nonempty attributes, external map chains, startup forks, dirty
volumes and extent-overflow data remain unsupported. The complete attributes
fork is copied byte for byte and its volume-header extents are relocated.

Ten synthetic tests pass, including fragmented attributes relocation and
malformed/nonempty rejection. Independent traversal of the real clean shutdown
disk from the legacy-upgrade investigation confirms all 5,178 ordinary forks and
the entire 1 MiB attributes fork are unchanged after a separate one-block-smaller
copy. AddressBook bytes match and SQLite integrity_check returns ok. The source
stays unchanged. This tiny shrink verifies relocation; it is not an additional
meaningful memory saving or proof that every old phone fits 168 MiB. The already
168MiB source has too little free space for an integer 167 MiB target while meeting
the compactor's 4 MiB reserve. Evidence:
`.context/legacy-upgrade/empty-attributes-compaction-report.json`.

This capability is part of [the retained-data upgrade investigation](legacy-upgrade.md).
It does not make mounted snapshots safe to compact or add an automatic browser
upgrade flow.
