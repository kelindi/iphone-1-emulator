# Project overview and work log

## What this project is

`iphone-1` is an S5L8900 (original iPhone SoC) emulator whose goal is to boot and interact with the genuine Apple iPhone OS 1.0 build `1A543a` on an emulated `iPhone1,1` / `M68AP`. The emulator derives from the S5LBox research base, but adds a clean M68 machine target, OS 1.0 firmware tooling, and both a native harness and a WebAssembly/browser build.

Fidelity rule: the project runs Apple's real kernel, frameworks, launchd, SpringBoard, and applications without patching Apple binaries. The guest screen is the emulated framebuffer; it is never recreated in HTML/CSS or substituted with later firmware. The surrounding physical case and browser controls use HTML/CSS. Work-image data and boot-time device-tree adjustments are listed below.

Repository: https://github.com/kelindi/iphone-1
Main PR: https://github.com/kelindi/iphone-1/pull/1
Detailed engineering log: `docs/iphone1_1_os1_port.md`

## High-level architecture

```
core/                    portable emulator core, no host UI, C11 only (WASM-friendly)
  src/arm/               ARMv6 interpreter + VFP
  src/mmu.c              ARMv6 short-descriptor MMU + TLB
  src/soc/               shared S5L8900 devices (VIC, GPIO-IC, timers, CLCD, SPI, I2C,
                         I2S, DMA, NOR, NAND, MBX, PMU, UART, USB OTG, buttons, Z1/Z2)
  src/board/             machine definitions: M68 (iPhone1,1) and N82 (iPhone 3G)
  src/boot/              OS 3/iBoot bringup and OS 1.0 direct-XNU bringup
  src/firmware/          8900/IMG2/IMG3 unwrap, LZSS, Mach-O loader, kernel symbols
tools/iphone1.c         native host harness: run loop, tracing, screenshots, input
tools/firmware/         Python IPSW extractors, HFSX tools, work-image builder
docs/iphone1_1_os1_port.md   detailed boot log, MMIO notes, and current blockers
```

The key split is: shared S5L8900 SoC code in `core/src/soc/`, and per-board wiring in `core/src/board/` behind a `s5l_board_t` vtable. No board-specific `if` statements are in the SoC code.

## Status

**Apple logo loader (2026-10-13):** browser startup now shows an Apple logo and an indeterminate bar over black. Stage text remains accessible; errors reveal their message and recovery controls. Unit, loader/recovery and offline checks pass locally, with fresh recording and thumbnail. Genuine guest rendering is unchanged; deployment deferred. [139](../tweets/139_apple_logo_loader.md).

**Minimalist browser shell and sharing preview (2026-10-13):** removed the 3D option and its renderer/dependency; the existing front-facing phone remains, with the bottom instruction label removed. Added a static OG card using the genuine guest home screen and a light, condensed Instrument Serif title reading “The Iphone”, plus canonical/social metadata for `https://2007.mobile/`. Local unit, WASM smoke, physical controls, offline recovery and focused mobile/keyboard checks pass. Deployment and old-domain redirects are deferred at the user's request. [138](../tweets/138_minimalist_phone.md).

**YouTube audio startup failure traced (2026-09-10):** the original silent-clip attempt reaches `AudioDeviceStart`, which returns `0xe00002d8` after AppleBasebandOutput reports DMA not ready. MeCCA maps this to the generic movie error. Passive observation matches all 252,624,866 control snapshot bytes. A second qualified kernel replay confirms the I2S synchronization wait times out without callbacks, before DMA submission. The audio clock/transport model and playback remain unfinished. [137](../tweets/137_youtube_audio_start.md) · [evidence](../docs/traces/youtube-audio-start/README.md).

**YouTube player rotation repaired (2026-09-10):**3,024 renderer checks pass; ordinary native replay completes all1,695 graphics operations with zero rejections/restarts. Browser shows the real movie error without graphics restarts, but its harness cleanup timed out and remains failed. Exact MP4 and Motion-JPEG delivery work; playback does not. Qualified observation exposes generic media-preparation error1; underlying cause remains open. [136](../tweets/136_youtube_player_rotation.md) · [evidence](../docs/traces/youtube-player-rotation/README.md).

**Original YouTube movie delivery (2026-09-10):** the original CoreMedia player requests and receives both MP4 byte ranges, including the entire owned clip. Rotation support reaches the actual movie error alert in native and browser runs. Playback still fails; media decoding remains under investigation. Latest renderer has 2,964 passing checks and four unqualified alternate-shader draws remaining. [135](../tweets/135_youtube_movie_delivery.md) · [evidence](../docs/traces/youtube-movie-delivery/README.md).

**Original YouTube local catalog (2026-09-10):** the original app consumes a local token and displays an owned clip's title, thumbnail and duration. Exact exchanges and a clean browser recheck pass. Tapping the clip exposes unsupported 90-degree graphics commands and driver restarts; playback remains unfinished, with the graphics repair in progress. [134](../tweets/134_youtube_local_catalog.md) · [evidence](../docs/traces/youtube-local-catalog/README.md).

