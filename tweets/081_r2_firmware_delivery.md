# The browser phone loads from R2

## Tweet idea

The original iPhone emulator's 187 MB firmware package has moved out of the web deployment and onto Cloudflare R2. Every public object is SHA-256 verified before use, the real phone boots and navigates, and an intentionally corrupted offline copy still fails closed and recovers cleanly.

## What you're seeing

[The actual R2-backed browser recording](videos/r2_firmware_delivery.mp4) starts with the in-phone download indicator and reaches the genuine iPhone OS 1.0 Home screen at **0:07**. Settings opens at **0:08–0:20**, Home returns at **0:20–0:27**, Safari opens at **0:27–0:34**, and the physical Home button returns again at **0:34–0:41**. The [thumbnail](thumbnails/r2_firmware_delivery.jpg) is the completed remote-backed startup frame.

This is the unedited Playwright recording from the accepted browser run. A separate offline run installs the same remote package, disconnects the browser, reloads the genuine guest, corrupts the cached kernel deliberately, observes integrity rejection and cache removal, reconnects, and restores the phone from R2. Its six checks pass in 24.1 seconds.

## ELI5 technical explanation

The webpage itself is small, but a usable emulated phone also needs a kernel, a hardware description, a prepared disk image, and a saved Home-screen state. Those six compressed files total about 187 MB, which is awkward to bundle into every frontend deployment.

They now live at `https://iphone.files.garden/`. The manifest points to those exact public objects, and the loader permits that one external origin while continuing to reject unrelated hosts. CORS lets the browser read the files, but CORS alone is not treated as trust: each complete object is hashed in the browser and must match the checked-in SHA-256 value before it is cached, decompressed, or handed to the emulator.

All six remote objects were independently streamed and hashed against the local package. Their byte sizes and SHA-256 values match exactly. The manifest refresh tool also understands the approved R2 URLs while still hashing the local source artifacts, so future intentional firmware rebuilds retain the same verification workflow.

## Why it matters

The frontend can now be deployed separately from the large firmware package, including to hosts with small source-upload allowances. R2's public files preserve the existing multipart download, offline cache, corruption recovery, and genuine framebuffer behavior. This does not change or patch the Apple binaries; it changes only where the already-prepared, integrity-checked bytes are delivered from.
