# 110 — Generated code returns through a checked C interface

## Tweet idea

The new iPhone compiler now calls back through a checked C interface. It must
report exactly how much work completed and why it stopped. Malformed results,
double-counted instructions and an unreported ARM/Thumb switch are rejected.

## What you're seeing

This is a nonvisual runtime-interface milestone. The Settings/Home portion of
the [106 baseline recording](videos/adaptive_compiler_baseline.mp4) remains
contextual footage of the unchanged phone, not this isolated bridge. [Native
and actual-WASM evidence](../docs/traces/adaptive-result-contract/README.md)
records the new tests; no UI or speed result is claimed.

## ELI5 technical explanation

The compiler does the arithmetic, but the emulator decides when devices and
interrupts need attention. The interface between them now checks the compiler's
receipt: completed instructions, stopping reason, and where execution resumes.
Only a valid receipt adds the completed work to the CPU counter, exactly once.

If generated code returns nonsense after changing state, rerunning the same
instructions could duplicate effects. The interface therefore reports a fatal
error rather than pretending nothing happened. Switching instruction sets also
requires an explicit boundary so the scheduler can regain control.

## Why it matters

Both native C contract tests and generated WASM executing through the C wrapper
pass. This prepares scheduler integration; it does not yet provide admission,
real memory guards, invalidation, recovery or a faster phone.
