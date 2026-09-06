# Our own program executes in genuine iPhone OS 1.0

One private clean boot passed. Original launchd started our independently authored ARMv6 Mach-O executable; original dyld resolved its three imports from the unchanged OS1 libSystem. Actual `getpid()` returned21, actual `write(1, ..., 38)` returned38, and the guest entered original `_exit(0)`. The emulator observed the exact UART message, then stopped after a normal run-loop return at262,000,000 instructions. [Run report](run-1/report.json), [full console](run-1/stdout.log), [UART](run-1/serial.log) and [independent artifact audit](run-1/independent-audit.json) retain the evidence.

This is a nonvisual program-execution milestone, not a screen or speed milestone. The sole run took11.16 seconds within10B instructions/900 seconds; permitted concurrent functional browser work makes this unsuitable as a benchmark. It does not prove process reaping, loopback configuration, HTTP delivery, an app forecast, or production integration. Existing unrelated OS warnings remain in the full console.

## Exact guest witnesses

| Observation | Guest instruction counter |
|---|---:|
| Own212-byte text matches, ARM user mode, TTBR14ca4000 |261,023,318|
| Original getpid returns21 to own code |261,029,040|
| Original write receives exact38-byte buffer |261,029,160|
| Write returns38 to own code |261,034,930|
| Original exit entry, argument0 |261,034,937|

The exact line is `IPHONE1_OWN_PROGRAM_V1 pid=0000000021`. The UART logger's248567746 prefix labels an earlier buffered chunk; it is **not** the marker's execution instant. The observer binds one user process/address space and compares actual memory. It neither supplies a syscall result nor inserts the marker. It always returns false from the instruction hook and stops outside `s5l8900_run`; no longjmp, fabricated instruction completion or snapshot publication is involved.

## Build, imports and provenance

[Own source](hello.c) contains no SDK headers, raw syscall numbers or Apple implementation bytes. [Build commands](build-hello.sh) use host Clang15 targeting ARMv6 and historical cctools-port tag `cctools-870-ld64-134.9-1`, commit `4a15314c61852e230a2096a428783f93e1ab95df`. The [host portability patch](cctools-host-portability.patch) handles modern Linux headers/CPU counting and old C++ syntax only. Toolchain license notices are retained in [toolchain/APPLE_LICENSE](toolchain/APPLE_LICENSE) and [toolchain/COPYING](toolchain/COPYING); the original source remains at [its pinned tag](https://github.com/tpoechtrager/cctools-port/tree/cctools-870-ld64-134.9-1). No claim is made to relicense upstream code.

The8,396-byte [own executable](iphone1-own-program), SHA256 `5de86fdf8b2a9a22aac87ce891a7216eeb677d98d91e9829db85d865a8a88bfb`, uses LC_UNIXTHREAD at0x1f04 and only historical load commands. Three eager data relocations resolve `_getpid`, `_write`, `__exit`; eager pointers avoid a missing CRT lazy-binding helper. The [binary audit](binary-audit.json) verifies original export addresses, relocations, sections and load-command bounds. There is no code-signing-policy edit or new boot policy: this uses the existing OS1 debug-enabled boot configuration. No Apple binary was modified.

Primary API/format sources are Apple's historical [Libc declarations](https://raw.githubusercontent.com/apple-oss-distributions/Libc/Libc-498.1.7/include/unistd.h), [Mach-O header](https://raw.githubusercontent.com/apple-oss-distributions/cctools/cctools-667.3/include/mach-o/loader.h), [launchd manual](https://raw.githubusercontent.com/apple-oss-distributions/launchd/launchd-258.1/launchd/src/launchd.plist.5) and [HFS+ technical note](https://developer.apple.com/library/archive/technotes/tn/tn1150.html). [Source receipts](primary-sources.json) retain exact downloaded hashes. General ARM AAPCS documentation is supplementary, not proof of every historical Apple ABI detail; the actual guest call/return proves the narrow imports used here.

## Disposable image and preservation

The known clean168MiB shutdown image remains SHA256 `87271faa21af0830e2c8d174759f525d25e62adc31bceccaae9acb5230a82e12`. Its primary header says unmounted; its known stale alternate header is retained without normalizing flags. [Installer](prepare_image.py) copied it and added only our executable at `/usr/libexec/iphone1-own-program` (root:wheel0755) and our launchd plist at `/System/Library/LaunchDaemons/com.iphone1.qualification.hello.plist` (root:wheel0644). Label `org.iphone1.qualification.hello`, RunAtLoad and console paths are explicit. The output is SHA256 `4a6261b4043b9acd8a1db368b93862d503b1d312915f94ffcf9a18f9854f888a`.

All2,589 prior files and5,178 ordinary forks remain exact. Existing file catalog records remain exact; only two parent directory valences increase. Four new catalog rows fit existing leaves without changing their first keys. [Install audit](install-audit.json), [preflight](image-preflight.json) and a separate read-only catalog/fork traversal in [audit_run.py](audit_run.py) verify this. The isolated permission extension validates mode/owner fields before any mutation; [five synthetic tests](permissions-test.log) compare catalog bytes and unchanged forks, while [14 host behavior checks](hello-test.log) exercise the own program with mock imports. No production rootfs helper was changed. Original filesystem/library/kernel bytes are intentionally not redistributed in this archive.

## Preserved setup failures and next boundary

Historical linker host build failures, missing Python dependency, missing CRT lazy binder, too-strict alternate-header equality and an unsuitable catalog insertion key were corrected **before** the only guest run. The original proposed plist basename would have changed a leaf first key; choosing another own basename avoided expanding the editor. Binary-auditor index/base assumptions were corrected against the old linker source. The independent post-run auditor also initially mishandled the reserved HFS root parent and volume path; both source versions/logs are retained. These are tooling failures, not guest failures or retries.

Next is a new independently authored local server/client that logs actual interface state and API errors, waits boundedly for ordinary boot configuration, binds a high loopback port and exchanges an exact HTTP response through the real guest stack. This result alone does not establish that lo0 is already configured or that Safari/Weather can reach a service. No wireless-driver work is part of this path.
