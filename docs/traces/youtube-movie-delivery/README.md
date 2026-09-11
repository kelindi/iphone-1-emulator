# Original YouTube MP4 delivery and player error

`media-audit.json` verifies two complete original CoreMedia exchanges in `service.log`: Range bytes=0-1 and bytes=0-8825, each HTTP206, with exact Content-Range, MIME, length and owned fixture bytes. The complete body SHA256 is ea46a569f354d32c426475539bb40b8072d4aa30c3f39d4f9865bcf0886d7178. `report.json` binds the native post-player snapshot readback. Dirty-cache incompleteness cannot negate the complete exchanges actually retained.

`browser-report.json` is a completed clean run of frozen renderer v2. Its passing requirements are catalog visibility and the real player ERROR, explicitly not successful playback. `player-error.png` and milestone135 recording retain the original framebuffer.

Private candidate source/archive/executable pins and ordinary control traces are in `.context/youtube-services/rotation-v{1,2,3}`. Native v2 has12 graphics rejections; v3 reduces this to4 alternate-shader rejections. Neither replay proves working video. `mbx-tests.txt` records2,964 passing renderer checks. The recording uses v2, not v3.

The media task remains incomplete. Exact media failure status, actual decoder initialization and video scanout require further evidence. No watched h264bpd access was observed in these bounded replays; this alone does not establish why setup failed. A previous CPU-observer comparison failed full-state equivalence and is not used for these claims.

The earlier observer mismatch has now been mapped to `last_contacts+9` and `held+9`: trailing C padding in a10-byte touch-contact structure whose final field is byte8. These bytes were serialized by the old snapshot code. The failed result remains failed; a new paired diagnostic explicitly zeroes touch padding and keeps hook scheduling identical before attempting full-state qualification. Private offset map: `.context/youtube-services/observer-offset/finding.json`.

New media/upstream diagnostic pairs each match all252,628,962 saved bytes and the exact framebuffer. The upstream probe actively observes six pinned original YouTube call sites. No Celestial target has been hit; runtime library mapping is being checked, so this is not evidence that decoder initialization was skipped. See the two `*-comparison.json` reports.

Motion-JPEG follow-up: `mjpeg/` retains the independent boot/image audit, browser report, exact service exchanges and owned fixture manifest. Original CoreMedia fetched bytes0–1 and0–6841 with206; full6,842-byte body equals the fixture SHA. Browser reaches the same error. The two-second software-codec candidate is an experiment, not evidence that the JPEG decoder executed. No default asset or gateway was changed for this private candidate.
