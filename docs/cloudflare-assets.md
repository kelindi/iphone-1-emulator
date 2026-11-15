# Opt-in Cloudflare asset delivery

The split builder preserves the ordinary checked-in `web/` runtime. This branch
also contains an explicit Vercel deployment configuration in `web/vercel.json`:
it builds and serves only the small `.cdn-site/` shell, with runtime assets loaded
directly from Cloudflare. No DNS or billing settings are changed.

`deploy/cdn-release.json` pins the published release. The Vercel build fails
closed if source changes generate a different release, or if any public asset has
the wrong SHA-256, content type, CORS/CORP headers, or immutable cache policy.
Upload every new release before updating the pin. Never overwrite old releases.
The current production deployment is not changed just by creating this branch.

## What is already remote

`web/manifest.json` already loads firmware and the initial saved phone from
`https://iphone.files.garden/`, with SHA-256 verification. The remaining browser
modules, workers, WASM runtime and approximately 12 MB instruction seed were
served from the application origin. CDN cache hits still count as CDN requests.

## Build two matching bundles

From the repository root:

```sh
npm run build:cdn -- \
  --asset-base https://iphone.files.garden/ \
  --out .context/cdn-release
```

The output directory must not already exist. Each build produces:

- `site/`: HTML, firmware manifest, two tiny same-origin worker entry scripts,
  robots/sitemap, a generated service worker and Vercel header configuration.
- `assets/releases/<content-id>/`: browser modules, worker implementations,
  CSS, images, WASM and the matching JIT data. Upload **the contents of `assets/`**
  under the selected asset base, preserving its `releases/` paths.
- `release.json`: public asset URL, file hashes, size and required response headers.

The content ID includes input bytes, asset base and packaging code. Never replace
objects in an existing release. Keep previous releases available for open tabs,
offline users and rollback. Do not point the current production site at a release
until every object is uploaded and verified.

The checked-in `web/` files are not rewritten. Both build output and test evidence
belong in ignored `.context/`, not in Git.

## Required Cloudflare configuration

Verify the actual files.garden/R2 setup before deployment. It is not modified by
this experiment. On all release object responses set:

```text
Access-Control-Allow-Origin: *
Cross-Origin-Resource-Policy: cross-origin
Cache-Control: public, max-age=31536000, immutable
```

Use correct content types: `.js` = `text/javascript`, `.wasm` =
`application/wasm`, `.json` = `application/json`, `.css` = `text/css`, and
appropriate image types. Configure R2 CORS and Cloudflare cache rules as needed;
a JSON metadata file alone does not configure HTTP headers. Test requests with
an `Origin` header and verify CORS remains correct for cache hits.

Serve objects directly from Cloudflare. A Vercel rewrite/proxy to files.garden
still passes requests through Vercel and defeats the request-count objective.
If a Content Security Policy is used, it must permit the release origin for
scripts, styles, images and fetches while retaining same-origin worker entry
points. Test the real production headers in a preview before promotion.

## Why the worker entry scripts stay local

A page cannot construct a Worker directly from an arbitrary cross-origin URL.
The generated page creates tiny workers on its own origin; those scripts import
the versioned implementation. This keeps IndexedDB profiles, browser saves and
worker URL options (`jit=0`) on the existing page origin. No blob workers, runtime
URL overrides or arbitrary user-controlled script origins are introduced.

The generated service worker verifies every local and remote shell file against
SHA-256 before caching it. It serves only exact release URLs across origins and
does not use `skipWaiting`: updates must not mix JS/WASM versions over a running
phone. Firmware keeps its existing separate verified cache.

On a first visit, missing CDN code is a startup failure, not a silent fallback to
large Vercel copies. An already-installed offline bundle can continue from cache.
Keep the existing deployment available for rollback and test fresh-cache visits.

## Local verification

```sh
npm run test:unit
npm run test:cdn:unit
# Set CHROME_PATH if the Playwright Chromium browser is not installed.
CHROME_PATH='/Applications/Google Chrome.app/Contents/MacOS/Google Chrome' npm run test:cdn
# Browser subprocesses blocked? Start the two local origins and inspect the URL:
npm run test:cdn -- --serve
```

