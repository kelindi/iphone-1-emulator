# Existing WebAssembly VFP conformance failure

An actual-WASM execution of the unchanged VFP test suite returns **595 passed, 12 failed** on the released build's non-LTO core. The separately compiled LTO core produces byte-identical complete test output and the same exit code 1. This is an existing WebAssembly correctness defect, not an LTO regression and not a passing VFP conformance result. No expected result, guest code or production VFP implementation was changed.

The first control failure stopped the original correctness script before candidate execution. That stop and the subsequent explicitly relative candidate comparison remain preserved in [the LTO package](../web-lto/README.md). The candidate does pass the same literal nonfused VMLA checks, but that does not erase the twelve failures below.

| Failed checks | Actual instruction behavior | Existing source mechanism |
|---|---|---|
| 1: short-vector exception accumulation, test line 801 | Two-lane VDIV with `1/0` and `1/3` produces no DZC or IXC; FPSCR remains `0x00010000` | The Emscripten `host_exceptions()` path explicitly returns zero |
| 3: short-vector FZ ambiguity and lane preservation, lines 816–818 | The second multiply uses minimum-normal × `(1 − 2^-24)`. The intended ambiguity refusal does not occur; both destination lanes are committed instead of retaining −11 and −12 | `fz_out32()` recognizes rounded minimum-normal ambiguity only when IXC is present; absent host status bypasses that check |
| 1: scalar division by zero, line 980 | `1/0` reaches infinity but does not set DZC | Absent host exception flags |
| 2: invalid arithmetic, lines 996 and 1008 | `0/0` and infinity × zero produce NaNs but do not set IOC | Absent host exception flags |
| 2: overflow, lines 1019–1020 | Maximum finite binary32 × 2 reaches infinity without OFC or IXC | Absent host exception flags |
| 1: inexact division, line 1030 | `1/3` does not set IXC | Absent host exception flags |
| 1: signalling-NaN widening, line 1298 | VCVT.F64.F32 leaves FPSCR at the preexisting `0x2`, rather than adding IOC to produce `0x3` | Widening also samples the empty host exception path |
| 1: directed arithmetic rounding, line 1487 | Under RP, binary32 `1 + 2^-25` produces 1, rather than the next representable value (`1.000000119…`) | `vfp_execute()` requests FE_UPWARD via `fesetround`; the pinned Emscripten libc implementation accepts the mode but its underlying setter is a no-op |

The first eleven failures stem from status dependence; three of those also expose incorrect conditional refusal and destination-register behavior, not just missing flags. Arithmetic's non-default rounding has a separate mechanism. The positive test cases for RM/RZ use a positive input that also rounds to 1 under nearest; their passing result does not establish general directed-mode correctness.

In [the frozen VFP source](../web-lto/frozen-tree/core/src/arm/vfp.c), lines 394–403 deliberately implement an empty WebAssembly host exception path. Arithmetic and widening sample it; `fz_out32` at line 300 depends on IXC, and the directed-mode wrapper at lines 1325–1330 assumes a successful host rounding request changes the arithmetic environment. In the pinned Emscripten 4.0.16 libc, [the target header](source/fenv.h) defines FE_ALL_EXCEPT as zero, [the wrapper](source/fesetround.c) accepts the defined direction values, and [the fallback implementation](source/fenv.c) always reports nearest and returns success without changing a mode.

[Control output](control-vfp.log), [candidate output](candidate-vfp.log), [relative result](relative-focused-report.json), and [source/hash inventory](provenance.json) retain the exact findings. The original test's integer register bits, FPSCR expectations, lane atomicity, result bits and failure statuses were not relaxed. These tests are independent original ARM encodings executed through the actual WASM interpreter, not inferred browser pixel failures.

A future correctness fix needs a separate reviewed software implementation of the relevant result/rounding/status semantics or an explicitly bounded refusal policy. Merely returning the requested rounding mode or manually setting a flag without computing the corresponding result would not repair the exposed behavior. Any fix must preserve literal independent tests, nonfused multiply/add, signed zeros/NaNs, vector all-or-nothing writes, conversion behavior, and actual guest compatibility. No such fix is mixed into the LTO optimization; the scope and performance cost remain open.
