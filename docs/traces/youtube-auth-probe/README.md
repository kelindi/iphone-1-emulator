# Original YouTube local authentication experiment — 2026-09-10

**Final result: the original app's device-signature proof passes.** The complete
9,265-byte backing log contains the ordered first/second authentication requests
and both response-delivery receipts. Challenge hashes and echoes pass. The
actual 2048-bit RSA signature verifies against both the submitted certificate
and the independently read guest public key. [Proof](auth-proof.json),
[exact log](service.log), [identity readback](identity-readback.json),
[browser observation](browser-report.json), [native preparation](native-boot.json).
The [independent review](review.json) also recovers the signed digest through
OpenSSL and confirms the submitted certificate DER exactly equals the phone's
backing activation certificate.

The local challenge probe and copy-only identity installer are implemented.
The probe deliberately returns HTTP 503 after the signed request; it cannot
show a working catalog or playback. See the [protocol and reproduction
guide](../../../tools/legacy_services/guest_http/YOUTUBE.md).

## Retained attempts

1. **Old identity:** original YouTube sends both authentication requests.
   The backing filesystem contains only 1,032 log bytes, but 8,905 actual log
   bytes are recovered from noncontiguous RAM pages. All challenge checks pass;
   the 128-byte signature cannot match the submitted 2048-bit certificate.
   [Proof](old-identity-auth-proof.json), [observed log](old-identity-recovered-service.log),
   [RAM provenance](old-identity-recovered-log.json).
2. **Wrong public-key destination:** a newly provisioned matching pair is
   insufficient because the installer initially used `public_key.pem` instead
   of the original `device_public_key.pem`. This run contains two complete
   first-step exchanges and no signed second step. Its 5,180-byte backing log
   is complete, not truncated; independent RAM recovery matches it.
   [Proof](wrong-path-auth-proof.json), [log](wrong-path-recovered-service.log),
   [installation](wrong-path-identity-install.json),
   [readback](wrong-path-identity-readback.json),
   [browser observation](wrong-path-browser-report.json).
3. **Correct original filename:** the final image reuses the preceding
   2048-bit device key and installs its public counterpart at
   `device_public_key.pem`. The activation bundle is regenerated consistently.
   The actual app now emits the signed second request, and the independent
   signature checks pass. This establishes that the original path works with
   this 2048-bit identity; no 1024-bit downgrade was needed.

The wrong-path readback's `certificateMatchesGuestPublicKey: true` compares the
files our installer supplied; it does not establish that original lockdownd
loaded that public filename. The corrected installer and helper now use the
literal `device_public_key.pem` from lockdownd 0x733d0/0x8fd8. Original private
key loading and YouTube signature allocation use dynamic sizes; these results
do not establish a 1024-bit key-size restriction.

## Build and filesystem qualification

- Frozen gateway: 47,148 bytes, SHA256
  `f4e5a36e9e7ee9161dfcb43858c120c1ba21e363e0ac807847048d15e1898d13`.
- [Linked runtime audit](runtime-audit.json): 7,557 instruction words, zero r9
  writes, 262 compiler-marked literal words excluded, owned null-terminated
  environment. Existing original-libSystem import set retained.
- [Ten old response comparisons](previous-response-equality.json) are
  byte-identical for existing Safari/Weather/Stocks routes. This is a host
  regression check, not a new browser qualification of those apps.
- [Corrected identity installation](identity-install.json) retains 5,121 other
  file forks, adds the two normal key files, and replaces three own
  activation/certificate data forks. No Apple executable is patched.
- A first installation attempt failed on a full HFS catalog leaf before
  publication. Preflight had modeled reusable IDs while the actual file writer
  allocated sequential IDs. Both now share the explicit hole-reuse policy for
  this installer; default allocation remains sequential. Earlier post-install
  fork audits remain valid, but their simulated preflight IDs were inaccurate.
- Focused tests: eight auth/auditor suites, eight DNS suites, 36 installer
  suites, and four activation-identity suites passed. The final filename fix
  also passed the 16 focused identity/allocation suites.

## Recording and limitations

The [final recording](../../../tweets/videos/youtube_device_signature.mp4) and
[thumbnail](../../../tweets/thumbnails/youtube_device_signature.jpg) show the
corrected phone, original YouTube loading, and the expected connection error
after our deliberate 503 response. The cryptographic proof comes from the
retained exchange, not the screen. The browser observation completed without
harness errors; its `passed` is null because it is not a catalog-success test.

The earlier [recording](../../../tweets/videos/youtube_auth_probe.mp4) and
[thumbnail](../../../tweets/thumbnails/youtube_auth_probe.jpg) show the actual
wrong-path browser attempt: dismissing the phone alert, opening YouTube,
loading and its connection error near the end. This is an observation run
(`runCompleted: true`, `passed: null`), not a successful YouTube test.
The recording does not visualize a signature proof.

Raw firmware snapshots remain local artifacts. Public certificates, nonces,
signed exchanges and hash receipts are retained here; private key bytes are
not published. Physical RAM search/reconstruction is not process-ownership
proof. Initial contiguous-page candidates were rejected when their form bodies
failed validation; final recovered logs contain only observed bytes with
complete framing and delivery receipts.

The device challenge/signature gate is cleared. No local authentication token,
original catalog, thumbnails, video playback, live YouTube access or speed
improvement is established. Next: deliver a local token through the normal
response, observe the deferred feed request, and qualify a three-entry owned
demo catalog before attempting playback.
