# Project update tweets

Each `.md` file is a single tweet-able milestone:
- **Tweet idea** — ready-to-post copy (or a thread hook).
- **What you're seeing** — the matching screen recording file and timestamp.
- **ELI5 technical explanation** — what actually happened and why it took work.
- **Why it matters** — why this moves the project forward.

The recordings and thumbnails live in `videos/` and `thumbnails/`.

| # | Update | Recording | Thumbnail |
|---|--------|-----------|-----------|
| 001 | [Genuine 2007 home screen](001_home_screen.md) | `browser_home_settings_safari_cpi1.mp4` | [thumb](thumbnails/browser_home_settings_safari_cpi1.jpg) |
| 002 | [Touch input opens real apps](002_touch_input.md) | `browser_home_settings_safari_cpi1.mp4` | [thumb](thumbnails/browser_home_settings_safari_cpi1.jpg) |
| 003 | [Home button returns from Safari](003_home_button.md) | `browser_home_settings_safari_cpi1.mp4` | [thumb](thumbnails/browser_home_settings_safari_cpi1.jpg) |
| 004 | [It runs in the browser](004_browser_port.md) | `browser_home_settings_safari_cpi1.mp4` | [thumb](thumbnails/browser_home_settings_safari_cpi1.jpg) |
| 005 | [CPI knob vs real speed](005_cpi_lesson.md) | `browser_cpi2_timing.mp4` | [thumb](thumbnails/browser_cpi2_timing.jpg) |
| 006 | [Interpreter speed pass](006_speed_pass.md) | *(to be recorded)* | *(placeholder)* |
| 007 | [Parallel subagents take on the hard parts](007_parallel_subagents.md) | *(in progress)* | *(placeholder)* |
| 008 | [First pixels / lock screen](008_first_pixels.md) | `browser_home_settings_safari_cpi1.mp4` | [thumb](thumbnails/browser_home_settings_safari_cpi1.jpg) |
| 009 | [M68 / iPhone1,1 machine target](009_m68_board.md) | *(architecture docs only)* | *(placeholder)* |
| 010 | [Reverse-engineering Z1 touch](010_z1_touch.md) | `browser_home_settings_safari_cpi1.mp4` | [thumb](thumbnails/browser_home_settings_safari_cpi1.jpg) |
| 011 | [Why USB is currently unmatched](011_usb_workaround.md) | *(kernel race docs only)* | *(placeholder)* |
| 012 | [Browser end-to-end test](012_browser_e2e.md) | `browser_home_settings_safari_cpi1.mp4` | [thumb](thumbnails/browser_home_settings_safari_cpi1.jpg) |
| 013 | [Timer4 one-shot down-counter fix](013_timer4_fix.md) | *(boot trace recording TBD)* | *(placeholder)* |
| 014 | [MBX 2D packet / app-close animation fix](014_mbx_2d.md) | `browser_home_settings_safari_cpi1.mp4` | [thumb](thumbnails/browser_home_settings_safari_cpi1.jpg) |
| 015 | [Activation without patching Apple binaries](015_activation.md) | `browser_home_settings_safari_cpi1.mp4` | [thumb](thumbnails/browser_home_settings_safari_cpi1.jpg) |
| 016 | [M68 NOR/NVRAM CHRP partition](016_nor_nvram.md) | *(cold boot recording TBD)* | *(placeholder)* |
| 017 | [Privileged tick batching speed gain](017_interpreter_tick_batching.md) | *(terminal benchmark TBD)* | *(placeholder)* |
| 018 | [Browser CI + GitHub Pages deployment](018_browser_ci.md) | `browser_home_settings_safari_cpi1.mp4` | [thumb](thumbnails/browser_home_settings_safari_cpi1.jpg) |
| 019 | [Project status after the initial PR merge](019_project_status.md) | [existing browser demo](videos/browser_home_settings_safari_cpi1.mp4) | [thumb](thumbnails/browser_home_settings_safari_cpi1.jpg) |
| 020 | [Plan for a usable browser iPhone](020_web_iphone_plan.md) | [baseline only; planning milestone](videos/browser_home_settings_safari_cpi1.mp4) | [thumb](thumbnails/browser_home_settings_safari_cpi1.jpg) |
| 021 | [Reproduced browser baseline](021_reproduced_browser_baseline.md) | [recording](videos/browser_baseline_20260905.mp4) | [thumb](thumbnails/browser_baseline_20260905.jpg) |
| 022 | [Clickable browser iPhone and worker](022_clickable_browser_iphone.md) | [recording](videos/clickable_browser_iphone.mp4) | [thumb](thumbnails/clickable_browser_iphone.jpg) |
| 023 | [A real note survives browser reload](023_persistent_hello_note.md) | [recording](videos/persistent_hello_note.mp4) | [thumb](thumbnails/persistent_hello_note.jpg) |
| 024 | [Original touch coordinates and scrolling](024_touch_calibration.md) | [recording](videos/touch_calibration.mp4) | [thumb](thumbnails/touch_calibration.jpg) |
| 025 | [Auditing all 16 original apps](025_real_app_inventory.md) | [recording](videos/real_app_inventory.mp4) | [thumb](thumbnails/real_app_inventory.jpg) |
| 026 | [Offline reload and corrupt-cache recovery](026_offline_reload_recovery.md) | [recording](videos/offline_reload_recovery.mp4) | [thumb](thumbnails/offline_reload_recovery.jpg) |
| 027 | [Calendar and Contacts persistence](027_calendar_contacts_persistence.md) | [recording](videos/calendar_contacts_persistence.mp4) | [thumb](thumbnails/calendar_contacts_persistence.jpg) |
| 028 | [Saved-checkpoint recovery](028_saved_checkpoint_recovery.md) | [recording](videos/saved_checkpoint_recovery.mp4) | [thumb](thumbnails/saved_checkpoint_recovery.jpg) |
| 029 | [Shorter save pause with identical backups](029_shorter_save_pause.md) | [recording](videos/shorter_save_pause.mp4) | [thumb](thumbnails/shorter_save_pause.jpg) |
| 030 | [Sleep, wake and the original unlock slider](030_sleep_wake_unlock.md) | [recording](videos/sleep_wake_unlock.mp4) | [thumb](thumbnails/sleep_wake_unlock.jpg) |
| 031 | [Original WM8758 codec control protocol](031_wm8758_control_protocol.md) | Nonvisual driver/transport trace | — |
| 032 | [Contacts editing after memory recovery](032_contacts_memory_recovery.md) | [recording](videos/contacts_memory_recovery.mp4) | [thumb](thumbnails/contacts_memory_recovery.jpg) |
| 033 | [The first genuine networking gate](033_networking_first_gate.md) | Nonvisual cold-boot trace; [reproduce](../plans/networking.md) | Not applicable |
| 034 | [Tracing the silent ringtone hardware gates](034_audio_playback_hardware_gates.md) | Nonvisual register/DMA trace | — |
| 035 | [Current speed baseline and priority](035_current_speed_baseline.md) | [recording](videos/current_speed_baseline.mp4) | [thumb](thumbnails/current_speed_baseline.jpg) |
| 036 | [Home returns without the service timeout](036_home_service_recovery.md) | [recording](videos/home_service_recovery.mp4) | [thumb](thumbnails/home_service_recovery.jpg) |
| 037 | [Shorter touch hold improves real typing](037_shorter_touch_hold.md) | [recording](videos/shorter_touch_hold.mp4) | [thumb](thumbnails/shorter_touch_hold.jpg) |
| 038 | [Original animations follow real time](038_interactive_guest_clock.md) | [recording](videos/interactive_guest_clock.mp4) | [thumb](thumbnails/interactive_guest_clock.jpg) |
| 039 | [Offline updates preserve the saved phone](039_offline_bundle_upgrade.md) | [recording](videos/offline_bundle_upgrade.mp4) | [thumb](thumbnails/offline_bundle_upgrade.jpg) |
| 040 | [Genuine MBX composition probe](040_mbx_composition_probe.md) | Nonvisual command/geometry trace | — |
| 041 | [Native touch release reaches the real driver](041_native_touch_release.md) | Nonvisual driver/SPI trace; [reproduce](../plans/native-input.md) | — |
| 042 | [Clipped copies from the original graphics driver](042_mbx_clipped_composition.md) | [recording](videos/mbx_clipped_composition.mp4) | [thumb](thumbnails/mbx_clipped_composition.jpg) |
| 043 | [Pause remains available while saving](043_pause_during_save.md) | [recording](videos/pause_during_save.mp4) | [thumb](thumbnails/pause_during_save.jpg) |
| 044 | [An old phone keeps its data through a fresh boot](044_legacy_phone_data_preserved.md) | [recording](videos/legacy_phone_data_preserved.mp4) | [thumb](thumbnails/legacy_phone_data_preserved.jpg) |
| 045 | [Faster repeated apps with guarded WebAssembly](045_guarded_wasm_speed.md) | [recording](videos/guarded_wasm_speed.mp4) | [thumb](thumbnails/guarded_wasm_speed.jpg) |
| 046 | [Calendar hours render and scroll through MBX](046_calendar_mbx_rendering.md) | [recording](videos/calendar_mbx_rendering.mp4) | [thumb](thumbnails/calendar_mbx_rendering.jpg) |
| 047 | [A real minute on the original Stopwatch](047_foreground_stopwatch.md) | [idle recording](videos/foreground_stopwatch.mp4), [Save-load recording](videos/stopwatch_during_save.mp4) | [idle thumb](thumbnails/foreground_stopwatch.jpg), [Save-load thumb](thumbnails/stopwatch_during_save.jpg) |
| 048 | [Twelve real Calculator workflows](048_calculator_workflows.md) | [recording](videos/calculator_workflows.mp4) | [thumb](thumbnails/calculator_workflows.jpg) |
| 049 | [Notes edits and deletions survive reload](049_notes_edit_delete_persistence.md) | [recording](videos/notes_edit_delete_persistence.mp4) | [thumb](thumbnails/notes_edit_delete_persistence.jpg) |
| 050 | [Safari address keyboard through MBX](050_safari_mbx_address_keyboard.md) | [recording](videos/safari_mbx_address_keyboard.mp4) | [thumb](thumbnails/safari_mbx_address_keyboard.jpg) |
| 051 | [Saving keeps the foreground clock moving](051_foreground_clock_carry.md) | [production recording](videos/foreground_clock_carry_production.mp4), [candidate recording](videos/foreground_clock_carry.mp4) | [production thumb](thumbnails/foreground_clock_carry_production.jpg), [candidate thumb](thumbnails/foreground_clock_carry.jpg) |
| 052 | [Text opens its original composer through MBX](052_text_mbx_composer.md) | [recording](videos/text_mbx_composer.mp4) | [thumb](thumbnails/text_mbx_composer.jpg) |
| 053 | [Photos returns Home without a graphics restart](053_photos_mbx_transparency.md) | [recording](videos/photos_mbx_transparency.mp4) | [thumb](thumbnails/photos_mbx_transparency.jpg) |
| 054 | [Clock renders its reversed texture column](054_clock_mbx_mirrored_column.md) | [recording](videos/clock_mbx_mirrored_column.mp4) | [thumb](thumbnails/clock_mbx_mirrored_column.jpg) |
| 055 | [Measured faster Home returns through MBX](055_mbx_navigation_speed.md) | [recording](videos/mbx_navigation_speed.mp4) | [thumb](thumbnails/mbx_navigation_speed.jpg) |
| 056 | [Sixteen apps in one uninterrupted phone](056_continuous_sixteen_apps.md) | [recording](videos/continuous_sixteen_apps.mp4) | [thumb](thumbnails/continuous_sixteen_apps.jpg) |
| 057 | [The original Wi-Fi driver reaches card registers](057_sdio_first_card_registers.md) | [Nonvisual cold-boot trace](../docs/traces/sdio-first/candidate-mmio.log) | — |
| 058 | [New phones use accelerated graphics](058_accelerated_default_phone.md) | [controls](videos/accelerated_default_phone.mp4), [Notes](videos/notes_accelerated_default.mp4) | [controls thumb](thumbnails/accelerated_default_phone.jpg), [Notes thumb](thumbnails/notes_accelerated_default.jpg) |
| 059 | [The Marvell driver requests its SDIO function](059_sdio_driver_function_request.md) | [Nonvisual driver trace](../docs/traces/sdio-identity/local-mac-mmio.log) | — |
| 060 | [The Wi-Fi driver requests its first helper transfer](060_sdio_first_helper_request.md) | [Nonvisual driver/transfer trace](../docs/traces/sdio-enable/candidate-mmio.log) | — |
| 061 | [The real audio-engine reset request and isolated SRAM checks](061_amc_reset_request_identified.md) | [Nonvisual original-driver trace](../docs/traces/amc-first/baseline.log), [storage evidence](../docs/traces/amc-first/stage-a/verification.json) | — |
| 062 | [The first Wi-Fi helper frame completes](062_sdio_first_helper_completion.md) | [Nonvisual transfer/IRQ trace](../docs/traces/sdio-data/candidate-mmio.log) | — |
| 063 | [Short taps retain a real held-contact report](063_short_tap_contact_report.md) | [short tap](videos/short_tap_contact_report.mp4), [Calculator](videos/calculator_accelerated_default.mp4) | [tap thumb](thumbnails/short_tap_contact_report.jpg), [Calculator thumb](thumbnails/calculator_accelerated_default.jpg) |
| 064 | [The complete Wi-Fi helper is received](064_sdio_helper_received.md) | [Nonvisual full-transfer and next-failure trace](../docs/traces/sdio-helper/receive-mmio.log) | — |
| 065 | [The original audio program reaches SRAM](065_original_audio_program_uploaded.md) | [Nonvisual guest upload and original-file oracle](../docs/traces/amc-upload/README.md) | — |
| 066 | [The Wi-Fi driver sends its first main payload](066_sdio_first_main_payload.md) | [Nonvisual original-driver header/body trace](../docs/traces/sdio-main/positive-mmio.log) | — |
| 067 | [The original audio driver reaches its engine command](067_original_audio_engine_command.md) | [Nonvisual acknowledgement/configuration/command trace](../docs/traces/amc-run-request/README.md) | — |
| 068 | [Save compression leaves the page thread](068_save_compression_worker.md) | [recording](videos/save_compression_worker.mp4) | [thumb](thumbnails/save_compression_worker.jpg) |
| 069 | [The complete Wi-Fi main program reaches card memory](069_sdio_complete_main_received.md) | [Nonvisual complete-transfer and handoff trace](../docs/traces/sdio-full-main/full-mmio.log) | — |
| 070 | [The original audio driver supplies real ringtone bytes](070_original_audio_input_captured.md) | [Nonvisual input request and original-file proof](../docs/traces/amc-input/README.md) | — |
| 071 | [Faster initial snapshot capture — integrated](071_faster_snapshot_capture.md) | [recording](videos/snapshot_page_scan.mp4) | [thumb](thumbnails/snapshot_page_scan.jpg) |
| 072 | [Raw audio frames survive a partial input buffer](072_raw_audio_frame_boundaries.md) | [Nonvisual raw/partial decoder proof](../docs/traces/aac-framing/README.md) | — |
| 073 | [A verified restart point for the original Wi-Fi driver](073_reusable_wifi_checkpoint.md) | [Nonvisual upload, reload and continuation evidence](../docs/traces/sdio-checkpoint/v2/actual/README.md) | — |
| 074 | [Weather units and city removal persist](074_weather_controls_persist.md) | [recording](videos/weather_controls_persist.mp4) | [thumbnail](thumbnails/weather_controls_persist.jpg) |
| 075 | [The original driver reaches its first decoded audio buffer](075_first_guest_audio_buffer.md) | [Nonvisual guest PCM and independent audits](../docs/traces/amc-first-pcm/README.md) | — |
| 077 | [The original Wi-Fi driver accepts an initialized device model](077_original_wifi_verification_branch.md) | [Nonvisual original branch, full trace and scoped negative](../docs/traces/sdio-initialized-service/README.md) | — |
| 078 | [The browser is just the phone](078_minimal_phone_ui.md) | [recording](videos/minimal_phone_ui.mp4) | [thumbnail](thumbnails/minimal_phone_ui.jpg) |
| 079 | [Original RTC interrupt reaches the real setter](079_original_rtc_interrupt.md) | [existing UI context](videos/manual_time_diagnostic.mp4) | [thumbnail](thumbnails/manual_time_diagnostic.jpg) |
| 080 | [Faster returning Settings, still experimental](080_faster_returning_settings.md) | [recording](videos/seed_cache_return_settings.mp4) | [thumbnail](thumbnails/seed_cache_return_settings.jpg) |
| 081 | [The browser phone loads from R2](081_r2_firmware_delivery.md) | [recording](videos/r2_firmware_delivery.mp4) | [thumbnail](thumbnails/r2_firmware_delivery.jpg) |
| 082 | [Original time picker survives real autosave](082_manual_time_autosave.md) | [picker](videos/rtc_manual_time_autosave.mp4) · [final release](videos/rtc_final_release.mp4) | [picker thumb](thumbnails/rtc_manual_time_autosave.jpg) · [release thumb](thumbnails/rtc_final_release.jpg) |
| 084 | [Compiled seed reuse survives a real offline return](084_offline_compiled_seed_reuse.md) | [return](videos/seed_cache_offline_return.mp4) · [training/autosave](videos/seed_cache_offline_training.mp4) · [final release](videos/rtc_final_release.mp4) | [return thumb](thumbnails/seed_cache_offline_return.jpg) · [training thumb](thumbnails/seed_cache_offline_training.jpg) |
| 085 | [Visible recovery inside the phone](085_visible_recovery.md) | [recording](videos/visible_recovery.mp4) | [thumb](thumbnails/visible_recovery.jpg) |
| 086 | [Our own program executes in original OS1](086_own_guest_program.md) | [Actual guest trace; nonvisual](../docs/traces/guest-hello/README.md) | Not applicable |
| 087 | [Real guest loopback HTTP exchange](087_guest_loopback_http.md) | [Actual native/UART trace; nonvisual](../docs/traces/guest-loopback/README.md) | Not applicable |
| 088 | [Mixed compiler coverage experiment](088_mixed_seed_coverage_diagnostic.md) | [recording](videos/mixed_seed_coverage_diagnostic.mp4) | [thumb](thumbnails/mixed_seed_coverage_diagnostic.jpg) |
| 089 | [Calculator input observation](089_calculator_input_feedback.md) | [recording](videos/calculator_input_feedback.mp4) | [thumb](thumbnails/calculator_input_feedback.jpg) |
| 090 | [Actual Calculator frame delivery](090_calculator_frame_delivery.md) | [recording](videos/calculator_frame_delivery.mp4) | [thumb](thumbnails/calculator_frame_delivery.jpg) |
| 091 | [Original Safari local demo, form and persistent bookmark](091_safari_local_demo.md) | [Full recording](videos/091_safari_local_demo.mp4) | [thumb](thumbnails/091_safari_local_demo.jpg) |
| 093 | [Default local demo qualification failure](093_default_local_demo_qualification_failure.md) | [Full recording](videos/default_local_demo_qualification_failure.mp4) | [Thumbnail](thumbnails/default_local_demo_qualification_failure.jpg) |
| 094 | [Original Photos first workflow failure](094_photos_first_workflow_failure.md) | [Full recording](videos/photos_first_workflow_failure.mp4) | [Thumbnail](thumbnails/photos_first_workflow_failure.jpg) |

