# Filling known compiler gaps gives a modest improvement

## Tweet idea

We added256 frequently missed instructions to the original iPhone emulator's prepared code cache—without changing its CPU or safety checks. In a clean88-action browser trial, warm Settings fell from2.29 to 2.17 seconds; measured p95 fell from2.67 to 2.22 seconds. Useful evidence, but still short of the speed we want.

## What you're seeing

[Full candidate session](videos/seed_gap_comparison.mp4) · [Thumbnail](thumbnails/seed_gap_comparison.jpg). The uncut clip includes startup, first Settings/Home pair, then ten warm pairs. It is one of four clean comparison sessions. The interrupted earlier attempt is retained in the [evidence archive](../docs/traces/seed-gap-experiment/README.md), including its10-second lock-screen frame.

## ELI5 technical explanation

The compiler could already translate these instructions, but the fixed cache had not prepared their exact positions. We filled the256 busiest supported holes across23 existing regions, preserving all512 regions and all original instructions. That reduces some interpreter fallback, but most phone work remains elsewhere.

272 focused checks and full replay state comparisons pass. The clean browser trial completes88actions, with20 warm samples per action/version. Settings median improves5.47%; Home median improves about49 ms and its p95 is effectively unchanged. First Settings stays around3.36 seconds. Frame differences are limited to the existing Settings scrollbar fade and remain recorded as strict pixel mismatches.

The first trial was interrupted by a concurrent change that made the phone open locked. Hardlinked test files picked up that change mid-comparison, so the harness tapped the lock screen. We preserved that failure, made independent test copies and reran the whole comparison. It was not evidence of a CPU hang.

## Why it matters

This is a modest measured gain from the exit census, not a full-speed claim. It misses the predeclared10% median improvement threshold, so the candidate stays isolated and the production seed remains unchanged. The concurrent lock-screen feature is preserved. The source, recordings, limitations and selection are retained for a reviewable next decision.
