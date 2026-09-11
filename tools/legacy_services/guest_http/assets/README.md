# Owned local video fixture

`clip-1.mp4` is an independently generated five-second animation: a yellow
square moves across a dark background, with “LOCAL DEMO” and a frame counter.
It contains 50 frames at 160×120, 10 fps, H.264 Constrained Baseline level 1.2,
YUV420p, no B frames, with the MP4 index before the media. It is silent to
isolate video decoding from the original audio subsystem.

`clip-1.jpg` is its first frame. `clip-with-tone.mp4` retains the same animation
plus a 440 Hz AAC-LC mono tone at 44.1 kHz/16 kbit/s for a later audio test; the
gateway does not serve that file. These assets contain no YouTube material.

Creation used the repository's `node_modules/ffmpeg-static/ffmpeg` (FFmpeg 6.0),
lavfi color sources (160×120 dark blue and 16×16 yellow, both 10 fps), overlay
position `x=16+20*t:y=58`, Noto Sans text at y=12 and y=96, libx264 CRF30,
`-profile:v baseline -level:v 1.2 -pix_fmt yuv420p -bf 0 -g 10`, and
`-movflags +faststart`. The silent fixture was remuxed using `-map 0:v:0 -c copy
-an -movflags +faststart`; JPEG extraction used `-frames:v 1 -q:v 5`.

Both MP4 tracks decoded completely with FFmpeg on the host. That validates the
fixture, not the emulator. `manifest.json` binds the exact served bytes. Run
`python3 tools/legacy_services/guest_http/embed_assets.py` to regenerate the
bounded static arrays in `youtube_assets.h` from those checked-in files.
