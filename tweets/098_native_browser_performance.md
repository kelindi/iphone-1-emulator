# Native is faster, but streaming is not yet the answer

## Tweet idea

Would moving our original iPhone emulator onto a server make it fast? On the same
machine, native cuts first Settings from3.56s to2.27s. Repeat launches improve only
9%, and Home only36ms. Network delay could eat that gain. We're keeping browser
execution while targeting the emulator's actual CPU cost.

## What you're seeing

[Full browser comparison arm](videos/native_browser_comparison.mp4) ·
[Home thumbnail at0:13](thumbnails/native_browser_comparison.jpg).

The uncut recording shows a fresh phone loading, then four Settings/Home pairs.
Around0:07–0:14 is the first pair; later pairs show repeat navigation. The clip
includes measurement confirmation and one-second gaps. It runs at original speed.
This records the browser arm, not a streamed native session. Both native arms'
actual final frames and timings are retained in the
[comparison evidence](../docs/traces/runtime-comparison/README.md).

## ELI5 technical explanation

We ran the same real phone on the same computer using two execution paths: our
current browser build and the native emulator. Both use the same original apps,
input handling and clock policy. We alternate the run order to reduce ordering
bias, inspect all32 final screens, and separate first launches from repeat use.

Warm Settings takes2.243s in Chrome versus2.045s natively. Home takes0.842s versus
0.806s. Native gets ahead, but a remote phone must then send its screen back to you.
Total added streaming delay above198ms erases the warm Settings advantage; above
36ms erases the Home advantage. We have not measured an actual internet stream.

A separate small H264 test encodes actual recorded screen motion at about0.90ms
of CPU per frame and0.55Mbps for this clip. Encoding looks affordable; this is
throughput evidence, not a claim about end-to-end latency or simultaneous emulation.

## Why it matters

The browser has a cost, especially on first launch, but removing it does not make
the current engine dramatically faster during ordinary use. These small pilot
samples support keeping the existing web architecture rather than immediately
paying for streamed phones. Reference-Mac performance remains unmeasured. No
production speedup, all-app acceptance or deployment is claimed.
