# 139 — Apple logo and loading bar

## Tweet idea

An Apple logo and a quiet loading bar while the original iPhone gets ready. Once loading finishes, the real 2007 lock screen takes over.

## What you're seeing

[Recording](videos/apple_logo_loader.mp4) · [Thumbnail](thumbnails/apple_logo_loader.jpg)

The first three seconds show the new loading overlay, including mobile sizing and reduced-motion behavior. The recording predates the final removal of the instruction label beneath the phone. A deliberately delayed HTTP 503 then tests the error view; Try again starts the genuine guest, and the logo/bar disappear when it is ready. The later error is injected at the Worker message boundary to verify runtime recovery and keyboard retry, not a reproduced emulator failure. This recording is a controlled local recovery test, not a normal-startup speed benchmark.

## ELI5 technical explanation

The browser has to download and prepare the phone before the original operating system can draw its screen. We replaced the spinner and visible loading copy in that browser overlay with an inline Apple silhouette and a small gray loading track. The logo's SVG geometry comes from [Simple Icons](https://github.com/simple-icons/simple-icons/blob/develop/icons/apple.svg).

The worker reports loading stages and received bytes but does not know the total time or size of all preparation work. The bar therefore indicates ongoing work without claiming a percentage. Its motion uses a CSS transform, and reduced-motion preferences stop it. Loading stage text remains available to screen readers. Errors hide the logo and bar, reveal the actual error, and retain the existing recovery buttons.

This is browser loading UI. Apple's guest binaries and genuine framebuffer remain unchanged. The revised HTML/CSS are included in the content-hashed offline shell.

Verification: 115 unit tests, 18 loader/recovery browser checks and six offline checks pass locally. The browser checks cover mobile fit, accessibility, reduced motion, unknown-progress semantics, error visibility, physical-input gating, genuine startup after retry, keyboard retry and no page errors. Offline installation, disconnected reload, corrupt-cache detection and recovery also pass. Screenshots and reports are retained in `.context/apple-loader-final/` and `.context/apple-loader/offline/`.

## Why it matters

The loading screen now fits the phone's visual language while still telling users when setup is active and how to recover if it fails. Deployment remains deferred at the user's request.
