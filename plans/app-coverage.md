# Original iPhone app coverage audit

Audit date: 2026-09-05. Genuine iPhone OS 1.0 build 1A543a in the browser emulator. This is milestone 0 discovery for [the web iPhone plan](web-iphone.md), not release acceptance.

**All 16 original apps reached recognizable genuine app UI.** This does not mean all apps are useful: network apps cannot fetch content, the personal media libraries are empty, Camera never produced a preview, and Phone/Text report an unavailable baseband DMA device. Offline editors and navigation provide a useful foundation for the next acceptance tests.

## What was actually exercised

Each app was opened from the supplied Home snapshot in a fresh Chrome browser context. The sweep observed 25 seconds after the icon tap, then another 15 seconds after a basic control probe where applicable. Every app has a guest screenshot, surrounding page screenshot, console log, and guest diagnostics in the generated report. These observation windows are not performance measurements; engine experiments and other browser work ran concurrently.

The initial sweep exposed a touch calibration problem: first-row icon centers at y65 did not respond, while their lower hit regions at y95 did. The sweep used y95 for those four apps and recorded both coordinates. This did not prove coordinate correctness. The subsequent CE calibration work and recording are documented separately in [the touch calibration milestone](../tweets/024_touch_calibration.md). Calibration retries and later workflow evidence must be distinguished from the original sweep.

| App | Observed genuine behavior | First blocked or unverified workflow |
|---|---|---|
| Text | Opens New Message and the original keyboard; top-right Cancel returns from the composer. The isolated MBX fix also verifies Cancel → reopen → Home with 3,155/3,155 graphics commands complete and zero rejects ([recording](../tweets/052_text_mbx_composer.md)); combined production recheck passes the same workflow with active JIT, no errors, and exact 144,000-pixel raw RGB Home return. UART reports `AppleBasebandOutput: could not start DMA: device is not ready`. | No message was sent or received. Implement and verify baseband/SIM/SMS readiness and delivery; editor appearance is not SMS support. |
| Calendar | Created `Meet`, reloaded, renamed it `Meetup`, reloaded, deleted it, and reloaded to the empty day. All three explicit checkpoints preserved their state. Guest date is Dec 31, 2009. | Correct the wall clock; verify date editing, repeat, and a foreground alert with sound. Full speed remains unverified. |
| Photos | Photo Albums displays No Photos. | Populate the genuine photo database/thumbnails or capture a photo, then verify album browsing, gestures, slideshow, and persistence. |
| Camera | Closed iris and shutter UI appear; no preview. During the post-shutter observation interval the app returns to Home. No explicit crash was captured. | Establish sensor/controller readiness and a live preview. Determine why the app exits; then capture into the real camera roll. |
| YouTube | Featured view appears, followed by Cannot connect to YouTube over No videos. | Guest connectivity and compatible catalog/video responses, then genuine video decoding and sound. |
| Stocks | Default ^DJI/AAPL/GOOG/YHOO/T list and chart-range controls appear. Quote values are absent; chart reports Error retrieving chart. | Guest network plus a compatible quote/chart service. Symbol edits and retention remain untested. |
| Maps | Search and map/satellite/list controls appear, but the map is blank. | Fetch real guest-compatible tiles/search/routes. Initial search-field probe did not focus; repeat after calibration before judging input. |
| Weather | Unit selection °F→°C→°F and removal of New York survive three exact Save/reload comparisons ([074](../tweets/074_weather_controls_persist.md)). Forecast still reports Update failed. | Connect the prepared demo backend through guest networking; verify city addition, forecasts and actual temperature conversion. |
| Clock | World Clock displays Cupertino. Genuine Stopwatch Start/Lap/Stop/Reset works. Idle timing passes at −0.336%; after fixing lost foreground time, production Save-load timing passes at 60.4 displayed seconds over 60.0022 wall seconds (+0.663%). [Production recording](../tweets/051_foreground_clock_carry.md). | Countdown timer, audible foreground alarm, correct calendar date, and wider mixed-app timing remain unverified. The earlier Save-load timing failure and its correction are preserved below. |
| Calculator | [Twelve maintained workflows](calculator.md) pass with actual display pixels: clear, four arithmetic operations, negative/positive results, fractions/decimals, repeated digits and chained calculations. | Memory keys, hidden correction gestures, exceptional arithmetic, digit limits, saved Calculator state and full-speed acceptance remain unverified. No sign-toggle or backspace button is visible on the original keypad. |
| Notes | Genuine keyboard creates Hello, reload preserves it, editing to Hello web survives reload, and deletion survives a third reload. Every reload matches all 144,000 pixels below the status bar exactly; content is independently visually reviewed. | [Milestone 049](../tweets/049_notes_edit_delete_persistence.md) and `npm run test:notes` cover the basic workflow. Broader note data, interrupted-save/upgrade combinations, and full speed remain separate gates. |
| Settings | Genuine preference rows appear, including disabled No Wi-Fi. The integrated RTC fix passes two real manual-time edits, natural autosave and reload after each, plus final unpaused generated execution. [Recording and scope](../tweets/082_manual_time_autosave.md). | All 12 clock lifecycle checks and the combined runtime upgrade gate pass. Complete calendar-consistent dates in 2000–2099 are supported; alarms, partial/invalid date writes, other preference effects and broader persistence remain unverified. The initial drag did not scroll. Subsequent CE coordinate/velocity fixes verify one slow Settings drag; flick/pinch and broader gesture acceptance remain unverified. |
| Phone | Dial keypad opens. Contacts creates `Ada` using the original keyboard and retains it after reload; a separate run deletes it and retains the empty list after reload. UART reports unavailable baseband DMA. | Original-snapshot name editing failed under urgent guest memory pressure. A fresh imported compact-rootfs candidate passes Ada→Adam editing, reload, and subsequent Clock/Settings launches; the newer USB-matched pristine Home also passes Ada→Adam with both checkpoint reloads and Home/Settings navigation. See [guest memory investigation](guest-memory.md) and the regression details below. Existing user-state migration, broad stability, simulated calls, and audio remain unverified. |
| Mail | Welcome to Mail lists providers; Other opens the manual IMAP/POP/Exchange account form. | Configure a compatible test mailbox and verify actual send/read/reply/delete. No authentication, TLS, or mail transport was exercised. |
| Safari | [091](../tweets/091_safari_local_demo.md) verifies original numeric URL entry, a labeled local page, link, `hi` GET form, bookmark creation and Home/revisit. Real interval autosave/reload preserves the bookmark and server; seven actual guest HTTP exchanges and three exact screenshot comparisons pass. | This uses an isolated imported phone with our own loopback daemon. The maintained pristine accelerated-default qualification [093](../tweets/093_default_local_demo_qualification_failure.md) renders two local pages but fails at form entry with graphics hangs and daemon absence of unknown cause. Default provisioning/discovery, broader browser behavior, external internet access and full-speed acceptance remain open. |
| iPod | Empty library displays No Content; More does not change this state. | Import correctly indexed DRM-free music/video, then verify guest audio/video pipeline, volume, and rotation/Cover Flow. |

