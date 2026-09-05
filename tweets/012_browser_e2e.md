## Tweet idea

> End-to-end browser test passes: hard refresh loads the site, boots from a snapshot, opens Settings and Safari, and returns home — all in a canvas, no native install.

## What you're seeing

Screen recording: `videos/browser_home_settings_safari_cpi1.mp4` (0:00–0:34)

After a hard refresh, the status bar shows loading progress. Once `Running` appears, the user taps Settings, escapes home, taps Safari, and escapes home again. No console errors and no broken frames.

## ELI5 technical explanation

- The web build compiles the same C emulator core with **Emscripten** to `web/iphone1.wasm`.
- `web/app.js` fetches `firmware/manifest.json`, downloads each firmware part, de-chunks and gunzips it, then passes the bytes into WASM.
- It calls `emu_run()` in chunks and copies `emu_framebuffer()` to a 320×480 HTML canvas every simulated vsync.
- Mouse clicks become touch events; `Escape` becomes the Home button; hold (`Shift+click`) becomes the Sleep/Wake button; `Ctrl+Shift+R` reloads fresh.
- The browser is slower than native (~28 M instr/s vs ~42 M instr/s) because WASM has extra dispatch overhead and the single-threaded JS main loop can't spin as aggressively.

## Why it matters

The browser is the final delivery vehicle: a single URL anyone can open. Passing this test means the core is portable enough for the web and the snapshot format is stable.
