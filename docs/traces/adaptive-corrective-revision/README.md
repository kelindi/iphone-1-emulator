# Single corrective revision

After the first gate failed at 0.337x aggregate throughput, this revision targets
measured repeated compilation and admission overhead. It does not expand ISA
coverage or relax any installed-unit context, mapping, literal or version rule.

- Exact-WAT module LRU: 2,048 entries / 32 MiB accounted UTF-16 WAT + binary
  payload, per owner. Native engine allocation is not represented by that byte
  accounting. Fresh captures, instances and strict installation remain mandatory.
- Installed refresh rejects invariant context mismatch before mapping recapture.
  Entry dispatch avoids checking the same physical version twice without an
  intervening callback. Installation still performs the complete check.
- Replay and benchmark pin the new source and snapshot nested cache statistics
  before teardown. Timing, workloads, baseline and acceptance thresholds remain
  unchanged.

Five cache/owner tests pass, using real WABT/WASM with a mocked ownership runtime.
Native refresh572 + registry8,511 + observer2,757 + index6,417 checks pass (18,257).
The preserved old implementation fails the new no-mapping-query assertion;
`preflight-negative.log` is the expected regression demonstration, not a failure
of the corrected candidate. Build and source identities are preserved here.

All three 600M-credit app replays pass all nine checkpoints and retain exactly
the previous runtime counters; see `replay-summary.json` and the app reports.
The [second gate](../adaptive-second-gate/README.md) also passes correctness but
fails performance at 0.379797123x aggregate throughput. This revision
alone is not evidence of a speedup. The accepted plan requires review if the
second gate fails.