## Evidence and reproducibility

Run the real browser discovery harness against a built and served `web/` directory:

```sh
BASE_URL=http://localhost:8080 \
ARTIFACT_DIR=.context/app-inventory \
node web/tests/inventory.mjs
```

`APP_LIST=Calculator,Weather` selects a subset. `RECORD_APPS=Calculator` enables real screen recording for named apps. `OBSERVE_MS` and `ACTION_MS` adjust bounded observation windows. The script uses one context at a time and saves each app's evidence immediately, so a later failure does not lose earlier findings. Playwright, Chrome, and Playwright's ffmpeg are required for recordings.

The generated `.context/app-inventory/inventory.json`, per-app `report.json`, screenshots, and `observations.md` provide the detailed workspace evidence; `.context` is intentionally gitignored. The table above is the durable audit and stands without those local artifacts. Selected genuine workflows are preserved in the [app audit milestone](../tweets/025_real_app_inventory.md) recording. It illustrates selected apps, not proof of all app acceptance gates.

## Verified organizer workflows

The [organizer milestone](../tweets/027_calendar_contacts_persistence.md) records Calendar create/edit/delete with a browser Save/reload after each change, plus Contacts creation and deletion in separate sessions. The guest still uses December 31, 2009; this does not establish correct wall clock or alarms. Contacts name editing after reload failed on the original snapshot. A separate fresh-profile import of the compact-rootfs candidate then passed Ada creation/reload, name editing to Adam/reload, and Clock/Settings launches. This is a clean-phone candidate result, not migration of existing user data or broad stability acceptance; see [the guest memory investigation](guest-memory.md).

```sh
BASE_URL=http://localhost:8080 ARTIFACT_DIR=.context/calendar-report node web/tests/organizer.mjs
```

This command runs the tracked Calendar action sequence in a fresh disposable profile. No previous workspace artifacts or injected guest data are required. It records the real browser, saves screenshots/action logs, and compares the guest framebuffer below the status bar after each explicit checkpoint reload. All three Calendar comparisons and the separate Contacts create/delete comparisons had zero changed samples. Human screenshot review confirms the event/contact content; image equality alone does not prove semantic correctness. `--interactive` accepts newline-delimited JSON touch/keyboard steps; `ACTIONS_FILE` replays custom action logs. Original failure exports are retained in workspace evidence for diagnosis.

