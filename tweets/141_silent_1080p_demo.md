# 141 — A silent 1080p tour of the original iPhone

## Tweet idea

18 seconds with the original iPhone OS: slide to unlock, open Settings, calculate 12 + 34, and return Home. Real emulator footage, framed closer and played 1.5× faster than the previous edit.

## What you're seeing

[1080p demo](videos/iphone_demo_1080p.mp4) · [Thumbnail](thumbnails/iphone_demo_1080p.jpg)

The 18-second landscape MP4 is 1920 × 1080 at 30 fps, with 540 frames and no audio stream, titles, or added effects. It shows the current minimalist phone shell centered on its existing plain background, enlarged by 30% with the whole case visible.

- 0–2.7 s: original lock screen and real slide-to-unlock gesture.
- 2.7–4.1 s: genuine Home and the Settings launch.
- 4.1–6.5 s: original Settings, followed by physical Home.
- 6.5–11 s: Home, Calculator launch, and the initial zero display.
- 11–16.1 s: clear and enter `12 + 34 = 46`, holding the result.
- 16.1–18 s: physical Home, original closing animation, and settled Home.

Timestamps are approximate scene boundaries. Only the initial browser startup was trimmed; all retained footage plays continuously at 1.722× original speed (1.5× the previous 27-second edit), as requested in the follow-up. The thumbnail comes from the final encoded clip at 3.2 seconds. This is a functional tour, not a speed benchmark.

## ELI5 technical explanation

The phone screen is drawn by the original iPhone OS inside the emulator. We recorded the browser at the final 1080p dimensions, using a disposable profile and byte-identical local copies of its shipped firmware to avoid waiting on remote downloads. Mouse gestures and the existing physical Home control interact with the genuine guest; its screen is never recreated or substituted.

The original screen has only 320 × 480 pixels. Recording a 1920 × 1080 browser preserves the sharp surrounding case and background, while retaining the guest's native pixel detail. It does not invent higher-resolution app graphics.

An isolated Rust project in `.context/iphone-demo/` uses fframes 1.2.0 with its Metal GPU renderer. It plays the captured video through synchronized video media and `AudioMap::none()`, then exports H.264 with CRF 16. The intermediate capture is retimed from 31 to 18 seconds at 30 fps. fframes applies a constant 1.3× centered scale, leaving about 75 pixels above and below the complete case. Runtime media has no populated duration metadata in this fframes version, so the video explicitly declares the verified 18-second duration.

The first recording tapped Notes instead of Calculator; the independent display check rejected it. That failed attempt is retained under `.context/iphone-demo/capture/`. The corrected recording under `capture-final/` completes every action without browser errors. Its Calculator display matches the repo's independently reviewed `46` reference within the existing two-level RGB tolerance; the largest actual channel difference is one.

Verification: fframes inspected 69 frames with no problems; the 12-frame contact sheet and full-resolution lock, Settings, result and final Home views were visually reviewed. FFprobe confirms 1920 × 1080, 30/1 fps, 540 frames, 18 seconds, H.264/yuv420p and no audio stream. FFmpeg decodes the complete final MP4 without errors. The file is 2,282,469 bytes. Emulator code and deployment were not changed.

## Why it matters

There is now a compact, shareable video showing the original phone's working unlock, app launches, arithmetic and Home navigation, using the current browser presentation.
