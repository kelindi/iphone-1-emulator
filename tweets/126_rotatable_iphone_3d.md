## Tweet idea

The original iPhone now has a 3D view. Spin it around to see the aluminum back and black antenna cover, then slide to unlock on its actual running screen. One phone, two views.

## What you're seeing

[Recording](videos/rotatable_iphone_3d.mp4) · [Thumbnail](thumbnails/rotatable_iphone_3d.jpg).

At approximately 11–14 seconds, switch from 2D to the angled 3D lock screen. At 14–21 seconds, slide to unlock through that angled screen. At 22–26 seconds, drag the case to reveal its back and sides. At 27–30 seconds, open Settings and press the model's Home button. At 30–32 seconds, rotate with the keyboard, zoom, return to 2D, and check a 390-pixel mobile viewport. The final check deliberately loses the WebGL context and returns to the working 2D phone. Playback is not sped up.

## ELI5 technical explanation

Inspired by the 2D/3D switch and rotatable case at [ipod.kelindi.ca](https://ipod.kelindi.ca/), the surrounding phone now has a locally authored Three.js model: rounded aluminum body, chrome rim, black glass, antenna cover, camera, controls, and port details. The geometry is an approximation of the first-generation iPhone, not a scanned or CAD-certified replica.

The screen is a texture fed by the existing emulator canvas. Apple's actual OS still draws every guest pixel. A ray from the pointer into the scene finds the screen and converts its location into the original 320 × 480 touch coordinates. Screen drags operate the phone, while case drags rotate the model. Home and edge buttons route to the existing device controls. Pointer cancellation, changing views, and losing focus release held inputs.

The model also supports mouse-wheel zoom, arrow-key rotation, and a Front view reset. It renders on changed frames or camera movement, and stops its animation loop in 2D. The library and textures are local; the offline bundle includes the new modules. WebGL initialization failure or context loss falls back to 2D. Runtime errors also reveal the original recovery controls.

`npm run test:3d` passes 12 Chrome checks, including a real angled unlock, Settings, Home, view switching, and injected WebGL context loss. Screenshots of the model, Settings, and mobile layout were reviewed. All 115 JavaScript unit tests pass. [Retained report](../docs/traces/phone-3d/report.json). Rotation/zoom/layout checks include visual review; this is not a geometry-fidelity or performance benchmark. Testing used cloud Chrome with software WebGL, including a narrow viewport; physical mobile devices, Safari, multitouch gestures, every edge button, and a complete disconnected update cycle were not qualified here.

## Why it matters

The phone can now be explored as a physical object while still running the genuine original software. Users can inspect the first iPhone's recognizable back and sides, interact with its live screen, and switch to the familiar flat view without restarting their session.
