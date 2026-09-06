# Calendar’s hours return on the accelerated graphics path

## Tweet idea

Our original iPhone’s accelerated Calendar view was blank. Its graphics driver stores a whole day in a tall image and shows a small slice. We now decode that slice correctly: hours scroll, Month renders, and the event editor opens. 2,016 graphics checks pass.

## What you’re seeing

[Original-speed recording](videos/calendar_mbx_rendering.mp4): **0:25–0:35** opens Calendar Day; **0:35–0:43** scrolls from afternoon/evening to earlier hours; **0:43–0:49** switches to Month; **0:49–0:55** opens Add Event; **0:55–1:02** cancels and returns to complete Home. The [thumbnail](thumbnails/calendar_mbx_rendering.jpg) is the actual scrolled-hour frame at 0:42. The initial startup and explicit candidate-snapshot import are retained. No animation is sped up.

This isolated candidate completes 449/449 2D commands and 1,559/1,559 3D renders with zero rejected draws. Its final Home pixels match the starting Home exactly below the status bar. This recording verifies navigation and rendering; it does not demonstrate saving a new event or measure app speed.

## ELI5 technical explanation

The day view is like looking through a window at a long sheet of paper. Calendar draws its hours into an image with 1,024 or 2,048 rows, then copies just the visible rows onto the phone. Our decoder only understood shorter allocations and misread one height bit. Reading the original graphics producer and capturing its real command revealed the independent height exponent and exact source rows. The correction accepts bounded, unfiltered slices and checks every sampled memory page before writing pixels.

The Month transition also stretches a narrow gradient across the screen. Its correction reuses the existing bilinear sampler, requiring the full texture width, integer source rows and unchanged vertical size. Earlier malformed cropped-strip tests still reject their commands. The combined changes add 117 checks to the existing 1,899, including captured pixels, malformed bounds and missing late source/destination pages without partial writes or false completion. Apple’s framework and app binaries remain unchanged.

## Why it matters

Accelerating a genuine phone requires preserving every app’s actual drawing behavior. This fixes two concrete Calendar graphics gaps and gives us a recorded app navigation gate. The correction is integrated, and a separate production-browser replay passes the same navigation with the compiled CPU engine enabled. The default phone still uses software composition while Safari and broader accelerated app coverage remain under review. [Detailed evidence and scope](../plans/graphics-speed.md).
