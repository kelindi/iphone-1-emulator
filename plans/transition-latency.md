# App transition latency investigation

> Historical recordings and publishing artifacts have been removed from Git history. The summaries below describe the original runs; remaining test reports and screenshots are retained. Historical hash receipts may predate this archival cleanup.

Measured 2026-09-05, CPI 1 unchanged. The initial sections explain the original delay; the verified USB-service recovery below records the resulting improvement. The browser worker already spends approximately 94% of its measured wall interval executing the emulator; scanout/presentation costs about 2–3%. Rewriting the physical case or requesting more canvas frames would not remove the major observed delays.

## Two different delays

A diagnostic-only browser worker adds instruction/timebase counters to actual framebuffer messages and measures emulator/presentation time. It does not change production files, instructions, CPI, input duration, or guest clock policy. Concurrent engine/benchmark work ran in this VM, so these wall times characterize this run rather than isolated hardware throughput.

| Action | First changed guest pixels | Final app/Home shape | Actual issue |
|---|---:|---:|---|
| Settings launch | 122 ms | 10.106 s |Busy CPU stretches a short genuine animation; later app content/settling also costs work.|
| Settings→Home | 497 ms (black) | 13.896 s |A 30 guest-second service connection wait dominates the black interval.|

Ready means retrospective comparison to the final Settings framebuffer or original full-size Home framebuffer, with 0.5% sampled-pixel tolerance below the status bar. Settings rows are already visible at 7.252 s; the stricter final-reference match includes subsequent small changes. This is distinct from first visible feedback. No fixed six-second settling floor is used.

For the busy Settings zoom segment, guest time advances from 0.1749 s to 0.5019 s while wall time advances from 0.741 s to 4.412 s: approximately 0.327 guest seconds consume 3.671 wall seconds. The instruction delta is 134.7M, exactly the expected instruction-driven clock contribution at 412 MHz with CPI 1. This part is CPU throughput/clock-policy work, not idle waiting.

For Home, black appears at guest +2.8017 s and full Home at +32.8011 s: **29.9994 guest seconds** pass during 13.399 wall seconds. Unpaced WFI skips let an idle guest advance faster than wall time, so making the clock track wall time would lengthen this particular wait. An independent native control reproduced black at guest +0.797855 s and Home at +30.797978 s, a 30.000123 guest-second interval. Do not claim that increasing CPI fixes CPU speed or this dependency.

## The blocked guest thread

A snapshot captured on the first black framebuffer shows SpringBoard's main thread in:

```
mach_wait_until → nanosleep → sleep → liblockdown __local_connect → lockdown_connect
```

The saved user PC is `0x30021938`, with return frames at `0x300217e5`, `0x30021693`, `0x31dbb3b0`, and `0x31dbb40c`. The symbol-table nearest-name lookup labels `0x31dbb3b0` incorrectly as an offset into `lockdown_copy_value`; disassembly identifies the separate local-connect routine beginning at `0x31dbb308`.

That routine creates an AF_UNIX socket and retries a failed connection ten times, sleeping one guest second after each failure. `lockdown_connect` supplies `/var/run/lockdown/localcomm`. The 30 second interval is consistent with repeated connection routines in the Home handler; the exact call count still needs a live call trace.

No `lockdownd` process exists in the black-state process list. Cold boot independently executes `/usr/libexec/lockdownd`; at 2B retired instructions it is alive with a local-server thread, so absence of a precreated socket directory is not the complete explanation. Static daemon code at `0x6388–0x6398` calls fatal `errx` if matching `IOIpodUSBDevice` returns no USB mux service. Its local thread does not survive an exit of the whole process. The current boot excludes the whole `arm-io/usb-otg` node to avoid an earlier USB registration crash. A later live trace confirms the first fatal USB lookup at 4,946,760,865 retired instructions, followed by process exit with status 0x100, then repeated launchd respawns hitting the same fatal path. The USB-service recovery below removes that failed dependency without modifying an executable or shortening its timeout.

