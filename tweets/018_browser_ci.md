## Tweet idea

> The browser build is wired to CI: every push builds the WASM emulator and deploys via GitHub Pages. Enable Pages once and the site goes live on `.github.io`.

## What you're seeing

Screen recording: `videos/browser_home_settings_safari_cpi1.mp4` (0:30–0:34)

The page is served locally, but the same build artifacts are what CI deploys. A GH Pages load recording will be added once the repo is enabled.

## ELI5 technical explanation

- GitHub Actions builds the project on every push: native `cmake` test suite and Emscripten `emcmake` for the `web/` WASM target.
- The action concatenates firmware chunks, builds `web/iphone1.wasm`, and deploys the `web/` directory to GitHub Pages.
- After the PR merges, the owner goes to repo Settings → Pages → Source: "GitHub Actions", and the site appears at the usual `.github.io` URL.
- No Apple firmware is in Git itself? The browser build reuses the chunked firmware assets under `web/firmware/`.

## Why it matters

CI + Pages is the "visit the website and it works" part. It turns the emulator from a local demo into something anyone can open in a browser.
