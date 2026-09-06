# Original Safari loads a real local demo

## Tweet idea

The original 2007 Safari now loads pages from a tiny server running inside the emulated iPhone. Links, a form and bookmarks work—and the running server and bookmark survive autosave/reload. Real Safari, real guest sockets, clearly labeled local demo content.

## What you're seeing

[Full recording](videos/091_safari_local_demo.mp4) · [Thumbnail](thumbnails/091_safari_local_demo.jpg)

The 11:16.76 recording runs at original speed. Around 03:45–04:15, the numeric address opens the local page; 04:20–06:25 shows the link and `hi` form submission; 06:35–08:30 shows bookmark creation and Home/revisit; 08:55–11:00 shows real autosave/reload, the retained bookmark and new server requests. Initial snapshot Import is diagnostic setup for this isolated phone.

## ELI5 technical explanation

We wrote a small program that the original OS can launch. It listens on the phone's own local network address and sends three simple HTML pages. Safari uses its original keyboard, networking code and renderer to load them. Nothing draws a replacement browser or patches Apple's app.

The difficult part was preserving a real running phone and service through a snapshot. One native boot reached Home with the listener active; a checked, lossless snapshot then restored in the browser. Seven actual requests and responses, 20 browser checks and three exact screenshot comparisons pass. The last two requests arrive after ordinary autosave/reload.

## Why it matters

Safari now has a verified useful workflow through the guest's real network stack. This creates a path for compatible local app services. The demo remains isolated and is **not installed in the default phone yet**; general internet access and Weather data are separate work. [Evidence, preserved diagnostic failures and limits](../docs/traces/guest-safari/README.md).

Later scope clarification: retained filesystem inspection verifies this imported
phone used software composition (`LK_ENABLE_MBX2D=0`, OGL0). The current
accelerated default uses MBX2D1. Its separate maintained-service qualification
fails at the form workflow; [093](093_default_local_demo_qualification_failure.md)
records that failure. This successful isolated recording is not accelerated-default
acceptance.
