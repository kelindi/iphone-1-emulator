# 108 — Connect decoded instructions across code windows

## Tweet idea

The iPhone compiler can now follow branches and loops across captured code
windows, assembling bounded units instead of treating each window in isolation.
This is the map the next WASM emitter will turn into executable code.

## What you're seeing

This is a nonvisual compiler-data milestone. The Settings/Home section of the
[existing baseline recording](videos/adaptive_compiler_baseline.mp4) illustrates
the unchanged phone only. It does not show a new compiler or speed result.
[Source and focused test evidence](../docs/traces/adaptive-connected-units/README.md)
cover the connected graph builder; no dedicated screen recording applies to this
pure data transformation.

## ELI5 technical explanation

A program jumps between instructions, including backward into loops. The builder
follows those possible routes and divides them at junctions so a compiler can
keep working across branches. It stops at missing or unsupported code instead
of guessing what is there.

Two different iPhone addresses can also point at the same physical memory.
We retain both addresses but track one underlying code dependency, rejecting
conflicting copies. Copied input bytes and frozen output prevent later mutation
from silently changing the graph. Limits of 32 blocks, 512 instructions and eight
physical code granules keep each compilation task bounded.

## Why it matters

The explicit decoder now feeds a connected graph with tested branches, loops,
joins, aliases and bounds. Runtime invalidation and executable WASM emission
remain to be implemented; this milestone does not make the phone faster yet.
