# Weather choices survive a browser reload

## Tweet idea

The original 2007 Weather app remembers our changes: switch to Celsius, delete New York, reload the browser, then switch back to Fahrenheit. Three saved/restored screens match pixel for pixel. Forecast delivery is still a separate networking task.

## What you're seeing

[Recording](videos/weather_controls_persist.mp4) · [thumbnail](thumbnails/weather_controls_persist.jpg).

The continuous3:47.80 recording shows genuine Weather city management at0:22, Celsius and its reload at0:52–1:14, actual city removal/reload at3:02–3:24, and Fahrenheit/reload at3:24–3:47. Inspection pauses are retained; this is not a speed benchmark. The earlier two-second delete-control capture is transient; the later settled capture exposes Delete before it is clicked.

## ELI5 technical explanation

These controls belong to Apple's real app. Weather changes its own preferences inside the emulated phone, then the browser saves the phone's state. Reloading must bring those choices back as well as draw the screen correctly.

We used ordinary taps and Save, with no edits to the guest's files or memory. Each reload matches every RGB pixel below the status bar—144,000 pixels per comparison. An independent check of the retained screenshots also matches exactly and rejects screens with the wrong units or the removed city still present. The guarded CPU accelerator is active throughout the workflow.

## Why it matters

Weather's unit selection and city removal now have explicit persistence evidence. Adding cities, fetching forecasts and verifying actual temperature conversion remain open. A separate deterministic backend passes ten HTTP/backend tests, but this recording does not connect it to the guest or show live weather. [Full evidence and reproduction](../docs/traces/weather-controls/README.md).
