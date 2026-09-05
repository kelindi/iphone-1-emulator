## Tweet idea

> The M68 NOR/NVRAM model now programs a valid CHRP partition image, or the OS 1.0 kernel hangs forever in `IODTNVRAM::initNVRAMImage`.

## What you're seeing

No dedicated recording; this is a boot-prerequisite fix. A cold boot recording will be added.

## ELI5 technical explanation

- The original iPhone stores NVRAM (boot/settings variables) in two 8 KiB copies inside the NOR flash at `0xfc000` and `0xfe000`.
- The M68 NOR is at physical `0x24000000`. `core/src/board/m68.c` maps that region and fills it with a CHRP-style NVRAM partition, including the length and checksum fields the kernel's `IODTNVRAM` driver expects.
- The driver walks NVRAM partitions by their length fields and, in this kernel, has no lower-bound guard. A zero/blank NVRAM makes it follow an invalid length and hang the boot.
- This was an early milestone fix: before NVRAM worked, the kernel couldn't even read variables like `boot-args`.

## Why it matters

It sounds tiny, but without valid NVRAM the kernel can't read boot configuration. This is part of making the emulated hardware "look real enough" to unmodified Apple code.
