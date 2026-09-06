# Sixteen original apps in one uninterrupted phone

## Tweet idea

One browser tab, one original iPhone OS session, all 16 apps. The real Calendar editor, Calculator, keyboard and Settings list work through the emulated graphics chip, and every app returns to the same intact Home screen. The remaining network, camera and media limits are visible too.

## What you're seeing

[Original-speed recording](videos/continuous_sixteen_apps.mp4) · [thumbnail](thumbnails/continuous_sixteen_apps.jpg).

The complete recording lasts about **10 minutes 28 seconds**. Representative moments: **00:47** Text composer, **01:36** Calendar Add Event, **04:21** Maps keyboard, **06:25** Calculator showing 5, **07:45** Settings after scrolling, **08:22** Contacts, **09:38** Safari and **10:24** final Home (thumbnail). The detailed sixteen-app timeline is in [the graphics plan](../plans/graphics-speed.md#one-continuous-sixteen-app-phone).

This is one ordinary MBX checkpoint import and one uninterrupted browser/guest session. The full recording keeps loading, deliberate observation waits and every Home return. It contains no speed-up or stitched app sessions. Fixed waits and concurrent functional work make it a navigation/rendering demonstration, not a latency benchmark; measured speed is documented separately in milestone 055.

All **46 screenshot stages** were manually reviewed. The same guest completes **6,489/6,489 2D and 16,850/16,850 3D commands with zero rejections**. Each of its 16 Home returns matches all 144,000 native RGB pixels below the status bar exactly: **2,304,000 pixel comparisons, zero changed pixels**. There is one worker, one import and one post-import JIT generation, with compiled execution active throughout. Counts subtract initial from final cumulative telemetry once; intermediate samples are never added together.

The Clock probe remains on World Clock in this recording, so **Stopwatch navigation is unverified here**. Its focused Start/Stop/Reset proof is [milestone 054](054_clock_mbx_mirrored_column.md), a separate session. This missed transition is preserved, not retried or hidden.

## ELI5 technical explanation

Opening each app on a freshly restored phone can hide problems that appear only after several apps have shared memory and graphics resources. This test opens the original apps one after another on the same running phone. The emulator counts every submitted graphics command, checks that it finishes without rejection, and compares the Home screen after every visit.

The app pixels come from the genuine operating system, not browser recreations. The HTML around them provides the case and clickable physical buttons. All 16 Home returns are checked using the native framebuffer below the changing status bar, while screenshots confirm what the apps actually show. A visible Calendar editor check also rejects the unchanged Day screen, so a tap coordinate alone cannot count as a successful action.

Earlier failed probes and overly strict image comparisons are preserved separately in the engineering evidence. They do not count as passing walkthroughs or emulator rendering failures. The accepted pass uses the graphics/Home guards, the concrete Calendar check and manual review of all app stages.

## Why it matters

This adds evidence that the combined renderer survives an accumulated sequence of genuine app visits, beyond the earlier independent fresh-phone checks. It does not establish unlimited session stability, every app workflow, Internet access, calls/messages, camera preview/capture or media playback. Camera retains its storage alert; YouTube, Stocks and Weather show connection/data errors; Maps has no tiles; Photos and iPod have empty libraries. iPod's More probe stays on No Content.
