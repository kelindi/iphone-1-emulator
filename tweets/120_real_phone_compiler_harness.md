# 120 — Connect compiler qualification to the real saved phone

## Tweet idea

The new compiler now has a test harness around the actual browser iPhone.
Before compiling apps, we verified that merely enabling its execution path
preserves every byte of the saved phone.

## What you're seeing

Nonvisual qualification work. [Evidence](../docs/traces/adaptive-workload-runtime/README.md)
contains source identities and full-state comparisons. The Settings/Home section
of [106's recording](videos/adaptive_compiler_baseline.mp4) provides baseline
context only; there is no new speed recording or speed claim.

## ELI5 technical explanation

The compiler needs to inspect real code without accidentally changing the phone
while looking at it. A new capture API checks existing mappings and copies their
identity without executing an instruction or warming a cache. Tests also catch
a stale mapping left behind when the memory context changes.

The harness then restores the genuine phone and compares ordinary execution
with an empty compiler registry. An initial mismatch was one cache-flush counter:
we had initialized the two runs differently. Identical initialization fixes it;
all 244,887,522 saved bytes now match without excluding counters.

## Why it matters

287 passive-capture checks pass, and the source-pinned real-phone harness passes
its initial state comparison. We can now build actual workload collection on
this foundation. Compiled app execution, the three-workload throughput gate and
user-visible speed qualification remain outstanding.
