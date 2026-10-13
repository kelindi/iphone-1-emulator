# Genuine MBX composition as a speed path

> Historical recordings and publishing artifacts have been removed from Git history. The summaries below describe the original runs; remaining test reports and screenshots are retained. Historical hash receipts may predate this archival cleanup.

**Status: isolated probe, not enabled in the production phone.** Enabling SpringBoard's existing MBX composition setting reaches recognizable genuine Home and Settings screens and reduces instruction work in one native Settings replay. The initial probe exposed real rejected render commands and graphics-driver restarts. The strict 3D and clipped 2D-copy corrections now clear the measured command gates and are integrated into the production renderer. The production phone still uses software composition; wider app navigation and the mixed browser speed results are being assessed before enabling the accelerated default.

## Why investigate this path

The 600-million-instruction Settings profile in `.context/speed-next/settings-symbol-profile.txt` attributes 30.423% of captured instructions to CoreGraphics and 27.635% to `_layer_blit_stack`, a software pixel-composition routine. The histogram captured 557.4 million instructions; 7.1% overflowed its bounded collision table, so these percentages are shares of captured work, not exact shares of the whole run.

The clean 168 MiB work image currently sets `LK_ENABLE_MBX2D=0` and `LK_ENABLE_OGL=0` in SpringBoard's launch plist. The probe copies that image and changes only MBX2D to `1`; OpenGL stays `0`. A full comparison of all 5,120 file forks finds exactly one changed fork, the launch plist. Decoding both plists confirms that this single environment value is the only semantic change. No Apple executable is patched.

| Image | SHA-256 |
|---|---|
| Original compact image | `0b544d47fb67603167099e658d50e3c284c28233d489263b269682c4f9ac53b0` |
| Isolated MBX-enabled copy | `73d0d2a0ed4564dc16c67c7c6efc167725d2833461cd47919faa676595c0ded9` |

Evidence lives in `.context/graphics-speed/image-verification.json` and its independent `verify-image.py` catalog/fork walker. The original image and production firmware remain unchanged.

## Genuine cold boot and command evidence

The candidate genuinely cold-boots from that copy with the original device tree, USB matched, 256 MiB RAM, CPI 1 and the existing native core. It is not an old Home snapshot with a host-side toggle. At 10 billion retired instructions it displays Home behind the original Repair alert.

Although the environment variable is named MBX2D and OGL remains disabled, the genuine composition path submits both decoded 2D commands and 3D STARTRENDER operations. The register trace establishes this directly.

| Phase | 2D commands completed / candidates | 3D renders completed / candidates | Rejected 3D |
|---|---:|---:|---:|
| Cold boot to 10B instructions | 1,370 / 1,370 | 1,182 / 1,185 | 3 |
| Dismiss Repair through genuine input | 523 / 523 | 478 / 478 | 0 |
| Open Settings | 367 / 367 | 369 / 370 | 1 |

The cold boot's original AppleMBX driver reports “Graphics Restart during 3D Blit” at 4,720,626,983; 5,000,495,046; and 5,031,778,520 retired instructions. Many later commands complete, so a final recognizable image does not prove a clean transition.

Every rejected command has its register set, object-list words and full 44-word producer record preserved in `cold.err` or `api-settings.err`. The first measured gates are:

| Render | Actual clip registers | Captured producer geometry | Current rejection |
|---|---|---|---|
| Cold #1 | X `0x01400140` = 320..320; Y `0x01900170` = 368..400 | X 319.551..378.551; Y 380.612..395.612 | Empty/out-of-bounds aligned sprite clip |
| Cold #2 | X `0x01400140` = 320..320; Y `0x01800140` = 320..384 | X 319.551..378.551; Y 320.612..380.612 | Empty/out-of-bounds aligned sprite clip |
| Cold #3 | X `0x00280000` = 0..40; Y `0x013000f0` = 240..304 | X -23.896..35.104; Y 244.474..304.474 | Clip registers disagree with producer geometry |
| Settings #1 | X `0x00780070` = 112..120; Y `0x00c000a0` = 160..192 | X 111.504..117.411; Y 173.953..180.171 | Clip registers disagree with producer geometry |

These point to an empty-scissor case and clip rounding at pixel centers/edges. That is an investigation hypothesis, not permission to accept arbitrary unknown packets or signal completion without validating their geometry and memory references.

## Native instruction comparison

The corrected replay uses the browser input API, reanchors the v39 touch timestamp to restored device ticks, releases stale input, then delivers the real Settings tap. Both samples use the same native core, direct RAM writes and 5-million-instruction frame intervals. Each runs for 405 million additional instructions. The accelerated starting Home comes from the genuine boot/dismiss sequence above; the software reference is the current USB-matched pristine Home.

For each run, the offline analyzer samples the same pixels below the status bar every four pixels and finds the first frame with less than 1% of samples differing by over 25 channel levels from that run's final Settings image, sustained through the remaining frames. The final images are also visually reviewed as Settings.

| Single native replay | First sustained final-layout match |
|---|---:|
| Software composition | 255 million retired instructions |
| MBX-enabled composition | 105 million retired instructions |

This is a 60% reduction in guest instructions for this bounded observation, **not** a browser latency measurement, repeated statistical result, or full-speed acceptance. Its resolution is 5 million instructions. The accelerated run includes one render rejection; incomplete/transient drawing can artificially reduce work and must be fixed before interpreting the gain as shippable.

The final Settings images have 818 differing RGB pixels among 147,200 below the status bar. After the isolated correction, all 818 differences are exactly one channel level; there are no larger differences below the status bar. This is small raster-rounding disagreement, not exact byte equivalence. The comparison is recorded in `pixel-comparison.json`.

Earlier raw `iphone1 --at ... touch up ...` sequences did not dismiss the alert. Inspection found that the command-line dispatcher submits BREAK as a still-present contact, while the Z1 API intentionally interprets its argument as the full active contact set and derives phases itself. Explicit up therefore remains held; its separate auto-tap/lift path correctly supplies an empty set. The command-line snapshot loader also omits the browser's uptime reanchor, but that alone was not established as the cause. These raw sequences are excluded from the navigation comparison. The API replay releases the contact correctly, applies the existing restoration correction, and demonstrably dismisses the alert; no touch-model change was made for this probe.

## Reproduce and next gate

```sh
python3 tools/firmware/rootfs_work.py \
  .context/organizer-debug/rootfs-compact168-checked.img \
  .context/graphics-speed/rootfs-mbx2d.img \
  --launchd-env com.apple.SpringBoard.plist=LK_ENABLE_MBX2D=1,LK_ENABLE_OGL=0
S5LBOX_MBX_TRACE=1 .context/build-mbx/tools/iphone1 --board m68 --ram-mb 256 \
  --kernel web/firmware/kernel_1A543a.macho \
  --devicetree web/firmware/devicetree_m68ap.bin \
  --ramdisk .context/graphics-speed/rootfs-mbx2d.img \
  --debug-enabled --cmdline 'debug=0x8 serial=3 rd=md0 -v' \
  --steps 10000000000 --save-at 10000000000 .context/graphics-speed/cold.snap \
  --serial .context/graphics-speed/cold-serial.log \
  --screenshot .context/graphics-speed/cold.ppm \
  > .context/graphics-speed/cold.log 2> .context/graphics-speed/cold.err
```