Audio follow-up (2026-09-06): [isolated functional-engine contracts](../docs/traces/amc-functional-contracts/README.md) now exercise actual decoded buffers, physical DMA byte addresses, partial input, ownership, interrupts and reviewed rejection paths. The current adapter matches the full original raw corpus. This is native model verification only; no new guest playback or visual milestone is claimed.

Not every milestone has a unique recording yet — several share the same browser end-to-end clip because the work was captured during that run. New recordings will be added for speed passes, SDL desktop, and the in-progress subagent work as they finish.

Milestone [063](063_short_tap_contact_report.md) also retains the unpromoted Thumb POP browser follow-up: [original-speed diagnostic recording](videos/thumb_pop_browser_diagnostic.mp4) · [thumbnail](thumbnails/thumb_pop_browser_diagnostic.jpg). Equal warm p95 and two retained scrollbar-image failures; no speed improvement claimed.


Progress note (2026-09-05): the isolated register-transfer experiment passes all focused and full-machine checks, but its small timing advantage reverses with run order. It is parked, with production unchanged. [Source, negative controls and complete results](../docs/traces/jit-register-layout/README.md) explain the result; this is an engineering investigation, not a new visual speed milestone. The existing [navigation recording](videos/mbx_navigation_speed.mp4) remains the visual speed evidence in milestone055. Actual browser CPU attribution is next.

