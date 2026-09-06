# Saving keeps the foreground clock moving

## Tweet idea

Saving our browser iPhone was throwing away stopwatch time. The fix keeps elapsed time while processing it in small steps. Production now passes: 60.4 seconds on the real Stopwatch over 60.0022 wall seconds while Save runs. Apple’s app still draws every digit.

## What you're seeing

[Production recording at original speed](videos/foreground_clock_carry_production.mp4): **0:07–0:19** opens Clock and selects Stopwatch; **approximately 0:20–1:20** contains the measured minute; **0:41–0:55** runs an ordinary browser Save; **1:20–1:25** records Lap and Stop; **1:25–1:29** resets to zero. The [production thumbnail](thumbnails/foreground_clock_carry_production.jpg) shows the stopped new lap near 1:24. Loading, navigation, Save, and the complete minute remain in the recording.

The actual raw guest display reads **00:00.4 → 01:00.8** at browser `performance.now()` values **19,628.2 → 79,630.4 ms**: **60.4 displayed seconds over 60.0022 wall seconds (+0.663%)**, passing the 2% threshold. These are tenth-second display readings, not submillisecond app precision. Hardware ticks independently advance about 60.003 seconds. The saved checkpoint timestamp advances during the minute, compiled guest execution remains active, and no browser errors occur.

Lap records **lap 1 01:00.9**, the next lap stops at **00:02.4**, and Reset returns **00:00.0** with an empty lap list. Subsequent stopped/reset captures are byte-identical. The recording uses the final production build, default guarded WebAssembly, software graphics, and a fresh disposable profile. Other functional tests ran concurrently. The earlier isolated candidate also passed at **60.3 / 60.002 seconds (+0.497%)**; its [recording](videos/foreground_clock_carry.mp4) and [thumbnail](thumbnails/foreground_clock_carry.jpg) remain available.

Production validation also passes **26 native suites**, **34 JavaScript tests**, **12 real-browser clock lifecycle checks**, **6 physical-control checks**, and **7 offline checks**. The lifecycle run includes genuine hidden/frozen tabs, exact paused backup restore, and compiled execution resuming in a new generation. [Detailed evidence](../plans/transition-latency.md#foreground-save-time-conservation).

## ELI5 technical explanation

To save a phone, the emulator briefly copies its complete state. The previous browser code then restarted its clock anchor, forgetting time spent making that copy. Another protection limited recovery from a host stall to eight milliseconds and discarded the rest. An earlier real Save-load minute consequently showed only **58.0 seconds over 60.0097 seconds (−3.349%)**; [milestone 047](047_foreground_stopwatch.md) preserves that failure.

A diagnostic repeat measured approximately **755 ms** lost at three capture boundaries and **37 ms** discarded by the recovery limit. The correction keeps the anchor through an ordinary Save and remembers elapsed foreground time still to be processed. Each device update remains bounded to eight milliseconds; previously modeled idle time is subtracted so it is never counted twice. Explicit Pause, hidden-tab pause, restore, and invalid clock readings clear the pending time and require a fresh anchor.

The native default clock policy and older phone compatibility checks remain intact. Pending time is host bookkeeping, excluded from the unchanged v39 backup format. Tests cover long stalls, fractional ticks, idle waits, overflow, rate changes, pause/resume, and exact snapshot bytes. The recording’s diagnostic exports are removed from the clean integration patch. Apple’s binaries and displayed digits remain unchanged.

## Why it matters

Ordinary saving should preserve the phone’s state without steadily slowing its clock. This moves the foreground Stopwatch timing gate from a documented failure to a production pass while retaining bounded device updates and verified pause/restore behavior. It does not establish accurate wall-calendar dates, countdown timers, audible alarms, or every mixed-app workload.