## Verified USB-service recovery

The original device tree now boots successfully with USB matching enabled. Controls using both the current core and the preserved `origin/main` native binary also boot with the original larger work image. Thus this result retires an obsolete workaround in the tested environment; it does not attribute the recovery to today's compaction, a new USB register implementation, or a device-tree reorder. The exact earlier change that made the historical registration race cease reproducing is not identified.

The new Home snapshot keeps the genuine `lockdownd` alive as PID 17 with three threads, both before and after a Settings/Home cycle. In native tracing, first visible Home pixels follow black by 0.241543 guest seconds, and the fully restored original Home shape follows black by 0.644217 guest seconds, replacing the old approximately 30-second interval. Native wall timing deliberately includes a diagnostic snapshot save at black and should not be used as the uninterrupted latency measurement.

The independent real-browser test supplies that measurement: four Home returns take **3.372, 3.350, 4.012, and 3.850 seconds**, and Settings launches take **7.600, 6.800, 6.700, and 6.800 seconds**. There are no browser errors. Full recording and its method are in milestone 036. Settings remains CPU/clock-policy work, and neither action meets the two-second product target.

Memory stays at `kern_memorystatus_level = 20`. Before the native cycle, free/wired pages are 4,354/49,518; afterward they are 3,927/49,699, with 4 KiB pages. The process and memory captures are `.context/clock/usb-ready-{lockdownd,memory}-{before,after}.txt`; the exact frame trace is `.context/clock/usb-ready.log`. This bounds the tested cycle, not every app's memory demand.

The fix needs a new guest boot. Old checkpoints preserve their unmatched USB registry and cannot acquire this service merely by loading a new WASM binary. Export before Reset; restoring an old backup restores the old service state too. USB cable transfers remain outside this proof.

## Clock policy audit

The core provides `s5l8900_set_active_host_clock` and `s5l8900_set_wfi_host_pacing`, but the current web API installs neither. Active execution advances devices from retired instructions using 412 MHz / 6 MHz; WFI deterministically advances to an eligible device event without host sleeping. Therefore the guest can run slower than wall time during busy rendering and faster while idle.

A proper optional interactive host-clock mode must pair active monotonic timing with paced WFI, reset its anchor across pause/resume/restore, bound catch-up after background suspension, and preserve deterministic regression mode. The host-clock candidate was initially paused when the exact Home service wait was discovered, then tested against the USB-enabled phone after that dependency was repaired. Clock-policy work remains separate from an execution-engine gain.

## Interactive clock validation

With the USB service dependency repaired, the existing core's monotonic active clock plus honestly paced idle waits shortens the original animations. No CPU rate, retired-instruction count, or Apple animation routine is falsified. The guest now observes elapsed foreground time while it renders, so its own animation code can finish without spending many seconds drawing intermediate frames from an artificially slow clock.

| USB-enabled phone | Instruction clock | Interactive clock candidate |
|---|---:|---:|
| Native Settings rows visible | 5.151 s | 2.560 s |
| Native complete Home shape | 2.796 s | 1.053 s |
| Browser Settings ready | 6.700–7.600 s, four rounds | 3.350 / 3.200 s, two rounds |
| Browser complete Home ready | 3.350–4.012 s, four rounds | 1.050 / 1.080 s, two rounds |

The browser methods are identical 50 ms genuine-canvas sampling with a 500 ms stability requirement. These bounded samples are not an isolated-machine throughput benchmark or a product percentile. Settings still misses the two-second target. Candidate reports and original recordings are in `.context/clock-browser-performance`; native traces without diagnostic snapshot pauses are `.context/clock/usb-clean-{control,host}.log`.

