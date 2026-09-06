# Text opens its original composer through MBX

## Tweet idea

A 32-pixel texture stretched to 6,848 pixels was stalling the original iPhone's graphics driver. The real command also carried a small clip. Modeling that distinction lets Text open its 2007 keyboard, cancel, reopen, and return Home without a graphics restart.

## What you're seeing

[Original-speed recording](videos/text_mbx_composer.mp4) · [thumbnail](thumbnails/text_mbx_composer.jpg).

The disposable browser run imports the genuine MBX-enabled phone, then opens Text's New Message screen and full keyboard (about 00:38), taps Cancel to reach the empty Text Messages list (about 00:46), opens the composer again (about 00:52), and returns Home (about 00:56). The exact recording times are retained in `.context/text-mbx/browser/report.json`. Loading, import, and observation waits remain visible; this is a functional recording, not a speed benchmark.

The isolated candidate completes **1,160/1,160 2D commands and 1,995/1,995 3D draws**, with zero rejections or page errors and active compiled CPU execution. Screenshots were reviewed for the actual composer, conversation list, and keyboard. Home matches the initial displayed image below the status bar. The decoder and tests are integrated. A separate fresh production run on the combined WASM build also passes composer → Cancel → reopen → Home with active compiled execution and no browser or graphics-restart errors. Every one of the 144,000 raw RGB pixels below the status bar matches the initial Home exactly. Evidence is `.context/graphics-final/production-apps/text/`; production exposes no GPU command counters, so the command totals above belong only to the isolated recording.

## ELI5 technical explanation

Drawing commands can describe an image much larger than the screen. Another part of the same command says which small piece is allowed to appear. Text stretched a 32×26 texture horizontally by 214 times, while a retained command's clip limited the result to just 214×13 visible pixels.

The emulator already knew how to sample and blend that texture. An earlier 512-pixel geometry limit stopped it before the clip could be checked. The original graphics driver then waited for a drawing job that never completed.

We traced the unchanged Apple LayerKit and MBX2D producers and captured their real coordinates, UVs, texture allocation, pitch, and clip. The decoder now allows this specific wider horizontal family, with bounded coordinates and unchanged texture height. Every existing command, tile, memory-mapping, and clipping check still applies before staged pixels are committed. Arbitrary wide transforms remain unsupported.

The native suite passes **2,470 checks**. The exact captured packet fails its pixel and completion checks on the unchanged decoder, then passes with the fix. An additional replay uses the retained snapshot's real source texture and checks all **153,600 framebuffer pixels**: 2,782 pixels are drawn correctly and nothing outside the clip changes. Negative tests reject cropped UVs, changed vertical scaling, an excessive coordinate envelope, and malformed or missing memory state.

## Why it matters

Another original app workflow can render through the modeled graphics hardware without replacing Apple's interface or modifying its binaries. The default web phone still uses software composition while wider MBX coverage is checked. This verifies composer graphics and navigation; sending SMS still needs the separate baseband implementation.
