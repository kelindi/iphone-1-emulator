# Safari draws again with the wider source rows

## Tweet idea

Safari's graphics commands exposed a bad assumption in our iPhone GPU model: its font-image rows can be wider than the screen. A bounded fix passes 2,847 native and browser-core checks. The real app now redraws its page and keyboard from the failed phone state. Server and Photos issues remain open.

## What you're seeing

[Complete original-speed recording](videos/safari_stride472_recovery.mp4) ·
[thumbnail at 3:20](thumbnails/safari_stride472_recovery.jpg).

The 4:33 recording shows the isolated corrected core. After importing the failed
Safari state, the old overlapping keyboard is still visible. Around 1:00 the real
Home button returns to Home; around 1:45 Safari redraws its retained page. Around
2:15–3:25 its original address editor clears and types `glyph`; at 3:20 that exact
text is visible. A second Home return and Safari revisit follow, then final export.
No Go, page refresh or HTTP form submission is performed.

This is a functional check with observation pauses, not a speed comparison.
The original failing default workflow remains in 093. An earlier launcher for this
new check closed stdin before navigation, waited for its 360s cap, and exported
without performing actions. Its complete video/report remain
[in the evidence](../docs/traces/safari-stride472/README.md); it is explicitly
incomplete, not a hidden successful run.

## ELI5 technical explanation

A graphics copy has two different row widths: the image being copied from and
the screen being copied to. Our decoder incorrectly limited both to the screen's
320 pixels. Actual commands in the failed Safari phone used 472-pixel source rows.
Apple's original command builder stores that source pitch independently.

The correction permits that observed BGRA8 bound while keeping destination size,
row address translation, other pixel-format limits and whole-batch checks intact.
Tests cover real packet fields, source edges, noncontiguous memory pages and
malformed late commands that must leave every destination byte untouched. All
2,815 existing tests plus 32 new checks pass natively and in actual WebAssembly;
the old decoder fails four positive gates. An independent retained-memory oracle
matches 4,583,272 staged source bytes without guest mutation.

In the real browser continuation, every screenshot was inspected, `glyph` matches
the intended text, and both Home crops match 144,000 RGB pixels exactly. Complete
serial capture has no new graphics hang/restart through final export and closure.
The served candidate uses the exact current release's API/runtime and unchanged
JIT seed. [Pinned source, reports and failed-state receipts](../docs/traces/safari-stride472/README.md).

## Why it matters

This removes one concrete obstacle to original Safari drawing on the accelerated
phone. It does not establish HTTP service recovery: the failed phone's local
server is separately absent, and the visible page is retained content. Photos'
failed navigation also remains separate. Production promotion requires the
root-owned saved-phone upgrade/offline checks; this recording covers the isolated
rendering correction only.