**YouTube device signature verified (2026-09-10):** the original app completes both local challenge requests and its 2048-bit signature verifies against the certificate and guest public key. Matching identity provisioning and the exact original public-key filename fixed the gate. Final token is deliberately withheld; catalog and playback remain next. [133](../tweets/133_youtube_device_signature.md) · [evidence](../docs/traces/youtube-auth-probe/README.md).

**YouTube service check (2026-09-08):** original catalog schema and pre-feed device authentication mapped. Current app still fails; local handshake appears feasible, with original device-identity/signing as the next unqualified gate. No catalog or playback success claimed. [132](../tweets/132_youtube_authentication_gate.md) · [plan](youtube-services.md).

**Weather and Stocks local services (2026-09-08):** original Weather now displays Cupertino72F/NewYork64F forecasts, swipes between cities, refreshes after Home/revisit and converts to22C. Stocks quotes pass on the same build. Exact DNS/HTTP bytes and recording retained. Fixed demo data; isolated candidate, not default deployment. [131](../tweets/131_weather_stocks_local.md).

**Original Stocks demo quotes (2026-09-08):** real DNS and finance HTTP now produce four visible sample quotes in the original app, reproduced in two browser runs. Charts and Weather remain unfinished; the Weather request parser must accept its original `t` parameter. Isolated candidate only. [130](../tweets/130_stocks_local_quotes.md).

**Safari gateway persistence (2026-09-08):** distinct local form responses pass before and after normal autosave/reload and real unlock, with complete retained HTTP exchanges. Toolbar rendering remains imperfect; isolated candidate only. [129](../tweets/129_safari_gateway_persistence.md).

**3D startup click corrected (2026-09-08):** the model now opens before the emulator is ready and displays loading feedback. Fifteen Chrome checks pass, including independently delayed worker/model downloads and real angled unlock. [128](../tweets/128_3d_startup_click.md). The user rejected the procedural model’s visual quality; a licensed online replacement is being evaluated.

**Combined app gateway (2026-09-08):** own port80 service boots and original Safari submits its local form. Weather/Stocks adapters pass host contracts, but those apps fail before reaching the gateway in the retained log. Original Weather’s asynchronous resolver skips the monitored hosts-file path and reports cannot-find-host; Safari confirms named-address failure. Their routing/parser/display acceptance and fresh post-reload request remain open; no default promotion. [Milestone127](../tweets/127_combined_app_gateway.md), [evidence](../docs/traces/combined-app-gateway/README.md).

**Rotatable iPhone view (2026-09-08):** a 2D/3D switch now exposes a locally authored first-generation iPhone model with the real guest framebuffer. Case dragging, wheel zoom, keyboard rotation, angled screen input and model Home work. Twelve Chrome checks and 115 JavaScript unit tests pass, including mobile viewport and WebGL-loss fallback. [Milestone126](../tweets/126_rotatable_iphone_3d.md) retains recording, evidence, and fidelity/performance limits.

**App-service work resumed (2026-09-08):** current-renderer Safari retest renders the local form response, then loses its server. Original launchd records SIGBUS10; the faulting daemon instruction remains unknown. [125](../tweets/125_safari_service_exit_identified.md). Combined local gateway and Weather/Stocks adapters are in progress; [other-app opportunities](app-services-opportunities.md). Compiler redesign remains parked.

**Second compiler throughput gate failed — review required (2026-09-08):** the single corrective revision adds bounded exact-WAT module reuse and cheaper context rejection. All 54 checkpoints pass, but throughput is **0.380x baseline (2.63x slower)** against the 2x target. [Evidence](../docs/traces/adaptive-second-gate/README.md), [update124 and recording](../tweets/124_second_compiler_gate_review.md). The correction allowance is exhausted; optimization stops for the accepted plan’s review. The product engine is unchanged and the usable-speed goal remains unmet.

**First compiler throughput gate failed (2026-09-08):** all18 runs/54 full-state checkpoints pass correctness, but aggregate CPU throughput is0.337× the frozen seeded engine (about3× slower), against a2× target. [Gate evidence](../docs/traces/adaptive-first-gate/README.md). Profiling now informs the one corrective revision allowed by the accepted plan; a second failed gate requires review. No browser activation or speedup claim.

**Generation-only code reuse (2026-09-08):** installed units atomically revalidate unchanged mappings/context after TLB generation changes; queued stale installs remain rejected. Continued collection removes the prototype lifetime-attempt cutoff while preserving live caps. All nine full app checkpoints pass with135–145M generated instructions per trajectory;18,178 native checks pass. [122](../tweets/122_reuse_compiled_code_after_tlb_changes.md). Next is the predeclared ABBA throughput comparison; no new speed claim.

