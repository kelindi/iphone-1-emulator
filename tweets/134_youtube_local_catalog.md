# The original YouTube list has a local video

## Tweet idea

The 2007 YouTube app now lists our own demo video—with its thumbnail, title and
duration. The original app signs in to a local compatibility service and parses
the feed itself. Playing it exposed the next obstacle: a graphics operation
the emulator cannot draw yet.

## What you're seeing

[Recording](videos/youtube_local_catalog.mp4) ·
[Thumbnail](thumbnails/youtube_local_catalog.jpg).

The opening section shows the original Featured list and our LOCAL DEMO
thumbnail. The later section taps the clip; the player transition does not
complete. This is a catalog milestone, not working video playback. The recording
is seconds 35–135 of the clean browser recheck; no replacement app UI is used.

## ELI5 technical explanation

We gave the old app the kind of server replies it understands. It completes its
own challenge, reads a local service token, asks for Featured, and receives a
one-entry feed plus the exact JPEG we created. Captured requests and an
independent signature/feed audit confirm this sequence.

The clip itself is a tiny, silent five-second animation with a moving square and
frame numbers. The local server supports complete and partial downloads so the
old player can request sections of the MP4. Host tests check those responses,
but they do not prove that the phone can decode the video.

Tapping the row exposes a graphics failure during the movie-player transition.
The original driver repeatedly restarts a failed 3D blit. An ordinary native
replay captures the rejected command: texture coordinates rotated by 90 degrees
that the current renderer does not support. Work on drawing those pixels
correctly is in progress; sending a pretend completion would hide the failure.

## Why it matters

The original YouTube app can now consume a local catalog. That separates the
retired-service problem from graphics and video-decoding work. Playback, sound,
live YouTube and default deployment remain unqualified. [Evidence](../docs/traces/youtube-local-catalog/README.md).
