# First connected WASM/runtime/scheduler integration

The actual adaptive emitter now runs through the C registry, live instruction
guards, C result validator and real machine scheduler in one isolated WASM build.
[Four-case report](report.json) and [build commands/source pins](build-report.json)
record the qualification. [Sources](source/) and the small [module](machine-bridge.wasm)
with its [loader](machine-bridge.cjs) are preserved.

Three 128-credit synthetic workloads match **all 17,887,393 saved-machine bytes**
against the interpreter: a connected loop, a cross-window loop, and changed-code
fallback. The first two execute 125 generated guards; changed code triggers one
guard and retires its function exactly once before correct interpretation.
A fourth case verifies that a malformed compiled result latches a fatal state
and that neither disabling adaptive execution nor requesting one instruction
can bypass it. Complete equality is for these small workloads, not real apps.

The registry implements 2,048 units, eight witnesses/unit and 32 MiB declared
installed binary bytes. Function slots have one owner until explicitly drained;
replacement, reset and STALE exits preserve retirement ownership. Mutations
while a function is pinned are fatal, and cycle credit is withheld for invalid
results. The host must bind before reset/restore/free and remove only drained
function-table handles. Binary byte counts are supplied by the trusted host;
cache artifact validation and background jobs are not implemented here.

[Native lifecycle results](registry.log): **8,473 checks pass**, including live
and pending duplicates, stale epochs/mappings, exact binary/unit caps, queue
saturation without lost handles, active mutation and rebind recovery.
The epoch-exhaustion branch is reviewed but not forced through 2^32 binds.
[Existing ARM suite](interpreter.log): **1,067 pass, zero fail**.

Review found that generation/literal equality alone was insufficient if a
translation was replaced after cache eviction. The new pure `arm_fetch_cache_peek`
observes the current executable cache/TLB mapping without a walk, counter change
or device read. Guards and installation compare its host mapping to the witness.
[Real-interpreter guard tests](guard.log) cover an actual colliding fetch and
same-generation remap with identical code bytes at a different physical address,
as well as alias mapping, XN fault handoff and device-like fetch refusal.
The initial eviction fixture used a noncolliding address; correcting it to the
actual 4,096-entry cache geometry produced the intended rejection test.

Reproduce the integrated test:

```sh
node tools/jit/adaptive/test-machine-runtime.mjs
```

The script builds into `.context`; `EMCC` and `EMCMAKE` can override compiler paths.
It hashes selected implementation inputs before and after the run. Native tests:

```sh
cc -O2 -std=c11 -Icore/include -Iweb tools/jit/adaptive/tests/registry.c \
  web/adaptive-runtime.c core/src/arm/arm_interp.c core/src/arm/vfp.c \
  core/src/mmu.c -lm -o /tmp/iphone-registry
/tmp/iphone-registry
```

Remaining: full emitted execution with MMU on and device/clock boundaries, wider
instruction coverage, all-writer physical versions, host trap recovery, background
compilation/cache lifetimes and the three real-app correctness/2× throughput gate.
An escaping WASM trap leaves the C pin active; this implementation requires
replacing the WASM instance, not blindly rebinding and replaying its state.
The product build is unchanged and this is not a browser latency measurement.
