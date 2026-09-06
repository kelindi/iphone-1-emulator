# The genuine graphics path exposes its next missing cases

## Tweet idea

The original iPhone's graphics driver can already submit thousands of real drawing commands to our emulator. Enabling its composition path in a copied work image reaches Home and Settings—and the first four rejected renders led us to its original clipping rules. An isolated correction now passes those drawing gates. Browser timing and broader app checks come next.

## What you're seeing

**Nonvisual command-trace milestone.** There is no claimed browser speed recording or newly shipped visual feature. The evidence is the genuine MBX register/command trace, full rejected producer records, and the [graphics investigation](../plans/graphics-speed.md). Native screenshots are retained as diagnostic evidence and are not presented as an original-speed screen recording.

## ELI5 technical explanation

The phone currently asks its CPU to combine most screen pixels. Its original software also has a path that gives drawing work to the graphics chip. We enabled that setting in a copied disk image, kept OpenGL's separate setting disabled, and booted the genuine system.

Comparing all 5,120 file forks proved that only the launch setting changed. The real driver submitted both 2D commands and 3D render operations. Most completed, but several reached clip cases that the strict renderer does not yet understand. The driver restarted graphics instead of receiving a made-up successful result.

One recorded command clips the whole drawing at the right edge: its left and right clip positions are both 320. Others expose a disagreement between fractional vertex coordinates and tile-aligned clip bounds. We preserved their original command words, then disassembled the genuine producer. It adds exactly 0.5 to each clip edge before converting to integers; the decoder previously modeled a later producer's different biases. The isolated correction supports both measured rules, validates empty drawing regions, and passes all 1,849 command/pixel checks. A fresh boot then completes every measured submission without a graphics restart.

## Why it matters

This identifies a concrete hardware-model path for reducing software composition work. The native instruction probe is encouraging, but real browser measurements and broader transition checks still block promotion. The final native Settings comparison differs from the software reference only by one channel level in 818 pixels. The production phone keeps its current software path until correctness and real browser timing checks pass.
