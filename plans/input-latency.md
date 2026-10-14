# Input hold timing: 100 ms versus 20 ms

> Historical recordings and publishing artifacts have been removed from Git history. The summaries below describe the original runs; remaining test reports and screenshots are retained. Historical hash receipts may predate this archival cleanup.

**Implemented: a 20 ms touch hold, while physical buttons retain 100 ms.** Three paired Chrome typing runs showed about 100 ms lower median character-commit latency, with exact text in all six notes. A further mixed-row/repeated-letter/backspace check passed. There is no demonstrated Settings/Home speedup, and these experiments do not establish that 20 ms is the minimum safe value for every app or gesture.

## What is being delayed

`web/emu_api.c` previously used one 600,000-tick (100 ms) floor on the 6 MHz guest timebase. It now separates `EMU_TOUCH_HOLD_TICKS = 120000` from `EMU_BUTTON_HOLD_TICKS = 600000`. A host release received sooner is retained until the applicable floor expires. The ringer remains a separate latch. Release still has to survive the controller's pending report or GPIO interrupt consumption.

The Z1 model now preserves MAKE/TOUCHING/BREAK phases, queues releases behind pending frames, and supplies correct coordinates and velocities. Those corrections make the earlier conservative 100 ms floor worth revisiting. They do not prove that every gesture recognizer or physical-button debounce path accepts arbitrarily short input.

At uninterrupted execution with CPI 1, 100 ms represents about 41.2 million emulated instructions. At 30 MIPS that would cost approximately 1.37 seconds of host execution. **This is not a general tap-latency prediction.** When the guest is idle, WFI can advance time without executing that many instructions. In one actual Home → Notes trace, the 100 ms hold's BREAK was consumed after only 2.72 million instructions. Release delay also includes the Z1's 10 ms report schedule and guest processing.

## Isolated builds and method

During the isolated A/B runs, production files were left unchanged. `.context/input-latency/20/` and `/100/` contain separate copies of `emu_api.c`, compiled with 120,000 and 600,000 hold ticks respectively. Their WASM modules link the same existing portable-core archive. Native probes include those API variants and trace the original framework's touch decoder at `0x313097a4`.

The keyboard fixture comes from the pristine compact Home snapshot, with a normal touch opening the real Notes app and its original keyboard. Each variant restores that same empty-note state. Ten immediate down/up pairs tap the top keyboard row in order, with 20 million instructions between requests and a final drain interval. No text is inserted through a host API or guest-memory edit.

The browser comparison uses actual Chrome, 30 ms host mouse pulses, four Settings/Home rounds, and the maintained `web/tests/performance.mjs` sampler. It polls the canvas every 50 ms, excludes the status bar, and requires the target layout to remain stable for 500 ms. Its ready timestamp marks the start of that stable interval. Both runs use the compact fresh-phone baseline; the candidate module is served separately at port 8082.

## Correctness results

| Check | 100 ms | 20 ms |
|---|---:|---:|
| Original keyboard MAKE frames consumed | 10 | 10 |
| Original keyboard BREAK frames consumed | 10 | 10 |
| TOUCHING frames in that workload | 93 | 18 |
| Malformed phases | 0 | 0 |
| Final queued/held/pending contacts | 0 | 0 |
| Visible note | `Qwertyuiop` | `Qwertyuiop` |
| Browser Settings/Home cycles completed | 4 | 4 |

The 20 ms native probe also delivered three repeated Settings tap MAKE/BREAK pairs and finished with no pending Home release or held button. Its fixed instruction-budget screenshots sometimes caught the black portion of the Home animation, so those screenshots are **not** used to establish app readiness. The completed browser cycles provide that navigation check.

Keyboard release-consumption measurements, from host tap request to the real decoder's BREAK call:

| Median across the ten keys | 100 ms | 20 ms |
|---|---:|---:|
| Guest elapsed time | 104.95 ms | 31.41 ms |
| Executed instructions | 5.50 million | 1.08 million |

