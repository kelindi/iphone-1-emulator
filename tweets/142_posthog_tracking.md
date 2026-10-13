# 142 — PostHog tracking for 2007.mobile

## Tweet idea

The 2007 iPhone demo now has usage tracking: visits, startup time, the first real touch, Home and physical controls, saves, recovery and offline setup. The original OS still draws every screen.

## What you're seeing

This is an analytics milestone, with no visual UI change. The earlier [emulator recording](videos/browser_home_settings_safari_cpi1.mp4) and [thumbnail](thumbnails/browser_home_settings_safari_cpi1.jpg) provide guest context; the current shell is shown in the verification screenshots. Browser verification screenshots and request receipts are in `.context/posthog/`; no separate screen recording is needed for invisible instrumentation.

## ELI5 technical explanation

The browser sends small named events to PostHog's US service. Its SDK loads asynchronously, and early events wait in the official snippet's queue. The emulator runs independently of that download. Physical controls include keyboard presses; the first touch is counted once per page, without recording every touch coordinate or animation frame.

Tracking covers pageviews/pageleaves, emulator start/ready/failure, startup duration, first touchscreen interaction, physical controls, save success/failure, pause/resume, backup import/export, reset, retry, previous-save recovery, and offline setup success/failure. Localhost and branch previews do not load the analytics SDK; production canonical and legacy public domains do.

Guest framebuffer pixels, entered text, serial output, file names and backup contents are not event properties. Session replay, automatic DOM capture and automatic exception capture are disabled. Error events report the operation and error category, not raw messages. Full current/referrer URLs are denied as event properties; page/host and standard traffic metadata remain available. Browser Do Not Track is respected; anonymous visitors are not identified into person profiles.

The small local `analytics.js` module is included in the content-hashed offline bundle and its upgrade fixture. A blocked third-party SDK cannot break input, startup or offline restoration. The provided browser project key was verified against the US flags endpoint; the EU endpoint rejected it. No project administration or settings were changed in PostHog.

Verification: 122 JavaScript unit tests and the WASM smoke pass. Real Chrome checks load the actual PostHog SDK alongside the genuine guest, verify configuration, unlock and keyboard Home events, and inspect batched pageview/start/ready requests. Test analytics requests are intercepted, rather than ingested into production. A test-only bot-filter opt-out permits that automated browser; production retains PostHog's normal bot filter. The first offline fixture changed analytics bytes while retaining the original hash; its failure is retained, and the corrected fixture serves original bytes to the service worker. The corrected disconnected reload passes. A separate blocked-SDK browser run reaches guest ready and delivers Home input without page errors.

## Why it matters

Visits can be compared with successful emulator startup and actual interaction, while failures and startup duration expose where the demo loses users. The instrumentation adds no dependency to the guest's boot or rendering path.

## Release

Deployed to **https://2007.mobile/** from source commit `7f779a53c3c3367038b8769b4f2debbcb42c79eb` via production deployment `dpl_A6jf5eoUpHS7w785MQ3g1JN8AgJK` (READY). The live guest and actual SDK reached ready without page errors. One labeled `$is_test: true` verification event was accepted by the US ingestion endpoint with HTTP 200 and `{"status":"Ok"}`. Production bot filtering remains enabled; only the disposable verification browser opted out for this event. Receipt: `.context/posthog/live-report.json`; screenshot: `.context/posthog/live-1080p.png`. [PR #8](https://github.com/kelindi/iphone-1/pull/8).

SDK reference: [PostHog JavaScript configuration](https://posthog.com/docs/libraries/js/config).
