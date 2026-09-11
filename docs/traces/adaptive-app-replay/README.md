# First complete adaptive app workload replays

Settings, Contacts and Notes each execute the frozen 600M-credit trajectory.
Every complete saved-machine checkpoint at 100M, 300M and 600M matches the
independently frozen seeded reference. All three runs contain actual generated
retirement, not just guard calls or interpreter fallback.

| Workload | Matching checkpoints | Generated retired | Installed over run |
|---|---:|---:|---:|
| Settings | 3 / 3 | 569,061 | 2,000 |
| Contacts | 3 / 3 | 736,058 | 1,928 |
| Notes | 3 / 3 | 760,959 | 1,944 |

This proves correctness for this prototype's executed generated slice. It does
not establish useful coverage or a speedup: the initial collector exhausts its
2,048 lifetime attempts and most subsequent execution falls back. The three
full runs ran concurrently for correctness; their CPU/wall times are diagnostic.
The predeclared warmup/ABBA throughput gate has not run.

## Implementation and evidence

A synchronous admission observer captures actual validated code after 1,024
misses at a resident PC/ISA/mode/generation key. Installation returns to the
original interpreter instruction before later execution can use the unit. The
compiler follows connected code, keeps only used virtual witnesses, and owns
function handles until the runtime explicitly retires them. Runtime wrapper and
WASM identities, compiler sources and all checkpoint hashes are retained.

A fixed 1,024-bucket PC/ISA index replaces the full 2,048-unit scan on every
lookup miss. Ownership scans, capacity limits and permission/version/literal
checks are unchanged. 6,417 focused index checks, 8,511 registry checks and
2,700 observer checks pass. The index replay preserves the Settings 100M
checkpoint and generated count before the three full workloads.

- `settings.json`, `contacts.json`, `notes.json`: full reports with nine matching
  checkpoints, source identities, compilation accounting and generated counts.
- `*-progress.log`: retained progress, including the flat generated count after
  attempts exhaust.
- `build-report.json`: exact runtime build and wrapper/WASM hashes.
- `index-tests.log`, `registry-tests.log`, `observer-tests.log`: focused checks.
- `source/`, `source-sha256.json`: this exact indexed runtime and collector.
- [Earlier pilot](../adaptive-app-pilot/): initial 1M diagnostic and unindexed
  100M runs; the 1M prefix has no state checkpoint and is not correctness proof.

Next remove the artificial lifetime-attempt cutoff (the accepted 2,048 cap is
on installed units) and qualify atomic generation-only re-admission of already
installed unchanged code. Pending stale installations must remain rejected.
Full workload equality must be repeated after those behavior changes.
