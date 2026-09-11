# Original YouTube signs our local challenge

## Tweet idea

The 2007 YouTube app just signed a challenge from our local server—and the
signature checks out. Its original security code is doing the work. Next:
give it a small demo video catalog. Playback is still ahead.

## What you're seeing

[Recording](videos/youtube_device_signature.mp4) ·
[Thumbnail](thumbnails/youtube_device_signature.jpg).

The middle section shows opening the original YouTube app and its loading
screen; the final section shows the connection error. That error is expected:
this diagnostic server deliberately returns 503 instead of a final token.
The recording demonstrates the real app run, not visible catalog success.
The accompanying [wire proof](../docs/traces/youtube-auth-probe/auth-proof.json)
establishes that the original app answered the challenge correctly.

## ELI5 technical explanation

Before fetching videos, the old app asks the phone to prove its device identity.
Think of the certificate as a name badge and the private key as the matching
stamp. Our earlier activation setup supplied the badge but discarded its stamp;
the phone generated a different one. It could sign, but its signature did not
match its certificate.

We added a matching pair through normal configuration files. Our first attempt
used the wrong public-key filename. Reading the original OS's exact filename
fixed that mistake. With the corrected files, the unchanged app loads its
identity, signs the local server's challenge, and sends the full second request.
The signature verifies against both its certificate and its public key.

Another trap was delayed disk logging: one snapshot's saved file omitted most
of the exchange. We recovered the observed log pages from RAM, preserving their
offsets and rejecting incomplete candidates. The final passing run has the full
exchange directly in its saved filesystem. Tests and the failed attempts are
retained in the [engineering evidence](../docs/traces/youtube-auth-probe/README.md).

## Why it matters

We have cleared the device-signing obstacle using original Apple code. The next
bounded task is to return a local service token normally and render three demo
entries in the original list. This milestone does not establish working videos,
modern YouTube access, Google accounts or faster emulation.