`rootfs_work.py` needs its documented Python cryptography dependency. This workspace installed it into `.context/graphics-speed/python` and supplied that directory through `PYTHONPATH`. The replay harness, all frame samples, trace logs, snapshot fixtures and `instruction-comparison.json` are retained in the same evidence directory.

Next: derive the actual producer's clip arithmetic and test isolated renderer corrections against the unchanged captured commands. Preserve strict failure for unsupported packets. Require zero rejected commands/driver restarts on the measured transitions, inspect complete frame sequences and final pixel differences, then compare actual Chrome against the same clock/input configuration. Production remains on software composition until those gates pass. Milestone 040 records the nonvisual protocol finding.

## Isolated correction and verification

The genuine OS 1.0 MBX2D binary resolves the rounding question. `_mbx3DCtxQuadCopyPerspective` at `0x30b389fc` loads the exact `0x3f000000` (`0.5f`) literal at `0x30b38dec`. Instructions `0x30b38a00` through `0x30b38a60` add that same value to each clipped extremum, convert through VCVT.U32.F32, and align X to eight pixels and Y to sixteen. The current decoder's `0.468505859375` / `0.531494140625` pair comes from the separately measured later producer. Disassembly and the original extracted framework are retained as `MBX2D`, `quad-producer-dis.txt` and `mbx2d-symbols.txt`.

The isolated `mbx.c` copy accepts either complete independently derived clip pair. It does not mix rules between axes, accept arbitrary clip registers or branch on a machine/firmware name. Pixel-centre coverage remains independently checked. An empty tile rectangle is allowed only when coverage is empty; source allocation, target boundary and mappings still validate, and unsupported state still fails without completion. Exclusive tile bounds avoid underflow at the empty right edge.

All 1,803 existing MBX checks pass. Two captured-command regressions add 46 checks, including the original Settings minified quad, the compact right-edge empty scissor, exact expected pixels, completion/acknowledgment, unrelated clip bounds, unknown texture state and missing source/target mappings. The unchanged renderer fails four new positive assertions; the isolated correction passes all 1,849 checks. These diagnostic tests currently live in `.context/graphics-speed/test-os1.c`; they must be promoted alongside any eventual production change.

A fresh genuine cold boot on the corrected isolated core completes all 870 submitted 2D commands and 1,698 3D renders with **zero rejected commands and zero Graphics Restart messages**. An API dismissal of Repair then completes 243 2D and 469 3D operations, also without rejection, producing clean Home in `fixed-dismiss.snap`. Raw CLI explicit-up behavior is separately being corrected by the harness owner and is not used as the acceptance input path.

Replaying the same pre-correction Home through corrected Settings completes 433 2D commands and 596 3D renders with zero rejection. Its final-layout instruction marker is 110 million, versus the software sample's 255 million. The larger draw count than the failed initial replay is expected: accepting the valid render lets the guest finish work that its earlier graphics restart interrupted. This corrected native sample is a 57% reduction in guest instructions, still not a browser latency claim. Its final Settings image differs from software only in the one-level rounding pixels described above.

The strict renderer correction and both captured-command regressions are now integrated into the production core after independent review. All 25 native suites pass, including 1,849 MBX checks and 51 native input checks. The accelerated work image and default snapshot remain isolated; production firmware and manifest are unchanged. Next gate: package a consistent isolated WASM candidate, exercise actual Chrome on the fresh corrected Home, and measure complete Settings/Home frame sequences under the same worker clock configuration as the software reference. Broader app/scroll/persistence and snapshot gates remain necessary before default promotion.


## Browser gate in progress

The frozen isolated software and corrected-MBX WebAssembly bundles use the same production foreground clock, USB compatibility detection, 20 ms touch hold, worker, and frontend. Their only diagnostic additions export cumulative 2D/3D submitted/completed/rejected counters. `browser-bundle-hashes.json` records every bundle file.

| Artifact | SHA-256 |
|---|---|
| Software WASM | `fce564bed916f9e8dbf81b777c226b3e5b8873ab2b383b0d20fb8ff9dc4e73e9` |
| Corrected MBX WASM | `3ae9189d91780618033e6727968344f69517452fb613d183ced976dd9d04745b` |
| Fresh corrected MBX Home snapshot | `764486ea35ba9832f35c8118397f361026ed21119295321e0a29519509de1244` |
| Software USB-enabled Home snapshot | `28591a3003b8677469b2e506185792e26ac128ba9383b7f110959f4f4fb0c82d` |

The first four-round Chrome run reaches Settings in 4.20 / 3.67 / 3.95 / 3.85 seconds and Home in 1.15 / 0.95 / 0.95 / 0.90 seconds. All final Settings/Home screenshots are retained in `.context/graphics-speed/chrome-mbx`; the Settings layout is recognizable and visually correct. Cumulative counters show 2,671/2,671 completed 3D renders but two rejected 2D commands already present at the first measurement. Their timing and cause are under investigation; they must not be concealed by the final screenshot.

These absolute timings cannot be compared directly against the older ~3-second software measurements: the VM was resumed/recreated and current software control is also slower. A contemporaneous alternating pair, command diagnosis, scrolling, and save/import gates are pending. This is **not yet a demonstrated browser acceleration milestone** and the MBX-enabled default has not been promoted.


The first contemporaneous software-control run completed two Settings/Home rounds, then its third Home return stalled with the real app icons visible and the dock missing. The benchmark correctly failed its Home-layout match after 90 seconds; it did not count this incomplete screen as ready. `chrome-software-current/2-home.png` and `report.json` preserve the failure. The software telemetry at that point was 54/54 completed 2D and 75/75 completed 3D commands, with zero rejects. This is a separate pre-existing software-path visual gate, not evidence that the isolated MBX correction caused the failure.


A fresh quiet two-round pair, with the other compiler replay and root builds held, gives the following bounded comparison. Both use the same Chrome binary and recording configuration; software and MBX runs are sequential, so this is a small contemporaneous pair rather than randomized statistical acceptance.

| Action | Software, first / repeated | MBX, first / repeated |
|---|---|---|
| Settings ready | 5.40 / 4.35 s | 4.65 / 3.56 s |
| Home ready | 1.40 / 1.25 s | 1.20 / 0.90 s |

The observed Settings improvement is 14–18%, substantially smaller than the native instruction reduction because rendering now consumes host work and animations use foreground time. The final displayed Settings pixels differ at only 787 of 129,300 RGB pixels below the status bar, all by exactly one channel level; there are no larger differences (`chrome-pixel-comparison.json`). Reports and original-speed recordings are retained in `quiet-software/` and `quiet-mbx/`. The rejected-command and broader interaction gates remain open.