## USB-matched fresh-phone regression

On 2026-09-05, the newer pristine Home booted with USB matching passed the previously failing Contacts workflow in an actual disposable Chrome profile:

1. Import the candidate through the browser's normal Import UI.
2. Open Phone → Contacts, create `Ada` with the original keyboard, then Save and reload.
3. Open the restored contact's name editor, change it to `Adam`, then Save and reload again.
4. Return Home and open Settings successfully.

Human review confirms the Ada and Adam contact cards, the working editor, Home and Settings. Both checkpoint comparisons reported zero changed samples from the raw guest canvas. An additional independent comparison decoded every pixel of the saved/restored guest screenshots: **zero differences across all 135,300 displayed RGB pixels for each pair**, including the status bar. These are full comparisons of the 300×451 displayed screenshots, not a claim that the initial sampler checked every raw 320×480 canvas pixel.

The source fixture is `.context/lockdown-root/usb-ready-home.snap`, SHA-256 `28591a3003b8677469b2e506185792e26ac128ba9383b7f110959f4f4fb0c82d`. The full action log, screenshots, 234-second original-speed WebM, report and independent comparison are in `.context/usb-contacts-regression/`; its separate `usb-contacts-edited.snap` export preserves the resulting Adam phone. Replay the recorded actions using `ACTIONS_FILE=.context/usb-contacts-regression/actions-input.json` with `web/tests/organizer.mjs`.

This is a correctness gate for a **fresh** phone. Existing failing and healthy fixtures were preserved; no old user state was migrated, reset or overwritten. The run used conservative observation waits and is not a navigation performance measurement. Baseband audio errors still appear; Contacts success does not establish calling or sound support. No frontend/core changes were made for this regression.

A later, separate [retained-data experiment](legacy-upgrade.md) powers off a disposable older 168 MiB Adam phone through the genuine UI and reboots its clean, unchanged disk with USB enabled. Adam remains visible in Contacts, `lockdownd` remains alive, and the new boot enables interactive timing. [Milestone 044](../tweets/044_legacy_phone_data_preserved.md) records that result. This extends evidence to one older edited fixture; automatic browser migration, larger old disks and wider retained app data remain unverified.

## Foreground Stopwatch timing

On 2026-09-05, a fresh disposable production Chrome phone with default guarded WebAssembly and software graphics passed the idle portion of the foreground Stopwatch target. The actual app displayed **00:00.5 → 01:00.3** across **60.0016 seconds** measured with browser `performance.now()`: **59.8 seconds**, or **−0.336%** drift, within the 2% target. Readings come from unmodified raw guest framebuffer captures, not an injected host timer. The displayed tenth-second resolution and actual screen-update behavior are part of this measurement. Separately sampled hardware ticks advanced 59.482932 seconds and do not replace the visible-app result.

The real Start and Stop buttons acknowledged about 654.1 ms and 248.4 ms after the mouse-release call completed, using the first observed button-color change. These sampled response times do not identify exact guest input-delivery timestamps. Measuring between already-running screen captures keeps those delays separate from clock drift. Stop left **01:00.5** on screen, with byte-identical raw captures 2.3343 seconds apart. The run had zero browser errors, visible/unpaused samples throughout, and positive guarded-code execution counters. Another agent briefly ran offline reload checks in a separate browser; this remains one idle guest workflow, not the representative mixed-app load gate.

Reproduce the original UI capture with:

```sh
BASE_URL=http://localhost:8080 ARTIFACT_DIR=.context/stopwatch-production node web/tests/stopwatch.mjs
```

The harness records a full video, raw canvas samples, input timing, JIT counters, and diagnostic hardware ticks. Its `captureComplete` flag only means evidence collection completed: a reviewer must read the real baseline/end digits and evaluate the separate `visualReview`. The idle report is `.context/stopwatch-production/report.json`.

The follow-up with `ARTIFACT_DIR=.context/stopwatch-save-production` and `--save-load --controls` **fails the 2% timing gate**: **00:00.9 → 00:58.9** over **60.0097 wall seconds**, or **58.0 displayed seconds / −3.349%**. Ordinary Save is clicked around 20 seconds into the interval and the saved checkpoint timestamp advances by the 40-second sample. No manual Pause is used; samples remain visible/unpaused with active compiled execution and no browser errors. Other agents also ran functional Chrome workloads. Hardware ticks advance 58.144639 seconds, corroborating actual clock loss rather than only a delayed repaint; this run does not isolate Save from other scheduling load.

After timing, genuine Lap records **lap 1 00:59.5** and starts the next lap. Stop holds the next lap at **00:02.2** while retaining the first lap; Reset returns **00:00.0** and clears the lap list. Subsequent raw captures of each stopped/reset state are byte-identical. Both complete original-speed videos and thumbnails are preserved in [milestone 047](../tweets/047_foreground_stopwatch.md). The loaded timing failure, countdown timer, audible alarms, and broader Clock behavior remain open.

