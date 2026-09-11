# Original YouTube authentication probe

This is a local diagnostic for the OS 1.0 application, not a working YouTube
service. The unchanged app receives a challenge and signs its response through
the original Lockdown/Security APIs. The gateway deliberately returns HTTP 503
for the second step and never issues an authentication token.

`youtube_auth.c` accepts only POST requests to the two original
`/youtube/accounts/applelogin1` and `applelogin2` endpoints with
`Host: dm5.google.com` (optionally port 80). Bodies use bounded form parsing.
The first step validates a 20-byte web-safe-base64 `hr1`; its response contains
the fixed local challenge bytes 00 through 13 hex and an opaque echo field.
This repeatable challenge is diagnostic data, not a production authentication
scheme. The second step always refuses authentication. No catalog, thumbnail,
media route, Google account login or external proxy is implemented here.

## Routing and identity

Explicitly pass `--youtube-auth` to `install_services.py`, together with the
expected resolver-file hash, to add `dm5.google.com` to the existing local
resolver rule. The DNS listener answers only that exact name and
`iphone-wu.apple.com`. Existing resolver entries remain intact.

`install_identity.py --source SOURCE --output NEW_IMAGE --source-sha256 HASH`
provisions an internally consistent, locally generated activation bundle on a
new image. Its two key destinations must be absent:

- `/private/var/root/Library/Lockdown/device_private_key.pem`
- `/private/var/root/Library/Lockdown/device_public_key.pem`

The public filename **includes `device_`**: original lockdownd's literal at
0x733d0, loaded at 0x8fd8, establishes this contract. An initial experiment used
the wrong filename and is retained as a failed attempt. Merely comparing two
files that our installer wrote does not prove the OS loaded either one.

The installer replaces the three own activation/certificate data forks and
adds two PEM files. It preserves all other existing file forks and catalog
records except the required parent valence and replacement lengths. Explicit
catalog-ID hole reuse keeps the known crowded HFS catalog within capacity;
preflight and the actual writer use the same allocation policy. Default
`rootfs_work.Work` allocation remains sequential. The output image is mode 600
and contains the private key; receipts contain hashes, not private key bytes.

## Evidence and verification

Run the original app by touchscreen, export its snapshot, and retain complete
gateway exchanges. A saved filesystem log can lag behind the guest's dirty
cache. Missing backing-file bytes alone do not establish missing requests.
RAM reconstruction must retain observed bytes and source offsets, validate
all framing and receipts, and distinguish physical search hits from proven
process ownership.

`audit_youtube_auth.py LOG --guest-public-key PUBLIC_PEM --output NEW_REPORT`
checks the complete ordered challenge/response pair and delivery receipts.
It verifies `hr1 == SHA1(r1)`, challenge echoes, and the actual original RSA
signature over SHA1(r2 || r1). OS 1.0's raw-signing path uses PKCS#1 v1.5
padding around the raw 20-byte digest, without a DigestInfo wrapper. The audit
checks against the submitted certificate and, optionally, the independently
read guest public key. Certificate trust-chain validation is outside its scope.

A passing signature proof does **not** mean authentication succeeded: the
probe's final response is intentionally 503. The next separate acceptance test
must issue a local token through the normal response path and observe the
deferred original feed request. Catalog rendering and playback are later gates.

Focused tests use synthetic inputs, not Apple firmware:

```sh
python3 -m unittest discover -s tools/legacy_services/guest_http -p 'test_*auth.py'
python3 -m unittest discover -s tools/legacy_services/guest_http -p test_dns_packet.py
python3 -m unittest discover -s tools/legacy_services/guest_http -p 'test_install*.py'
python3 -m unittest discover -s tools/firmware -p test_local_activation_identity.py
```

The identity/audit tests require `cryptography`; image-tool dependencies must
also be on the Python path. Build instructions remain in the gateway README.

## Explicit local catalog build

`OWN_YOUTUBE_DEMO=1` enables a bounded compatibility service. The original
phase-two form must pass the existing six-field parser before the service
returns `Auth=iphone1-local-demo-token`. This is a local compatibility marker:
the C handler does not verify certificate trust, signatures, nonce freshness,
or challenge association. Independent signature evidence remains the separate
audit above. The default probe build still returns503 and issues no token.

The demo requires that exact token in the original `GoogleLogin` Authorization
header for its catalog, and accepts the original `Host: gdata.youtube.com`.
Featured, most-viewed, top-rated and the one owned single-video endpoint serve
one static local entry, `iphone1-demo-1`; query keys are bounded to start-index1,
max-results1–100, format2,3 and the known time qualifiers. Unknown paths,
parameters and hosts are rejected; no upstream requests are made.

The entry references only `/demo/clip-1.mp4` and `/demo/clip-1.jpg` at
127.0.0.1. Their compiled arrays live in `youtube_assets.h`. GET and HEAD support
one byte range, including suffix/open-ended ranges; malformed, duplicate,
overflowing and unsatisfiable ranges return416 with the complete size. All
responses fit the32768-byte gateway cap. Feed parsing and original-player
playback still require guest verification; host wire tests are not playback
proof. The initial served clip is silent to isolate video decoding.
