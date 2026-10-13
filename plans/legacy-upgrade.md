# Preserving an older phone while updating its boot configuration

> Historical recordings and publishing artifacts have been removed from Git history. The summaries below describe the original runs; remaining test reports and screenshots are retained. Historical hash receipts may predate this archival cleanup.

Investigation started 2026-09-05. This is a disposable-fixture experiment, not a
released migration feature. Existing browser profiles remain untouched.

## Why an exported snapshot stays slow

A v39 checkpoint includes the entire running kernel, USB registry and mounted
RAM disk. Loading it in a newer emulator preserves the old USB-unmatched boot,
including the missing `lockdownd` dependency. The conservative interactive-clock
gate correctly leaves that phone on its earlier timing policy. See
[the transition investigation](transition-latency.md).

The possible data-preserving route is a genuine guest shutdown, extraction of
its flushed disk, then a fresh boot of that disk with the original USB-enabled
device tree. This replaces running OS state while retaining the phone's files.
It is different from copying a mounted dirty RAM disk or resetting to the
pristine starting phone.

## Genuine shutdown evidence

The disposable source is the older compact Contacts fixture, with the original
Ada contact already edited to Adam. It is USB unmatched and uses a 168 MiB disk.
A five-guest-second Power hold displays Apple's real slide-to-power-off control.
A normal touch drag across that control performs shutdown. The diagnostic
harness sends normal button/touch input and reads retained boot-args/device-tree
RAMDisk geometry; it does not change an Apple binary or filesystem metadata.

| Event | Retired instructions | Guest timebase ticks | Primary HFSX flags |
| --- | ---: | ---: | --- |
| Loaded old saved phone | 21,235,800,000 | 5,917,273,709 | `0x00000000` (dirty) |
| Guest marks disk clean | 22,171,650,000 | 6,174,674,754 | `0x00000100` |
| PMU standby command observed | 22,174,250,000 | 6,175,808,647 | `0x00000100` |

Serial output records daemon termination, `CPU halted`, baseband/modem power-off,
LCD disable, and `pmu go stdby`. PMU standby alone also occurs in normal sleep,
so the clean disk transition and shutdown sequence are essential evidence.
The alternate header still carries its older dirty flag; no host code changes
it. The maintained compactor already validates geometry/identity against that
alternate header while using the clean primary header.

The extracted disk's allocation audit finds 41,745 fork-owned blocks, 41,747
allocated blocks, and exactly the two reserved volume-header blocks `[0,43007]`
remaining. There are **zero missing or overlapping fork blocks**. All ordinary
forks are readable through their inline extents. The AddressBook SQLite database
passes `PRAGMA integrity_check` and contains Adam.

Shutdown legitimately changes filesystem content: the OS flushes global
preferences, creates audio/network preference files, and removes its shutdown
cookie and mDNS runtime files. Of 5,178 post-shutdown ordinary data/resource
forks, 5,173 match their pre-shutdown equivalents byte for byte; the AddressBook
database is unchanged. Compaction must compare its output to the clean
post-shutdown disk, not require the genuine OS to leave every runtime file
unchanged during shutdown.

## Compaction gate found and bounded extension

The original copy-only compactor refuses this clean disk with
`attributes/startup forks are unsupported`. Guest activity has allocated a
1 MiB attributes B-tree fork even though it contains no attribute records:

- Allocation extent: start block 41,439, length 256 blocks.
- Logical length 1,048,576 bytes; node size 8,192 bytes.
- Tree depth, root, leaf-record count, first leaf and last leaf are all zero.
- 128 total nodes and 127 free nodes; only the header node is allocated.

This was an explicit unsupported-layout gate, not evidence that shutdown failed.
The already-168-MiB fixture can test a fresh boot without compaction. Larger
legacy images need either strictly verified empty-attributes relocation or
proper support for the layouts they actually contain. Do not weaken clean
volume, ownership, overflow, attributes-record or space-budget checks.

The compactor now accepts only a strictly verified empty attributes B-tree and
relocates its fork without changing any bytes. Ten synthetic tests pass,
including fragmented allocation, malformed header/map rejection and nonempty
attributes rejection. An independent real-disk audit verifies all 5,178 ordinary
forks and the entire 1 MiB attributes fork byte for byte. AddressBook integrity
also passes and the clean source remains unchanged. The output is just one
4 KiB block smaller: this proves preservation, **not useful RAM recovery**.
The reboot proof below uses the original clean disk, not this compacted copy.
Report: `.context/legacy-upgrade/empty-attributes-compaction-report.json`.

## Artifact provenance and reproduction

Workspace evidence is kept under `.context/legacy-upgrade/`:

| Artifact | SHA-256 |
| --- | --- |
| Source `.context/contacts-compact-report/compact-contacts-edited-healthy.snap` | `b872a2633b4f1f566df63923fe016561cf963195a4d5f7d51f8804c874e89d95` |
| Extracted clean `shutdown/disk.img` (176,160,768 bytes) | `87271faa21af0830e2c8d174759f525d25e62adc31bceccaae9acb5230a82e12` |
| New original-USB boot `reboot.snap` | `4439a8b6a547f55af305fd197f6483b2918ab2827eb2b206c5746f9092420cc6` |
| Chrome-exported Adam screen `browser/verified.snap` | `b46910e790c28531ad2832155f77a589c6c0a1cb50f59c1990adc560609004b6` |

`probe.c` loads a disposable checkpoint, finds the retained RAMDisk property,
traces clean flags and PMU state, and writes an extracted disk/checkpoint.
`hold.log` and `shutdown.log` record the exact sequence; `hold/screen.png`
shows the original power-off slider. `allocation-audit.txt` and
`fork-audit.json` preserve ownership results and hashes for every ordinary fork.
The full original source checkpoint is unchanged.

## Verified new boot and retained Contacts

A fresh USB-enabled boot of the exact clean 168 MiB disk completes 10 billion
instructions and reaches genuine Home with the usual Repair Needed alert.
`reboot.log`, `reboot-serial.log`, `reboot.snap` and `reboot.png` retain that
result. Both this Home checkpoint and the later Chrome export contain
`lockdownd` PID 17 with three threads. The actual retained boot-tree gate reports
`clock_compatible=1 active_clock=1`; the original saved fixture reports USB=0.

A disposable real Chrome profile imports the new checkpoint, dismisses the
original alert, opens Phone → Contacts and displays the existing Adam contact.
No contact is re-created or edited in this run. The final export preserves that
screen and still passes the USB/clock and daemon checks. Browser errors are
empty. The sampled guest/wall rate is 0.985; this is a bounded correctness proof,
not a navigation latency or CPU throughput benchmark.

Milestone 044 links the complete
original-speed browser recording. Its report and screenshots are under
`.context/legacy-upgrade/browser/`. The screen inspection confirms Adam in the
actual Apple application; SQLite contents alone were not accepted as proof.

## Remaining product gates

This establishes a manual, data-preserving boot upgrade for **one old compact
168 MiB saved phone**. It does not establish that every older 184.64 MiB profile
can be shrunk safely, that nonempty attributes/overflow layouts are supported,
or that every app's data and external account state survive a restart.

A product upgrade must preserve the old full checkpoint as a rollback, obtain a
clean shutdown without losing queued edits, reject unsupported filesystems,
validate the new phone before switching the active profile, and survive
interruption or quota failure. A large old profile also needs a safe disk-size
budget and enough free space. None of that migration UI or storage transaction
is implemented by this investigation. The existing Export/Reset behavior
remains unchanged.