An isolated foreground-time correction subsequently passes the same Save-load gate: **00:00.6 → 01:00.9**, or **60.3 displayed seconds over 60.002 wall seconds (+0.497%)**, with ordinary Save completed, active compiled execution, and zero browser errors. Hardware ticks independently advance about 60.002 seconds. Lap/Stop/Reset pass again. The correction retains unpaid foreground time while preserving the eight-millisecond device-step bound; it leaves explicit pause/restore and legacy clock gates intact. [Milestone 051](../tweets/051_foreground_clock_carry.md) records this candidate; [the detailed investigation](transition-latency.md#foreground-save-time-conservation) tracks production integration and lifecycle verification. The original failure remains recorded rather than replaced by the newer result.

The clean **production build** then passes the full repeat: **00:00.4 → 01:00.8**, or **60.4 displayed seconds over 60.0022 wall seconds (+0.663%)**. Ordinary Save completes while compiled execution remains active; hardware ticks advance about 60.003 seconds. Lap records **01:00.9**, the next lap stops at **00:02.4**, and Reset returns zero with no lap rows. Stopped/reset captures are byte-identical afterward. The report is `.context/clock-production-final-stopwatch/report.json`; [milestone 051](../tweets/051_foreground_clock_carry.md) now leads with the complete production video and thumbnail. Separate production lifecycle, physical-control, and offline regressions pass 12, 6, and 7 checks respectively. These clear the measured idle and Save-load Stopwatch gates, not countdown/alarm or every mixed-app timing scenario.

## What this changes about the backlog

1. Prioritize accurate touch, text entry, guest timing, and durable offline edits. Calculator, Calendar, Clock, Notes, Settings, and Contacts expose useful workflows before networking exists.
2. Implement the guest network and a controlled compatible service before promising Weather, Stocks, Maps, Mail, Safari internet access, or YouTube content. The observed errors match missing external dependencies, not missing HTML recreations.
3. Treat media import, audio, and Camera as distinct device tasks. No Photos/iPod content means playback and gesture support remain untested; Camera's closed iris needs separate hardware investigation.
4. Use the captured baseband DMA error to guide Phone/Text research. A working dialer or message composer is not a working carrier.
5. Preserve separate performance and stability gates. The browser worker improves host UI responsiveness, but this inventory establishes neither full speed nor 30-minute mixed-app stability.


Photos graphics followup: [053](../tweets/053_photos_mbx_transparency.md) verifies genuine No Photos launch and complete Home return through the candidate MBX path,775/775 graphics commands and zero rejects. This closes the known translucent-solid rendering failure; importing/viewing photos remains unverified. Default composition stays software pending combined validation.

Photos import discovery (2026-09-06): the original CameraAlbum enumerates DCF files beneath the current user's Media/DCIM directory and loads separate THM images through UIImage. [The source contract](../docs/traces/photo-library-contract/README.md) identifies a bounded camera-roll fixture experiment that does not require first reconstructing the synced-album database. An [own guest importer](../docs/traces/photo-bootstrap/README.md) now writes two JPEG/THM pairs plus an invalid-name control through original filesystem calls on a separate pristine-image copy. Full readback and independently resolved HFS hard-link payloads pass, and a preserved4B→10B continuation reaches Home. The first browser workflow now verifies Camera Roll(2), both thumbnails and the first full image, but image navigation leaves stale content/a narrow strip. The workflow fails; second-image and persistence gates remain incomplete. [Milestone094](../tweets/094_photos_first_workflow_failure.md) retains the full recording and observer limitations. No production phone is changed.


## Continuous combined-renderer audit

A later single 10m28s MBX session opens all 16 apps on one uninterrupted phone and returns Home after each, with zero renderer rejects and exact comparison of all 2,304,000 Home pixels. All46 screenshot stages are manually reviewed. [The complete recording and thumbnail](../tweets/056_continuous_sixteen_apps.md) and [per-app timeline](graphics-speed.md#one-continuous-sixteen-app-phone) distinguish this from the original fresh-context discovery.

Calendar's actual Add Event editor, Calculator 2 + 3 = 5, Maps keyboard, Weather city management, Notes Done, Settings scroll, Contacts, Mail Other and Safari Done are visible. The Clock Stopwatch probe in this continuous clip remains on World Clock and is **unverified**; focused production Stopwatch evidence remains separate. Camera's capacity alert, empty Photos/iPod libraries and network failures persist. This is a rendering/navigation continuity gate, not a claim that all workflows, online services or full-speed targets are complete.