**Full compiler app replays (2026-09-08):** Settings, Contacts and Notes each complete600M credits with all nine frozen full-state checkpoints matching. Indexed dispatch and synchronous actual-code collection are implemented. [121](../tweets/121_full_compiler_app_replays.md) records exact generated counts and limitations: compilation attempts exhaust and generated coverage remains low. Generation-only installed-unit re-admission and continued collection are next; ABBA throughput gate and product activation remain pending.

**Real-phone compiler harness (2026-09-08):** passive capture by virtual PC passes 287 native checks. An isolated real-browser-API build restores the frozen phone and matches all 244,887,522 saved bytes after interpreter versus empty-adaptive execution. [120](../tweets/120_real_phone_compiler_harness.md) records the corrected derived-state setup and scope. Next: hot-entry collection, actual compiled app replay and the throughput gate. No generated app coverage or new speed result yet.

**Compiled warm RAM stores (2026-09-08):** ARM/Thumb word and byte stores now use the validated write cache; code overlap forces immediate exit and retirement. 51 integrated cases, 33,455 instruction comparisons, 115 unit tests, 150 native cache checks, 529 runtime checks and all 30 native suites pass. [119](../tweets/119_compiled_ram_stores.md) preserves failure corrections and limits. Next is real-app capture/integration and the frozen throughput gate; no product activation or measured speedup.

**Compiled warm RAM loads (2026-09-08):** ARM/Thumb byte and word loads now emit real WASM reads through a qualified warm-cache helper. 37 integrated cases preserve full snapshots and exact cache accounting; 33,043 interpreter comparisons, 107 unit tests, 52 native cache checks and all 30 native suites pass. [118](../tweets/118_compiled_ram_loads.md) records scope and the corrected signed-JS fixture. Stores, broader memory/writer qualification and real-app speed gates remain; product activation is still pending.

**Compiler lifetime barriers (2026-09-08):** adaptive builds invalidate before restore/free/boot/load; disable also cancels capture-only pending work. Actual WASM restore/continuation passes, alongside 25 integrated cases, 172 lifecycle checks and all 30 native suites. [117](../tweets/117_compiler_lifetime_barriers.md) records scope, review correction and remaining engine/writer qualification. No browser integration or speed result yet.

**Physical code versions (2026-09-08):** optional RAM tracking now rejects changed physical dependencies during capture/install/entry/active guards. CPU cached stores, bus/DMA, MBX direct copies and the host loader notify committed writes. 24 integrated cases, 652 writer checks, 8,511 registry checks and all 30 native suites pass. [116](../tweets/116_physical_code_versions.md) and the [writer audit](../docs/adaptive-ram-writers.md) distinguish implemented hooks from remaining legacy-generated and boot/restore bypasses. Literal guards remain; no product activation or app-speed result.

**Thumb and memory handoffs (2026-09-08):** Thumb arithmetic/shifts now compile; 32,631 interpreter comparisons, 100 unit tests and 19 integrated cases pass with complete machine-state equality in the integrated cases. New cases exercise actual MMU-enabled Thumb execution, precise memory faults, partial stores, MMIO and guest code modification. [115](../tweets/115_thumb_and_memory_handoffs.md) retains evidence and corrected fixture failures. Memory still uses reference fallback; physical versions and real-app speed qualification remain outstanding.

**Register ALU coverage (2026-09-08):** ARM register operands and immediate/register-controlled shifts now compile. 12,731 interpreter comparisons, 97 unit tests and ten integrated runtime cases pass; dispatcher groups preserve the full 512-instruction cap. [114](../tweets/114_register_operand_compilation.md) records evidence and 6,333 newly admitted static positions. Memory/Thumb coverage, code versions and real-app performance remain outstanding.

**MMU/timer qualification (2026-09-08):** nine integrated cases pass, including nonidentity mappings, guarded slow-load handoff and a real Timer4 IRQ after exactly 69 generated instructions. Full snapshots remain the comparator; a missing TLB-hit update was fixed rather than excluded. [113](../tweets/113_mmu_and_timer_execution.md) preserves the failure and final results. Broader instruction coverage and real-app speed gates remain.

**Compiler/runtime connected (2026-09-08):** actual emitted WASM now runs through the C registry, live guards and real scheduler in an isolated build. Three synthetic workloads match complete saved-machine bytes; fatal single-step bypass is blocked. 8,473 registry and 1,067 interpreter checks pass. [112](../tweets/112_compiler_runtime_connected.md) preserves results. Real-app coverage, MMU-on generated execution, device boundaries and performance gates remain. No product activation or speed claim.

**Guard/scheduler protocol (2026-09-08):** real interpreter/MMU mapping tests pass, including alias and XN-fault handoff. Optional scheduler dispatch now preserves full snapshots across tested native callback exit policies and stops on fatal results without replay. [111](../tweets/111_guard_and_scheduler_qualification.md) records scope. Actual WASM registry/guard/lifetime integration remains next; the product build does not enable this path.

**ABI2 C result interface (2026-09-08):** static result layout, fatal malformed-result checks and exact cycle ownership are implemented. Native negative tests and actual emitted WASM through the C wrapper pass. [110](../tweets/110_compiler_c_result_bridge.md) preserves source and test commands. Admission, real guards and machine scheduler dispatch remain next.

