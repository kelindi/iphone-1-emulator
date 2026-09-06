# 093 — The default-phone local demo reaches Safari, then fails qualification

## Tweet idea

Our own tiny HTTP server now boots inside a pristine accelerated iPhone OS1 phone. Original Safari loads its pages—but the form test exposed graphics hangs and a vanished server process. We kept the complete failure instead of shipping the demo prematurely.

## What you're seeing

[Full original-speed recording](videos/default_local_demo_qualification_failure.mp4) · [thumbnail](thumbnails/default_local_demo_qualification_failure.jpg).

The roughly13-minute recording includes diagnostic snapshot import, genuine Home/Settings/Contacts, numeric address entry at3:20–5:00, the labeled local landing page around5:34, and Next around6:06. The form interaction stalls from about6:59 onward. Around12:18 we export the failed state; the session ends without a completed form, bookmark or reload gate. The thumbnail shows the actual stalled original Safari form. No screen is fabricated and no retry is hidden.

## ELI5 technical explanation

The little server is our own ARMv6 program running through Apple's original operating-system APIs. Its installer preserves every Apple file and the default accelerated graphics settings. Native boot proves it is listening, and Safari visibly loads two pages.

A full workflow is a stronger test. During form entry Apple's graphics driver repeatedly reports a pending blit hang. A later snapshot also shows the server process is gone, with no recorded reason. Apple's crash-reporting tool itself left a crash report, but that report is not for our server. We preserved the snapshot, logs, actual frames and exact program/runtime hashes so these two unresolved problems can be investigated separately.

## Why it matters

This establishes where the maintained default-image integration works and where it fails. The earlier isolated demo remains valid; default distribution remains blocked. No production firmware, graphics setting, daemon restart policy or user data was changed to hide the failure. [Gate table and retained evidence](../docs/traces/guest-http-default/README.md).
