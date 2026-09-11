# Browser iPhone performance: prior art and next architecture

Research date: 2026-09-06. This is a source review and proposal, not a new speed
benchmark or implementation. No third-party benchmark was reproduced in this pass.

## What the evidence establishes

We have **not demonstrated a physics limit**. The failed experiments reject
particular implementations, not fast ARM emulation in a browser. Conversely,
examples from other emulators do not guarantee full-speed original iPhone OS.

The current engine has real generated WebAssembly execution, but its accelerator
is a prebuilt set of512 one-KiB regions with a partial instruction emitter. There
is no ongoing adaptive compiler choosing new hot code during normal use. Register
values already remain local inside each generated function. Ordinary memory
accesses already have a software translation-cache fast path. Recommending “add
JIT” or “cache memory” alone would ignore what exists.

The recorded warm Settings metric window contains69,053,832 generated retirements
and8,040,880 generated calls: about8.59 instructions per call. Calls include
zero-progress attempts, and periodically reported metrics do not exactly bracket
the profiler. This is evidence to investigate frequent handoffs, not a measured
cost attribution. [Derived calculation](compiler-residency-metric.json).

The current native pilot used our x86 native interpreter, while browser execution
used the WASM accelerator. It did **not** compare an optimized native ARM recompiler
with an optimized browser recompiler. The small native advantage therefore does
not establish either the browser's overhead ceiling or the best possible native
performance. The actual original-device launch baseline and reference-Mac
performance also remain unmeasured.

## Prior work worth studying