This native measurement establishes reduced work before release consumption, not visible-character latency. The later Chrome experiment below measures the visible character separately. Existing baseband audio errors also occur in the keyboard workflow and are a separate unimplemented sound path.

## Browser navigation observations

Warm medians below cover rounds 1–3; round 0 is shown separately. These are small diagnostic samples from a shared VM, with possible concurrent build/probe load. They are not isolated performance acceptance results or p95 measurements.

| Action ready time | 100 ms hold | 20 ms hold |
|---|---:|---:|
| First Settings open | 7.35 s | 6.90 s |
| Warm Settings open, median | 7.87 s | 7.41 s |
| First Home return | 14.20 s | 14.20 s |
| Warm Home return, median | 14.85 s | 15.00 s |

There is **no demonstrated navigation gain** from changing the hold in these runs. The broad multi-second app and animation costs remain. One baseline Home sample was unusually short (6.20 s), further cautioning against attributing small differences to the input floor.

The 100 ms report was recovered from all eight completed action records printed to stdout after final JSON serialization failed on a diagnostic BigInt. The maintained script now handles BigInts, and the 20 ms report completed normally. The recovered baseline has action timings and video, but no recovered per-action emulator metrics.

Local evidence:

- `.context/input-latency/keys20.png`, `keys100.png`, corresponding logs and `key-summary.json`: exact note contents and consumed phases.
- `.context/input-latency/probe.c`: native genuine-guest probe; `nav20.log` records the additional tap delivery and final released state.
- `.context/speed-input100/report-recovered.json` and its original recording: completed baseline navigation timings.
- `.context/speed-input20/report.json`, screenshots and recording: candidate browser navigation.

## Browser typing comparison and adoption

`web/tests/typing-performance.mjs` ran three paired tests in alternating order: 100/20, 20/100, 100/20. Each used a fresh disposable Chrome context, the same genuine empty Notes keyboard, and ten 30 ms clicks spelling `Qwertyuiop`. All six final screenshots contain that exact text. The sampler checks every 25 ms, removes the blinking caret from its dark-pixel mask, then matches samples retrospectively to the settled glyph pixels. It separately observes the keyboard-popup strip and retains a screenshot witness for each committed character. This avoids treating the popup or caret blink as a typed letter.

| Measurement, 30 characters per build | 100 ms floor | 20 ms floor |
|---|---:|---:|
| Pooled median visible-character commitment | 437.75 ms | 337.85 ms |
| Per-run median commitments | 387.85 / 462.75 / 425.20 ms | 337.85 / 362.75 / 362.85 ms |
| Median first popup | 100.30 ms | 125.25 ms |
| Complete guarded ten-key sequences | 8.01 / 8.20 / 8.15 s | 6.35 / 7.71 / 6.05 s |

The candidate reduced median visible-character latency by approximately **100 ms** in this bounded workload. It did not improve the press popup; the hold governs release. Both builds still had individual commits around 2.1 seconds. The typing harness waits at least 400 ms per action and 200 ms after observing commitment, so the whole-sequence times are guarded test runs, not maximum typing throughput. The sample is too small and environment-specific for a product-wide percentile or a full-speed claim.

The focused candidate-only sequence `codexhello<o` (`<` means Backspace) used all three keyboard rows, repeated `ll`, deleted the final character and retyped it. Its final screenshot reads exactly `Codexhello`. This establishes those additional correctness cases without changing the paired benchmark's text.

Production then adopted **touch-only** 20 ms. Momentary buttons keep their previous 100 ms floor because the shorter physical-button debounce, especially Power, was not independently established. No snapshot schema or controller phase/edge logic changed. All 24 native suites pass, including 10,149 checks in the browser API suite. New deadline checks verify that an unread MAKE survives the touch floor and is followed by BREAK, and that an acknowledged Home press remains held until its separate 100 ms deadline before raising a genuine release interrupt. Existing cancellation and snapshot-input checks also pass. A final actual-Chrome check of the rebuilt production module (20 ms touch, 100 ms buttons) repeated the mixed-row/backspace sequence and visibly produced the exact `Codexhello` note; evidence is in `.context/typing-production/`.

