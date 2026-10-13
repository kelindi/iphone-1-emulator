# Default-image local HTTP qualification — failed browser gate

> Historical recordings and publishing artifacts have been removed from Git history. The summaries below describe the original runs; remaining test reports and screenshots are retained. Historical hash receipts may predate this archival cleanup.

One pristine accelerated-default image boots our maintained, quiet ARMv6 daemon and reaches genuine Home. One original Safari workflow then loads its landing page and link, but stalls at the first form. **The default integration is not accepted or published.** Milestone093 and full recording.

| Gate | Actual result |
|---|---|
| Current manifest root and MBX configuration | PASS: raw root `73d0d2a0…c0ded9`, `LK_ENABLE_MBX2D=1`, OGL0; original USB DT retained |
| Install only our executable and launchd plist | PASS: all 5,120 old forks and 5,828 catalog records preserved |
| Quiet final binary/build/import/contracts | PASS: original ARMv6 classic format and 16 declared libSystem imports; parser 12,504, quiet 100,530 and verbose 100,530 native checks; permissions 5, installer 7 |
| Native listener and actual Home | PASS: entry 256,916,410; bind/listen succeed; Home+Repair at 8.5B; normal capture 9.197B within 10B/900s |
| Native process health | PASS: SpringBoard 12, lockdownd 17 and own daemon21 present with threads |
| Exact current browser assets / JIT | PASS:66 received local bodies match 15 required assets;16 projected files unchanged; generation 2 remains active |
| Original Settings / empty Contacts / local landing page / Next | PASS: actual retained screenshots reviewed |
| First fresh form result | **FAIL/unfinished**: old content, keyboard and spinner remain; first pending2D hang at 419.749 s |
| Bookmark / real autosave+reload / final Home | **Not reached**. Background saves alone do not satisfy the persistence gate |
| Final diagnostic export | PASS as evidence capture, **failed form state**, despite generic filename `final-home.snap` |

The 800.005-second browser run deliberately ended at the first unresolved boundary. Its original [report](browser-run-1/report.json), [artifact audit](acceptance-audit.json), frames and serial are retained. Seven script checks passed; this does not mean the full workflow passed. No browser console errors or request failures occurred. The only report error records the unfinished form gate. No retry, rendering fallback, changed daemon lifecycle or production promotion followed.

## Two unresolved boundaries

Guest serial contains 365 pending2D-blit hang messages and five 3D-blit restarts. This is actual Apple graphics-driver reporting, not a browser exception or inferred unsupported-packet counter. An independent artifact audit subsequently identified 905 retained copy packets with BGRA stride 1,888 bytes (472 pixels), beyond the current 320-pixel source-width cap. That concrete renderer rejection is being investigated in an isolated candidate; it does not explain the separate daemon absence or establish a successful corrected workflow.

The full read-only process walk finds our daemon PID 21 at native Home but not in the final 18-process list. MobileSafari 89, SpringBoard 12 and lockdownd 17 remain present. This establishes absence at the final snapshot, **not when or why the daemon exited**. Neither retained serial nor extracted filesystem logs contain its failure marker. We therefore cannot attribute the entire form failure to rendering alone.

An artifact-only original-v39-loader inspection of the captured RAMDisk found a new [crashdump report](browser-run-1/guest-log-audit/7.bin): Apple `crashdump` PID 134, parent `crashreporterd` 14, EXC_BAD_ACCESS/KERN_PROTECTION_FAILURE at 0, PC 3188d51c. It is absent from pristine input and the native Home snapshot ([comparison](browser-run-1/guest-log-audit/native-comparison.json)). **It is not a crash report for our daemon**, and no causal link is established. The reporting process itself failing limits the available evidence. No low-memory/kill cause is proven.

The [filesystem audit](browser-run-1/guest-log-audit/report.json) walks 2,596 file records using RAMDisk PA 085d0000, length 176,160,768, read through the original loader into anonymous host memory. It makes no guest or image mutation. Dirty guest filesystem buffers might not have reached backing bytes; absent logs do not prove absent events. No large persistent RAMDisk duplicate was created. Apple executable bytes selected during the local inventory are not redistributed here.

Source lifecycle inspection: the production loop is persistent (`OWN_SERVER_KEEP_RUNNING()` expands to1), with no 64-request or hour limit. Quiet mode disables successful request tracing, not startup/failure output. Fatal listener poll/accept/close failures return through `LOCAL_HTTP_FAIL_V1`; rejected requests normally continue. Console logging itself has a bounded write loop and can lose output. The job has RunAtLoad=true and KeepAlive=false. Signals, low-memory termination and lost output remain possibilities, not established explanations. No lifecycle change is justified by these artifacts alone.

## Exact artifacts and preservation

- Final own quiet executable SHA256 `bfcc524cae77250b3d3c8d754e7bdfe328734395a0d625fa9735f00ccdd1a19f`; own source is in [maintained-source](maintained-source/README.md), build receipt in [build-quiet-2](build-quiet-2/build.json).
- [Source qualification](source-qualification.json), installer receipts and native post-pins bind the actual current manifest root. The initially proposed old software root was rejected before installation.
- Native listener snapshot raw 245,071,842 B SHA256 `2ee50f4e68b0e1afb523312ace3c45d894866f2e33b91051eabcc49d37317afa`; local gzip `.context/guest-http-default/native-run-1/listener.snap.gz`.
- Failed-form snapshot raw 260,767,808 B SHA256 `c407b3734919c76dc0eae153b1bb70366969c4548e3fa2232716a1bc3395e693`; local lossless gzip `.context/guest-http-default/browser-run-1/final-home.snap.gz` after verified archival. **It is not Home.** Snapshot cycles 15,216,550,735 differ from the later final browser metric because execution continued after export.
- Installed disposable image raw SHA256 `c23c34c07ae4690a4853bd35cab3f5e0f8620c2abcda972ca34835623063b775`, retained losslessly in `.context/guest-http-default/installed/default-local-demo.img.gz`; raw removed only after full pin/roundtrip audit.
- Actual core `7a72add6…969088`, seed `d9c66155…6b3881` and loader `3c2cc412…597e9` remain unchanged. Source and response receipts are in [browser-pins](browser-pins.json), [site-pins](site-pins.json) and original report.
- The initial installer test's overstrict alternate-header assertion remains in checks. Corrected tests allow only actual accounting updates while preserving the distinct header flags. No failed test or first browser outcome was overwritten.

The browser imported the native candidate fixture through hidden diagnostic controls; export likewise used the existing hidden button with enabled wait. Numeric entry and application navigation use real touchscreen actions. No normal default-user availability, raw quiet HTTP wire receipt, offline transport, external networking, speed result, bookmark persistence or completed form is claimed. The prior verbose isolated 091 proves those narrower HTTP exchanges on its different retained software-rendering phone; it cannot substitute for this failed accelerated-default qualification.
