# Original YouTube local catalog — 2026-09-10

**Catalog, token consumption and thumbnail delivery pass. Video playback does
not yet work.** The [independent wire review](wire-review.json) checks all four
complete exchanges in [the saved log](service.log): original signed challenge,
local token response, authenticated Featured request, and exact owned JPEG.
The original signature verifies against the certificate and guest public key;
the local C service itself deliberately supplies a compatibility token rather
than implementing cryptographic server authentication or Google accounts.

The [clean browser recheck](browser-report.json) passes only its declared
catalog/thumbnail requirements. [Catalog frame](catalog.png). It also records
the unsuccessful playback attempt as an observation. Its recording is linked
from [milestone134](../../../tweets/134_youtube_local_catalog.md).

## Build, fixture and tests

The isolated gateway is 63,776 bytes, SHA256
`073c1a110341bdf31db5af2285f6aaec394c860be0e441dec92400ea6f056c16`, built
with `OWN_YOUTUBE_DEMO=1`. Probe builds still refuse the final token.
[Runtime audit](runtime-audit.json) checks 8,934 instruction words, zero r9
writes, and the owned empty environment. The first audit rejected a new
compiler-marked 32-bit jump table; the corrected audit recognizes that Mach-O
data kind and verifies every relocated target remains aligned inside the
object's text. It does not skip arbitrary undecodable instructions.

[Ten previous response comparisons](previous-response-equality.json) remain
byte-identical. Host tests pass four catalog/range suites, the existing eight
HTTP and four auth suites, and four build-contract suites. Both quiet and
diagnostic socket harnesses pass 7,134 assertions with identical wire digest
`9aa294eead140b3b`. These are host contracts, not playback measurements.

The [fixture](../../../tools/legacy_services/guest_http/assets/README.md) is a
five-second, 160×120, 10 fps, silent H.264 Constrained Baseline MP4, 8,826 bytes.
Its 1,575-byte JPEG matches the delivered thumbnail exactly. GET/HEAD and
bounded single-byte-range responses are implemented. No live YouTube material
is fetched or served.

[Service installation](service-install.json) and [identity installation](identity-install.json)
operate on new images and preserve unrelated existing file forks. Native boot
completed at 10B credits; image SHA256 remained
`d0bf680eb61974a505c137ff81755d1daaacaef46a8cfe6d9c81a47da3bd511c`.
The exported pre-playback catalog snapshot is
`.context/youtube-services/local-clip-browser/final-home.snap`, SHA256
`f686464091bb599b3cfa471528e96a33f7e09a30218f6d40c9f66efb762a3ca8`.

## Playback diagnosis and limits

The [actual browser serial log](initial-browser-serial.log) records repeated
original `Graphics Restart during 3D Blit` messages after the tap. An ordinary
1B-credit native replay captures 75 rejected STARTRENDER commands and 74 driver
restarts. The rejection includes a genuine 90-degree UV rotation; loosening a
float-equality check would not render it correctly. No watched H.264 device
access was observed in that replay. That is not a claim that video decoding
will work once graphics is fixed.

The passive CPU-hook comparison failed exact full-state equality (two device
payload bytes plus checksum differed). Its results are not treated as a
qualified observer. The graphics diagnosis instead comes from the ordinary
control with the existing MBX trace facility. Full witnesses and failed-state
archives are retained under `.context/youtube-services/player-audit/`.

The [initial browser run](initial-browser-report.json) was interrupted by the
configured disk reserve after valid catalog captures. Completed artifacts were
losslessly archived; the clean recheck used an explicit 256 MiB reserve with
no export and completed normally. The initial run is not relabeled successful.

No default image or production frontend was promoted. Movie-player graphics
repair is in progress; video decoding, audio and modern YouTube access remain
separate, unqualified steps.
