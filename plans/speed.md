# Plan: decoded-block ARM execution engine

## Goal
Make the emulator run fast enough to feel interactive — target ≥2.5× the current native ~42 M instructions/s while keeping guest behaviour bit-identical and preserving Emscripten/WASM portability.

## Why it matters today
The guest CPU runs at 412 MHz, but the interpreter only retire ~42 M instructions/s on native and ~28 M instructions/s in Chrome. Saturated phases therefore run at ~1/10 of real time, so touch/button response and animations feel sluggish. The current interpreter re-decodes every instruction and walks a deep dispatch chain.

## Core idea
1. **Pre-decoded block cache** — decode straight-line ARM/Thumb code into arrays of pre-decoded ops (handler pointer, operands, condition, flags-needed), stopping at branches, exception-capable instructions, and 1 KiB page boundaries. Key by physical address + mode + privilege; invalidate on TLB generation change, CP15 cache/TLB maintenance, self-modifying writes, and snapshot load. Reuse/tighten the existing 1 KiB fetch-block cache in `core/src/arm/arm_interp.c`.
2. **Block execution loop** — count retired instructions exactly, and stop at the same points the current batched-tick code stops: MMIO access, IRQ/FIQ changes, external input, `level_dirty`, exceptions, mode/privilege changes, WFI, undefined/unpredictible ops.
3. **Fast data TLB** — small host-virtual direct-mapped page cache per privilege/access type for RAM; fall back to `arm_mmu_translate` + bus access for MMIO and faults.
4. **Keep it portable C** — no host JIT so the Emscripten/WASM build continues to compile and behave identically.
5. **Runtime state only** — block cache/TLB are not part of snapshots; rebuild after `--load`.

## Files to extend
- `core/src/arm/arm_interp.c`, `core/include/arm.h` — dispatch and block cache
- `core/src/arm/arm_mmu.c` (or `mmu.c`) — fast data TLB hooks
- `core/src/soc/machine.c` — tick-batching integration, WFI/exception boundaries
- `core/tests/test_soc.c` — equivalence tests

## Correctness gates
- All existing `ctest` tests pass; add targeted tests comparing the new engine against the existing `arm_step` + `s5l8900_tick(1)` reference for: conditional execution, Thumb/ARM interworking, self-modifying code, stores into the executing block, MMIO access mid-block, IRQ mid-block, SWI/undefined/data abort, PC-relative loads, LDM/STM with PC, mode switch.
- Cold boot to home screen still reaches SpringBoard; screenshot PNG.
- Snapshot resume with `--load fw/home_ship.snap --at 9800000000 tap 160 315 --at 9900000000 tap 45 370 --steps 10400000000 --screenshot out.ppm` still dismisses the alert and opens Settings.
- Serial log shows no new warnings/panics compared to the base branch.

## Benchmark method
Run the snapshot resume command with `--steps 10190000000 --no-fb`. It executes exactly 400 M instructions after load. Measure wall time with `date +%s.%N` before/after and subtract the load offset (`--steps 9790000000`, zero instructions executed). Rate = 400e6 / (total − offset). Profile with `perf` and list the top symbols. Do not report "target steps / total time".

## Deliverable
A PR from a branch `speed` targeting `main` that:
- implements the block cache, fast data TLB, and correct tick accounting,
- adds tests,
- updates `docs/iphone1_1_os1_port.md` §"Interpreter speed" with baseline and new rates,
- states whether the shipped `web/firmware/home_1A543a.snap.gz.part*` needs regeneration (it should not if the snapshot format did not change).
