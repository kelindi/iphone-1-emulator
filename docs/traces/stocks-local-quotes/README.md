# Original Stocks local quote qualification

> Historical recordings and publishing artifacts have been removed from Git history. The summaries below describe the original runs; remaining test reports and screenshots are retained. Historical hash receipts may predate this archival cleanup.

The dedicated browser run passes its declared Stocks quote gate, and the original app displays the sample values on two separate runs and after Home/revisit. This is a quote milestone, not complete Stocks functionality. Charts receive an explicit HTTP400 and do not render. Weather fails; its later request-header recovery identifies an unsupported original `t` parameter.

- `browser-report.json`: dedicated Stocks run, passed true. A review correction records that the final chart is blank, not a visible error message.
- `first-combined-report.json`: earlier combined run remains failed because its required Weather forecast never appeared. Its export predates Stocks success; the recorded correction makes that boundary explicit.
- `report.json`, `service.log`, `wire-report.json`, numbered wire files: independent snapshot readback, exact byte counts, real DNS A responses, original finance getquotes request, complete HTTP200 and matching four quote values. The separate getchart request receives400.
- `install-report.json`: new-image-only installation; all5119 other original forks remain unchanged. The sole replaced old fork is IPMonitor's resolver configuration; the original mdns entry is preserved.
- `build-report.json`: native contracts and own executable checks. The tested binary is54d2b12e11d024fba18bb8f340da43813a6186de63f890739b4e3dd2748212d5. A later label-validation correction is a separate candidate, not retroactively guest-qualified by this run.

Recording and explanation. All displayed prices are fixed demo data. Original Apple applications, frameworks and kernel are unchanged; no UI replacement or injected guest values are used.

## Private artifact storage

Completed own snapshots/images listed in `archived-artifacts.json` are retained as gzip files to keep enough disk space for current tests. Every archive was decompressed and SHA256 checked before the corresponding raw file was removed. Historical receipts retain their original raw paths; the archive index maps those paths. Restore to a new owned path with gzip and verify the recorded raw hash before reuse. The current test image and original work image were not changed.
