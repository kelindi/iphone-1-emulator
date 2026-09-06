# Safari reaches its original keyboard through the MBX renderer

## Tweet idea

Safari's original address keyboard now opens without restarting the emulated graphics driver. One clipped white fill was meant to draw a single row, and some valid draws covered no pixels at all. The real 2007 commands now complete, then Home returns intact.

## What you're seeing

[Original-speed recording](videos/safari_mbx_address_keyboard.mp4) · [thumbnail](thumbnails/safari_mbx_address_keyboard.jpg).

This fresh browser run imports the genuine phone snapshot with accelerated MBX composition enabled, opens Safari's Bookmarks, taps Done, focuses the address field to reveal the original keyboard, and returns to Home. Approximate sections: 00:25 Home after import, 00:38 Bookmarks, 00:43 browser page, 00:49–00:50 address keyboard, and 00:54 complete Home. The thumbnail shows the keyboard. Playback is unchanged; loading, import, and deliberate observation waits remain visible. This is functional evidence, not a speed benchmark.

The run completes **296/296 2D commands and 1,699/1,699 3D draws, with zero rejections**. The final Home image matches the initial Home exactly below the status bar, and the screenshots were also reviewed for the actual Bookmarks, keyboard, and complete icon layout. There are no page errors. Reports, exact command witnesses, and the original recording live in `.context/safari-solid/browser-scales/`.

## ELI5 technical explanation

The real OS sends drawing jobs to its PowerVR MBX graphics chip. Each job describes both the picture and the memory it may touch. The emulator previously recognized much of that language, but several Safari forms were still rejected. The guest then waited for completion and eventually restarted its graphics driver.

The first missing solid rectangle extended far below the screen. A separate clip record limited it to **one row, y=410**; the framebuffer's coarse tile bounds covered rows 400–415. Filling the whole rectangle or tile would corrupt the screen. The decoder now validates the actual corner ordering and independent clip, intersects them with the screen, and writes only the covered pixels. The new solid family initially accepts opaque colors because its fractional-alpha rounding is not yet established.

Some animation frames legitimately cover no pixels after clipping and rounding. Those jobs still need to complete. They now do so only after their full command structure, tile list, parameter records, and destination memory mappings have been checked, without writing a pixel.

Safari also stretches a single texture row into separators, squeezes its address-field texture horizontally while retaining its height, and draws a two-row shaded strip. Narrow checks admit those captured forms through the existing bilinear sampler and blending code. Unsupported transforms still fail safely; we did not replace Safari's UI or modify Apple's binaries.

The combined native suite passes **2,428 checks**. Tests preserve the exact captured solid packet, compare all framebuffer pixels, and cover nonfinite coordinates, malformed clips, color/alpha mismatches, incorrect tiles, and missing late memory pages. Before the respective fixes, the unchanged decoders fail the captured positive cases. The new address-texture tests additionally reject coherent cropped UVs and unsupported vertical scaling while retaining the earlier regressions.

## Why it matters

A useful original app interaction now works through the modeled graphics hardware, including its animated transition and keyboard. The default phone still uses software composition while broader accelerated app coverage is checked. This does not establish internet access, complete Safari functionality, full-speed acceptance, or all-app graphics coverage.