**Executable compiler slice (2026-09-08):** connected IR now emits actual WASM for the initial integer/branch slice. 4,098 comparisons against the retained C interpreter and 95 unit tests pass. [109](../tweets/109_executable_compiler_slice.md) documents the evidence and limits. The emitter is isolated from production; ABI2 runtime/scheduler integration, real memory guards and performance qualification remain.

**Connected IR units (2026-09-08):** graph construction now follows code across windows, splits loop/join targets, preserves dynamic branch exits and enforces 32-block / 512-instruction / eight-physical-granule bounds. All 88 JavaScript tests pass. [108](../tweets/108_connected_instruction_units.md) records source and validation. WASM emission and ABI2 runtime integration remain next; no adaptive speedup is claimed.

**Compiler foundation (2026-09-08):** first explicit branch/immediate-ALU decoder and fixed Settings/Contacts/Notes workloads implemented. Each workload replays twice with matching complete machine checkpoints; all 79 JavaScript tests pass. [107](../tweets/107_compiler_instruction_foundation.md) records the limits. No adaptive code execution yet; ABI2 integration and connected code generation are next.

**Adaptive compiler Stage 1 (2026-09-08):** independent frozen inputs and lock-aware navigation qualification are implemented. A complete 62-action cloud baseline gives warm Settings median 2.318s / p95 2.567s and Home median 0.866s / p95 0.868s. [Milestone 106](../tweets/106_adaptive_compiler_baseline.md) preserves evidence. [Accepted implementation plan](adaptive-compiler.md) and [correctness contracts](../docs/adaptive-compiler-contracts.md) define the next compiler stages. This is baseline tooling, not an adaptive-engine speedup. Reference-Mac measurements remain pending because Mac execution is unavailable in this session.

**Measured gap-fill trial (2026-09-07):** one isolated256-position addition across23of the existing512regions passes272focused checks and complete replay states. Fresh88-action browser ABBA gives warmSettings median2.293→2.167s(5.47%),p952.667→2.218s;Home median0.867→0.818s,p95effectivelyunchanged. Below the predeclared10%median screen; production seed/core unchanged by this trial. [105](../tweets/105_measured_seed_gap_trial.md) records the modest gain and limits. An initial run was invalidated by the concurrent lock-screen worker edit propagating into hardlinked staging; independent original-shell copies and served-worker hashes were used for the clean comparison. The product lock-screen changes remain intact.

**Slide to unlock on opening (2026-09-07):** browser startup now uses the real Sleep/Home buttons to enter the original lock screen before exposing frames and input. Seven Chrome checks pass for fresh opening, partial/full slides, and reopening saved Home/locked/asleep sessions; all 58 JavaScript unit tests pass. [Milestone 104](../tweets/104_open_with_slide_to_unlock.md) includes the recording and scope.

**Compiled-exit diagnosis (2026-09-07):** isolated observer passes512 unchanged-WAT projections,7 counter checks, complete100M/400M state comparisons and six browser windows with exact count conservation. Two warm Settings runs agree:40.1% of generated exits reach unprepared positions,38.6% cross regions,18.2% hit memory checks,3.1% exhaust budget. Separately74.3% of rejected entries have emitter-supported observed opcodes. Next bounded test should fill frequent supported holes within the existing512-region seed before another compiler architecture; counts do not predict speedup. Production unchanged. [103](../tweets/103_compiled_exit_census.md) retains evidence and recording.

**Connected compiler feasibility (2026-09-06):** QEMU-WASM reuse needs CPU/MMU adapters, so an isolated existing-emitter prototype keeps registers local across up to four connected regions. 32 generator checks, 403 focused CPU/MMU comparisons and full real-phone checkpoint hashes pass. Quiet 400M ABBA replay is **4.07% slower** despite 7.77% fewer compiled entries. Candidate rejected; production unchanged. [102](../tweets/102_connected_compiler_experiment.md) records evidence. Next prerequisite is an exit-reason/frequent-edge census, not another blind cluster-size change.

**Performance research (2026-09-06):** [prior-art review](../docs/research/browser-emulation-strategies.md) finds credible untested architectural options: connected ARM/Thumb compilation, code-page invalidation, better memory slow paths and offline optimized translation. Experimental QEMU-WASM is a reuse candidate to assess, not a demonstrated ARMv6/iPhone replacement. The native interpreter pilot and failed micro-tweaks do not establish a physics ceiling. [101](../tweets/101_browser_emulation_prior_art.md) records the research; no implementation or new speedup.

**Core-hotspot attempt closed (2026-09-06):** current function81 is reliably bound to `arm_step`. One isolated ARM decoder partition passes exhaustive routing and complete guest-state comparisons, but88 real browser actions show warm Settings/Home medians effectively tied (2.317s/0.867s). Reject the candidate; no production speedup. [100](../tweets/100_core_decoder_experiment.md) records the experiment.

