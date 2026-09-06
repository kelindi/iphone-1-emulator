# One fixed-work LTO CPU timing series

Prepared for explicit coordinated quiet START only. No browser, production change, new seed, or VFP fix. Relative correctness is established for all404 focused scenarios/1,212 state rows per build and eight full100M checkpoint states. The actual-WASM VFP suite remains595pass/12fail identically in both builds; this is a known baseline conformance failure, not a passing fullVFP gate.

Use exactly released-control7a72 and candidate7a5288, original512seed d9c661 and unchanged runtime/ABI/clock/device settings. The runner adapts retained `.context/jit-continuation/quiet.mjs` but removes that parked candidate and its changed counter policy. Fixed genuine MBX snapshot764486…, same kernel/tree; press Settings at(275,250), release at5M, Home down at300M and up at305M. Each run retires exactly600M requested instructions in1M API calls. Host-clock pacing is not enabled in this deterministic CPU replay; guest cycles and device deadlines remain original. This measures CPU execution, not physical elapsed navigation.

Instantiate both exact cores and the original seed once in one Node process. Warm each core with one complete600M workload, restoring the identical fixture and reinstalling all512definitions. These two warmups are not timing samples. Because the historical600M reports precede the RTC snapshot extension, their full raw hashes cannot be reused. Instead, the byte-identical released control warmup establishes six fresh100M-spaced raw references. Its first100M checkpoint must match the independently retained7a72 report exactly. Candidate warmup must match all six raw hashes, sizes, explicit TLB counters and original JIT generated/call/active counters plus finalframe before measured arms may start. This is a bounded extension of the same-state gate, not ignored differences or normalization.

Then run exactly **original → candidate → candidate → original**, restoring the fixture, removing old table functions, reinstalling original512definitions and resetting JIT state before every arm. Six full raw snapshot hashes and exact counters per arm must match the control warmup; final320×480frame must also match. Positive generated retirement is mandatory. Thus36 checkpoint comparisons/receipts cover bothwarmups andfourarms,24 of them measured; firstreferencewarmup creates its own post100M references and is not falsely called an independent36-way reference comparison.

Measure summed synchronous `_emu_run(1M)` elapsed intervals and process user+system CPU deltas around those same calls. ProcessCPU can include background V8 work occurring during a sampled interval; both builds receive the same full warmup and accounting. Exclude fixture restore, seed installation, input calls, snapshotcapture, hashing and artifact writes from these metrics; separately retain elapsed including checks. Capture and hashes still perturb between-block execution equally under this declared method. No compiler overhead, first-useChrome latency, browserlaunch or p95 claim follows.

Retain raw per-order timings, both paired ratios, combinedmean CPU/elapsed and everycheckpoint. Do not discard an outlier, choose one order or turn a mixed/tied result into an accepted gain. A repeated CPU improvement would justify a separately reviewed browser gate, not production promotion.

The controller rejects an existing timing directory, verifies exact source/SDK-built module/input pins, requires>4.5GiBfree for at most one failed-state artifact, and caps the **entire process at420seconds** includingtwo600Mwarmups+four600Mmeasuredarms (3.6Btotalinstructions). No retries or cap extension. It closes only its own processgroup on timeout. Firstfailure preserves partial report/log; any failed raw/counter checkpoint additionally writes its complete raw state losslessly asgzip before stopping. Successfulstates retainfullSHA/size/counterreceipts rather than duplicating36largefiles. Prior eightverified100Mrawfiles retain their names, bytes andhashes after independently verified atomic hardlinkdeduplication against frozen historical equalgzipwitnesses.

Frozen entry, only after quiet START:

```
python3 .context/web-lto/timing-once.py
```

All other agents and known externalheavyjobs must be END/held; release them promptly after own processclosure. No heavy guest or benchmark has started under this contract yet.