Evidence: `.context/typing-input-final/report.json` and its six run directories contain the paired traces, per-character witnesses, final notes and unedited recordings; `.context/typing-mixed20/` contains the focused correctness run. Milestone 037 includes sequential original-speed recordings of the first baseline and candidate runs. That video illustrates one pair; the table above summarizes all three pairs.

Remaining work: verify broader sustained gestures and app workflows, and repeat timing across hosts/browsers. The much larger execution and Home-return bottlenecks remain separate. This touch change is a modest typing improvement, not a navigation fix.

Reproduction requires explicit URLs for isolated 100 ms and 20 ms builds, for example `BASELINE_URL=http://localhost:8081 CANDIDATE_URL=http://localhost:8082 ROUNDS=3 node web/tests/typing-performance.mjs`. Current production uses 20 ms touches, so serving it as the historical 100 ms baseline would invalidate the comparison. The maintained script records the served WASM SHA-256 and rejects identical modules under different hold labels. For the focused check, use `HOLDS=20 INPUT_TEXT='codexhello<o' ROUNDS=1` with an explicit candidate URL.

## Short first taps retain a normal held-contact report

The MBX default exposed a real missed first Calculator launch: a 50 ms pointer pulse immediately after `iphone-ready` leaves Home visible. The public DOM/worker trace contains both accepted contacts messages, with no cancellation or blur. Settled 50 ms and immediate 180 ms pulses visibly open Calculator. The initial investigation incorrectly extended the first Home screenshot to all four exact-image failures; inspection of every PNG corrected that claim. Those other three screens show Calculator at 0 and differ from the software-renderer golden only at the image level. Renderer-oracle handling is a separate Calculator test concern.

The failing diagnostic browser delivers MAKE and BREAK to the controller, but no held-contact report. Startup work queues both host messages in one worker turn. Its first CPU slice catches up roughly 89 ms of trusted foreground time while MAKE remains unread, satisfying the 20 ms release floor. The release is retained correctly, then delivered immediately after MAKE consumption. Nothing remains stuck or disappears from the host queue.

A controlled native replay uses the same genuine default snapshot, API, original Apple trajectory decoder at `0x313097a4`, and a simulated 90 ms host-clock stall. The baseline sends MAKE (3)→BREAK (5) and remains Home, despite 101 ms between report timestamps. The isolated correction under the identical host schedule sends MAKE (3)→TOUCHING (4)→BREAK (5) and opens Calculator at 0. Both retire 300 million instructions after the input sequence and end at device tick 4,133,888,876. This is a deterministic functional experiment, not a latency measurement or proof of every Apple recognizer's internals.

The API now keeps a normal release queued while a removed contact's existing last Z1 report remains MAKE. The normal controller tick generates its held report; unread reports and release backpressure remain intact. The 20 ms minimum still applies. There are no new state fields, snapshot changes, forced Apple events, or frontend waits. Explicit cancellation/Pause/restore cleanup still uses `emu_release_inputs`, which bypasses this normal-tap condition and drains the original release path.

The candidate passes 10,241 native API checks, including unread MAKE and held-report preservation, partial multi-contact release, repeated same-ID quick taps, and existing cancellation/checkpoint/clock/button contracts. The new test fails the old implementation at the expired-floor/unread-MAKE boundary. Three disposable actual-Chrome candidate starts with immediate 50 ms pulses all visibly open Calculator at 0; each trace shows exactly MAKE→TOUCHING→BREAK, with all queues drained and the release condition exercised. Root reviewed and applied the API/test patch; rebuilt production acceptance is pending as this section is written.