The additional candidate persistence replay passes all 24 checks (the existing 23-check suite plus an explicit MBX-fixture import). Raw export, repeated compressed saves with previous-generation recovery, invalid-import preservation, actual reload, valid import, paused state and physical ringer restoration all pass. Sampled Settings pixels match exactly after reload/import. Headless Chrome did not emit native tab-switch blur; the suite records its synthetic blur fallback instead of claiming an operating-system blur test. Full report: `persistence-mbx/report.json`.

A genuine Settings → General → drag/bounce → Home run completes 1,319/1,319 2D and 2,115/2,115 3D operations with no rejection or driver restart. A second run scrolls the longer Settings list to Mail/Phone/Safari/iPod/Photos and back to Airplane/Wi-Fi/Usage, with visually correct displaced rows and complete Home. This second run reproduces the intermittent launch rejection: a three-command 2D batch at ring offset `0xe198` fails the existing “decoded BGRA8 or ARGB1555 unity simple-copy” gate. No further rejects occur while scrolling. Its diagnostic and original recording live in `diagnostic-scroll/`.

The quiet two-round accelerated comparison itself has zero rejected commands (929/929 2D, 1,151/1,151 3D). Another four-round diagnostic completes 1,958/1,958 2D and 2,322/2,322 3D with no rejection. These successful repeats do not erase the intermittent failed launch: the precise rejected command words and worker responsiveness during active raster work are the remaining immediate gates before accelerated-default promotion.


The first active-animation responsiveness observation acknowledges Pause in 16.1 ms and Resume in 5.0 ms. Its actual framebuffer capture shows the small Settings window expanding between moving Home icons, not an idle app. The diagnostic maximum is 11.4 ms per `emu_run(50000)` and 13.1 ms per complete worker slice, showing that bounded instruction chunks do not establish a strict 6 ms wall-time bound. This is one successful sample, not a latency distribution. The next harness action did not complete and is being guarded against clicking a disabled Pause button: production currently disables Pause throughout save/compression, and automatic saves run every 30 seconds. That disabled-control interval must not be described as emulator worker execution latency.


The guarded responsiveness replay directly confirms two disabled-Pause exclusions during background save. Its two valid mid-animation samples acknowledge Pause in 14.7/15.2 ms and Resume in 5.7/6.4 ms. With concurrent browser work, the diagnostic maximum `emu_run` and slice reaches 50.8 ms; these are observed maxima under that load, not a promised bound or a timing attribution to raster work alone. The original valid sample above had less contention. The root implementation is addressing the ordinary-save Pause restriction separately; frozen graphics comparison bundles retain their measured frontend.

An eight-round diagnostic repeat completes 3,601/3,601 2D commands and 4,647/4,647 3D renders with no rejection, retaining complete Home every time. Exact rejected-command logging is enabled, but that successful run contains no rejected packet to decode. A diagnostic-only manifest now boots the same corrected MBX Home directly, avoiding a software startup followed by UIImport for the bounded fresh-launch investigation. The compared software/MBX bundles remain frozen.


## Captured remaining 2D command variant

The first launch from the diagnostic direct-MBX startup reproduces the rejection and preserves its exact packet. The three-command batch starts at ring offset `0xdcf0`; its failing copy starts at aperture offset `0x00a0dd30`:

```text
a0060500 018d7000 94060500 01adb080 30000000
00000001 01400000 01e00014
60800200 8004cccc ffffffff 00000014 014001e0 70000000
```

The words following this packet belong to the next command and are not part of the 14-word sequence above. Compared with the existing unity-copy form, this contains three extra words before the familiar copy controls and sets an additional `0x00040000` mode bit. The extra values can be read as a clip tag plus X=0..320 and Y=20..480, matching the content area below the status bar. This is a decoding hypothesis until confirmed against the genuine MBX2D producer; the renderer must not accept it by merely skipping arbitrary words.

`fresh-launches/report.json` retains the diagnostic, genuine first-Settings screenshot and original screen recording. The run counts 367/370 completed 2D commands (the complete three-command batch is rejected atomically) and 350/350 completed 3D renders. `rejected-command-capture.txt` preserves the exact log independently. The accelerated default remains unpromoted. The next bounded graphics task is to derive and test this clipped-copy packet variant, then repeat the failed fresh-launch gate and broader rendering checks.


## Strict clipped-copy correction

The genuine `_pack2DCtxBlitCopy` producer confirms the captured layout. A nonzero context byte at `+0x4c` reserves three additional words at `0x30b3a99c..0x30b3a9ac`; `0x30b3ad00..0x30b3ad58` emits tag `1`, packed left/right and top/bottom, and advances the cursor by 12 bytes. The same flag independently adds mode bit `0x40000` at `0x30b3ae50..0x30b3ae6c`. The preceding float operations intersect the clip and destination; their strict `>` tests allow equality at an edge. `clipped-copy-producer-dis.txt` and its standalone disassembler retain the evidence.

The isolated correction requires the complete tag/mode/rectangle combination and the known source descriptor, unity scale, unrotated copy mode and colour constant. It preserves the source-to-destination offset when clipping removes pixels from the left/top, validates only the bounded visible footprint, and permits an empty intersecting clip only with known mapped allocations and the existing batch alias policy. Compact clipped commands contain 14 words, while padded forms can contain up to 19; the existing ring-end and atomic-batch rules are retained. Unsupported clipped blend or rotated forms still fail.

All **1,849 existing plus 50 new checks pass (1,899 total)**. The new checks cover the actual captured full framebuffer and status-bar guards, nontrivial source offsets, an off-screen original rectangle with bounded visible pixels, mapped empty clips, malformed tag/mode/source/geometry, missing late source and destination PTEs, compact old/new packet mixtures, aliases, atomic late failure, ring wrap and ARGB1555 offset/conversion. The unmodified renderer fails nine positive assertions in the first 48-check addition, demonstrating that these are not tests that already passed before the correction.

Reviewable patches are `.context/graphics-speed/clipped.patch` and `clipped-tests.patch`; the standalone test is `test-clipped.c`. The first corrected browser bundle has WASM SHA-256 `693bb22da858dc55e5b50296c4dc4b62c17cd9bda32ffd76734730012a1b786a`. Fresh launch and wider browser gates are running; the accelerated default remains unpromoted pending their results and independent review.


The clipped correction is now integrated after independent review: all 25 native suites and production WASM smoke checks pass. Eight independent corrected direct-MBX fresh launches complete **2,960/2,960 2D and 2,953/2,953 3D** operations with zero rejects. An additional diagnostic counter then proves the new decoder is reached by the genuine browser guest: the 750 ms idle-start launch validates two clipped copies while completing 330/330 2D and 334/334 3D operations. Its final Settings screenshot and original recording are in `clipped-path-proof/`. The counter bundle has WASM SHA-256 `90943284a190de6d923d8d15df384265a62b7565e9ee0ffd46dfe15e2a216dcb`. The counter is diagnostic-only and is not added to production device state.

