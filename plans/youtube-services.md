# Original YouTube compatible-service investigation

> Historical recordings and publishing artifacts have been removed from Git history. The summaries below describe the original runs; remaining test reports and screenshots are retained. Historical hash receipts may predate this archival cleanup.

## Current result

**Local catalog now works.** The explicit demo build returns a local token via
the normal response. The original app then fetches Featured and displays the
owned five-second clip, including its exact JPEG thumbnail, title and duration.
An independent wire audit and clean browser recheck pass.
[Milestone134](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/youtube-local-catalog/README.md).

Playback remains unfinished. The movie-player transition repeatedly triggers
graphics restarts. Ordinary native tracing captures genuine 90-degree texture
and destination rotations outside the current renderer's supported cases.
Correct rendering of those captured commands is the current task. The first
clip is intentionally silent to separate video from audio qualification.

## Previous signing gate

**2026-09-10: the original device-signature gate passes.** On an isolated phone,
the unchanged OS1 YouTube app completes both local challenge requests. The
complete saved gateway log and offline verifier prove its 2048-bit signature
matches the submitted certificate and the guest public key. The server
deliberately returns 503 instead of a token, so the app still displays Cannot
connect to YouTube. This is a successful signing diagnostic, not completed
authentication, catalog rendering or playback. [Milestone133](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/youtube-auth-probe/README.md),
[evidence and failed attempts](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/youtube-auth-probe/README.md),
[implementation guide](../tools/legacy_services/guest_http/YOUTUBE.md).

The earlier baseline had no new retained gateway traffic. Static inspection
established a three-entry catalog candidate; its asset URLs remain placeholders.
No Apple executable, saved app token, production image or media playback path
has been modified. [Original contract and baseline](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/youtube-service-check/README.md).

## Original-app contract

- The feed parser accepts feed/entry and startIndex/totalResults/itemsPerPage. Entry IDs remain unchanged; use bare owned identifiers rather than URLs.
- Entries can carry title, author, description, thumbnail, rating, view count and video references. The media-reference parser accepts its profile identifiers2 and3, a URL and duration. These identifiers are not proof of any working codec.
- The request URL resolves iphone-wu.apple.com; authenticated requests explicitly use HTTP Host:gdata.youtube.com. Neither host getter reads a preference.
- A missing device-authentication token defers the feed request and invokes the original YTAuthenticator. Its two endpoints are dm5.google.com/youtube/accounts/applelogin1 and applelogin2. This is separate from a user's modern Google account login.
- The local gateway now implements the two bounded authentication probe routes and exact dm5.google.com DNS handling. Resolver installation is explicit. Feed and thumbnail/media routes remain to be implemented.

## Prior work

[TubeRepair's archived server](https://github.com/kevinf100/tuberepair.uptimetrackers.com/tree/df7846fd2a7209d260d471b742b8e088e2ff76a2) provides classic-app feed templates and video-serving routes. It is useful protocol background, not demonstrated compatibility with this original OS1 binary. Its classic template uses newer /feeds/api/ paths, while this app uses /feeds/ paths and an earlier authentication exchange. The repository is GPL-3.0; no implementation was copied into our maintained gateway. The independent candidate is derived from the supplied original app's parser.

## Authentication result

The first local response supplies a challenge and an opaque echoed field. The
original app now obtains its device identity and signs the challenge through
original Security APIs. The old activation helper discarded its generated
device key; the new opt-in helper/installer retains a coherent pair. The public
filename must be `device_public_key.pem`, as the original binary specifies.
The initial provisioning mistake and its failed run are retained in the evidence.

The final proof checks the original raw PKCS#1 signature over SHA1(r2 || r1),
including nonce and echo checks. No Apple-held service secret was needed for
this local challenge probe. A nonempty token appears to release the deferred
feed in static inspection; that next dynamic step is still unqualified.

## Next bounded experiment

1. Completed: local token through the original response, authenticated Featured request, one owned clip entry and actual JPEG. The user requested one playable local clip, so the first fixture has one entry rather than three.
2. Repair only the captured graphics operations with correct sampling, bounds and atomic commit; qualify non-square pixel fixtures and the actual movie transition in native and browser runs.
3. Once the player opens, qualify actual MP4 requests, container parsing, decoding and moving frames. A catalog or functioning player UI alone is not playback. Recheck Weather and Stocks on the final identity/gateway/renderer combination before any default promotion.
4. Only after a local clip works, consider modern catalog retrieval and conversion to accepted media. Live YouTube access, account support and reliable upstream availability remain unqualified.

The original media/audio path is unfinished. Browser video overlaid on the guest display would not meet this project's original-app fidelity requirement. Playback work may reuse a host decoder behind an accurately implemented emulated device, but that is an untested implementation strategy, not an existing feature.

## Movie delivery follow-up (2026-09-10)

Original CoreMedia fetched bytes0–1 then0–8825 of the owned MP4; exact206 responses and complete body match pass. Renderer v2 reaches the original movie error in a clean browser recording; v3 reduces native graphics rejections to four alternate-shader commands, with2,964 checks passing. Playback is still unfinished. Next: capture the original media setup/decoder failure status before choosing a format or device repair. See [milestone135](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/youtube-movie-delivery/README.md).

## Rotation completed; preparation still fails

Renderer v4 completes all1,695 native drawing commands with0rejections;3,024 unit checks pass. Browser interaction has0graphics restarts but the harness failed at context cleanup, preserved honestly. The separate Motion-JPEG MOV also arrives intact and fails playback. Qualified live error capture finds NSOSStatusErrorDomain/code1; original AVController prepareForPlaybackReply: supplies this result. No typed underlying NSError was exposed. Trace that preparation status producer next. [Milestone136](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/youtube-player-rotation/README.md).

Qualified preparation trace now identifies the actual legacy MeCCA pipeline: reference-movie check returns0, playback preparation returns1 in the separate media process, and callbacks forward that1 to YouTube. Inspect MeCCA internal preparation next. The initial VT/software-JPEG hypothesis was not demonstrated as this app's active decoder path.

## YouTube silent-clip startup diagnosis (2026-09-10)

Qualified original media-process observation proves `AudioDeviceAddIOProc` succeeds, then `AudioDeviceStart` returns `0xe00002d8`. AppleBasebandOutput reports DMA not ready immediately before that return. The mixer maps the failure to MeCCA result 1 and the generic movie error. All 252,624,866 saved bytes match the same-schedule control. A second qualified replay directly confirms the original I2S synchronization timeout: matching configuration writes, no callback hits, state 4 at return and r6=`e00002d8`. A real external-clock/FIFO/DMA model remains next; clock rate/gating and FIFO behavior are still unproven. Silent video does not bypass audio startup. [Evidence](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/youtube-audio-start/README.md) · [milestone 137](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/youtube-audio-start/README.md).
