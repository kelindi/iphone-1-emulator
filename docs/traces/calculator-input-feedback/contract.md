# Calculator input feedback measurement

Current unmodified local build with visible recovery, genuine default MBX guest,
normal foreground clock, autosave and original input dispatch. Chrome on this VM;
not a Mac, Safari, app launch, animation or photon-to-photon claim.

After the independently reviewed initial Calculator display, warm up one 9/C pair,
then measure 30 pairs, retaining every observation. Each real pointer pulse is
30ms. Record actual capture-phase pointerdown/up timestamps in the page clock.
On each animation frame with a new guest framebuffer sequence, compare a 40x40
button crop against its pre-press bytes and the entire 320x88 display against the
existing independently reviewed 9/0 reference (max RGB difference2, exact alpha).
A button change requires at least16 pixels differing by >12 RGB. First matching
display must still match after100ms, excluding confirmation from its timestamp.
Report down-to-button-feedback, down-to-correct-display, and signed
release-to-correct-display, with event/observation cadence and missing-feedback
counts. Do not clamp negative times or substitute display completion for an
unobserved button highlight. The conservative primary responsiveness check uses
pointerdown to first genuine button feedback, and requires30 observed presses
per key with p95<=150ms; display correctness independently must pass for all60.

Use650ms post-result spacing, retaining ordinary autosave overlap if it occurs.
Startup120s, per-input5s, process180s; one run after agents report heavy END.
No repeat on an unfavorable result. Keep uncut recording, report, references and
exact current served/runtime hashes. No private saved phone is imported/reset.