The automated browser test runs the real emulator across two HTTP origins. Only
firmware delivery is replaced by exact local bytes already checked by the loader;
runtime code is not mocked. It checks startup, JIT, request routing, compression,
IndexedDB round-trip, hash-verified offline installation and disconnected reload.
Each run uses a new output directory and browser context. Stop manual servers with
Ctrl-C. Do not use an existing personal emulator profile for codec fixtures.

### Verification on November 14, 2026

- All **124 existing JavaScript unit tests passed**.
- **3 split-build tests passed**: URL validation, deterministic release generation,
  worker entry origins, manifest retention, file hashes and refusal to overwrite.
- WASM smoke passed after creating the ignored `web/dist/package.json` with
  `{"type":"commonjs"}`; this Node-only file is not deployed. A fresh checkout
  with Node 26 otherwise misclassifies the committed classic runtime as ESM.
- Standalone Playwright could not launch Chrome under the current process sandbox.
  This automated suite is provided but is **not reported as passed** on this host.
- The build was instead exercised in Aside's real browser. It booted the genuine
  OS with **510 JIT regions enabled**, downloaded page modules and compression
  code from the second origin, and completed autosave.
- The generated service worker installed and cached all **22 shell resources**.
  After **both local HTTP servers were terminated**, reload restored the same
  saved phone with JIT enabled. This confirms application/CDN-server outage
  recovery; it is not a claim that the browser's entire network was disconnected.
- Evidence: `.context/unit-tests.log` and
  `.context/cdn-browser/aside-browser-report.json`.

The browser suite also covers additive release publication, release B waiting
while a release A client remains open, activation after A closes, and offline
reload of B. The checkpoint ownership assertion deliberately expects a detached
input buffer after transfer to the compression worker.

Still required before production: actual Cloudflare header verification, a
Vercel preview with real headers, clean-cache and existing-save tests, and an
old-to-new offline update check. If the automated browser launch is unavailable,
record the equivalent manual checks explicitly rather than claiming the suite
passed. No Vercel savings occur until the production site uses the remote assets.

### Live Cloudflare verification, November 14, 2026

- Published immutable releases `892f0f27196a7ee1ce60faed` and corrected
  `bc87c6b14984b0c9f72d37f8` to the existing `iphone` bucket. Existing firmware
  files were not changed. The deployment pin selects the corrected release.
- Added rules matching only `https://iphone.files.garden/releases/*`: response
  headers set one-year immutable caching and CORP cross-origin; a separate cache
  rule makes these release paths eligible for one-year edge caching.
- The fail-closed Vercel build verified all 16 corrected public assets against
  SHA-256, MIME, CORS, CORP and cache-header requirements.
- The real CDN runtime booted with 510 JIT regions and restored an existing test
  checkpoint on the same page origin. Local two-release testing confirmed that
  B waits for A to close, then activates and restarts with both local servers off.
- Found and fixed an offline-install problem in this browser: caching the original
  streamed Cloudflare response after hashing a clone failed for `jit/seed.wasm`.
  The generated worker now reads once, verifies the bytes, and caches those exact
  decoded bytes with encoding/length headers removed. The real-CDN offline
  installer then completed successfully. The ordinary web template is unchanged.
- Repeated full-size saves failed with `Failed to fetch` inside the compression
  worker in this browser. The same failure reproduced on the unchanged `web/`
  baseline across three manual saves; this is not isolated to the CDN migration.
  Earlier complete checkpoints remained readable and restored successfully.
  Do not report the full automated persistence suite as passed.
- Deployment is not complete: Git staging was blocked by write access to
  `/Users/kelindi/ikigai/iphone-1/.git/worktrees/iphone-1-cloudflare-test/index.lock`.
  No commit or push was made, and no Vercel preview or production promotion has
  happened. Vercel CLI browser sign-in also stalled with a disabled authorization
  control; its pending login was terminated. Use the existing Git-connected
  `slideto.run` project (root `web`, outside-root build files enabled) after access
  is granted. Production remains on `da5e9cc`.

Final real-CDN check: the corrected release waited while the original release
remained open, then activated after that tab closed. It restored the exact same
saved checkpoint, retained all 22 verified shell files, and restarted with JIT
from cache after the local application server was terminated. The browser's
entire network was not disabled. Evidence is in
`.context/final-cloudflare-report.json` and `.context/final-live-verification.log`.