Actual displaced Settings scrolling and Home return on this corrected counter bundle complete 1,119/1,119 2D and 1,611/1,611 3D operations, without rejection. The new persistence run prints all 24 UI assertions as passing, but an added diagnostic metric contained a JavaScript BigInt and the report serializer failed after the browser closed. The diagnostic serializer is fixed, and the complete rerun passes all 24 checks with a clean report in `clipped-persistence-verified/report.json`. This includes repeated compressed saves, prior-generation recovery, raw export, valid and invalid import, actual reload, Settings pixels, physical ringer state and paused state; the headless blur limitation remains explicitly recorded. The previously passed renderer/persistence recordings and original failed-report artifacts are preserved.

## Final identical-core speed comparison

A later quiet pair uses byte-identical corrected WASM and worker files for both arms; only the genuine starting snapshot/disk setting differs. It starts each phone directly, avoiding fixture-import timing, and records two actual Settings/Home rounds.

| Action | Software, first / repeated | Corrected MBX, first / repeated |
|---|---|---|
| Settings ready | 4.25 / 4.25 s | 4.55 / 4.05 s |
| Home ready | 1.70 / 1.25 s | 0.90 / 0.95 s |

This pair shows mixed Settings results (about 7% slower first launch and 5% faster repeated), while both Home returns are faster. The earlier 14–18% Settings gain did **not** persist as a general conclusion across runs. Reports and original recordings are in `final-quiet-software/` and `final-quiet-mbx/`; these are bounded observations, not release p95 or full-speed acceptance. Host rendering and other guest work remain significant. Wider Safari/Contacts/Calendar navigation on the integrated production renderer and the counter candidate is the next default-promotion gate.


## Broader app rendering gates

The integrated production browser was tested with an explicitly imported accelerated snapshot, while the production default remains software. All three complete Home returns match their reference exactly. Contacts opens and its new-contact form visibly scrolls. Calendar Month and Add Event render, but Day is blank apart from the scroll indicator. Safari’s initial bookmarks transition is incomplete. These are failures to fix, not successful app launches inferred from the absence of JavaScript errors (`broader-production/report.json`).

Separate fresh-Home runs on the diagnostic candidate isolate each app, preventing Safari’s failure from contaminating Calendar/Contacts observations:

| App | 2D completed / submitted | 3D completed / submitted | Observation |
|---|---|---|---|
| Phone → Contacts | 71 / 71 | 248 / 248 | Visible Contacts, zero rejects |
| Calendar → Add Event | 97 / 97 | 363 / 369 | Six rejects: tall texture allocation/split-pitch and UV bounds |
| Safari → attempted Done | 88 / 88 | 322 / 335 | Thirteen rejects: new solid-quad controls |

Exact rejected 44-word records, registers, screenshots and original recordings are retained independently under `.context/graphics-speed/app-diagnostics/{calendar,phone,safari}/report.json`. Calendar’s header `a6818000` carries a larger height exponent than the current decoder’s three-bit field; the genuine producer and safe sampled-memory bounds are under review. Safari’s separate solid form includes control `a6106673`; root is independently investigating that witness. No unknown draw is acknowledged as a successful render. Accelerated-default promotion remains closed until these app gates pass.

Milestone 042 records the verified clipped-copy correction and actual candidate Settings launch. Its scope deliberately excludes full app coverage and a general Settings speedup.


## Calendar tall-source correction under review

The fresh Calendar witness samples rows 694–1064 of a 2048-row allocation into destination rows 109–479. A second genuine resized record samples rows 694–1016 from a 1024-row allocation. The original `_mbx3DCtxQuadCopyPerspective` computes independent height exponents at `0x30b38a68..0x30b38ab8`; exponent 8 uses header bit 23, which the old three-bit decode discarded. The isolated correction admits heights through 2048 only for the already defined unfiltered, non-affine perspective crop with a visible source rectangle at most 320×480. Filtered, compact and affine tall textures remain unsupported. Sampling still stages only the actual checked source window.

The two exact captured records add 66 checks: **1,965 pass**, including all earlier 1,899. The old decoder fails four positive assertions, covering 118,720/103,360 output pixels and their completion events. Malformed 4096-row allocations, coherent UV shifts outside allocation, duplicate fields and clip/tile corruption remain atomic. The final combined tests below additionally exercise late source/destination page failures on both tall captures. Reviewable artifacts are `calendar.patch`, `calendar-tests.patch` and their base/source files.

Actual Chrome on this isolated module now shows the Day hour labels (4 PM–11 PM), then moves to 10 AM–6 PM when dragged. Through this scroll it completes 91/91 2D commands and 402/402 3D draws without rejection (`calendar-corrected-browser/report.json` and screenshots). Switching Month exposes a further 8×18 gradient stretched to 320×18 by the known modulated bilinear sampler. Its exact record is retained; this separate horizontal-scaling gate is being corrected and tested rather than counted as a successful whole-Calendar run.

The final quiet Settings visual oracle has also been repeated independently on both rounds. Each compares 129,300 displayed RGB pixels below the status bar: 128,513 match exactly, 787 differ by one channel level, and none differ by more (`final-quiet-pixel-comparison.json`).


The combined Calendar candidate also recognizes the captured Month gradient: a complete 8-pixel texture width stretched to 320 pixels with integer source rows and unchanged 18-row height. It reuses the established modulated bilinear sampler, while requiring the complete-width invariant and preserving cropped-strip, vertical-scale and unrelated-family rejection. A first overly broad horizontal exception was rejected during development because it accepted an existing malformed Spotlight crop; the final complete-width rule retains every earlier regression.

**2,016 native checks pass (1,899 existing plus 117 new)**. `calendar-v2.patch` and `calendar-v2-tests.patch` are the combined correction against `calendar-base.c` and `calendar-test-base.c`, not sequential patches after the earlier tall-only files. Final isolated source SHA-256 is `432a72ab9c1ef50d977cdf83c3d76be4b33dab5ac6eca20964ba1b93c86fa9d3`; final test source is `7d37d9fc1fc004c9959d672a06a7ef4b7912e32a89a7f7355205bdb1f0108291`. Candidate WASM SHA-256 is `fd6b11ee643195f11137217af872f0f6e685c2b98627fd1f08f42570ab9eed30`; core and worker clocks remain unchanged from the frozen graphics candidate.

The complete fresh browser replay now passes **Day → hour scroll → Month → Add Event → Cancel → Home**, completing **449/449 2D commands and 1,559/1,559 3D renders, zero rejects**. Semantic screenshot review confirms the day’s hour labels, displaced rows, full December month grid, event editor fields and complete dock. Home matches its reference exactly (difference 0). Original recording, all screenshots and machine-readable results are in `calendar-v2-corrected-browser/report.json`. This is a functional navigation gate with fixed waits, not a speed measurement or proof of creating/persisting an event. The earlier candidate persistence suite passed; integrated-production Calendar regression remains pending root integration and the independent compiler timing window.