**Focused compiler decision (2026-09-06):** one warm Settings profile rejects the proposed short-loop specialization before implementation: all 47 eligible regions together account for only 4.5% of sampled worker time, too little to support the 20% target. Core execution remains the largest category (59.7%); generated code is 27.8%. These are one profiled interval's residency shares, not a speed benchmark. [099](../tweets/099_compiler_hot_path_profile.md) retains evidence and recording. No production change or speedup.

**Architecture comparison (2026-09-06):** a same-VM native/Chrome/Chrome/native pilot completes32 Settings/Home actions with all final frames reviewed. Native improves fresh-session first Settings from3.56 to2.27s, but warm Settings only2.243→2.045s and Home0.842→0.806s. These are small-sample medians on the current2.90GHz VM, not a production speedup or comparisons to the earlier2.50GHz host. Streaming adds delay to the native figures; keep browser execution for now. [098](../tweets/098_native_browser_performance.md) records the benchmark, inexpensive local encoding test and remaining network/Mac uncertainty.

**Default graphics correction (2026-09-06):** the Safari source-row fix is now in the local web package. Actual previous-runtime saved-phone upgrade passes 30 checks and the final unmarked package passes 21, including ordinary autosave and disconnected restoration. All 30 native suites and 58 JavaScript tests pass. [Milestone 096](../tweets/096_graphics_fix_default_release.md) records the result and retained first audit failure. App launches remain too slow. Following the user's concern about elapsed time, work is narrowing to perceived responsiveness; further audio/network expansion is deferred while existing runs close out.

**Latest speed experiment (2026-09-06):** expanding compiled coverage preserves all four complete guest checkpoints and passes 88 browser screen checks. Warm Settings p95 improves from 3.467 to 3.261 seconds in the pooled comparison, but first-launch results are mixed and reverse-order Home p95 worsens by about 100 ms. The candidate remains isolated; the default CPU is unchanged. [Milestone 088](../tweets/088_mixed_seed_coverage_diagnostic.md) includes the complete result and the disk-cleanup interruption before the final control. An isolated full-web LLVM LTO build preserves the public API, JIT ABI, focused CPU boundary states and all four complete 100M checkpoints in both interpreter and original-seed execution. A quiet fixed-work comparison reduces CPU time by 5.81%, with exact checkpoint matches. The browser comparison is now complete with the failed first control explicitly retained: all 88 screen pairs match, warm Settings median improves only 1.53%, and pooled Home p95 worsens 49 ms. The candidate remains parked; [milestone 092](../tweets/092_web_lto_navigation_diagnostic.md) includes the separately frozen continuation and recording limitation. Production CPU and seed remain unchanged. An exact executed-work counter now passes focused state/count tests and all four complete checkpoints in its single100M trajectory check. Retained counts identify already generated hash/search loops; a separately bracketed warm-browser census remains unrun.

**Existing browser floating-point defect:** the unchanged WASM core and LTO candidate produce identical VFP test results: 595 passed, 12 failed. Missing exception flags and ineffective directed rounding explain the failures, including three incorrect vector refusal/commit checks. [The retained conformance report](../docs/traces/wasm-vfp-conformance/README.md) separates this baseline correctness issue from relative LTO validation. Native test success does not cover these browser semantics; a separate fix remains open.

**Calculator input measured (2026-09-06):** direct canvas delivery resolves the skipped-frame uncertainty in [089](../tweets/089_calculator_input_feedback.md). All 60 measured presses pass independent retained-pixel checks; button feedback p95 is 135.7 ms for 9 and 134.0 ms for C, meeting the 150 ms target for this workload on cloud Chrome. [Milestone 090](../tweets/090_calculator_frame_delivery.md) retains the complete recording, observer overhead and limits. This confirms existing behavior; it is not a production speedup, a physical-paint measurement, or acceptance of every app. App launches remain too slow.

**Original Safari local demo (2026-09-06):** one isolated imported phone passes 20 checks: original keyboard URL entry, rendered local pages, link, form, bookmark, Home/revisit and ordinary autosave/reload. Seven actual guest HTTP exchanges are independently verified, including two fresh requests after restoring the running server. [Milestone 091](../tweets/091_safari_local_demo.md) includes the full recording and exact retained-screen comparisons. This builds on [own program execution](../tweets/086_own_guest_program.md) and [loopback qualification](../tweets/087_guest_loopback_http.md). Default-phone distribution, external networking and Weather delivery remain open; the demo is not yet available in the normal starting phone.

**Default local-demo qualification failed (2026-09-06):** the maintained quiet daemon boots from the actual pristine MBX default image and original Safari renders its landing page and Next. The first form stalls with guest graphics hangs; the final process list also lacks the daemon, whose exit cause remains unobserved. [093](../tweets/093_default_local_demo_qualification_failure.md) retains the complete single attempt and failed-state evidence. Default distribution remains blocked; no retry, software fallback or restart-policy change is hidden.

