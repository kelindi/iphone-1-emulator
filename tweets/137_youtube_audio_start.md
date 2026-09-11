# A silent movie still waits for the audio hardware

## Tweet idea

Why won't a silent video play on our emulated 2007 iPhone? The original player still starts its audio device—and that device reports “not ready.” We traced that real error all the way back from YouTube's generic movie alert. The next missing piece is audio hardware behavior.

## What you're seeing

Reuse [the player recording](videos/youtube_player_rotation.mp4), particularly the final 15 seconds, and [its thumbnail](thumbnails/youtube_player_rotation.jpg). The visible error is unchanged; this milestone is the diagnosis behind it. A dedicated recording would be clearer once playback advances. The earlier browser harness cleanup failure remains recorded; this diagnosis uses separately qualified native runs.

## ELI5 technical explanation

The app downloads the whole test movie and opens its player. Before playing, Apple's media software starts a sound device, even though this test movie has no sound track. Registering the sound callback works, but starting the device returns “not ready.” Several layers simplify that error until YouTube only knows “This movie could not be played.”

We watched those original function returns without changing their behavior: a control run and the observed run produced identical saved states, comparing every one of 252,624,866 bytes. A second qualified run follows YouTube into the original kernel audio driver: its clock-synchronization wait times out, with no clock callbacks, before it starts audio transfers. No clock fix or working playback is claimed.

## Why it matters

We have a specific startup failure to fix instead of trying more video formats blindly. The local catalog, exact movie delivery and player rotation already work. Video decoding, audio output and live YouTube are still unfinished. [Evidence](../docs/traces/youtube-audio-start/README.md).