| Primary source | Relevant result or mechanism | Limit for this project |
|---|---|---|
| [v86 implementation overview](https://github.com/copy/v86/blob/master/docs/how-it-works.md) | Tracks hot pages and reachable pages, generates WASM, uses a software TLB and lazy flags. | x86, not ARM. Useful architectural reference, not an interchangeable CPU. |
| [QEMU translator internals](https://www.qemu.org/docs/master/devel/tcg.html) | Specializes translated blocks to CPU state, links blocks, tracks code writes and uses physical indexing/software MMU. | Native code patching and host page protection cannot simply be copied into WASM. |
| [QEMU Wasm](https://github.com/ktock/qemu-wasm/blob/0ef7b4e2814b231705d8371dd7997f5b72e70baf/README.md) | Experimental TCG-to-WASM backend; cold blocks use TCI, frequently used blocks compile through browser APIs. ARM64 examples are provided. | No demonstrated original ARMv6 iPhone machine or speedup here. Whole-system integration and backend extraction both require work. |
| [CheerpX overview](https://cheerpx.io/docs/overview) | Two-tier execution with an interpreter and optimizing hot-code compiler. | x86; [its Linux environment implements syscalls rather than running the Linux kernel](https://labs.leaningtech.com/blog/cx-10). This avoids work our genuine XNU system must perform. |
| [PCem WASM port notes, sections4b and CPU JIT in the app](https://raw.githubusercontent.com/va3jfl/PCem-WebAssembly/main/PORTING_NOTES.md) | Author reports tight-kernel gains but desktop regressions when tiny blocks pay repeated lookup, indirect-call and entry costs. Hotness thresholds/cost selection reduce waste. | Self-reported project results, not reproduced here. Strong caution against claiming app gains from arithmetic microbenchmarks. |
| [N64Recomp](https://github.com/N64Recomp/N64Recomp) | Converts identified binary functions to C for offline compilation; supports indirect lookups and overlays. | MIPS games with metadata/runtime support, not an automatic whole-OS ARM translator. |
| [Cloudpilot ARM core](https://github.com/cloudpilot-emu/cloudpilot-emu/blob/f17182ab4885844ee2c362dd2130a0344c6efeb6/src/uarm/uarm/CPU.cpp) | Cached instruction decoding, large indexed TLB, recognized-routine acceleration. | Uses guest peephole substitutions and OS-specific hooks; its speed is not a strict unchanged-guest comparator. |
| [Flycast WASM](https://github.com/nasomers/flycast-wasm/blob/16e9c7b4b7065a4bd42e7490e8aab3e9283d5415/TECHNICAL_WRITEUP.md) | Runtime WASM compilation and block-chain engineering provide another concrete implementation to study. | Its SH4 JIT's documented MMU limitations make game results an unsuitable guarantee for XNU. |

No project in this review establishes that every OS1.0 app can run at full speed
on our target browser/hardware. The positive evidence is that dynamic translation
in WASM is practical and has substantially richer designs than our fixed seed.

## Ranked strategies

### 1. Keep execution in compiled code for longer

**Recommended main direction.** Build connected ARM/Thumb regions around actual
hot control flow, allowing multiple code pages in one compilation unit. Keep
registers and intermediate results local across those edges. Resume compiled
execution after an ordinary memory slow path when safe instead of repeatedly
returning through the full interpreter dispatcher.

This changes the execution boundary itself. The previous continuation experiment
still returned from every generated function, saved/restored state and repeated
entry checks. It did not implement a connected compiled graph. The previous
short-loop proposal covered only4.5% of sampled residency in its whole candidate
regions; this proposal addresses execution throughout the hot working set.

Retain genuine interrupt, fault, privilege, MMIO and instruction-budget exits.
One-KiB mapping/permission granularity may still be required even when compilation
units grow larger. “Larger” does not mean unbounded functions or ignoring page
boundaries. v86's design is relevant; optimal region size remains an experiment.

### 2. Make memory and code validation cheaper as part of that engine

Current generated code rereads instruction words and checks memory-cache state
frequently. Investigate physical code-page generations and write barriers, so
unchanged code can pass a compact validity check. Track all code mutations,
including guest stores through aliases, DMA/device writes and restored snapshots.
Use an enlarged or differently indexed data TLB only if actual miss/conflict data
supports it; larger tables can worsen host cache locality.

QEMU's invalidation and RAM/MMIO separation are the relevant models. This is a
replacement validity contract, not removing safeguards. Cold/faulting accesses
must still report the exact faulting instruction and preserve partial effects.

### 3. Compile the fixed OS ahead of time, more intelligently

The product intentionally runs a fixed old OS and app set. Use offline analysis
and recorded control-flow edges to lift known hot functions/graphs into optimized
WASM, keyed to exact code identity, with runtime fallback for unknown or changed
code. A build-time compiler can spend more effort on dead flags, compare/branch
fusion and register/data-flow optimization without delaying a user's first tap.

This is inspired by static recompilation projects, not a proposal to feed an
entire iPhone firmware image into N64Recomp. It extends our existing prebuilt-seed
idea with different compilation units and optimization, rather than merely
adding64 more pages. Interworking, indirect Objective-C dispatch, relocation,
fault recovery and generated code remain real engineering requirements.

A useful hybrid is offline hot code plus asynchronous runtime compilation for
missing hot code. Compilation must have a budget and benefit threshold; thousands
of eagerly compiled tiny modules are not the goal.

### 4. Use the browser's compiler deliberately

[V8's2025 inlining description](https://v8.dev/blog/wasm-speculative-optimizations)
explains limits around many-target indirect calls and cross-instance targets.
Our core-to-seed dispatcher is a many-target call across instances. That suggests
examining generated call structure, rather than assuming Chrome can inline the
boundary away. Those documented limits are a research lead, not proof of exactly
what Chrome152 emits for our build.

[WASM tail calls](https://v8.dev/blog/wasm-tail-call) offer another way to transfer
control without recursive stack growth. They do not guarantee register retention
or lower latency. Benchmark direct same-module edges, grouped functions and tail
calls on actual supported browsers before choosing an ABI.

[V8's compilation pipeline](https://v8.dev/docs/wasm-compilation-pipeline) also
makes compilation tier and debugging state relevant. Check cold/return-visit
behavior separately; cached loading is already implemented here. Do not publish
profiling-mode or experimental-engine-flag timings as normal browser performance.

### 5. Recognize expensive operations, not just individual instructions

A more aggressive option is recognizing an exact routine or loop and implementing
its result using bulk WASM operations: copies, fills, byte searches or arithmetic.
[Box86 library wrapping](https://box86.org/2021/08/a-deep-dive-into-library-wrapping/)
is prior art for escaping instruction-by-instruction execution, but its actual
bridging/patching scheme is not compatible with our fidelity rule as-is.

For this project, any accepted version would have to live in the translator,
leave Apple bytes unchanged, and preserve observable memory behavior, overlap,
flags, faults, retirement timing and interrupt points. Restrict fast paths to
proved ordinary RAM and bounded chunks; fall back otherwise. This is speculative
and should follow measured routine-level hotness, not presumed `memcpy` names.
Wholesale UIKit/XNU replacement would change the product and is not proposed.

## Approaches that do not presently deserve priority

- WebGPU for the general CPU instruction stream: dependent control flow is not an
  obvious parallel workload. Graphics/audio acceleration can still help workloads
  where those devices dominate; current Settings evidence does not show that.
- More cores for the single guest CPU: useful for background compilation, media
  and saves, not automatic parallel execution of dependent guest instructions.
- Native streaming as a proven speed fix: an optimized native recompiler has not
  been benchmarked. Streaming also introduces a separate latency/cost tradeoff.
- More decoder rearrangements: the old bits27:26 partition and milestone100's
  bits27:25 partition are the same broad hypothesis, and both failed to establish
  a gain. The latest attempt was not a new architectural strategy.
- Removing time/fault checks or fast-forwarding an arbitrary busy loop: no evidence
  supports changing guest-visible behavior to manufacture a speed result.

## Concrete next goal

**Establish whether a materially different compilation boundary can execute our
actual mixed ARM/Thumb, MMU-using workload substantially faster before porting the
whole phone.** A2× CPU-throughput target is a useful go/no-go ambition, not a
forecast of app-launch speed.

1. Review the QEMU-WASM backend's ARMv6 path, callback/MMU requirements and build
   scope against extending our emitter. Choose one route after this feasibility
   comparison; do not launch two full emulator ports.
2. Use a small set of recorded real guest workloads containing arithmetic, memory,
   cross-page control flow and interworking—not one easy tight loop. Existing
   snapshots and interpreter remain the reference. Reuse existing test plumbing.
3. Implement one connected compiled slice with explicit validity/exit rules.
   Measure successful instructions per entry, zero-progress exits, memory slow
   paths, module compilation cost and execution time. A fast CPU slice alone is
   still not product acceptance.
4. If it demonstrates sufficient headroom, integrate enough real coverage to run
   the Settings/Home workload, then use20+ warm samples and first-launch, input,
   save/reload and fault checks. Include the reference Mac/browser before claiming
   user-visible success. Otherwise stop with the actual limiting measurements.

Architectural equivalence is mandatory. Diagnostic cache-hit counters may
legitimately differ under a redesigned cache; do not confuse those with guest
registers, memory, device state or exceptions. Any changed test contract must be
explicit and reviewed before accepting the candidate, never adjusted after a
failure to make it pass.

This research justifies another **architectural feasibility experiment**, not an
unbounded rewrite or a promise that20%/2× is available. The honest result is that
there are credible untested strategies, and the existing evidence does not settle
the full-speed question.

## Pinned implementation entry points

- QEMU-WASM [TCG backend](https://github.com/ktock/qemu-wasm/blob/0ef7b4e2814b231705d8371dd7997f5b72e70baf/tcg/wasm32/tcg-target.c.inc) and [runtime support](https://github.com/ktock/qemu-wasm/blob/0ef7b4e2814b231705d8371dd7997f5b72e70baf/tcg/wasm32.c).
- Cloudpilot [instruction cache](https://github.com/cloudpilot-emu/cloudpilot-emu/blob/f17182ab4885844ee2c362dd2130a0344c6efeb6/src/uarm/uarm/icache.cpp) and [peephole substitutions](https://github.com/cloudpilot-emu/cloudpilot-emu/blob/f17182ab4885844ee2c362dd2130a0344c6efeb6/src/uarm/uarm/peephole.cpp). The current monorepo source is used, rather than assuming the separate cp-uarm repository is current.
- Flycast [actual recompiler](https://github.com/nasomers/flycast-wasm/blob/16e9c7b4b7065a4bd42e7490e8aab3e9283d5415/patches/rec_wasm.cpp). Its fused-chain implementation is relevant; missing JIT MMU support prevents direct transplantation.
