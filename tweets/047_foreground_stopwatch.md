# A real minute on the original Stopwatch

## Tweet idea

The original iPhone Stopwatch passes an idle minute, but saving exposes more work: 0.34% slow at idle, 3.35% slow with Save running. Lap, Stop, and Reset work in Apple’s actual app. We’re keeping both original-speed recordings—and fixing the clock without replacing the digits.

## What you're seeing

[Original-speed recording](videos/foreground_stopwatch.mp4): **0:06–0:17** opens Clock and selects Stopwatch; **0:17–0:19** presses Start; **approximately 0:19–1:19** contains the measured running interval; **1:19–1:22** presses Stop and holds the result. The [thumbnail](thumbnails/foreground_stopwatch.jpg) shows the genuine stopped display near 1:21.

The exact raw-canvas samples read **00:00.5 → 01:00.3** at browser `performance.now()` values **18,641.2 → 78,642.8 ms**: **59.8 displayed seconds / 60.0016 wall seconds**, a **−0.336%** difference. The app displays tenths of a second, so this is a displayed-timer measurement, not a claim of submillisecond precision. The final stopped **01:00.5** frame remains byte-identical 2.33 seconds later.

This is one fresh-profile foreground idle run on the production browser build, with guarded WebAssembly execution active and software graphics. An independent browser briefly ran offline reload checks. It does not establish representative mixed-app timing, lap/reset, the countdown timer, or audible alarms. Start and Stop acknowledged about **654 ms** and **248 ms** after the mouse-release call completed, using the first observed button-color change. These are sampled UI response times, not exact guest input-delivery timestamps; they are reported separately and excluded from the between-frame drift measurement.

The follow-up [Save-load recording](videos/stopwatch_during_save.mp4) retains a second complete minute: **approximately 0:19–1:19** is the measured interval; **0:39–0:54** requests and completes ordinary Save; **1:19–1:23** presses Lap and then Stop; **1:26–1:30** resets to zero. The [second thumbnail](thumbnails/stopwatch_during_save.jpg) shows the stopped new lap with the original lap retained. This run **fails** the 2% timing target: **00:00.9 → 00:58.9**, or **58.0 displayed seconds over 60.0097 wall seconds (−3.349%)**. The saved checkpoint timestamp advances during the minute, all samples remain visible/unpaused, and compiled guest execution continues. Other functional browser sessions ran concurrently, so the entire deficit cannot be attributed to Save alone.

Lap records **lap 1 00:59.5** and starts a new lap. Stop holds that new lap at **00:02.2**, retaining the first lap. Reset clears the lap list and returns **00:00.0**. Both stopped and reset screens remain byte-identical in subsequent captures. These controls pass even though the timing-under-load gate remains open.

## ELI5 technical explanation

The emulated phone needs both instructions and a clock. Making it execute instructions faster does not automatically make a minute last a minute. The browser now supplies monotonic foreground timing while the real operating system and Clock app continue doing their own work. Guarded WebAssembly executes supported guest code, with the interpreter handling the rest.

We pressed the app's real Start button, waited for it to begin, and captured its actual screen. Sixty wall-clock seconds later we captured the screen again and read the digits. A separate read of the emulated hardware timer advanced 59.483 seconds; it is supporting evidence, not a substitute for what the app displayed. No host timer was drawn into the phone and no Apple binary was changed. The full recording retains loading, navigation, input response, and the entire measured minute.

In the Save run, the hardware timer also falls short: it advances 58.145 seconds. Two existing policies can discard elapsed foreground time: restarting the host-clock anchor after an atomic checkpoint capture, and dropping time beyond an eight-millisecond catch-up step after a host stall. Those policies protect responsiveness and pause/resume behavior, but they need a measured correction for ordinary foreground work. The precise share of this run's deficit due to capture versus scheduling is still under investigation; no production clock change is claimed here.

## Why it matters

This clears the **idle foreground** part of the [Stopwatch timing target](../plans/web-iphone.md), verifies Lap/Stop/Reset, and exposes a reproducible failure under saving load. The mixed-load and sound gates stay open; this is not full Clock-app acceptance. [Coverage and reproduction](../plans/app-coverage.md#foreground-stopwatch-timing).