A browser worker cannot call native `nanosleep`. The candidate explicitly declines the core's synchronous WFI sleep callback, advances no future guest time, and returns from the outer API loop after that WFI. It records the requested deadline, at most eight milliseconds away. The worker waits asynchronously until that deadline before running again; the next active-clock sync accounts for the real elapsed time once. The existing synchronous-wait failure counter consequently counts intentional cooperative deferrals in this frontend. No callback claims to have slept when it has not.

The worker reinstalls the host clock on pause/resume, successful restore, and after the atomic checkpoint capture. It also cancels stale scheduled turns at those boundaries. Long unexpected host stalls have the core's eight-millisecond catch-up cap. CPU regression callers keep the instruction clock unless they explicitly enable the new API.

Lifecycle validation uses a disposable real Chrome window connected with Playwright's default focus override disabled. Normal Playwright pages force a visible/focused state; a first attempted freeze test emitted no lifecycle events and is excluded. In the real test, the hidden phone keeps exactly the same ticks through a two-second freeze, becoming visible resumes at wall rate, and a manual Pause survives hiding/freezing/showing. Import while paused preserves exactly 3,396,090,995 timebase ticks and 10.5B retired instructions. Foreground guest/wall rate is 0.997, Pause responds in 54 ms, and Resume in 43 ms. The existing app visibility handler passes; no speculative app lifecycle fix is required. Chrome documents these transitions in its [Page Lifecycle API guide](https://developer.chrome.com/docs/web-platform/page-lifecycle-api).

The complete ten-check lifecycle report is `.context/clock-natural-lifecycle/report.json`. Independent controls pass four actions; independent persistence passes 23 actual checks with no failures (the optional headless native-tab-blur check is skipped). Native SoC timing tests pass 5,920 checks. The isolated API proposal passes 10,171 checks including deterministic disable, resume reanchoring after a minute, bounded unexpected catch-up, real WFI yielding without advancing future time, and exactly-once elapsed-time accounting.

Production now enables this clock policy only for a guest with a proven USB-enabled boot configuration. A read-only scan validates the retained OS1 boot-args header, RAM geometry, bounded device-tree location, and the actual `arm-io/usb-otg` compatible property. The result is cached once after initialization or a successful restore; old and unknown layouts keep deterministic timing. This conservative selection matters because old USB-unmatched saves retain the unstable `lockdownd` dependency. An ungated legacy browser probe returned Home in 5.10 seconds; restoring the old captured black state took 11.50 wall seconds / 11.29 guest seconds to reach Home. Those observations expose service-state variability and do not establish a universal 30-second wall timeout. The underlying retry loop still uses guest time. Storage version alone cannot identify the state of an imported v39 backup; selection therefore inspects the actual restored guest state. Five real snapshots verify the gate: the new Home and edited USB Contacts snapshots enable the clock; the original larger Home, old compact Home, and old black-state snapshot do not. A production old-compact Import gives Settings 7.50/7.05 seconds and Home 14.20/14.45 seconds, consistent with its former timing. This does not migrate old profiles or give them the new speed. The comparison server at port 8084 remains isolated as an ungated reference.

The final **production default** passes four rounds: Settings **2.650 / 2.950 / 2.700 / 2.900 seconds**, Home **1.150 / 1.100 / 1.250 / 1.100 seconds**. The unaccelerated 34.44-second recording and matching thumbnail are in milestone 038. The final measured guest/wall rate is about 1.00; this is not a claim of higher CPU throughput. The core's 24 native suites pass, including 10,195 browser-API checks for valid, old, malformed and restored boot configurations. Eight JavaScript unit tests, six production offline checks and four production physical-control checks pass.

A maintained lifecycle regression now launches and cleans up its own disposable Chrome profile. It uses a real tab transition and Chrome freeze state, then exports the paused guest, advances it, imports that actual backup, and checks exact saved ticks and retired instructions. Its ten checks also pass against the production build in `.context/clock-production-lifecycle/report.json`: foreground guest/wall 0.988, Pause 81 ms, Resume 41 ms, exact saved tick/instruction restoration, and no background catch-up. It requires Chrome plus either an existing `DISPLAY` or `Xvfb` (on this VM: `sudo dnf install -y xorg-x11-server-Xvfb`). The default fresh phone must support interactive clock mode; an optional `FIXTURE_SNAPSHOT` can supply a known USB-enabled fixture.

