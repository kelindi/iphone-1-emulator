# Original Weather controls survive browser reload

2026-09-06. The real OS 1.0 Weather app passes a focused controls/persistence workflow on the current browser phone. It opens city management, changes Fahrenheit to Celsius, removes New York using the actual two-step delete control, and changes back to Fahrenheit. Each final selection/list survives ordinary Save and page reload.

This verifies existing app functionality; no production emulator change was needed for this gate. **It does not verify temperature conversion, adding a city, or receiving a forecast.** The app still reports Update failed. The separate [demo backend](../../../tools/legacy_services/README.md) has only loopback HTTP acceptance and was not connected to this run.

## Actual evidence

- `report.json`: original harness output, five passing checks, no page errors. Generated execution is required at startup and completion. Each of three reloads compares all144,000 original canvas RGB pixels below row30 exactly:432,000 pixel comparisons total, without tolerance.
- `screen-review.json` and `review.py`: separate PNG decoding compares all126,600 displayed RGBA pixels per saved/restored pair below the scaled status exclusion, with exact equality. The three wrong-state comparisons fail on10,897 pixels (wrong units),12,515 pixels (New York incorrectly retained) and10,897 pixels (wrong final units).
- `screen-contact.png`: all six saved/restored screenshots, manually reviewed. The rows are Celsius with two cities, Celsius with only Cupertino, and Fahrenheit with only Cupertino; saved is left and restored is right. Raw screenshots are retained individually.
- `actions.json`: actual public taps, waits and Save/reload steps. No guest RAM/file edits or fake service response was used.
- `source-hashes.json`: local production files and maintained harness after the run. There was no response-body interception; these hashes are not described as network capture evidence.

The first two-second observation after tapping the minus control catches its transient animation. A later settled observation shows the actual Delete button before it is clicked. The complete recording retains that sequence and manual inspection pauses. This is an untimed functional run, not an input-latency or app-speed benchmark. The full console/guest diagnostics remain in the report.

## Recording and reproduction

[Original-speed recording](../../../tweets/videos/weather_controls_persist.mp4) —3:47.80,1280×1000,25fps. [Thumbnail](../../../tweets/thumbnails/weather_controls_persist.jpg). Approximate sections:0:10–0:27 launch/city management;0:52–1:14 Celsius and first reload;1:14–1:54 delete-control inspection;3:02–3:24 actual removal and reload;3:24–3:47 Fahrenheit and final reload. The long interval before removal is manual inspection time, not measured app latency.

The recording is transcoded from the continuous Playwright WebM without speed changes or cuts. The thumbnail is the actual final restored phone. The temporary browser profile is deleted after the run; the user's own saved phone is unaffected.

The run used the maintained `web/tests/organizer.mjs --interactive` harness, with a copy retained here. Replay the recorded steps against a fresh production server from repository root:

```sh
ARTIFACT_DIR=.context/weather-workflow-replay EXACT_RELOAD=1 REQUIRE_JIT=1 \
  ACTIONS_FILE=docs/traces/weather-controls/actions.json node web/tests/organizer.mjs
```

Recorded actions include fixed observation waits; every screenshot still needs semantic review. The original interactive process had completed/closed Chrome and written its report, then its idle terminal input was closed with EOF so Node exited normally. This did not alter the completed capture or checks.