[Milestone 063](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/short-tap/verification.json) includes the actual 28.64 s before/after recording and [durable trace evidence](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/short-tap/verification.json). The recording's actual ready-to-down 75/219 ms and hold 56/64 ms timings differ, and its candidate already generated TOUCHING before release. It demonstrates visible behavior; the matched native schedule and three earlier candidate traces establish the causal result. This does not establish minimum safe timing for every app, flick, pinch, or multi-finger workflow.

Production follow-up: rebuilt WASM `603679fbc7961ddad95ac8822257074fecde5940bc013246d520ea3b6240c84f` retains the same CPU seed; offline bundle is `9f4dbb3e5ab97a91138875d9`. All 26 native suites and six production controls checks pass. The initial Calculator workflow correctly reaches the app, but its fixed eight-second assertion catches a gray, blank display. A separate production timeline reproduces that exact PNG at four seconds (SHA-256 `f7e68d908fb653bc7c7d5c860e0a014ff1c827554035990b38f68d877957016b`) and completes naturally at eight seconds, with no further input. At 8/12/20/30 seconds the original software zero reference matches within one RGB level on all 28,160 display pixels; alpha is exact. The original failed phone was already closed, so this continuation is explicitly a fresh reproduction. [Durable timeline](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/short-tap/production-render-timeline.json), [identical transient](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/short-tap/production-transient.png), [completed display](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/short-tap/production-complete.png).

This establishes an unfinished launch frame, not a demonstrated persistent graphics fault. Root is replacing only the initial fixed-time assertion with bounded readiness polling against the unchanged reviewed pixels; the immediate 50 ms input remains unchanged. Concurrent functional tests affect these observations, so they are not a new speed measurement. The slow app launch remains a performance issue.


Final production acceptance passes 26 native suites, 34 JavaScript tests, 59 browser lifecycle/update checks and all twelve Calculator cases with positive compiled execution. The initial readiness test now polls independently reviewed pixels with a 30-second deadline and unchanged immediate 50 ms input. The original references remain; independent review justifies a strict maximum two-level RGB difference with exact alpha, and all 132 other-answer comparisons fail it. Both earlier test failures remain preserved. [The final production reports](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/short-tap/production/verification.json) and [milestone 063 recording](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/short-tap/verification.json) distinguish functional input recovery from the still-unmet app-launch speed target.


## Current Calculator observation (2026-09-06)

[Milestone 089](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/calculator-input-feedback/README.md) records 60 correct real keypad presses. RAF-observed button p95 is 165.4/165.6 ms, but actual canvas sequences are skipped by the sampler. This does not establish a guest-side 150 ms failure; precise delivery timing remains open. The maintained `npm run test:input-feedback` returns nonzero for the literal observed threshold, and its report distinguishes that from passing correctness. [Evidence and sampling limitations](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/calculator-input-feedback/README.md).

### Direct frame delivery follow-up

[Milestone 090](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/calculator-frame-delivery/README.md) completes one separate 60-press diagnostic on unchanged production. Independent raw/source crops verify every 9/C result and frame association. Button canvas-completion p95 is 135.7/134.0 ms, meeting the 150 ms threshold for this observed Calculator workload; same-run RAF p95 is 148.6/148.9 ms. Correct-result canvas p95 from release is 138.8/129.3 ms, while down-to-result remains 171.4/161.1 ms. No physical-paint, other-app or production speedup claim follows.

Early page receipt to canvas completion is at most 0.5 ms. Observer post-draw work costs median 2.4/p95 4.1 ms and additional crop transport extends input spacing; these costs are retained, not subtracted. All measured saves remain absent under the ordinary idle guard. The earlier 089 dataset is unchanged and is not an uninstrumented paired control. [Evidence and remaining worker/guest attribution boundary](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/calculator-frame-delivery/README.md) explain why no scheduling optimization is justified from these results alone.