CPU investigation update: [actual production worker/main profiles](../docs/traces/browser-cpu-attribution/README.md) now separate generated code, C execution and Save-related native CPU. These are diagnostic samples with explicit symbol/module limits, not a speed milestone. The subsequent [Save isolation and worker comparison](068_save_compression_worker.md) demonstrate reduced page-thread CPU/stalls while preserving exact backups; no CPU instruction candidate was promoted.


CPU investigation follow-up: an [isolated interpreter decoder partition](../docs/traces/interpreter-dispatch/README.md) now targets fallback execution identified in the actual profile. Existing handlers remain intact, with 2,097,185 selection checks and 1,067 native ARM checks passing; the wrong media/transfer precedence is rejected. Full600M guest replay and a clean short timing comparison are now complete: exact state passes, but the small forward gain reverses with run order. The candidate is parked; this remains a diagnostic progress update, not a promoted speed milestone.


Progress note (2026-09-06): [original Weather protocol discovery](../docs/traces/weather-service-contract/README.md) now documents the real app's forecast and city-search XML for a future demo service. This is static preparation, with the initial annotation error retained; no successful guest forecast or new visual milestone is claimed.


CPU attribution follow-up: [actual browser compilation traces](../docs/traces/browser-first-use/README.md) now distinguish host first-use compilation from original guest application warmup. Production remains unchanged; one warm scrollbar comparison and the initial report-writing failure are retained. This is an engineering diagnostic, not a new visual speed milestone or replacement for the verified navigation recording.

