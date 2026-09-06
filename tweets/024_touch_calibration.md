## Tweet idea

Tapping the first iPhone's top row finally hits the icon under the cursor—and dragging now scrolls its real Settings app. The fix came from reading the original touch framework: its sensor grid differs from the later iPhone, and it needs motion velocity as well as position.

## What you're seeing

Recording: [touch_calibration.mp4](videos/touch_calibration.mp4), with [thumbnail](thumbnails/touch_calibration.jpg).

Sections: **0:04–0:12 Text**, **0:35–0:45 Calendar**, **1:06–1:17 Settings**, and **1:17–1:27 upward drag and scrolled list**. The 87-second recording is unsped-up browser output.

The recording shows taps at the actual Text and Calendar icon centers, returning Home between apps, then opening Settings and dragging its list upward. The Settings title remains visible while the list reveals Phone, Safari, iPod, and Photos. These are the genuine OS 1.0 applications responding to emulated Z1 touch reports.

## ELI5 technical explanation

The browser knows a cursor position in a 320 × 480 screen. The touch controller reports a position in its sensor's coordinate system, which the original Apple framework converts back to the screen. We had inherited a later iPhone's sensor calibration. Near the top of the display, a click at `(45,65)` reached roughly `(42,45)` inside the original guest—enough to miss an icon.

We read the family-1 grid setup in the original OS 1.0 `MultitouchSupport` framework and checked the loaded guest's grid in memory. Its X bounds are `0..4511`, and Y bounds are `0..6926`, with zero edge margins. The Z1 CE encoder now maps pixel centers into those bounds. Tests invert the guest's normalization for every X and Y coordinate. The later N82/Z2 encoder keeps its own calibration.

Correct positions exposed a second problem: dragging selected the row under the release point. The real OS reconstructs a touch trajectory from explicit velocity fields in each CE report; ours were always zero. Native tracing confirmed that trajectory code runs. Its conversion uses position units of 1/100 mm and velocity units of 1/8 mm per second, so the encoder now derives signed velocity from each contact's movement and elapsed guest time. Stationary reports stop motion; new and released contacts do not invent motion; large velocities clamp safely.

All 23 native test executables and the WASM smoke check passed. The Z1 suite has 951 checks covering contact transitions, pending-frame backpressure, coordinate mapping, and motion conversion. Browser screenshots confirm top-row app launches and one slow Settings drag. Flicks, pinch gestures, and interaction speed across all apps still need broader testing; this recording does not establish full-speed operation.

## Why it matters

Reliable input means the browser frame can behave like a usable phone: users can select the control they see and reach content farther down a real app's screen. The fix lives in the emulated hardware reports, so it benefits the original guest applications without changing Apple binaries or moving browser hit targets around.
