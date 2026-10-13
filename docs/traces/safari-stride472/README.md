# Bounded Safari source-row correction — candidate

> Historical recordings and publishing artifacts have been removed from Git history. The summaries below describe the original runs; remaining test reports and screenshots are retained. Historical hash receipts may predate this archival cleanup.

The actual failed default Safari phone retains BGRA8 copy packets with an independent 1,888-byte (472-pixel) source pitch. The current decoder rejects them because it limits source pitch to the 320-pixel display width. Apple's original OS1 packer independently masks the supplied source pitch to 15 bits; the retained packet is not a 472-pixel destination.

The isolated patch admits BGRA8 pitch up to the observed 0x760 bound. It preserves the destination 320×480 limit, other-format bounds, each GART row translation, aliases and all-or-nothing batch commit. This is bounded support, not unrestricted image dimensions. Production is unchanged.

- Native: all 2,815 existing checks plus 32 source-row, noncontiguous mapping, edge/bounds, alias and late-PTE checks pass. The old decoder fails four genuine positive gates with those same tests.
- Retained ring: 1,484 command heads, including 905 rejected wide copies. The candidate stages all of them without guest execution or bus writes. All 4,583,272 staged bytes equal independently read source rows; a changed actual output byte is rejected. This historical ring does not establish which batch was pending at failure.
- Actual WASM: all 2,847 checks pass. An unchanged relink of the released RTC archive reproduces production `7a72add6…69088` exactly. Only `mbx.c.o` changes in the candidate archive. Candidate WASM is `3b43ccf4a8292a4da776491aa92185b0ea88e2fc38a3002a6e1fe1d5a6aa4463`; exports/imports/API, JIT revision 1 and all offsets are unchanged, as is the original `d9c66155…3881` seed.

The release proof deliberately retains two setup failures: `web/build` was stale and reproduced the old core, so its baseline gate stopped; the first actual-WASM test launcher used `.js` inside an ESM package, so the identical generated wrapper was copied to `.cjs` and then passed. Neither is a guest success or model change.

Actual HFS catalog/fork evidence also corrects the comparison with milestone 091: both its installed filesystem and imported listener checkpoint use MBX2D=0; the failed default checkpoint uses MBX2D=1. These are different graphics policies. The failed default snapshot separately lacks the local server process; the stride finding does not establish its exit cause or qualify HTTP forms.

A single failed-state rendering continuation was prepared separately. Its first launcher closed stdin before commands could be sent, leaving navigation incomplete; the original import, video, timer and final export are retained. No visual recovery is claimed from these decoder checks. See the later rendering-run receipt when available.

Source snapshot: `.context/guest-http-default/browser-run-1/final-home.snap.gz`, raw SHA256 `c407b3734919c76dc0eae153b1bb70366969c4548e3fa2232716a1bc3395e693`. Despite its filename, this is the failed form state, not Home. Original MBX2D library SHA256 `6eb2cc431ecc7fe4b2c6ad7e492a44d5029ab0ecd0e4866b2d0cbe13ebb7572c`; producer instructions `0x30b3ac94..0x30b3acd4`. Retained scripts, reports, source patch and archive hashes accompany this note.

## Actual rendering continuation, completed

[The corrected frozen run](recovery-run-2/report.json) passes ten gates, including
manual review of every actual guest PNG and intended/observed address text
`glyph`. Both Home crops [0,30,320,450] match exactly across144000 RGB pixels
(and their alpha bytes). Full441-byte serial capture has zero new graphics
hangs/restarts including final export and closure. The exact candidate WASM,
original JIT engagement, required asset hashes and final export are verified by
[the independent artifact outcome](recovery-run-2/independent-outcome.json).

The original corrupted page/keyboard state is still visible immediately after
Import. Actual Home→Safari navigation redraws a normal retained page; original
address controls clear and draw `glyph`, then another Home→Safari returns to that
page. There is no Go/refresh/form submission. The old Loading/echo URL is not HTTP
acceptance, and viewport-clipped content is not full-page layout qualification.

[First attempt](recovery-run-1/independent-outcome.json) remains INCOMPLETE: closed
stdin preceded command iteration, so no guest navigation ran. It reached its
original360s cap and exported normally; no new graphics failure occurred. The
successor retains the same decoder/fixture with a TTY controller, early EOF check,
late-serial finalization guard and mandatory exact manual text evidence. Nineteen
focused finalizer controls plus a real delayed-readline EOF check pass. One Home
command sent before READY in the second launcher was discarded before the
iterator; the actual guest action log contains exactly two Home presses.

Both full original WebM recordings are archived alongside their reports. The
public historical recording (removed)
is the complete corrected recording at original speed; the
historical thumbnail (removed)
shows `glyph`. This is a functional rendering check, not a benchmark or controlled
browser before/after comparison. The original failing Safari run is retained in093.

Final second export raw SHA256 is
`a4317f614172c7363e9ef10a7dcb136e3eab0849b1272360ec8c87d16e30186c`
(259768584 bytes). Both closed exports and the parsed RAM are now verified gzip
archives with [shared-materializer receipts](../storage-reclamation/archived-safari-rendering.json).
Use their original adjacent receipts to restore an exact raw file for replay.
[Integration proposal](integration-proposal.md) remains root-owned; this evidence
alone does not promote production or qualify the missing daemon/Photos path.
