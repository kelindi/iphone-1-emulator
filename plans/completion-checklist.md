# Completion checklist and plain-English status

> Historical recordings and publishing artifacts have been removed from Git history. The summaries below describe the original runs; remaining test reports and screenshots are retained. Historical hash receipts may predate this archival cleanup.

Status review: 2026-09-06, after the previous PR merged and work continued on
`kelindi/untitled-v2`. This summarizes existing evidence; no new benchmark or
feature acceptance is claimed. The full targets remain in [the roadmap](web-iphone.md).

## What is already useful

- [x] Genuine iPhone OS 1.0 runs locally in the browser inside a clickable frame.
- [x] All 16 apps reach recognizable original UI; this is launch coverage only.
- [x] Basic Calculator, Notes, Calendar and Contacts workflows work; organizer
  edits survive reload. Stopwatch and manual date/time changes have focused checks.
- [x] Autosave, backup import/export, saved-phone upgrade and explicitly enabled
  offline restore have verified workflows.
- [x] Compiled-code reuse, accelerated graphics, reduced save interruptions and
  the Safari source-row correction are integrated.

## Remaining product gates, in priority order

- [ ] **Speed:** improve actual guest execution; warm app launches within2 seconds,
  first launch within3 seconds, representative input feedback within150ms and
  supported animation at30 distinct frames/second. Verify cached startup within5s.
  These are roadmap targets, not current general results.
- [ ] **Finish everyday interaction:** reliable typing, scrolling, gestures and
  all supported physical controls under normal use, saving and focus changes.
- [ ] **Complete offline apps:** broader Calculator/Clock/Settings coverage,
  timer/alarms, preference effects and remaining persistence cases.
- [ ] **Photos:** useful starting media, reliable previous/next, zoom, rotation,
  slideshow and saved-state restoration. The first populated workflow failed.
- [ ] **Sound and iPod:** audible, continuous music with playback/volume/mute,
  media import, artwork and the supported video workflow. Internal decoded audio
  buffers do not establish audible playback.
- [ ] **Camera:** preview, take a picture and reopen it from Photos after reload.
- [ ] **Online apps:** working guest connection plus compatible demo services for
  Safari, Weather, Stocks, Maps, Mail and YouTube. Isolated Safari loopback success
  does not establish default-phone internet access.
- [ ] **Phone/Text:** simulated call and message flows through the real apps,
  including retained history. Actual carrier service is outside the initial scope.
- [ ] **Release:** Chrome and actual Safari on reference laptops,30-minute mixed-app
  stability,10-minute audio, interruption/recovery, memory/storage and deployed
  build checks. Existing focused checks cover only part of this gate.

## What the speed results mean

Recorded cloud Chrome comparisons show Home tail latency around1.27→0.92s;
return-visit first Settings around5.0→3.5s with compiled-code reuse. Warm Settings
still takes roughly3–3.6s. Calculator feedback around134–136ms passes a narrow
workload. Save compression no longer occupies the page main thread for roughly
6.8s, but total save duration is not reduced by the same amount.

Recent CPU experiments were parked: expanding compiled coverage gave mixed
navigation results; whole-core LTO improved a fixed-work test5.81% but warm
Settings median only about50ms. The completed instruction census identifies
work, not time saved. The next warm-window measurement has not been accepted.
None of these results proves how fast the user's particular Mac feels.

## Why this has taken so long

We are reproducing enough of an old computer for its original software to run.
Each old CPU instruction needs execution or translation, and apps expect real
graphics, sound, camera and radio behavior. Missing hardware behavior can make
an app wait, draw incorrectly or exit. Online apps also need compatible services;
connectivity alone does not restore their old protocols.

There were also avoidable process costs: work spread across too many hardware
areas; small optimization candidates received extensive verification; and test
harness failures plus large saved-state artifacts consumed time. Those activities
produced useful evidence, but too little recent improvement in perceived speed.
The immediate priority is one measurable app-launch improvement, with a clear
before/after and no regressions to saves or guest behavior. There is no reliable
completion estimate for the remaining hardware research.

Real carrier calls/SMS, unrestricted modern websites, modern streaming accounts,
full original boot-chain reproduction and perfect storage-hardware fidelity are
not prerequisites for the planned first release.


Architecture decision update: [098](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/runtime-comparison/README.md) now compares actual native/browser navigation. Native repeat Settings gains only~9% and Home~36ms before network/display overhead. Keep local browser execution for now; fresh first launch gains more, but remote performance and reference-Mac behavior remain untested.