**Original Photos fixture (2026-09-06):** our own importer writes two image/thumbnail pairs using original filesystem calls, with complete payload verification. Actual Photos recognizes both and renders the first full image, but navigation then leaves stale content and a narrow strip. [Milestone094](../tweets/094_photos_first_workflow_failure.md) records the failed first workflow. Graphics, second-image navigation and persistence remain open; the default phone is unchanged.

**Visible recovery (2026-09-06):** failed downloads and later runtime errors now show recovery actions inside the phone. One real browser session passes 12 checks, including pointer and keyboard retry into the genuine OS. [Milestone 085](../tweets/085_visible_recovery.md) records the test and its injected-error scope. This frontend follow-up changes the shell generation to `6fc3927f86a86269a30dc811`; the accepted core and loader below remain unchanged.

**RTC and seed-loader integration (2026-09-06):** the default local web build now includes complete manual clock updates and Chrome compiled-seed reuse. Both real time edits survive autosave/reload; the actual previous-runtime upgrade passes 30 checks, followed by 21 checks on the exact final bundle, including physically disconnected restore. All 28 native suites, 58 JavaScript tests and the WASM smoke pass. The minimalist UI, R2 manifest and seed bytes are preserved. [Integration evidence](../docs/traces/rtc-loader-release/README.md), [clock recording](../tweets/082_manual_time_autosave.md) and [offline reuse](../tweets/084_offline_compiled_seed_reuse.md) cover the results. This is a local integration, not a public deployment or a full-speed claim.

**Remote firmware delivery (2026-09-06):** all six prepared firmware objects now load from `https://iphone.files.garden/`, with their public bytes matching the local package's checked-in SHA-256 values exactly. The browser permits only that external firmware origin, still rejects unrelated hosts, and verifies every object before use. A real R2-backed navigation run and a six-check offline corruption/recovery run pass. [Milestone 081](../tweets/081_r2_firmware_delivery.md) contains the recording and implementation explanation.

**Save responsiveness updates (2026-09-06):** two verified changes now reduce Save interruptions. Compression runs in a dedicated worker, reducing measured page main-thread CPU from about 6.8 s to 0.3–0.4 s ([milestone 068](../tweets/068_save_compression_worker.md)). Faster RAM page classification reduces the initial Chrome snapshot capture from 225–273 ms to 136–143 ms ([milestone 071](../tweets/071_faster_snapshot_capture.md)). Full backup bytes and all eight Home screens match exactly; production clock, persistence and actual old-runtime offline upgrade checks pass. These are separate comparisons on this VM, not a claim of faster total Save or general app navigation. Full-speed app functionality remains an active goal.

**Implementation resumed 2026-09-05:** follow [the execution log](execution.md) for current verified results and active work toward [the browser product roadmap](web-iphone.md). The historical status below describes the initial OS 1.0 bringup.

The current [16-app audit](app-coverage.md) records actual app behavior and remaining workflow/hardware blockers. Weather's unit selection and city removal now survive three exact Save/reload comparisons ([milestone 074](../tweets/074_weather_controls_persist.md)). Its standalone demo service passes ten local tests, but guest networking and forecast delivery remain open. New milestones, recordings, and explanations are indexed in [tweets/README.md](../tweets/README.md).

**Latest input verification:** a quick first tap now retains the touchscreen's normal held-contact report before release. The rebuilt production phone passes 59 browser lifecycle/update checks and all twelve Calculator workflows, alongside 26 native suites and 34 JavaScript tests. [Milestone 063](../tweets/063_short_tap_contact_report.md) contains the actual recording, preserved test failures and narrow image-comparison limits. This closes a reproduced missed-tap bug; app-launch speed remains below target.

**Current hardware work:** isolated tests now receive the entire original Wi-Fi helper and main-program container, with all 504 original driver acknowledgements and independently verified wire bytes, memory and write coverage. [Milestone 069](../tweets/069_sdio_complete_main_received.md) records the final handoff request. A verified private checkpoint now reproduces the full upload and subsequent 41.9 million instruction driver failure exactly ([milestone 073](../tweets/073_reusable_wifi_checkpoint.md)), allowing the next readiness transition to be investigated without another full boot. The exact helper selects a bounded semantic model; its ARM CPU is not emulated and no network interface is ready yet. Audio now connects the original driver's real 16 KiB input transfer to an experimental decoder and produces its first independently verified 4,096-byte PCM buffer ([milestone 075](../tweets/075_first_guest_audio_buffer.md)). The original guest now consumes every byte of the first buffer and returns ownership through its real driver ([milestone083](../tweets/083_original_audio_buffer_consumed.md)). A later isolated run verifies 53 consecutive PCM buffers read by the guest and 51 ownership returns. The original driver now also saves and suspends a real decoder generation; an observer code-binding check stops before downstream HAL entry is verified ([evidence](../docs/traces/amc-first-consumption/power-suspension/README.md)). Continuous sample timing, downstream transport and sound remain open. See [networking](networking.md), [audio](audio.md) and their milestone evidence. These experimental device models are not part of the production phone yet.

