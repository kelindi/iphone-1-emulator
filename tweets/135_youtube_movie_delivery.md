# YouTube downloads the movie; playback is the next gate

## Tweet idea

The original 2007 YouTube player now downloads our demo MP4. Fixing its rotated graphics got us to the real movie error screen. We can prove the file arrived intact; getting the old media stack to decode and display it is still unfinished.

## What you're seeing

[Recording](videos/youtube_movie_delivery.mp4) · [Thumbnail](thumbnails/youtube_movie_delivery.jpg).

The opening shows the original local Featured entry. The middle transitions into landscape; the ending shows “This movie could not be played.” This is seconds 25–100 of the browser recording using renderer candidate v2. It does not show successful playback. The later v3 compact-button fix has native replay evidence only.

## ELI5 technical explanation

The player asks for two bytes first, then the complete 8,826-byte movie. Our service replies with the exact requested parts, and a byte comparison confirms the whole movie arrived unchanged. This proves delivery, not decoding.

The phone also asks its graphics chip to rotate pictures and backgrounds. We added those measured operations to the emulator, drawing the actual pixels and preserving clipped edges. The current renderer passes 2,964 checks. Its replay still rejects four commands using a shader whose behavior is not established; those remain explicit failures.

Both native and browser runs reach Apple's real movie error alert. We are investigating the media setup and decoder path. Missing decoder/overlay emulation may matter, but the error alone does not identify the cause.

## Why it matters

We have separated server delivery from the remaining playback problem. The app, network requests and error UI are genuine. Moving video, sound, live YouTube and default deployment remain unfinished. [Evidence](../docs/traces/youtube-movie-delivery/README.md).

## Follow-up: software-codec experiment

A two-second96×72 Motion-JPEG MOV reaches the same original error after exact delivery of all6,842bytes. This tests another codec; it does not prove that its software decoder ran. [Recording](videos/youtube_mjpeg_attempt.mp4), first section catalog, ending error; [thumbnail](thumbnails/youtube_mjpeg_attempt.jpg). Source recording seconds80–120.
