# Clipped copies from the original graphics driver

## Tweet idea

Original iPhone graphics, one missing packet at a time: clipped screen copies now render correctly in our web emulator. 1,899 checks pass. Candidate Home returns are faster, but Safari and Calendar still show why we are testing every app.

## What you’re seeing

[Original-speed recording](videos/mbx_clipped_composition.mp4), approximately **0:08–0:19**: the genuine Home screen opens Settings inside the clickable phone frame. The [thumbnail](thumbnails/mbx_clipped_composition.jpg) is the actual Settings frame at 0:15. The first seconds include ordinary emulator startup; no animation is sped up. This particular launch reaches the newly supported clipped-copy decoder twice, with all 330 2D commands and 334 3D renders completing without rejection.

## ELI5 technical explanation

The original graphics driver sometimes adds three words that say which part of a screen copy is visible. Our decoder did not understand this form, so it rejected the whole group of drawing commands. We read the genuine driver’s instructions to confirm the extra tag, rectangle and matching mode bit, then taught the emulator to copy only that rectangle while preserving its original source offset. Every source and destination mapping is checked before the group writes any pixels. Apple’s application and framework binaries remain unchanged.

The correction adds 50 checks to the existing 1,849, including the captured command, malformed rectangles, missing memory pages, overlapping allocations and ring wrap. Eight independent browser launches complete without a rejected draw. Settings scrolling and all 24 save/import checks also pass on the isolated candidate.

## Why it matters

This fills a real gap in the graphics hardware model. In the latest small comparison, candidate Home returns take 0.90/0.95 seconds versus software composition’s 1.70/1.25 seconds. Settings results are mixed, so this is not a general speedup claim. Broader tests found separate unsupported 3D forms in Safari and Calendar; the accelerated work image remains a candidate and the default phone still uses software composition. [Detailed evidence and remaining gates](../plans/graphics-speed.md).
