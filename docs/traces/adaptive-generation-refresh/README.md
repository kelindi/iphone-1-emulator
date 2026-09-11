# Installed-unit generation re-admission

An inactive installed unit can refresh its TLB generation atomically when all
newly captured executable mappings have the same physical host, virtual block,
live epoch, nonzero physical code version, ISA, CPU mode and every recorded
CP15 context field. No dependency changes unless all captures pass. The same
function handle remains owned. Literal guards and code-write exits stay active.
Pending installations still reject stale captured generations; this is not a
repair mechanism for queued or background compiler work.

The collector now retries after another1,024 admission misses rather than
stopping forever after2,048 attempts. The accepted2,048 installed-unit/32MiB
binary bounds remain. The lifetime attempt diagnostic saturates at2^53−1
without disabling collection. No unbounded function/module cache was added.

## Evidence

493 refresh checks plus8,511 registry,2,757 observer and6,417 index checks pass.
Tests cover atomic multi-dependency refusal/retry, changed mappings, permissions,
context, versions, old pending installs and exact retirement/restore ownership.
All three complete600M-credit app trajectories again match all nine frozen
full-state checkpoints.

| Workload | Generated retired | Lifetime attempts | Installs over run |
|---|---:|---:|---:|
| Settings |135,522,619|9,703|6,468|
| Contacts |135,309,174|10,107|6,760|
| Notes |144,823,298|10,342|7,460|

Installs over a run include replacements; they are not simultaneously live
units. No install refusals occurred. Counters10/11/12 record refresh attempts,
successes and refusals. Roughly40–49M refusals per trajectory remain, motivating
further admission analysis only after the declared performance trial.

`settings.json`, `contacts.json`, `notes.json` retain raw checkpoints, counters
and diagnostic compilation timings; `*-progress.log` retain progress.
`build-report.json` pins runtime wrapper/WASM and build inputs. `source/` and
`source-sha256.json` retain the exact implementation. The focused native logs
are retained alongside them.

The three full replays ran concurrently for correctness. More generated
retirement is not proof of faster execution; these timings are not the ABBA
gate. Next run that predeclared comparison against the independently frozen
seeded engine, including compilation costs.
