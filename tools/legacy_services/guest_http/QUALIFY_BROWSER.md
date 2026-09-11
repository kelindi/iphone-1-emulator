# Original-app service qualification harness

`qualify-browser.mjs` serves a pinned site, starts a fresh disposable Chrome
context, imports exact owned snapshot bytes through the existing Import control,
then accepts JSON actions on stdin. It records actual page video, guest/page
screenshots, serial messages, browser responses and a final JSON report. It does
not type through a guest-memory shortcut, alter guest state or qualify the native
boot which produced the snapshot.

```sh
SITE_ROOT=.context/app-services-current/services-site \
ARTIFACT_DIR=.context/app-services-current/services-browser-run-1 \
SNAPSHOT_PATH=.context/app-services-current/services-boot/home.snap \
PORT=8231 \
REQUIREMENTS_JSON='["weather-forecast","stocks-quotes","local-home"]' \
node tools/legacy_services/guest_http/qualify-browser.mjs
```

Use a new artifact directory whose parent exists. Set `SNAPSHOT_SHA256` to require
an independently established file digest. `CAP_SECONDS` defaults to900 and cannot
exceed900; `CHROME_PATH` defaults to `/usr/bin/google-chrome`. `MIN_FREE_BYTES` is a positive safe integer, default1073741824 (1GiB), checked
before launch and between actions. This is an artifact-space gate; it does not
guarantee Chrome Blob storage/autosave succeeds under its separate disk policy. The example labels are an
illustration, not a complete app acceptance plan. Freeze requirements appropriate
to the actual intended workflow before launching; do not loosen them afterward.

Wait for `READY`, then send one object per line. Coordinates refer to the genuine
320×480 guest screen. Only the original mouse/touchscreen input path is used:

```json
{"tap":[160,220],"wait":3000,"shot":"weather-open"}
{"observe":"Weather","frame":"weather-open-guest.png"}
{"assert":"weather-forecast","frame":"weather-open-guest.png","expected":"The original Weather view shows the requested city and six independently checked demo forecasts."}
{"home":true,"wait":1500,"shot":"home-return"}
{"taps":[[30,300],[60,300]],"interval":900}
{"swipe":{"from":[40,420],"to":[280,420],"steps":20,"duration":800},"wait":1500,"shot":"after-swipe"}
{"autosaveReload":true}
{"finalExport":true,"shot":"after-export"}
```

`swipe` uses the same original screen-scaled page mouse path: move to `from`,
mouse down, bounded interpolated moves, then mouse up (also attempted on a move
failure). Both endpoints must contain exactly two finite coordinates within
0≤x<320 and0≤y<480. Defaults are20 steps and600ms; accepted limits are2–120
integer steps and100–3000 integer milliseconds, with at least5ms per step.
Requested duration excludes browser/host scheduling overhead; actual elapsed
time is recorded separately and is not a speed measurement. The example is only
a motion illustration; inspect the genuine lock slider before choosing endpoints.
`node --test tools/legacy_services/guest_http/test_qualify_swipe.mjs` covers bounds,
screen scaling, event ordering and release after failure using host mocks only.

`observe` is a labeled observation, including Weather/Stocks/Safari or another
app. It is **not a passing check**. `assert` is an operator's explicit review of
the named retained guest framebuffer and requires an `expected` description.
The harness verifies that this run captured the file and records its full SHA;
it does not automatically recognize temperatures, prices, chart correctness or
text. Do not assert a frame until actually inspecting it. Assertion names must
be unique. Screenshot names must also be unique, so previous evidence is not
overwritten. Assertions cannot refer to arbitrary external image paths.

`autosaveReload` waits for the real interval autosave timestamp to advance, reloads,
requires the identical restored timestamp, and checks unpaused generated execution.
Its generated check labels are `real-interval-autosave` and
`actual-autosave-record-restored`; visual persistence still needs independently
reviewed frames. Numbered autosaved/restored frames are retained for each cycle.
`finalExport` is an explicitly diagnostic use of the existing hidden Export
control, waiting until enabled and retaining a hashed download. It is not an
ordinary user Save action, and only one final export is allowed.

Send the literal line `finish` to finalize. All labels in `REQUIREMENTS_JSON` must
have passing checks, and final generated execution must be unpaused. Without any
requirements, `passed` remains null: an error-free completed observation run can
have `runCompleted:true`, but it is not an accepted app workflow. EOF without
`finish`, a missing assertion or a runtime/audit/cleanup error is not a completed
workflow. `requireRequest`, `requireNewRequest` and `requireResponse` are rejected:
this harness does not parse diagnostic serial text into independently verified
guest HTTP transactions. Raw serial is retained for a separate native/wire audit.

The15 required actual local assets must match pinned bytes; service-worker source
is also pinned and checked if requested. Site bytes are held immutably by the
private HTTP server and checked against on-disk files again at completion. This
does not claim offline lifecycle or native-code-cache acceptance. External asset
URLs and their browser request failures are retained separately. A frozen manifest
does not itself constitute a full independent audit of external response bodies.

Body collection and final observation/close operations are bounded. Context and
browser close are attempted separately, including startup failures; a pending
30-second browser launch is retained before final cleanup. Close timeouts remain
errors, not proof of process termination. The internal workflow cap is followed
by bounded cleanup; an outer supervisor must leave cleanup time and preserve any
failure instead of retrying automatically. Video is retained as Playwright's
actual recording; reported action/frame `elapsedSeconds` uses harness start,
which precedes recording, and must not be treated as exact video presentation
timestamps or app latency measurements.

Initial source qualification: `node --check` and16 pure contracts pass using
retained asset pins. They reject missing/mismatched/unreadable bodies, malformed
URL values, duplicate requirement labels, missing checks, failures and unfinished
runs. Evidence is `.context/services-browser-harness/pure-tests.json`; no browser
or guest was started by those tests. The first real run remains separately gated.