**Browser speed baseline:** the guarded WebAssembly CPU engine and foreground clock correction are enabled. The earlier quiet graphics comparison used the same engine in four alternating software/MBX sessions, with 20 warm observations per action and configuration. Accelerated Home returns improve from **1.27 to 0.92 seconds at p95**; Settings improves from **3.88 to 3.57 seconds**, still above the two-second target. The measured MBX package is now the default for new phones after the continuous sixteen-app rendering gate and 65 browser package checks passed. See [the default promotion and fresh-load recording](../tweets/058_accelerated_default_phone.md). Existing saved phones retain their state and data. These are measurements on this cloud VM, not the user's Mac, and do not establish full speed or animation/input acceptance. See [milestone 055 and its original-speed recording](../tweets/055_mbx_navigation_speed.md), [the graphics investigation](graphics-speed.md), [the CPU investigation](speed-experiments.md), and [the execution log](execution.md).

- **Goal achieved:** the genuine iPhone OS 1.0 home screen renders. SpringBoard displays the original 2007 home screen, the "Repair Needed" alert, and app launch/return animations.
- **Interactive in browser:** a hard-refresh of the WASM site loads multipart firmware, restores a home-screen snapshot, and lets mouse/keyboard interact (tap → Settings, Escape → Home, tap → Safari).
- **Historical interpreter speed:** native ~42 M instructions/s after `-O3`/tick batching. The browser now has a guarded WebAssembly CPU seed; current measured navigation and remaining speed gates are described above.
- **Workarounds:** USB OTG is not matched (PHY registration-order crash), and the root filesystem boots as a single ramdisk (`md0`) instead of NAND partitions.

## What was implemented

### M68 machine target (`core/src/board/m68.c`, `core/src/soc/machine.c`)
- Defined M68 vs N82 differences: NOR base `0x24000000`, Merlot LCD on SPI1 CS0, Z1 touch on SPI2 CS0, WM8758 audio on I2C0 `0x1a`, PCF50635 PMU.
- Wired M68 buttons: `hold`, `menu` (Home), `volup`, `voldown`, `ringerab`.
- Current browser work also attaches the SPI0 LM2512 display-link transport; see [the wake investigation](display-wake.md) for its measured scope and legacy snapshot routing.
- Programmed a valid CHRP NVRAM image for `IODTNVRAM::initNVRAMImage`.

### Firmware extraction and builder tools (`tools/firmware/`)
- `extract_1a543a.py`: IPSW → `kernel_1A543a.macho`, `devicetree_m68ap.bin`, HFSX rootfs image.
- `unwrap8900.py`: parses 8900/IMG2/`complzss` containers from the pre-IMG3 era.
- `rootfs_work.py`: copies the extracted HFSX image and builds a *work image* with a single `/dev/md0` fstab, SpringBoard env vars (`LK_ENABLE_MBX2D/OGL=0`), activation seed, and no-autolock prefs.
- `hfsx_extract.py`, `ppm2png.py`, `ksym.py`, `tsym.py`, `dis.py`, `machomap.py`, etc.

### OS 1.0 XNU boot path (`core/src/boot/bringup_os1.c`)
- Skips SecureROM/LLB/iBoot; constructs the kernel entry state directly from Mach-O, device tree, boot-args, and ramdisk.
- Handles OS 1.0-specific `__HIB` at `0xc0000000`, `__PRELINK` prelinked kext segment, and M68 memory map.
- Patches the device tree for CPU/root frequencies, `/memory`, `/vram`, debug-enabled, and RAMDisk.

### ARM/SoC fixes for OS 1.0
- Timer4 modeled as a true one-shot down-counter with proper re-arm (`COUNTBUF` + `STATE=START|UPDATE`); fixes `hertz_tick` and `IODelay` timing.
- `MSR CPSR` in exception handlers rejects ARM/Thumb bit writes matching ARM1176.
- MBX 2D packet parser handles variable `0x70000000` terminators and constant-u textured sprites.
- NOR/NVRAM layout for M68, Merlot LCD panel ID, PCF50635 registers.

### Z1 multitouch (`core/src/soc/mtz1.c`)
- Reverse-engineered from the genuine `AppleMultitouchSPI` kext: bootloader upload, firmware mode, attention/result reads, and touch-frame reports.
- Emits `0xce` input frames with contact coordinates, density, and device-uptime timestamps.
- Supports browser tap injection and harness `--at STEP tap X Y` / `touch` / `press` commands.

### Browser/WebAssembly build (`web/`, `core/CMakeLists.txt`)
- Emscripten build `web/dist/emu.wasm` using the same portable core.
- The browser loads `web/manifest.json`, reassembles multipart <100 MB firmware chunks, gunzips, and loads snapshot or rootfs. Current worker/frontend additions are tracked in the execution log.
- Canvas rendering, status bar, `?cpi=` knob, mouse-as-touch, Escape=Home, Shift+click=hold.
- GitHub Actions CI builds and deploys to Pages (currently expects Pages source = "GitHub Actions").