A final test-coverage audit extended the helper’s late-page block to the two tall unfiltered captures themselves (it previously ran only for scaled fixtures). Both now explicitly reject coherent nonunity geometry, a missing final sampled source page and a missing late destination page, preserving the first/last output guards and withholding completion. The updated combined total above is 2,016; the browser module is unchanged because this last adjustment only strengthens tests.


The combined renderer is now integrated into the production WASM. A separate actual-production replay imports the same genuine MBX snapshot through the UI and repeats Day → hour scroll → Month → Add Event → Cancel → Home successfully. Human screenshot review confirms all screens and the final Home comparison is exactly zero. The production seed engine remains enabled through imported generation 2, with 510 installed regions and 318,264,019 retired instructions by final Home (`calendar-integrated-production/report.json`). Production does not expose GPU diagnostic counters; zero-rejection totals remain the separate isolated-candidate proof, rather than an invented production measurement. The default manifest still selects software composition.

Milestone 046 contains the original-speed complete Calendar navigation recording and its actual scrolled-hour thumbnail.


## Safari opaque solids, separators, and address transitions

Milestone 050 records a complete genuine accelerated Safari workflow: imported MBX Home → Bookmarks → Done → page → address keyboard → complete Home. The final isolated combined browser completes **296/296 2D and 1,699/1,699 3D commands, zero rejects**, with exact Home pixels below the status bar and independent semantic screenshot review. Evidence is `.context/safari-solid/browser-scales/`; the maintained recording and thumbnail are linked from the milestone. The production phone's default composition setting remains software pending the broader graphics audit.

The new `0xa6106673` solid mode comes from the original OS1 LayerKit/MBX2D premultiplied source-over producer. Only alpha 255 is admitted initially. Its main TL/TR/BL/BR vertices are distinct from the separate BL/TL/BR/TR integer clip. Bounded finite world geometry intersects that clip and the 320×480 screen before the producer's uniform +0.5 integer conversion and 8×16 tile alignment. The first captured world rectangle y410.074..826.074 has an integer clip y410..411; the decoder fills only row 410, never the coarse 400..416 tile or the offscreen world extent. The old `0xa6104620` mode preserves its earlier geometry, sign, alpha, and bias behavior.

The retained 0x600-byte solid object has SHA-256 `808850396cc94b9b550de10947a08126228e50ce38a33434756f8e2ea14e094f`; the test array matches every byte, relocating only the two framebuffer-address fields into the synthetic GART. Direct decoding of the actual saved rejection additionally confirms 320 pixels and zero mismatches across the full framebuffer. Tests include smaller clips, negative/offscreen world intersections, screen bounds, coherent NaN/infinite geometry, fractional/nonfinite/reversed boundaries, normalized/color mismatches, unsupported fractional alpha, late parameters/tiles, and a missing last destination page after earlier rows map correctly.

Actual animation phases also demonstrate rounded zero coverage: y71.5795..72 becomes 72..72, for example. A valid no-op still requires all structural state plus the entire screen-clipped integer boundary's GART footprint before completion. Producer BGT comparisons also preserve exact touching edges, while disjoint rectangles remain rejected. Exact half-pixel raster ties and fractional-alpha solid rounding are not newly established by these captures.

Four constant-V separator captures use the known modulated half-texel sampler with a 512×8 allocation, 320-pixel pitch, U0..320, V=0.5 texels, and 320×1 destination. The unchanged sampler evaluates row 0. Five subsequent address-transition captures establish two additional narrow forms: modulated horizontal reduction of a 290×31 conservative source envelope in a 512×32/pitch296 allocation with UV(.5,.5)..(289.5,30.5), keeping its 31-row envelope height; and the known alternate sampler at unity 320×2 with a 512×8/pitch320 allocation, U0..320, V.5..1.5. These are explicit admission checks around the existing sampler/staging/blending implementation, not generic transform acceptance.

The final combined native suite passes **2,428 checks**. Constant-V negative control fails eight positive pixel/completion assertions across its four captures; the pre-address-scale combined core fails ten across its five captures. New coherent cropped-UV/changed-vertical-scale tests and existing late source/destination PTE, alpha, redundant-coordinate, tile, and control checks all pass. Reviewable separate and combined patches/sources are under `.context/safari-solid/` and `.context/safari-row/`. Earlier failed browser runs and their exact new command witnesses are preserved; the successful run does not erase those failure discoveries.


## Broader accelerated app discovery after Safari

A fresh normal-UI import of the MBX phone for each of all 16 apps found **13 clean bounded graphics workflows and three remaining families**. The combined Safari diagnostic build completed **5,688/5,688 2D commands and 14,676/14,695 3D draws**. Text contributed 12 rejected 3D commands, Photos two, and Clock five. No browser errors occurred; every final Home passed the sampled-image comparison. This is a discovery gate, not an exact full-frame oracle, app-functionality acceptance, or a speed measurement.

The sweep observed ten seconds after launch, five seconds after the listed basic control where applicable, and seven seconds after Home. All contexts imported `.context/graphics-speed/fixed-dismiss.snap` through the normal UI. The frozen combined diagnostic renderer and GPU counters ran on port 8102; other correctness work could run concurrently. Evidence, per-stage counters, genuine screenshots, manual observations, and the first failure's exported phone are in `.context/safari-solid/all-apps/`.

| App | Genuine screen/control reviewed | Graphics result |
|---|---|---|
| Text | New-message UI and compose/cancel probe | 12 rejected wide 32×26 texture draws; exact witnesses retained |
| Calendar | Day → Add Event fields → Home | Zero rejects |
| Photos | No Photos → Home | Two fractional-alpha `a6106673` solid draws rejected during return |
| Camera | Closed iris and no-room-to-take-pictures dialog after shutter | Zero rejects; preview/capture unavailable |
| YouTube | Featured and Cannot connect to YouTube dialog | Zero rejects; no control probe or playback |
| Stocks | Default symbol list and Error retrieving chart | Zero rejects; no control probe or live data |
| Maps | Search keyboard over blank map | Zero rejects; no tiles/results |
| Weather | City management, Cupertino/New York and °F/°C | Zero rejects; forecasts remain unavailable |
| Clock | World Clock → Stopwatch → Home | Five rejected mirrored constant-column draws during launch |
| Calculator | 2 + 3 displays 5 | Zero rejects |
| Notes | Done reaches No Notes list | Zero rejects |
| Settings | Scroll to Sounds through Photos | Zero rejects |
| Phone | Contacts tab, All Contacts | Zero rejects; no call/contact creation in this sweep |
| Mail | Other account editor with IMAP/POP/Exchange and server fields | Zero rejects; no account saved |
| Safari | Bookmarks → page → Home | Zero rejects; separate keyboard proof is milestone 050 |
| iPod | No Content; More probe leaves empty-content view | Zero rejects; More navigation/media playback unverified |

