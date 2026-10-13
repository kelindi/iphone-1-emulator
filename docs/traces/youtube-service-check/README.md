# Original YouTube service feasibility check

> Historical recordings and publishing artifacts have been removed from Git history. The summaries below describe the original runs; remaining test reports and screenshots are retained. Historical hash receipts may predate this archival cleanup.

YouTube remains at Cannot connect to YouTube. The current Weather/Stocks phone was imported, the original app launched, and the resulting phone exported. The observation harness completes without errors but has passed:null; there was no declared successful YouTube workflow. The local service log is identical before/after. This does not prove absence of attempted traffic to an unconfigured hostname.

`parser-report.json` binds the catalog schema and authentication gate to the original executable. `candidate-three-clips.xml` is an independently authored schema candidate with placeholder asset URLs, not a working catalog. It uses bare video IDs and unprefixed format attributes based on the original parser. Format2/3 are app profile identifiers, not a codec or playback qualification.

The URL resolves iphone-wu.apple.com, but after authentication the HTTP Host header is gdata.youtube.com. The existing Weather/Stocks gateway does not implement these YouTube requests. `auth-report.json` records the original two-stage device-authentication exchange. A local compatibility server appears feasible, but original device-identity lookup and signing must succeed before the app will send its feed request. Neither activation success nor a static XML fixture establishes this.

Prior art: the archived [TubeRepair server](https://github.com/kevinf100/tuberepair.uptimetrackers.com/tree/df7846fd2a7209d260d471b742b8e088e2ff76a2) has useful classic-app feed and video routes. Its newer endpoints and support claims do not qualify this OS1 app. No third-party implementation was copied into the maintained gateway, and no original Apple executable or app token was changed.

Recording and milestone. [Next bounded experiment](../../../plans/youtube-services.md): local first challenge, observe original second signed request, then catalog/thumbnail acceptance and finally one genuine playback attempt. No catalog, thumbnail, video, audio, live YouTube access or account functionality is claimed from this investigation.