Weather follow-up (2026-09-06): the [standalone demo backend](../tools/legacy_services/README.md) passes ten tests, including real loopback HTTP forecast/search delivery, and is covered by CI. This prepares the service; the original app is still disconnected and has no new forecast recording. [Evidence and retained test-client failure](../docs/traces/weather-service-contract/demo-backend/report.json).


Checkpoint investigation history (2026-09-06): the first private pair exposed a real Z1 receive/uptime omission and remains unpublished. The independently captured complete-device v2 now passes initial canonical equality,41,947,204 instructions of original-versus-fresh continuation and actual package reload. [Milestone073](073_reusable_wifi_checkpoint.md) records the verified private restart point; [the failed first attempt remains preserved](../docs/traces/sdio-checkpoint/README.md). Neither result supplies networking readiness.

CPU progress: [guarded region continuation](../docs/traces/jit-continuation/README.md) passes 404 focused cases and complete raw guest checkpoints, but its 4.2% fixed-workload CPU gain does not improve actual-browser p95 or Home materially. Four strict scrollbar comparisons remain failed, including three baseline frames. The candidate is parked with production unchanged; this is a diagnostic result, not a successful speed milestone.

Speed experiment result (2026-09-06): [smaller generated functions](../docs/traces/jit-lazy-groups/README.md) pass exact full-phone replay, targeted guards and all 24 browser screen checks, but regress first-use and warm Settings in both paired directions. The candidate is parked. [Complete original-speed candidate recording](videos/smaller_function_diagnostic.mp4) · [thumbnail](thumbnails/smaller_function_diagnostic.jpg). This records an unsuccessful speed experiment; production remains unchanged.

