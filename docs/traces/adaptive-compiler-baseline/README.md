# Adaptive compiler Stage 1 — cloud baseline

> Historical recordings and publishing artifacts have been removed from Git history. The summaries below describe the original runs; remaining test reports and screenshots are retained. Historical hash receipts may predate this archival cleanup.

2026-09-08, Chrome 152 on Linux, Intel Xeon 2.90 GHz. Current legacy engine;
no compiler speedup is claimed. Reference-Mac acceptance is still pending.

The complete run contains 31 Settings/Home pairs: one first pair plus 30 warm
samples per action. Settings median 2317.5 ms, nearest-rank p95 2566.7 ms;
Home median 866.45 ms, p95 867.9 ms. Only Home meets the sampled 1000 ms warm
target. One first sample per action cannot establish a first-launch p95.

- [Raw report](report.json): preparation, 62 actions, host and actual served hashes.
- [Summary](summary.json): qualified navigation statistics.
- [Freeze manifest](freeze-manifest.json): SHA-256 identities for 132 independent files.
- [Frozen inputs](frozen-inputs.tar.gz): executed harness, site, selected source and dependency metadata.
- [Frames](frames.tar.gz): original screenshots and lossless gzipped RGBA frames.
- historical recording (removed).

Manifest SHA-256: `b18ef5a92f31196eea95f3587a65df06983a3417951f0be3448c5917766762ce`.
All frozen file hashes were verified again after the run. Absolute paths in the
report identify the original cloud run; the archives preserve its inputs.
Firmware remains at the hash-pinned remote URLs in the frozen site manifest.
Extract inputs into a new directory and use `freeze-baseline.mjs PATH --verify`.

The harness performs a real slider gesture, then requires Home icon evidence
before timing Settings. Readiness is a 50 ms sampled visual heuristic with a
500 ms stable interval; it is not a complete application correctness test.
Prepared Home and final Settings frames were manually inspected. Startup,
touch latency, scrolling, other apps, and Mac speed are not qualified here.

Subsequent harness changes improve failure cleanup, fixture-byte pinning and
metadata validation. The full run retains its exact original harness; do not
replace archived files with newer source. Short follow-up runs qualify the
maintained harness separately and do not supply additional p95 samples.

Final maintained-harness qualification: [four-action report](qualified-sanity.json)
passes with verified Home, 14 matched served assets, no errors, and successful
context/browser closure. [Qualified manifest](qualified-manifest.json) pins 135
files; its SHA-256 is `bfa837ee884cd62d4cbd32589dac10e6c20a03a85eaea4523fa4eaf02059a2fb`.
The served site came from the original frozen baseline (identical site hashes);
the [qualified harness archive](qualified-harness.tar.gz) contains the newer
executed harness and helpers. All 135 hashes were verified after this run.
[Unit results](unit-tests.log): 73 tests pass, including failure cleanup,
fixture pinning, independent-copy tamper detection and malformed-report rejection.
An intentional [missing-manifest startup failure](startup-failure.json) returned
exit code 1, preserved the error report and never launched a browser.
