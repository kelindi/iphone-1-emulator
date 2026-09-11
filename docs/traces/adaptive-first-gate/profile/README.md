# First failed-gate candidate: bounded CPU profile

The actual Node `--cpu-prof` replay completed Settings 100M credits with exact
frozen checkpoint equality. This is diagnostic profiling, not a gate rerun.
Raw profile: `../workload-first.cpuprofile`; replay log:
`../workload-first-profile.log`; machine/counter evidence: `report.json`.

The profile contains 9,858 samples totaling 10,502.459 ms of weighted sample
intervals. This is main-thread sampling over the entire process, including
initialization, snapshot restore/save and hashing. It excludes V8 background
compiler threads and must not be substituted for process CPU gate results.
Inclusive figures overlap and must not be summed.

| Function/path | Self sampled ms | Inclusive sampled ms |
|---|---:|---:|
| arm_adaptive_try | 1051.316 | 4343.591 |
| workload_miss | 321.004 | 2549.848 |
| compiler JS callback | — | 2224.616 |
| arm_step | 1487.282 | 2332.034 |
| emu_run | 912.492 | 7998.180 |
| adaptive_guard | 443.973 | 498.112 |
| adaptive_read | — | 46.693 |
| adaptive_write | — | 26.649 |
| buildUnit | 201.795 | 698.442 |
| WABT parseWat | — | 898.122 |
| WABT toBinary | — | 162.460 |
| emitUnit | — | 110.951 |

All generated-unit WASM self samples total only 133.746 ms. WABT WASM+JS
self samples total 1251.553 ms. The core includes snapshot work: emu_load_snapshot
931.725 ms inclusive, emu_save_snapshot 438.849 ms inclusive; sn_raw's 1310.096 ms
self figure is serialization, not interpreter dispatch. Full function/category
lists are in `analysis.json`.

The replay retired 22,776,507 generated instructions in 2,907,781 calls (about
7.83 per call), with 2,470,062 BEFORE exits and 74,748,434 admission misses.
It attempted 1,298 compilations, installed 1,016 modules, and produced 4,894,528
binary bytes. Compiler instrumented process CPU is 3003.623 ms, versus 2327.518 ms
compiler wall time; it includes work not visible in a main-thread-only profile.
Total diagnostic replay CPU is 10193.013 ms. These are not comparable directly
to sampled inclusive durations or to the formal 600M ABBA gate.

The dominant opportunities are fallback/dispatch overhead and repeated compilation,
not faster generated arithmetic. Guard cost exists but is smaller than those
paths. Short generated runs and frequent BEFORE exits limit the benefit of
optimizing arithmetic alone. This profile does not establish that any single
correction can satisfy the 2x gate.

## Symbol attribution

The frozen runtime exports were mapped directly through its binary export
section and JS wrapper aliases. Internal symbols were obtained by an isolated
relink with `--emit-symbol-map`. That changes function ordering, so symbol indices
were NOT applied directly to the original profile. Both binaries were disassembled;
232/232 extracted function bodies matched uniquely after normalizing function
names, call targets and type identifiers. `symbol-mapping.json` retains original
indices, matched symbols, relinked indices and normalized-body hashes.
`map-symbols.py`, both WAT files and the relinked symbol artifact preserve this
procedure. The profiling run executed only the original frozen runtime.

Analyzer: `../analyze-first-profile.mjs`. It identifies the core by exported run
index plus instruction offsets within the original binary length, labels the
separate WABT module independently, and aggregates self/inclusive sample deltas.
