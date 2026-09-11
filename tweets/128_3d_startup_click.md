## Tweet idea

The 3D button now works while the iPhone boots. Open the model immediately, rotate it while the original OS loads, and watch its screen become the live phone.

## What you're seeing

[Recording](videos/3d_startup_click.mp4) · [Thumbnail](thumbnails/3d_startup_click.jpg).

The first two seconds show click feedback and the 3D model opening before the emulator worker is allowed to load. At approximately 19 seconds, its loading display becomes the genuine guest screen. At 23–30 seconds, the angled slider unlocks. The rest verifies case rotation, Settings/Home, zoom, view switching, narrow viewport layout, and WebGL-loss fallback. Playback is unsped.

## ELI5 technical explanation

The old button was disabled until the emulator became ready, without a visible explanation. Its enablement also depended on observing a future readiness mutation, so a late-loaded view controller could miss it. The model does not need the guest to run, so that dependency is removed. Clicking shows “Loading…” while the graphics module downloads; the model's screen shows a loading label until the real framebuffer is ready. Guest input still waits for readiness. Emulator failure reveals the original 2D recovery controls.

The browser regression deliberately stalls the worker and model downloads separately, verifying immediate button feedback and a working model before boot. All 15 checks pass, including the existing live-screen workflows. [Report](../docs/traces/phone-3d/startup/report.json). The normal Chrome probe reproduced the disabled-during-startup behavior and successfully opened 3D after startup; the user's exact URL/browser was not supplied. This does not establish that every reported click failure had the same cause.

## Why it matters

A visible control now responds immediately instead of silently waiting on a separate, slow emulator startup. The model's visual fidelity was separately rejected by the user; online replacements are being evaluated, and this click fix does not claim to resolve that concern.