```sh
BASE_URL=http://localhost:8080 npm run test:clock
```

## Reproduce

```sh
BASE_URL=http://localhost:8080 \
ARTIFACT_DIR=.context/transition-profile \
ACTIONS=settings-cold,home-first \
node web/tests/transition-profile.mjs
```

The tracked diagnostic script serves a locally instrumented worker through Playwright request routing, records each changed genuine framebuffer with guest counters, and writes JSON plus final screenshots. `--record` optionally adds video overhead; the timing run above did not record video. Native corroboration and the exact black snapshot are retained in `.context/clock/`, including `black/black.snap`, `black-springboard.txt`, `control.log`, and daemon/library disassembly. These workspace artifacts are gitignored; the measured facts and reproduction method are recorded here.

Final production persistence regression: 23 actual checks pass in 96.82 seconds, no failures, and the restored Settings framebuffer differs by exactly zero sampled pixels. The optional native-tab-blur check is skipped in that headless suite; the separate real-tab lifecycle suite supplies that coverage. Report: `.context/clock-production-persistence/report.json`.

## Foreground clock carry: independent review (2026-09-05)

The original eight-millisecond discard policy and worker reanchor after ordinary checkpoint capture can lose foreground time during Save. The carry candidate keeps representable elapsed foreground time as unpaid CPU-clock ticks, deducts already observed paced-WFI ticks, and applies at most the floor of eight milliseconds per synchronization. Explicit Pause, restore, invalid/backwards host samples, and clock-policy replacement clear transient debt and reanchor. A long stall without a lifecycle event remains foreground time; it is paid in bounded steps. This is clock conservation, not a CPU throughput claim.

Independent review rejected an initial one-second debt ceiling because it would still lose longer foreground stalls, identified a below-125Hz zero-payment corner case, and required live CPI changes to be rejected because they would reinterpret pending debt and fractional ticks in different units. The corrected candidate retains full representable debt, preflights the supported rate before browser clock-policy mutation, and rejects live frequency changes. An odd-frequency payment cap uses a fresh integer floor, independent of the saved fractional remainder.

The maintained native proposal `.context/clock-review/test_clock_carry.c` passes **9,692 checks with zero failures**, including the clean candidate with diagnostic fields removed (`maintained-native-clean.log`). It reaches real synchronization through public run/tick APIs. A separate rational oracle checks 600 deterministic intervals at each of 125, 1,001, 6,000,000 and UINT32_MAX Hz, with repeated 2.5-second stalls, observed WFI time, exact fractional carry and bounded individual payments. Additional contracts cover failed/backwards/overflow samples, a one-hour Pause, CPI rejection, byte-identical guest snapshots with and without host debt, restore preserving policy but clearing transient debt, and unchanged legacy discard behavior. The initial isolated oracle also passed 9,726 checks; the maintained version changes check grouping and adds snapshot contracts.

The maintained worker lifecycle test now runs actual snapshot handlers in both running and paused states, asserting begin/advance/free completion, preserved installed JIT tables, and no clock-setter calls during capture. Explicit Pause/Resume still must invoke the setter. The old worker fails both new anchor assertions; the candidate passes all 26 lifecycle/controller tests. Evidence is in `.context/clock-review/worker-{old,candidate}.log`. These are dependency-mocked lifecycle tests; genuine Stopwatch and browser persistence/lifecycle runs remain the product acceptance gates.

