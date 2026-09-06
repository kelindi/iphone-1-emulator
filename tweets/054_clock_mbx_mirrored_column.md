# Clock's reversed texture column now renders through MBX

## Tweet idea

The original iPhone Clock app was stretching one column of pixels across the screen—upside down. The emulator now understands that exact graphics command. World Clock opens, Stopwatch runs and resets, and Home returns without a graphics-driver restart.

## What you're seeing

[Original-speed recording](videos/clock_mbx_mirrored_column.mp4) · [thumbnail](thumbnails/clock_mbx_mirrored_column.jpg).

The recording imports the genuine MBX phone, opens World Clock, selects Stopwatch, presses Start, Stop and Reset, then returns to the complete Home screen. Approximate sections: 00:25 imported Home, 00:39 World Clock, 00:47 Stopwatch, 00:56 running at 00:06.1, 01:01 stopped at 00:08.9, 01:07 reset to zero, and 01:17 Home. The thumbnail shows World Clock. Playback is unchanged; loading and deliberate observation waits remain visible. This is a graphics and control demonstration, not a clock-drift or launch-speed benchmark.

The isolated browser completes **495/495 2D commands and 1,040/1,040 3D draws with zero rejections**. There are no browser errors. Screenshots were reviewed for the world clock, actual running/stopped/reset digits and complete final Home. The Home comparison below the status bar finds no pixels differing beyond its tolerance. Evidence is `.context/clock-mbx/browser/`.

## ELI5 technical explanation

A gradient does not need a whole screen-sized image. The real OS stores a narrow strip and asks the graphics chip to stretch it. Clock's captured command samples the centre of one column in an 8×32 texture, stretches it across a 380×30 rectangle, turns it through 180 degrees and clips it to the screen.

The emulator already knew how to sample rotated textures, but its safety checks accepted only certain rotation and scaling combinations. This unusually wide column failed those checks, so the guest waited and restarted its graphics driver. The correction admits the captured allocation, row pitch, column centre, vertical range, size and reversed orientation through the existing sampler. An exact 180-degree case is also tested. Other transforms still face the earlier restrictions, and every accepted command retains the complete clip, tile, parameter and memory checks before writing pixels.

New negative tests caught another issue: a collapsed texture range on an integer edge could produce a zero-sized source envelope. Dividing by that size creates infinity or NaN; ordinary comparisons can then miss an invalid case. The renderer now rejects empty envelopes before division. The old core fails three coherent integer-column tests as well as the captured and exact-rotation pixel/completion checks.

The Clock additions contribute **90 checks**. The isolated suite passes 2,518 checks; after combining the Text and Photos corrections, the maintained native suite passes **2,815 checks**. Both Clock fixtures compare 9,600 rendered pixels and exercise malformed UVs, coherent geometry changes, incorrect tiles, modulation, late missing memory pages and unknown controls. Apple's binaries and app UI remain unchanged.

## Why it matters

The broader graphics audit exposed a real failure that a successful final screenshot could hide. Capturing its exact command let us fix both the visible workflow and an invalid-input edge without accepting arbitrary drawing jobs. Software composition remains the default while the combined accelerated renderer is checked across all apps and measured for an actual speed benefit. This recording does not establish alarms, countdown timers, correct calendar date, full app functionality or full-speed acceptance.


The subsequent **combined production build** also passes an independent fresh-import replay: Cupertino World Clock and map → Stopwatch zero → Start at 00:09.1 → Stop at 00:09.3 → Reset zero → Home. The stopped raw frame remains byte-identical, and all 144,000 Home pixels below the status bar match exactly in RGB. Compiled execution remains active throughout; there are no page errors or graphics-restart diagnostics. The production build does not expose the diagnostic GPU counters, so the command totals above remain the separate isolated proof. The reviewed report is `.context/graphics-final/production-apps/clock/report.json` (WASM `da43870…1314c6d`, offline bundle `74d5bc97d495d04b7663c5b9`). This replay makes no drift or speed claim.
