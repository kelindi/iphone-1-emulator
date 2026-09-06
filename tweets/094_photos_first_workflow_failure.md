# 094 — Original Photos finds our images, then exposes a rendering bug

## Tweet idea

The real 2007 Photos app now finds our two test images and opens the first. Moving to the next image breaks the rendering. This is progress toward a usable web iPhone—and a clear graphics bug to fix before calling Photos functional.

## What you're seeing

[Full original-speed recording](videos/photos_first_workflow_failure.mp4) · [thumbnail](thumbnails/photos_first_workflow_failure.jpg).

The approximately five-minute recording includes fixture import and genuine Home, Camera Roll(2) around1:20, both thumbnails around1:50, and the first full circle image around2:40. Showing the controls around3:00 brings back stale content. Later Next attempts leave a narrow blue strip, and the run stops with its second-image gate unfinished. The thumbnail is the actual album at1:50. This is the complete first workflow, including waits and failure.

## ELI5 technical explanation

We gave the original app two pictures to discover. Our own small ARM program uses the old operating system's normal file calls to create two image/thumbnail pairs. A third image has an intentionally invalid camera filename: Photos correctly leaves it out. Every file's bytes were checked independently, and Apple's apps and frameworks remain unchanged.

Creating the files was tricky because the offline filesystem editor could not fit all their directory records. Installing just our small program lets the original filesystem handle that work itself. The app then finds both thumbnails and decodes the first full picture. The failure comes later: controls and next-image transitions leave stale or incomplete content. A live observation reports a 3D graphics restart, but the cause is not yet proven. The recording and all twelve captured raw frames preserve what actually appeared.

## Why it matters

Photos is no longer only an empty-library launch test. We have real file discovery and image decoding, plus a concrete rendering failure to investigate. Second-image navigation and save/reload still need to pass; the normal starting phone is unchanged. [Evidence, failed gate and observation limits](../docs/traces/photo-bootstrap/README.md).
