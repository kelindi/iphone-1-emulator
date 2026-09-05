## Tweet idea

> Visit the website and the original 2007 iPhone home screen shows up in your browser. The emulator core is compiled to WebAssembly; the firmware is fetched from multipart chunks. No install, no native binary.

## What you're seeing

Screen recording: `videos/browser_home_settings_safari_cpi1.mp4` (0:30–0:34)

A hard refresh (`Ctrl+Shift+R`) clears cache, the page fetches `manifest.json`, then downloads the 1A543a kernel, device tree, and compressed home-screen snapshot in parts. Status changes from "Loading…" to "Running" and the home screen appears.

## ELI5 technical explanation

- **WebAssembly / WASM** lets C/C++ code (our emulator) run in a web browser almost as fast as native, but sandboxed.
- The browser build reuses the same portable `core/` as the native build. `web/emu_api.c` exports functions like `emu_init`, `emu_run`, `emu_set_cpi`, and `emu_framebuffer` for JavaScript to call.
- The firmware files are committed in chunks (<100 MB each) to satisfy GitHub's blob limit; JavaScript concatenates and gunzips them in memory before passing the buffers to WASM.
- Instead of a multi-minute cold boot, the site loads a snapshot that restores RAM, CPU, and device state from an already-booted home screen.

## Why it matters

This is the long-term delivery format: anybody can load a URL and get a real original iPhone. It also proves the core is clean enough to be portable to browsers, not tied to a native UI.
