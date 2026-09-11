# Interpreter-backed guards and optional scheduler dispatch

The native [guard test](guard.log) uses the actual ARM interpreter/MMU to warm
identity and virtual-alias fetch mappings. The witness accepts those mappings,
rejects changed instruction bytes and expired lifetimes, and refuses an XN
mapping after a real section-descriptor change and TLB flush. `arm_step` then
delivers the expected prefetch abort; the guard itself changes no CPU field or
bus-read count. A device-like fetch without a host RAM mapping is not captured.

The scheduler has an opt-in `S5LBOX_ADAPTIVE_JIT` branch. Its runtime interface
requires admission, result validation and CPU cycle credit before returning.
Machine retirement/device accounting remains scheduler-owned. Completed EDGE
results can retry without an obligatory interpreted instruction; slow exits
use the existing `batch_pending` protocol. BOUNDARY forces a device refresh;
fatal dispatch returns ARM_HALT without injecting an exception or replaying code.
Disabled builds retain the existing ABI1/interpreter branches.

The [dispatch test](dispatch.log) compares full saved-machine bytes for all seven
policies (not admitted plus six exit reasons) against 32 interpreted ADDs. The
controlled callback uses the C invocation validator. A fatal callback also proves
there is no instruction after the fatal boundary. Initial scheduler setup may
interpret an instruction before compiled admission becomes possible; that work
is retained, not incorrectly attributed to the fatal call.

These are native protocol tests, not actual WASM inside the full phone. They do
not qualify active-clock deadlines, MMIO-helper partial effects, asynchronous
installation, code versions or performance. The prior isolated actual-WASM/C
bridge and this scheduler test still need to be connected by the real registry,
guard import and lifetime management. The new flag is not enabled by the product
build and no default-engine change has occurred.

Reproduce from repository root:

```sh
cc -O2 -std=c11 -Icore/include tools/jit/adaptive/tests/guard-interpreter.c \
  core/src/arm/arm_interp.c core/src/arm/vfp.c core/src/mmu.c -lm -o /tmp/iphone-guard
/tmp/iphone-guard
cmake -S . -B .context/adaptive-compiler/native-dispatch -DCMAKE_C_FLAGS=-DS5LBOX_ADAPTIVE_JIT=1
cmake --build .context/adaptive-compiler/native-dispatch --target emucore -j 4
cc -O2 -std=c11 -Icore/include tools/jit/adaptive/tests/dispatch.c \
  .context/adaptive-compiler/native-dispatch/core/libemucore.a -lm -o /tmp/iphone-dispatch
/tmp/iphone-dispatch
```

Two test-authoring errors were corrected before the passing result: a synthetic
BEFORE callback initially consumed its entire budget (correctly rejected by the
validator), and the fatal test initially assumed no setup instruction could
precede the first eligible dispatch. Neither required weakening runtime checks.