Text's wide extent, Photos' fractional solid alpha, and Clock's mirrored column are separate command families and are being corrected independently. Clock uses a modulated 8×32/pitch8 texture at constant U=.5 texel, V=.5..29.5, stretched to 380×30 and turned through 180 degrees. Photos uses premultiplied color `cc141414`, which deliberately exceeds the first opaque-only admission gate. The exact failure logs remain under `.context/safari-solid/{clock,photos}-witnesses.log`; successful final screens do not erase their driver restarts. **Software composition remains the default** while these broader gates and an actual speed benefit are verified.


## Text: a wide world rectangle with a small clip

Milestone 052 records the original Text composer, Cancel, reopening, and Home. The isolated candidate completes all 3,155 graphics commands without rejection; the reviewed decoder/test changes are now integrated and pass 2,470 native checks. A separate fresh run on the combined production WASM passes composer → Cancel → reopen → Home with active JIT, no errors or graphics-restart diagnostics, and exact equality across 144,000 raw RGB Home pixels below the status bar. Evidence is `.context/graphics-final/production-apps/text/`; the isolated command totals are not production GPU measurements.

The twelve earlier Text failures share the existing 44-word modulated half-texel perspective-copy sampler. A 32×32 allocation with pitch32 and integer UV0..32 by0..26 becomes a 6,848×26 destination (214× horizontally). Genuine producer inputs at MBX2D0x30b386e8 and LayerKit blit_quad confirm that unclipped world coordinates and independent integer scissor are intentional. The retained first-rejection batch has world−2763..4085 by450..476 but boundary19..233 by450..463, so only2,782 pixels may change.

Only this bounded wider family passes the old512-pixel extent gate: axis-aligned perspective/modulated/half-texel records, coordinates within−4096..4096, allocation32×32/pitch32, full32×26 UV rectangle and unchanged26-row height. Existing clipping, tile lists, source/target mappings, staged writes, interpolation, and blending remain intact. The source envelope remains tiny and framebuffer work stays bounded320×480. Coherent cropped UVs, vertical scaling and out-of-range geometry are rejected.

Evidence under `.context/text-mbx/` includes genuine producer traces/disassembly, immutable snapshot/binary hashes, the exact retained object, a full153,600-pixel replay oracle with zero outside-clip writes, baseline-negative and passing native logs, and the actual browser video/report. This establishes graphics navigation, not SMS delivery, general large transforms, or default accelerated composition.


## Photos fractional OS1 solids

Milestone 053 records the genuine No Photos → Home workflow, completing226/226 2D and549/549 3D commands without rejects. Every129,000 displayed Home RGB pixels below the status bar matches, with manual screen review. The original software-default phone remains unchanged.

The earlier opaque-only `a6106673` gate now accepts uniform premultiplied alpha. A dedicated OS1 solid equation follows the original CoreGraphics `_CGSBlendARGB8888toARGB8888` at0x3056c020: source + floor(destination × (255−alpha)/255). Executing its unmodified936-byte ARM function validates655,360 complete pixels/30,286,848instructions across vector-four/scalar-one and opaque/varying-alpha destination cases. Software-reference rounding is explicitly distinguished from unmeasured MBX silicon precision. Other producer math is unchanged. Geometry, integer clipping, tile/parameter validation and atomic mapped staging remain intact; zero alpha never bypasses validation.

Integrated native total2,725 passes; the old2470base fails255 newly admitted nonopaque cases. Current tests cover everyalpha/destinationbyte, untouched pixels, fractional zero-coverage late parameters and fractional late destination pages. `.context/photos-mbx/` preserves source/function hashes, original ARM oracle/disassembly, native positive/negative logs, two independent reviews, browser JSON/screenshots and original recording. The initial wrong-port browser run and wrong opaque-output oracle flag are retained as corrected harness failures. A fresh combined-production replay now independently confirms genuine No Photos and exact144,000 raw RGB Home pixels, advancing JIT and no errors/restart diagnostics (`.context/graphics-final/production-apps/photos/report.json`). Production has no GPU counters. Photo import/viewing remains unimplemented.


## Clock mirrored constant-column correction

The captured World Clock launch packet `a0218001` samples U=.5 texel in an 8×32 allocation with eight-pixel row pitch and V=.5..29.5. Its 1×30 conservative source envelope is stretched to 380×30 and turned through 180 degrees; the genuine vertices retain a tiny rotation residual. A narrow admission rule reuses the existing inverse-affine bilinear sampler, requiring the measured allocation, pitch, UVs, opaque modulation, size and reversed orientation. The exact axis-aligned limit enters that same gate; other previously rejected reversed-axis forms remain rejected.

Focused tests additionally expose a zero-envelope admission bug in the old affine scale comparison: collapsed integer U can give source width zero, producing infinity/NaN during scale checks. Width and height must now be nonzero before division. Old-core negative control fails seven independent assertions: four positive pixel/completion assertions across the captured and exact-180-degree cases, plus coherent U=0/1/8 admission checks. Status is acknowledged between negative probes so failures cannot contaminate later assertions.

The Clock additions pass **90 checks**, including two 9,600-pixel affine fixtures, coherent UV/geometry/shear negatives and the existing late source/destination PTE, normalized-coordinate, modulation, tile and control regressions. The isolated 2,428-check base reaches **2,518/0**; the maintained combined Text/Photos/Clock renderer reaches **2,815/0**. Root reviewed the narrow gate before focused integration, preserving the other agents' changes.

Actual Chrome completes **World Clock → Stopwatch → Start → Stop → Reset → Home**, with **495/495 2D and 1,040/1,040 3D commands, zero rejects or page errors**. Human review confirms the genuine screens and digits; the full-RGB Home comparison below the status bar has zero pixels beyond its tolerance. This is a bounded graphics/control replay, not a clock-drift or speed measurement. Milestone 054 preserves the original-speed video and World Clock thumbnail. The complete witness, isolated patch, old-core negative control and browser report are in `.context/clock-mbx/`. Software composition remains the default while the combined all-app audit and speed gate run.


An independent fresh-import replay on the combined production WASM (`da43870…1314c6d`, offline `74d5bc97d495d04b7663c5b9`) also passes Clock: genuine Cupertino World Clock/map → Stopwatch → Start/Stop/Reset → Home. It verifies an unchanged stopped raw frame and exact RGB equality across all 144,000 Home pixels below the status bar. Compiled retirement advances from 30,414,594 to 452,316,027; there are no page errors or graphics-restart diagnostics. Production exposes no GPU counters, so its visual/lifecycle proof is separate from the isolated 495/1,040 command totals. The reviewed report is `.context/graphics-final/production-apps/clock/report.json`; milestone054 now records this integration result.

## Combined sixteen-app rendering gate

The combined Text/Photos/Clock renderer now passes a fresh-import graphics audit across **all 16 Home-screen apps**. Each app starts in a disposable browser context, imports the same MBX-enabled Home through the normal UI, waits 10 seconds after launch, performs its bounded probe where provided (five seconds of observation), and returns Home with seven seconds of observation. Human review covers all **61 captured screen stages**. Every app remains live and returns to a complete Home screen; no visible rendering defect or page error appears.

