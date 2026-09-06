# Photos returns Home without a graphics restart

## Tweet idea

One translucent rectangle was enough to restart the original iPhone's graphics driver. We ran the 2007 OS's own blending code through 655,360 pixel comparisons, then taught our emulator how to draw that rectangle. Photos now opens and returns Home cleanly.

## What you're seeing

[Original-speed recording](videos/photos_mbx_transparency.mp4), approximately **0:30–0:48**: tap Photos, see the genuine Photo Albums / No Photos screen, then use the physical Home button and return to all sixteen icons. [Thumbnail](thumbnails/photos_mbx_transparency.jpg) is the actual Photos screen at **0:35**. The preceding section imports the disposable accelerated phone through the normal browser UI. Fixed observation waits make this a correctness recording, not a latency benchmark.

The diagnostic browser completes **226/226 2D and 549/549 3D commands**, with zero rejections and no browser errors. Every **129,000 displayed RGB pixels below the status bar** matches the starting Home image. This screenshot comparison covers CSS-scaled output; it is not a raw framebuffer digest. Root manually reviewed the actual Photos and Home screenshots. The generated CPU engine remains active.

A separate fresh production run on the combined WASM build also passes genuine Photo Albums / No Photos → Home, with active compiled execution, no browser or graphics-restart errors, and all 144,000 raw RGB pixels below the status bar matching the initial Home exactly. Evidence is `.context/graphics-final/production-apps/photos/`. Production exposes no GPU command counters; the totals above remain the isolated diagnostic recording’s measurements.

## ELI5 technical explanation

During its animation, Photos asks the graphics chip to draw a partly see-through gray rectangle. We already understood its shape, clipping boundaries and command format, but initially accepted only completely opaque colors. That conservative rule rejected the actual `cc141414` color and made the guest driver restart.

We traced the real producer's source-over blending and independently executed the original OS1 CoreGraphics ARM routine. Across every source-alpha and destination-byte value, both four-pixel and single-pixel paths agree with `source + floor(destination × (255 − alpha) / 255)`. The experiment checks **655,360 complete pixel results**, including destinations with varying alpha. Original MBX silicon rounding is still unmeasured; this is the contemporaneous OS1 software reference, explicitly limited to the OS1 solid producer.

The renderer now accepts valid uniform premultiplied colors through the same geometry, parameter, tile and mapped-memory checks. It checks every destination before committing any pixels. Transparent colors do not bypass those checks. **2,725 native checks pass** at this integration point, including exhaustive alpha coverage, exact preservation outside the clip, and fractional-alpha malformed-parameter/missing-page rejection. The old renderer fails all 255 newly admitted non-opaque cases. Independent reviews found no atomicity blocker.

Evidence and reproducible local probes are in `.context/photos-mbx/`. An earlier browser attempt hit an older server on port 8111; that failed run is explicitly marked invalid for this candidate. The verified candidate served on port 8121. The original ARM oracle's initially mistaken opaque-output flag is also preserved with its correction.

## Why it matters

This removes Photos' two known graphics restarts from the broader sixteen-app audit without changing any Apple executable or replacing its screen. The decoder is integrated; combined production browser verification follows with the Text and Clock fixes. Software composition remains the default until the broader graphics and speed gates pass. Importing and viewing actual photos are still separate unfinished workflows.