The diagnostic candidate WASM also retains ABI revision 1 and all 16 existing seed offsets (`.context/clock-review/jit-abi.json`). Its SHA-256 is `8e3feceba9314926f0df97a26fb5d4578d363db60686060c31b66715f0550767`; this identifies the reviewed diagnostic build, not the subsequent clean production module. Snapshot serialization remains v39: the source changes only the host structure-size guard and resets debt after load. The full production native, JavaScript, exact JIT replay, real Stopwatch-with-Save, and clock lifecycle gates are recorded separately from this source review.

After integration, the independent production browser regressions pass on WASM `c4d9e21cbc9da4209942c38857322dab7e086b9955b29b843213ebd6eb764f3c`, offline bundle `70cff33716687a33e5372482`, with the software-rendered firmware default preserved. **All 12 clock lifecycle checks pass** with positive compiled-execution assertions before and after restore. Foreground guest/wall ratio is 1.00036; Pause acknowledges in 104ms and Resume in 55ms. Hidden/frozen/manual-Pause guest ticks remain exact, paused Import restores exact saved ticks and retired instructions, and the restored foreground ratio is 0.99742. JIT generation changes from 1 to 2 and retires 7,537,032 instructions after restore. These short functional timing samples are not a launch-speed benchmark.

**All six physical-control checks pass**, including real LCD sleep/wake and genuine slide-to-unlock, with the resulting complete Home screen visually reviewed and 258,103,960 compiled guest instructions retired. **All seven offline checks pass**: a disconnected reload starts the phone and installs 512 seed regions (821,214 compiled instructions retired), corrupt cached firmware fails visibly and is evicted, and reconnecting plus Retry recovers. Reports are `.context/clock-debt/production-{lifecycle,controls,offline}/report.json`; all disposable browser sessions were closed afterward. The source proposal is now maintained as `core/tests/test_clock_carry.c`, registered in the native CMake suite.

## Foreground Save time conservation

The real Stopwatch exposed a foreground timing gap after the guarded execution engine was enabled. One idle minute passed at 59.8 displayed seconds / 60.0016 wall seconds (−0.336%). A second run with ordinary Save mid-interval failed: 58.0 displayed seconds / 60.0097 wall seconds (−3.349%). Hardware ticks advanced 58.144639 seconds, confirming actual guest-clock loss rather than just a delayed screen refresh. Other agents also ran functional browser workloads; this failure alone does not attribute the entire deficit to Save. [Both original-speed recordings](web-iphone.md) remain preserved.

A diagnostic-only build with unchanged clock semantics isolated the accounting mechanisms in another Save-load minute. Of a 799.024 ms hardware-clock deficit, three atomic captures consumed 752.8 ms; their anchor resets discarded 755.0 ms including the small pre-copy gap. An internal cumulative counter measured 37.400 ms additionally discarded by the eight-millisecond clamp. The remaining 6.624 ms versus reset gaps plus clamp loss falls at diagnostic read/scheduling boundaries. This lighter run displayed 59.5 seconds over 60.0012 seconds, which passes 2%; the earlier failed sample remains a valid load regression. Reports are `.context/clock-debt/baseline-accounting.json` and `.context/clock-debt/diagnostic-baseline/`.

The isolated correction adds an explicitly enabled foreground carry policy. It preserves the existing native default that discards excess stalled time. When enabled, elapsed monotonic time plus previously unpaid CPU-clock ticks is reduced by already modeled WFI ticks, then paid to devices in steps no larger than eight milliseconds. Integer debt and fractional conversion are retained without a one-second ceiling. Invalid/backwards samples and representational overflow fail closed; callback reinstall/clear, snapshot restore and CPU warm reset discard transient debt. Rates below 125 Hz are refused because a single tick would exceed the payment bound; changing CPI while carry and its host callback are live is refused to avoid changing the units of unpaid time. Browser startup sets CPI before enabling its compatible foreground clock, and older unmatched profiles retain their existing timing policy.