The diagnostic module is SHA-256 `341102761a85ece8945b7107f8eeca00ee558c36045ff791b4271cafb03618ea`, built from combined MBX source `37f384c1d9715aa745c9d2b8b8aef71343e8c486c8675b71aa37704e3e8c9589` and production runtime `da43870dab0e151d6a31597bb8ddbbfb3a480f7398ddb88a9334bea9b1314c6d`. Before testing, the served WASM on unused port 8127 was compared byte-for-byte with the isolated file, and the worker GPU metrics/API export were verified. Fixture SHA-256 is `764486ea35ba9832f35c8118397f361026ed21119295321e0a29519509de1244` (`.context/graphics-speed/fixed-dismiss.snap`). Positive JIT execution is confirmed at every captured stage.

| App | Completed 2D commands | Completed 3D commands |
| --- | ---: | ---: |
| Text | 678 | 1,267 |
| Calendar | 359 | 936 |
| Photos | 202 | 533 |
| Camera | 225 | 779 |
| YouTube | 466 | 1,282 |
| Stocks | 230 | 551 |
| Maps | 227 | 1,570 |
| Weather | 241 | 606 |
| Clock | 406 | 862 |
| Calculator | 284 | 1,103 |
| Notes | 487 | 1,091 |
| Settings | 878 | 1,277 |
| Phone | 201 | 607 |
| Mail | 513 | 942 |
| Safari | 275 | 1,030 |
| iPod | 198 | 466 |
| **Total** | **5,870** | **14,902** |

There are **zero rejected commands**. Totals add only the final `03-home` cumulative counter sample once per fresh phone. Every initial Home sample starts at zero; intermediate samples are checked for monotonicity, exact candidate/completion equality and zero rejects but are never summed. `.context/graphics-final/aggregate.py` reproduces these assertions and writes `all-apps/verified-summary.json`; `inventory.json`, per-app reports, original screenshots, contact sheets and `manual-review.json` preserve the actual evidence.

This gate covers rendering and bounded navigation, not full app functionality or speed. Camera still displays the genuine storage-capacity alert; network apps retain their connection/update/chart errors or empty map grid. iPod's More probe leaves its genuine No Content screen unchanged. Calculator actually displays 5 for 2 + 3; Calendar's hourly rows, Photos' rotated empty-library illustration, Clock's complete stopwatch digits, Notes' keyboard and Settings' scrolled rows are visibly correct. All final Home screens are complete; the coarse sampled Home difference is zero except three 0.000208 cases where the status-clock minute changed. This does not claim full-frame pixel identity.

Reproduce the frozen diagnostic discovery audit:

```sh
BASE_URL=http://localhost:8127 \
ARTIFACT_DIR=.context/graphics-final/all-apps \
FIXTURE_SNAPSHOT=.context/graphics-speed/fixed-dismiss.snap \
CHECK_HOME=1 OBSERVE_MS=10000 ACTION_MS=5000 \
node web/tests/inventory.mjs
```

The corrected Text, Photos and Clock families have their matching original-speed recordings in milestones 052, 053 and 054. Those recordings are focused demonstrations, not a recording of this complete sixteen-app audit. All audit and contact-sheet browsers were closed before releasing the next quiet CPU comparison window. This fresh-profile audit was followed by the continuous sixteen-app gate and quiet paired speed test below; the verified MBX package is now the default for new phones.


## Quiet paired browser comparison on the combined build

[Milestone 055](../docs/benchmarks/mbx-navigation-20260905.json) and its original-speed recording contain the final ABBA comparison on unchanged production WASM `da43870…1314c6d` / CPU seed `d9c66155…b3881`. Four fresh browser sessions import the corresponding software or MBX phone through the same UI, in software/MBX/MBX/software order. Each performs one first and ten warm Settings/Home rounds; twenty warm observations per action/configuration are aggregated. Other emulator/build jobs are closed; default autosave is active. Timings start at actual pointer release, with50ms image sampling and500ms stable readiness, reported at the stable interval's start.

Warm Home median improves1.142→0.867seconds andp951.267→0.918seconds (24.0%/27.5%). Warm Settings median improves3.453→3.294 andp953.882→3.567seconds (4.6%/8.1%). First Settings is4.714/4.765software versus4.209/3.865MBX. All88screens retain their content withinthreechannelvalues ofsame-run references; eight reference screens were manually reviewed. Raw timings and exact machine/engine/fixture identity are in [the durable report](../docs/benchmarks/mbx-navigation-20260905.json). Complete local artifacts are `.context/graphics-final/quiet-navigation/`.

The Settings release targets remain unmet and reference-laptop/animation/input acceptance remains separate. Both attempted CPU cache changes remain excluded after quiet comparisons failed to show repeatable gains. The verified MBX package is now the default for new phones; existing saved phones retain their boot state and graphics mode. The complete promotion and compatibility evidence follows.


## One continuous sixteen-app phone

The combined MBX renderer also passes a **single uninterrupted 627.55-second browser session**: one normal fixture import, one worker, all 16 genuine apps, bounded probes and 16 exact Home returns. All46 screenshot stages were manually reviewed. Milestone 056 now supplies the matching **complete original-speed MP4 and JPG**, without stitching independent app sessions. This is rendering/navigation continuity, not full app functionality or a latency benchmark; functional work ran concurrently.

The final cumulative counters are **6,489/6,489 2D and 16,850/16,850 3D, zero rejects**. Initial counters were all zero. Unlike the earlier fresh-phone audit, this run has only one guest: subtract its initial sample from its final sample exactly once. Intermediate samples are checked for monotonic counters, completion, zero rejects, positive compiled execution and unchanged JIT generation 2; they are not summed. The 16 Home checks compare **2,304,000 native RGB pixels in total, with zero changed pixels**. All exclude the changing status bar. WASM memory capacity remains 643,039,232 bytes throughout; this is allocated linear-memory capacity, not a live guest-allocation or memory-leak measurement.