### Speed passes
- Native `-O3` + IPO.
- Privileged tick batching in the interpreter, stopping at timebase edges, WFI, MMIO, host input, and host callbacks.
- `--cpi` knob for guest clock ratio; measured and documented that CPI affects guest time, not host throughput.

### Diagnostics in the harness
- `--serial` with instruction-count prefixes, `--trace-devices`, `--pc-log`, `--trace-from`/`--trace-last`, `--exec-pc`, `--swi-log`, `--dump-va`, `--break-pc`, `--break-swi`, `--shot-every` screenshots.
- Snapshot save/load (`--save-at`, `--load`) preserving guest instruction count and device state.

## Workarounds (not Apple binary patches)

1. **USB unmatch retired for new phones** — current-core and preserved baseline revalidation both boot the original USB stack without the historical early crash. The default phone now keeps lockdownd alive, removing the Home connection-retry delay. Old saved phones retain their old boot state; Export then Reset opts into the new phone. Host USB transfer remains unmodeled. See [the measured service recovery](../tweets/036_home_service_recovery.md).
2. **Single ramdisk root** — `/etc/fstab` points to `/dev/md0` instead of NAND `disk0s1`/`disk0s2`. Real fix is a NAND/disk0 model.
3. **Activation state** — the work image includes a seeded `data_ark.plist` and a locally generated, locally signed activation record so `lockdownd` reports `Activated`. No Apple binary is modified.
4. **No-autolock prefs** — seeds `SBAutoLockTime`/`SBAutoDimTime` to `-1` to prevent the screen turning off during slow experiments. This is a user preference, not a binary patch.

## Current blockers / next steps

1. **Speed.** Settings still exceeds the launch target. [Actual first-use Chrome traces](../docs/traces/browser-first-use/README.md) identify substantial compilation work; warm guest execution also remains slow. Runtime continuation across admitted regions passes correctness and improves a fixed CPU replay by about 4.2%, but leaves normal browser navigation p95 essentially unchanged, so it is parked. [Smaller generated functions](../docs/traces/jit-lazy-groups/README.md) also pass correctness and all 24 screen checks but make first-use and warm Settings slower. Neither experiment is in production. An isolated streaming loader proves Chrome can reuse compiled seed code across fresh browser processes ([milestone 076](../tweets/076_compiled_seed_reuse.md)). The controlled return-visit comparison now improves first Settings from 5.112/4.909 to 3.566/3.417 seconds and warm Settings p95 from 3.667 to 3.417 seconds, while Home p95 worsens 49.8 ms ([milestone 080](../tweets/080_faster_returning_settings.md)). All 88 strict screens pass. Real service-worker reuse and exact autosave restoration now pass 71 functional checks with the origin server physically stopped ([milestone 084](../tweets/084_offline_compiled_seed_reuse.md)); the combined actual-runtime upgrade/fallback gate and final unmarked-bundle verification also pass. The loader is now integrated with the RTC fix in the default local build ([integration](../docs/traces/rtc-loader-release/README.md)). Faster RAM page classification is integrated for Save capture; decoder partitioning, cache and seed-layout experiments without repeatable navigation gains remain parked.
2. **USB controller behavior.** The obsolete unmatch workaround is removed from new boots. Model transfers and active-controller reset behavior when host connectivity is added.
3. **NAND partitions.** Model disk0 so the real `/etc/fstab` works and `/private/var` exists.
4. **Networking.** Emulate Marvell 88W8686 SDIO Wi-Fi (or a simpler device) bridged to the host.
5. **SDL desktop window.** Native mouse/keyboard UI beyond the current browser harness.

## Work still in flight / per-task plans

The browser product roadmap is [A usable original iPhone in the browser](web-iphone.md). It defines the clickable device frame, acceptance targets for all 16 apps, browser speed gates, persistence, audio/camera, networking, and compatible services. It takes priority for product sequencing over the older implementation plans below; its features are planned, not completed.

Each parallel work stream has its own plan document under `plans/`:

- [Speed](speed.md) — decoded-block interpreter speed engine
- [USB](usb.md) — retired unmatch workaround; detached service works, host transfers remain
- [Wi-Fi](wifi.md) — Marvell 88W8686 SDIO Wi-Fi + host bridging
- [Legacy app services](legacy-services.md) — original Weather protocol discovery and remaining guest-network acceptance
- [Audio](audio.md) — original WM8758 control protocol verified; PCM transport and audible output remain
- [SDL](sdl.md) — SDL2 desktop frontend, outside the browser product's critical path

## How to run

See `README.md` for full build/run instructions and firmware extraction commands.


## Retained experiment artifacts

Eleven completed browser-test exports, five completed RTC test snapshots and nine historical CPU replay outputs are stored as verified gzip archives with adjacent `.archived.json` receipts to keep browser storage tests within the VM disk budget. [Exact locations and restore helper](../docs/traces/storage-reclamation/README.md). Current firmware, active replay fixtures and saved browser profiles are preserved.