The worker retains the active anchor through ordinary atomic capture, while its established Pause, hidden-tab pause, restore and Resume paths still reinstall or clear the clock. Pending time is host-only bookkeeping, not part of the v39 snapshot payload. A host suspension that supplies no visibility/pause event is treated as elapsed foreground time and repaid in bounded steps; explicit visible-document freeze/resume behavior is not separately established by the existing hidden-then-frozen lifecycle test. Explicit Pause discards pending debt as part of its clock discontinuity rather than draining it into a paused phone.

The candidate passes a recorded Save-load minute: actual digits 00:00.6 → 01:00.9 over 60.002 wall seconds, or 60.3 displayed seconds / +0.497%, within 2%. Hardware ticks advance 60.0019845 seconds, with zero queued debt at every sampled endpoint. Three roughly 842 ms of total captures are now accounted for. Ordinary Save completes, JIT remains active, Lap/Stop/Reset work, and there are no browser errors. Milestone 051 preserves the complete recording and thumbnail; `.context/clock-debt/diagnostic-candidate/report.json` retains human visual review separately from capture completion.

Validation before production integration: 1,118 direct per-sync contracts pass, including unchanged snapshot bytes with/without pending debt; an independent 9,726-check public-API conservation oracle passes both diagnostic and clean archives. The maintained proposal contains 9,692 checks, adds snapshot/restore coverage, and avoids a compiler-specific 128-bit integer dependency. Existing SoC and snapshot suites pass unchanged; the browser API suite explicitly changes its old unexpected-foreground-stall expectation to retained debt while preserving paused-minute exclusion. The maintained worker lifecycle regression fails on the former snapshot anchor reset and passes the candidate, preserving JIT tables and explicit Pause/Resume resets. Production integration uses `.context/clock-debt/production.patch`, which removes diagnostic exports/counters; the host struct guard changes by 8 bytes, with no snapshot-format or seed-CPU-ABI change. Full production build and lifecycle checks are tracked in the execution log.

Final production Stopwatch verification: the clean build at offline revision `70cff33716687a33e5372482` passes the measured Save-load minute. Actual digits **00:00.4 → 01:00.8** advance **60.4 displayed seconds over 60.0022 wall seconds (+0.663%)**, within the 2% target. Hardware ticks advance 60.0025385 seconds independently. Explicit ordinary Save completes, compiled instruction counters increase from 195,819,261 to 1,762,253,907, and no browser errors occur. Lap/Stop/Reset pass with lap 1 at 01:00.9, the next lap stopped at 00:02.4, and Reset returning zero and clearing rows; subsequent stopped/reset raw captures match exactly. Other functional browser tests ran concurrently, so this is a clock correctness workload rather than a quiet throughput comparison. The reviewed report is `.context/clock-production-final-stopwatch/report.json`; milestone 051 retains the full production recording and the earlier candidate recording.

## Initial Save capture reduction (071)

The integrated snapshot page classifier reduces the initial synchronous capture without changing saved bytes. In the quiet actual Chrome baseline/candidate/candidate/baseline sequence, capture is 272.6/224.9 → 135.6/142.8 ms and the first Pause acknowledgement is 328.3/250.2 → 180.9/206.2 ms. The 150 ms input target is still unmet in those candidate observations. Finalization is 529.0/508.8 → 546.9/563.5 ms, so this experiment does not establish a reduction in finalization, compression or total Save time. The separate fixed-phone WebAssembly-in-Node capture benchmark improves about 65%, but that result must not be substituted for browser interaction latency.

All four genuine Save/Pause/Resume/reload sessions pass 14 checks each with generated execution active. Independent screenshot review finds exact RGB agreement across all eight Home stages below the status bar. Full snapshot bytes remain identical across two actual fixtures and guest execution interleaved with capture finalization. Final production `b3237e…` additionally passes clock lifecycle 12, persistence 23 and real old-runtime-to-new-runtime offline upgrade 24; all native/JavaScript/smoke checks pass. [Original-speed recording and limits](../docs/traces/snapshot-page-scan/README.md) · [reports, source review and reproduction](../docs/traces/snapshot-page-scan/README.md).