| App | Approximate recording time | Reviewed result |
|---|---|---|
| Text | 00:57 | New Message and complete keyboard; Cancel returns to empty Text Messages list. No SMS send/receive. |
| Calendar | 01:36 | Day shows complete hourly rows; Add Event displays Cancel/Done, Title/Location, Starts/Ends, Repeat/Alert/Notes. No event created in this run. |
| Photos | 02:05 | Photo Albums with complete rotated empty-library illustration and No Photos. No album populated. |
| Camera | 02:44 | Closed shutter iris with genuine insufficient-room alert; shutter probe leaves alert unchanged. No preview/capture success. |
| YouTube | 03:15 | Featured/No videos with Cannot connect to YouTube alert. No network catalog or playback. |
| Stocks | 03:43 | Stock list and chart-range controls render; Error retrieving chart. No quotes or chart data. |
| Maps | 04:21 | Empty grid and controls; search focus shows complete keyboard. No tiles or routes. |
| Weather | 05:03 | Cupertino forecast card says Update failed; city management shows Cupertino/New York, delete and unit controls. No forecast or city edit claimed. |
| Clock | 05:41 | World Clock and changing analog second hand render correctly. Stopwatch probe stays on World Clock in this continuous run; no Stopwatch action success claimed. Prior focused milestone 054 is separate proof. |
| Calculator | 06:25 | Display begins at0 and genuine2+3 sequence shows5. |
| Notes | 07:04 | New Note and full keyboard; Done returns to No Notes. |
| Settings | 07:45 | List scrolls from Airplane Mode/Wi-Fi/Usage to Sounds/Brightness/Wallpaper with General through Photos below. Exact scroll position is not an image oracle. |
| Phone | 08:22 | Keypad and empty All Contacts view render correctly. No calls or contact edit in this run. |
| Mail | 09:00 | Provider screen followed by complete Other account setup with IMAP/POP/Exchange fields. No account configured or mail sent. |
| Safari | 09:38 | Initial Bookmarks followed by empty browser/address field and toolbar after Done. No page loaded. |
| iPod | 10:16 | No Content illustration and library tabs render; More probe stays on No Content. No media playback or More-menu success. |

Clock's Stopwatch tap does not visibly switch tabs in this recording. Its cause is not established by this run, and no Stopwatch action success is claimed here. The earlier focused production Start/Stop/Reset proof in milestone 054 remains separate. Camera's shutter is blocked by its actual capacity alert and iPod's More probe remains unchanged. Those limitations are visible in the uncut recording.

The accepted harness retains live GPU/JIT/worker continuity, original complete Home checks and an explicit Calendar Add Event region check, using a freshly captured and manually reviewed editor reference at the same viewport. The old Day frame fails that predicate on 10,377/12,800 pixels; the accepted editor region matches exactly. Other app actions are reviewed manually. The source is frozen for the run, with 180ms touch pulses, 10s launch/5s action observations and bounded Home polling. These deliberate waits and screenshot overhead must not be treated as latency measurements.

Earlier attempts are preserved in `.context/graphics-final/continuous/attempts.md`: the initial 80ms/2s Calendar probe missed its editor; two reference attempts mixed CSS-scaled pixels or a transient raw frame with a later screenshot; a third rejected a valid Settings scroll because its exact resting position differed. These are explicit probe/harness failures, not renderer failures. The final capture reports raw changes across screenshot acquisition instead of assuming simultaneity; Text/Maps cursors and Clock hands move between captures, while Home stays exact.

Provenance remains diagnostic WASM `341102761a85ece8945b7107f8eeca00ee558c36045ff791b4271cafb03618ea`, unchanged production renderer/JIT build `da43870dab0e151d6a31597bb8ddbbfb3a480f7398ddb88a9334bea9b1314c6d`, MBX fixture `764486ea35ba9832f35c8118397f361026ed21119295321e0a29519509de1244`, ABI 1 seed `d9c661556ab17bb8433ed9b0c3ecfd52e2fcafe58221b4cb84a920e4976b3881`. The diagnostic adds counters to the same accepted core. Default-package promotion is a separate root-owned step.

Reproduce/inspect workspace evidence with `node .context/graphics-final/continuous/run.mjs` (frozen diagnostic server 8127 and retained Calendar reference required), then `python3 .context/graphics-final/continuous/verify.py`. Accepted artifacts are `accepted-run/{report.json,manual-review.json,verified-summary.json}`, all 46 screenshots/native captures and the original browser WebM; the public recording is the original-speed H264 transcode linked above.


## Accelerated graphics is the new-phone default

The exact measured MBX package is now installed in `web/firmware` with its matching manifest. Rootfs SHA256 is `73d0d2a0ed4564dc16c67c7c6efc167725d2833461cd47919faa676595c0ded9`; Home snapshot SHA256 is `764486ea35ba9832f35c8118397f361026ed21119295321e0a29519509de1244`. Deterministic gzip parts decompress to the exact raw bytes. New file inodes preserve frozen older experimental copies; the old package is also retained in `.context/graphics-final/previous-default/`. Production WASM and seed are unchanged. Offline bundle: `75f559b623ef8143bb518064`.

Before promotion, the exact private preview passes 65 browser checks: 23 persistence, six offline recovery, six physical controls, 12 clock lifecycle and 18 actual firmware-upgrade checks. The latter changes software-rendered default assets to MBX assets while proving the older saved checkpoint's bytes and metadata remain identical, including disconnected reopening. The continuous sixteen-app rendering gate completes 6,489 2D and 16,850 3D commands with zero rejection; all sixteen returns Home compare exactly across 2,304,000 native RGB pixels. A missed Clock tab tap is explicitly unverified in that continuous recording, while focused production Clock Start/Stop/Reset remains separately passed.

A fresh load from the actual production server after promotion verifies the served manifest/WASM, requests the new snapshot without Import, executes the seeded engine and matches 144,000 reviewed Home RGB pixels exactly. The development server had stopped before the first connection attempt; that failed attempt is retained, and the restarted server passes. [Milestone 058](../docs/traces/accelerated-default/notes.json) includes the fresh-phone controls recording/thumbnail and [durable package evidence](../docs/traces/accelerated-default/verification.json). These are functional checks under concurrent work, not new latency samples.

Existing saved phones retain their data and graphics mode. Export makes a backup; Reset starts a fresh accelerated phone. Reimporting an old backup restores its original mode. A general data-preserving migration remains open, along with Settings speed, animation/input acceptance and hardware/services.

### 2026-09-06 — actual default Safari source-row blocker isolated

The failed default Safari snapshot retains 905 BGRA8 copy commands with 472-pixel source pitch, rejected by the existing display-width source cap. An isolated bounded correction passes all 2,847 native and actual WASM MBX checks, including 32 new row/mapping/atomicity checks; the unchanged decoder fails four positive gates. Independent actual-memory row audits match all 4,583,272 staged bytes without executing the guest. Exact released RTC baseline relink and unchanged JIT ABI/seed checks pass. [Evidence](../docs/traces/safari-stride472/README.md).

Actual filesystem evidence proves prior091 used software graphics while the failed default phone uses MBX2D. Its missing local server remains a separate unresolved boundary. No HTTP or Photos fix is claimed. The first rendering diagnostic launcher closed stdin before actions; it remains an incomplete retained attempt. Production is unchanged pending actual reviewed rendering evidence.

The corrected launcher now completes the separately frozen rendering-only continuation: ten checks pass, original Safari redraws its retained page and types `glyph`, both Home crops match144000 RGB pixels, and final441-byte serial has no new graphics hang/restart. Every PNG is manually reviewed; final export and exact served candidate/JIT identity pass. The initial EOF/timer attempt remains incomplete with its full video/export. [095 recording](../docs/traces/safari-stride472/README.md) and [durable browser evidence](../docs/traces/safari-stride472/README.md) retain the limits. Root owns maintained integration and old-save/offline qualification; HTTP daemon and Photos rendering are not qualified by this check.