Clock investigation (2026-09-06): [the original OS 1.0 RTC write path](../docs/traces/rtc-write-contract/README.md) is implemented and asynchronously writes seven PMU registers. This corrects an inherited panic-stub assumption in the backlog; actual device writes, date-setting UI and host synchronization still need acceptance. Static evidence only, with no changed clock or new visual milestone.

The follow-up [genuine date-picker diagnostic](../docs/traces/rtc-write-contract/README.md#actual-ui-diagnostic--acceptance-remains-incomplete) remains incomplete: the minute selection changes and the reload image sample matches, but final execution verification times out with the restored phone paused. Neither export shows an unsupported RTC write. [Uncut diagnostic recording](videos/manual_time_diagnostic.mp4) · [thumbnail](thumbnails/manual_time_diagnostic.jpg). No committed date change or successful persistence milestone is claimed.

The parked continuation benchmark has an [uncut original-speed diagnostic recording](videos/region_continuation_diagnostic.mp4) and [actual thumbnail](thumbnails/region_continuation_diagnostic.jpg). The full 106.36-second candidate session includes all eleven Settings/Home rounds; it documents the unsuccessful browser speed gate, not a promoted improvement. [Results and four retained image failures](../docs/traces/jit-continuation/README.md#browser-decision-parked).

Speed preparation: [an isolated streaming seed loader](../docs/traces/jit-stream-cache/README.md) passes49 integrity, HTTP and lifecycle checks and preserves production unchanged. Exact browser source supports retaining native-code-cache metadata; actual persistent reuse and return-visit improvement remain untested. This is source/test progress, with no new visual milestone or speed claim.

Audio continuation update: the [first consumption probe](../docs/traces/amc-consumption-probe/README.md) reproduces075 exactly and observes a real input-completion notification, then stops before a second transfer. No PCM reads or ownership release occurred; the requested new bytes were not captured. This preserved earlier protocol boundary is not a new playback or consumption milestone.

**076 — [Experimental compiled-seed reuse](076_compiled_seed_reuse.md):** Chrome accepts the exact seed's cached native code after the previous process tree closes; all8 original-reference phone screens pass. Production remains unchanged and speed acceptance is still open. [Full returning session](videos/seed_native_cache_return.mp4) · [thumbnail](thumbnails/seed_native_cache_return.jpg) · [full initial session](videos/seed_native_cache_first.mp4) · [thumbnail](thumbnails/seed_native_cache_first.jpg).

Header follow-up: both no-cache/ETag returned sessions prove accepted native reuse and all88 strict stages pass. External browser/encoder overlap invalidates quiet speed acceptance; all timings and regressions remain diagnostic. [Full uncut revalidation recording](videos/seed_cache_revalidation_diagnostic.mp4) · [thumbnail](thumbnails/seed_cache_revalidation_diagnostic.jpg) · [evidence](../docs/traces/jit-stream-cache/return-visit-v2/README.md).

Audio continuation evidence: the [second input capture](../docs/traces/amc-consumption-probe/second-input/README.md) now proves the driver requests the next contiguous 16 KiB of original audio. The observational stop and first-buffer state are unchanged; consumption and sound remain open. This is a nonvisual protocol update to075, not a playback milestone.

RTC diagnostic refinement: original Settings calls `settimeofday` when its picker changes, and the actual exported kernel calendar offset changed by20.757616seconds. No hardware RTC write was observed; the final unpaused browser check still failed. [Source and immutable-state evidence](../docs/traces/rtc-write-contract/app-commit/README.md) · [existing full diagnostic recording](videos/manual_time_diagnostic.mp4) · [thumbnail](thumbnails/manual_time_diagnostic.jpg). This is a clarification of the retained diagnostic, not a completed clock milestone.

Audio continuation: [second real input received](../docs/traces/amc-consumption-probe/second-admitted/README.md), with exact queue/PCM audits; the original driver requests more input before processing output. The bounded probe stops there. No consumption or playback milestone yet.

Audio source update: [unchanged-state ordering observations](../docs/traces/amc-consumption-probe/ordering/README.md) confirm the original session return and real refill indices. The output-start caller/interrupt ordering is the next question; no playback milestone is claimed.

RTC follow-up now observes the original setter itself: two requests60seconds apart pass18 verified instruction boundaries through the RTC queued-action entry. The asynchronous callback/hardware write remains unobserved at the fixed400M cap. [Actual trace, source and limits](../docs/traces/rtc-write-contract/setter-observer/README.md). This extends the existing manual-time diagnostic; it is not a completed clock-write milestone.

Audio investigation: [actual original caller return identified](../docs/traces/amc-consumption-probe/caller-return/README.md). Configuration and explicit output start are separate calls. Exact prior state/trace still match; a post-run harness error and artifact-only recovery are documented. No playback milestone.

RTC dependency correction: the original queued date action enables PMU IRQ7; it does not software-signal its callback. Both actual exports and original constructor/vtable bytes identify the missing SECOND event. [Checked source and state](../docs/traces/rtc-write-contract/second-interrupt/README.md). An isolated causal test is being prepared; production clock behavior remains unchanged.

**079 — [Original RTC interrupt reaches the real setter](079_original_rtc_interrupt.md):** the isolated SECOND event triggers the unchanged Apple callback and seven actual RTC bytes. Hardware values remain unsupported; no persistence or production claim. Nonvisual device evidence; [existing UI context recording](videos/manual_time_diagnostic.mp4) · [thumbnail](thumbnails/manual_time_diagnostic.jpg).


Audio investigation: [third actual input verified, fourth request preserved](../docs/traces/amc-consumption-probe/third-admitted/README.md). The driver wraps its input ring before starting output; the fixed probe stops without admitting more data. Exact original-byte and ownership audits pass, but no PCM consumption or playback milestone.

Wi-Fi follow-up to077: the next control reproduces the exact trace, but the first-command observer stops before capturing an unexpected20-byte packet or issuing runtime CMD53. The actual bytes remain unknown; a source-only MAC-command explanation and the failed run are [preserved](../docs/traces/sdio-initialized-service/request-observer-stop/README.md). No new milestone080 or networking success is claimed.


Audio source investigation: [matching MeCCA startup path identified](../docs/traces/amc-consumption-probe/third-admitted/startup-source/README.md). Original code initializes, reads output information, then explicitly starts AMC; refills are separate. No additional guest run or playback claim.


**080 — [Faster returning Settings, still experimental](080_faster_returning_settings.md):** coordinated first-Settings pairs improve5.112/4.909→3.566/3.417s (~30%); all88strict screens pass. Home pooledp95 worsens49.8ms, so no blanket nonregression or production promotion. RealSW/offline native reuse remains open. [Full uncut recording](videos/seed_cache_return_settings.mp4) · [thumbnail](thumbnails/seed_cache_return_settings.jpg).


Audio investigation: [successful kernel configuration return observed](../docs/traces/amc-consumption-probe/mecca-return/README.md), with every prior device-state byte unchanged. Userspace startup remains unobserved; no error or playback claim is inferred from absence.


RTC follow-up to079: the [complete original date now reaches the active hardware clock](../docs/traces/rtc-write-contract/complete-update-native/README.md) at the next pulse in one isolated native run. Pending snapshot roundtrips and actual-trace negatives pass. The [browser check stopped on autosave](../docs/traces/rtc-write-contract/complete-update-browser/README.md); no completed persistence or production claim. [Full failure recording](../docs/traces/rtc-write-contract/complete-update-browser/rtc-autosave-failure.mp4) · [thumbnail](../docs/traces/rtc-write-contract/complete-update-browser/rtc-autosave-failure.jpg).


Audio investigation: [real input backpressure reaches the userspace return address](../docs/traces/amc-consumption-probe/input-backpressure/README.md). The fifth transfer remains partial and uncompleted; a permission-related observer stop is preserved. No verified userspace return, PCM consumption or sound yet.


Audio investigation: [original userspace starts AMC and reads its first PCM buffer](../docs/traces/amc-consumption-probe/user-start-reads/README.md). All4,096 returned bytes are verified; an ownership-retirement observer assumption preserves an earlier stop. Final handoff/sample timing/sound are still open.


**083 — [Original audio buffer consumed](083_original_audio_buffer_consumed.md):** actual initialization/startup, driver notification, all4,096 returned PCM bytes and original ownership-return instruction verified. Eighteen actual artifact mutants reject. Nonvisual hardware milestone; no recording or audible playback claim. [Source, complete trace and audits](../docs/traces/amc-first-consumption/README.md). Continuous transport/timing and production integration remain open.

Audio083 follow-up: [actual original software-buffer copy destination and caller](../docs/traces/amc-first-consumption/copy-destination/README.md) now verified with the same consumption boundary. This is nonvisual evidence, not a new playback recording.

Audio083 follow-up: [actual software ring and transport-state census](../docs/traces/amc-first-consumption/transport-census/README.md) distinguishes completed PCM copy from I2S/DMA startup. Nonvisual, no playback claim.

Audio083 follow-up: [the next genuine service request](../docs/traces/amc-first-consumption/next-service/README.md) reaches the diagnostic stream cap12 instructions after ownership return. Nonvisual; continuous transport and sound remain open.


**Current CPU investigation — measurement, not a speed improvement:** a complete warm Settings/Home interpreter census now retains every observation and matches genuine guest screens. It identifies hot pages and following instructions missed by the older exit-only profile, motivating a bounded isolated coverage candidate. [Evidence and limitations](../docs/traces/jit-fallback-census/README.md) · [full actual recording](videos/warm_fallback_census_diagnostic.mp4) · [thumbnail](thumbnails/warm_fallback_census_diagnostic.jpg).


Audio083 follow-up: [53 actual PCM buffers read,51 ownership returns](../docs/traces/amc-first-consumption/continuous-stream/README.md). Consecutive buffer reuse and exact bytes are verified in one isolated guest. The next BSU state-saving control remains unsupported; this is nonvisual evidence, with no audible playback claim.

**088 — [Mixed seed coverage diagnostic](088_mixed_seed_coverage_diagnostic.md):** more genuine hot code compiles safely, with all raw checkpoints and 88 screens matching. Warm Settings improves modestly, but first launch is mixed and reverse Home tail latency regresses; the bigger seed stays parked. Includes the preserved disk stop and separately recorded fourth control. [Full actual recording](videos/mixed_seed_coverage_diagnostic.mp4) · [thumbnail](thumbnails/mixed_seed_coverage_diagnostic.jpg).


Audio083 follow-up: [original BSU lock and partial-input pause](../docs/traces/amc-first-consumption/bsu-lock-pause/README.md). Real decoder/queue ownership is retained; the driver saves its actual remaining input. Nonvisual hardware evidence, stopping before unsupported saved-state registers; no sound claim.


**089 — [Calculator input diagnostic](089_calculator_input_feedback.md):** all 60 real presses produce reviewed results. Sampled feedback p95 is 165–166 ms, but skipped canvas deliveries leave the 150 ms target unresolved. No production change. [Full recording](videos/calculator_input_feedback.mp4) · [thumbnail](thumbnails/calculator_input_feedback.jpg).

Audio083 source update: [corrected FIFO layout and reset-before-restore contract](../docs/traces/amc-first-consumption/bsu-lock-pause/context-source/README.md). Nonvisual source analysis only; Stage V remains the latest actual guest boundary.

**090 — [Actual Calculator frame delivery](090_calculator_frame_delivery.md):** all 60 presses pass independent raw-pixel checks. Canvas button-feedback p95 is 135.7/134.0 ms; the same-frame RAF observations arrive later. Calculator-only, with explicit observer overhead and no production speedup. [Full uncut recording](videos/calculator_frame_delivery.mp4) · [thumbnail](thumbnails/calculator_frame_delivery.jpg).

Audio083 investigation: [actual BSU save reads and preserved destination-observer failure](../docs/traces/amc-first-consumption/bsu-save-boundary/README.md). All source bytes are verified, but complete save acceptance remains open; no replay, power transition or sound claim. Nonvisual trace evidence.

Audio083 follow-up: [original saved context and both SRAM destinations verified](../docs/traces/amc-first-consumption/bsu-saved-context/README.md). Observer-only correction preserves the entire earlier guest trace;47 actual artifact negatives pass. Nonvisual evidence, with no power/reset/restore or sound claim.


**Audio083 follow-up — real suspension:** one Stage Y guest retires all four original power/clock writes and the driver's actual state3→5 store. One retained decoder generation owns the unchanged180,224 SRAM bytes and65,534-byte queue; active allocations/service/DMA/IRQ are cleared. All21 native targets and57 actual artifact negatives pass across the original audits and a separately preserved artifact-only correction. The first auditor's synthetic service1 assumption and final-USR field assumption remain documented; no guest retry. Execution stops at an unaccepted observer binding at31236650, so no HAL/sound claim. [Actual transitions, limits and failed/corrected evidence](../docs/traces/amc-first-consumption/power-suspension/README.md). Reset/restore and production integration remain open.


**092 — [Whole-core LTO navigation diagnostic](092_web_lto_navigation_diagnostic.md):** fixed-work CPU improves5.81%, but warm Settings median improves only~50ms and pooled Home p95 worsens49ms. All88 pixels pass; the original failed reporter/closure and truncated control recording remain explicit. Candidate parked, production unchanged. [Full uncut candidate recording](videos/web_lto_navigation_diagnostic.mp4) · [thumbnail](thumbnails/web_lto_navigation_diagnostic.jpg).


**Audio083 follow-up — observer stopped before an unmapped page:** the single Stage Z diagnostic preserves every Stage Y guest/resource byte and stop cycle. Actual L1`15c8ac48=08511001` and L2`085110d8=0` explain all four cloned READ/FETCH fault7 results; no actual guest fetch or HAL call occurred. All22 native targets and65 actual artifact negatives pass across the original proofs and an artifact-only correction using actual snapshot RAM geometry. The original auditor's synthetic-aperture failure is retained; no guest retry. [Evidence and limits](../docs/traces/amc-first-consumption/hal-binding-diagnostic/README.md). Next is the original kernel's normal fetch-fault path, not a fabricated mapping or audio event.


**Audio083 follow-up — original prefetch abort and mapped retry:** the single Stage AA run lets the unmodified CPU take the actual missing-code fault, then returns 1,636 core steps later to the same full user context with original MeCCA bytes mapped. It stops before the original StartIO entry instruction at `0x31236650`, not HALOutputProc. Exact fresh Z/083 prefixes, all retained SRAM/queue/ownership and suspension proofs pass; 23 native targets and 70 actual artifact negatives pass. No HAL, reset/restore or sound claim. [Evidence and bounds](../docs/traces/amc-first-consumption/original-prefetch-retry/README.md).


**CPU measurement tooling follow-up:** an isolated executed-work census now counts each generated retirement alongside real interpreter steps.404 focused scenarios preserve guest state and policy counters, with per-PC count conservation and deliberate broken-observer rejection. The actual100M/warm-phone capture is still held; no new speed or hot-loop claim. [Source and focused evidence](../docs/traces/jit-executed-work/README.md).


**094 — [Original Photos first populated workflow](094_photos_first_workflow_failure.md):** the guest importer creates verified images; original Photos finds two, excludes the invalid-name control and renders the first. Controls/Next then leave stale content and an image strip, so the workflow fails and persistence remains untested. Default unchanged. [Full first-run recording](videos/photos_first_workflow_failure.mp4) · [thumbnail](thumbnails/photos_first_workflow_failure.jpg).

**093 graphics follow-up, candidate only:** actual failed-state packets reveal the bounded 472-pixel source-row issue; isolated native and WASM checks pass 2,847/2,847, with independent source-memory pixel validation. Prior091's actual filesystem selected software graphics. Browser recovery remains unqualified after the first launcher closed input before navigation; no production or HTTP success claim. [Evidence](../docs/traces/safari-stride472/README.md).


Executed-work measurement update: the single100M count-only observer passes four complete original-state checkpoints and records every generated/interpreter completed step without overflow. The largest retained hash-shaped loop is already compiled; its count share is not a launch-time saving or a named routine claim. [Exact audit and bounded next question](../docs/traces/jit-executed-work/hotspot-note.md). No visual change or new recording is claimed.

**095 — [Safari wider source-row rendering recovery](095_safari_wide_source_recovery.md):** the isolated 472-pixel BGRA8 source bound passes 2,847 native/WASM checks. Original Safari redraws from the failed state, types `glyph`, revisits and returns Home with 144,000 exact RGB pixels; no new graphics hang/restart through export. First launcher failure retained. HTTP/Photos and production promotion remain separate. [Full original-speed video](videos/safari_stride472_recovery.mp4) · [thumbnail](thumbnails/safari_stride472_recovery.jpg).


**096 — [Graphics fix in the default local phone](096_graphics_fix_default_release.md):** actual saved-phone upgrade passes30 checks and final unmarked bundle passes21, including real autosave/offline restore. Previous audit failure retained. This fixes rendering, not app-launch speed. [Full recording](videos/mbx_stride_default_release.mp4) · [thumbnail](thumbnails/mbx_stride_default_release.jpg).
