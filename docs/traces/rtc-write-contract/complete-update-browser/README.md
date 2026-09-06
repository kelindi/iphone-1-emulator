# Isolated RTC browser acceptance — first run stopped at autosave

## Result

The single authorized workflow did **not** pass. Startup, fixture import, matching JIT ABI, actual generated execution and the first genuine picker input passed. The first natural autosave did not advance `savedAt` within its 65-second bound. The app itself reported **“Save failed: Failed to fetch. Export a backup to keep this session.”** The browser closed after 119.890 seconds; there was no retry or cap extension. The second picker action, persistence reload and final post-reload execution gate were never reached. No milestone081 or production promotion is claimed.

[`report.json`](report.json) retains five passed checks, all recorded stage metrics, the timeout stack and actual failure status. [`rtc-autosave-failure.mp4`](rtc-autosave-failure.mp4) is the full original-speed recording (converted from the retained WebM); [`rtc-autosave-failure.jpg`](rtc-autosave-failure.jpg) shows the actual failure-stage phone. No synthetic screen or error text was inserted. Controls remain hidden, so the saved failure status is in the report rather than visible in the frame.

## Isolation and compatibility

`site/` is a staged copy of the latest production UI with only its private `dist/` replaced by the already-built RTC candidate. `site-pins.json` covers127 files. `final-pins.json` verifies every served file remained unchanged and the UI, worker, storage, spinner/hidden-controls source and seed still match production. Candidate WASM SHA256 is `7a72add6d886440f64a853511375ba51cbbb23f2263831b6a05c51fde7969088`; unchanged seed SHA256 is `d9c661556ab17bb8433ed9b0c3ecfd52e2fcafe58221b4cb84a920e4976b3881`. `abi-report.json` proves revision1 and all16 offsets match the maintained ABI manifest. The real browser then enabled511 regions and retired generated instructions before the fixture workflow. No production file was edited. The disposable profile and evidence remain in `run-1/`.

## Actual workflow and evidence

1. Fresh profile on private localhost8201: current startup spinner dismissed, current companion controls stayed hidden, JIT retired instructions increased3,405,427→14,373,639 during the explicit startup execution check.
2. The hidden Import input loaded the existing `rtc-contract/browser-2/before-manual-change.snap`. This intentionally writes the initial imported checkpoint; that timestamp was **not** counted as natural autosave acceptance. The hidden Export control then saved diagnostic `before-picker.snap`. No hidden Save invocation, direct storage write, test-driven interval, or substituted checkpoint was used.
3. At39.148 seconds, a real180ms pointer press at guest coordinate(160,416) moved the selected minute wheel13→14. The imported historical fixture initially showed header4:14 but selected wheel13; both actual raw frames are retained. This proves the first picker interaction, not two separately demonstrated header changes.
4. At44.976 seconds, the guest was unpaused with118,577,197 generated instructions retired. The runner waited for the unmodified application’s natural30-second interval to commit a strictly newer timestamp. At110.310 seconds, `savedAt` was still the import timestamp1788673112195, the app reported the fetch failure, the guest remained unpaused, and generated retirement had reached442,141,215. The displayed clock advanced to4:15 while waiting. No page-level uncaught error was emitted.

Files include full-page PNGs, exact320×480 guest PNGs and614400-byte rawRGBA frames for all four observed stages; the initial live export; the original WebM; the script; reports and immutable pins. The original failed paused-final run in `rtc-contract/browser-2/` is unchanged.

## Precise unfinished boundary

The fetch error’s source is not established by this run: the harness retained the app status and page errors but did not collect console/network failure events. There is no basis to attribute it to RTC behavior. A subsequent diagnosis needs separately authorized instrumentation; this one-run task did not reopen the browser or retry. Autosave/reload and two complete time-setting workflows remain unverified. The candidate’s native complete-seven-register2000–2099 acceptance and unsupported partial/invalid-write scope are separate existing evidence, not replaced by this failed visual gate. The prior `inspect` binary read the initial export without executing guest instructions: RTC1262304858, ticks5150541545, unknownWrites0. No post-change snapshot was captured before the failed gate closed.

This durable folder retains the report, compatibility/source pins, executed harness, three guest frames and full MP4/JPG. The private site, profile, original WebM, raw RGBA and255MB original live snapshot stay under `.context/rtc-write/browser-preview/`; they are not redistributed here. No new milestone is asserted.

Subsequent diagnosis established VM Blob storage pressure, and a separately instrumented candidate workflow now passes both edits/autosaves/reloads. [Diagnosis](storage-diagnosis/README.md) and [new result](../manual-time-persistence/README.md). This original run remains failed.
