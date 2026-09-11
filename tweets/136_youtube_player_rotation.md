# The YouTube player transition stops restarting the GPU

## Tweet idea

The original iPhone YouTube player now rotates its UI without restarting the emulated graphics chip. All1,695 drawing commands in the replay complete. Video still fails separately, even after the complete local movie downloads—we have isolated the next problem to the media path.

## What you're seeing

[Recording](videos/youtube_player_rotation.mp4) · [Thumbnail](thumbnails/youtube_player_rotation.jpg).

The opening shows the original Featured list; the middle rotates into the movie player; the end shows Apple's “This movie could not be played” alert. The clip is seconds25–100 of the v4 browser run. This is graphics progress, not working playback. Browser cleanup timed out after the recorded interaction, so that harness run is retained as failed, not relabeled a clean pass.

## ELI5 technical explanation

The old app asks its graphics chip to turn rectangular pictures sideways, stretch a background color strip, and fade buttons. Our renderer rejected those specific operations. We added their sampling and blending behavior, checking every source address before changing the screen and preserving pixels outside a clipped shape.

The full renderer suite passes3,024 checks. An ordinary native replay completes1,695 of1,695 drawing commands with zero rejections and zero graphics restarts. The browser interaction also records zero graphics restarts. Its remaining movie error is real.

A separate observer captures the original playback error without changing any of the252,624,866 saved state bytes. Apple reports NSOSStatusErrorDomain code1 with the generic description; we have not established the underlying cause. Both the H264 MP4 and a separate software-codec Motion-JPEG MOV experiment reached an error after complete delivery.

## Why it matters

Graphics failures no longer obscure this movie attempt. The next task is to trace the media preparation result and get actual decoded frames onto the screen. Sound, live YouTube and default deployment remain unfinished. [Evidence](../docs/traces/youtube-player-rotation/README.md).
