# A faster-looking decoder does not make the phone faster

## Tweet idea

We found the original iPhone emulator's biggest CPU hotspot and tested a simpler
instruction dispatcher. It preserves the phone's state exactly—but across88 real
Settings/Home actions, repeat launch times are effectively unchanged. We rejected
it. Cleaner routing on paper was not the missing speedup.

## What you're seeing

[Full candidate session](videos/core_decoder_comparison.mp4) ·
[Settings thumbnail](thumbnails/core_decoder_comparison.jpg).

The uncut recording shows loading followed by11 Settings/Home pairs at original
speed. The first pair is a fresh launch; all subsequent pairs measure warm use.
This is an isolated candidate, not an improved production release. All four
sessions and their screens are in [the evidence](../docs/traces/core-decoder-experiment/README.md).

## ELI5 technical explanation

The interpreter reads an old ARM instruction, identifies its type and performs
its operation. Our CPU profile's largest function is this interpreter. We tried
sorting instructions into broad categories first, so common operations could
avoid checking unrelated types. Their actual behavior and safety checks stayed
the same.

More than536million routing cases agree with the original. Whole-phone state
also matches at every tested checkpoint in both interpreter and compiled execution.
But simpler source code does not guarantee less work for the browser's compiler.
The hotspot includes memory access, Thumb instructions and other work beyond the
routing we changed.

In alternating actual browser sessions, each version gets20 warm samples per
action. Settings median is2.317s in both; Home is0.867s in both. Settings p95 drops
about56ms, while Home is unchanged. That small tail difference does not meet the
20% goal or justify shipping this change. All Home frames match exactly below the status bar. Some Settings captures show
different scrollbar fade intensities; those pixel mismatches are retained.

## Why it matters

This bounded attempt identifies the largest function and tests one concrete idea,
then stops on evidence. There is no production speedup. A substantial gain remains
an execution-engine problem; this result rules out this decoder rearrangement,
not every possible engine improvement. The working phone and its existing data
remain on the current implementation.
