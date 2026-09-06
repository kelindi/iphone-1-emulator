# New browser phones use accelerated graphics

## Tweet idea

The original iPhone's accelerated graphics are now on by default in our browser build. Sixteen apps open and return Home without rejected graphics commands, and an older saved phone survives the upgrade unchanged. The screen is still Apple's real OS 1.0 framebuffer.

## What you're seeing

[Original-speed recording](videos/accelerated_default_phone.mp4), **0:00–0:24**, starts a fresh phone from the exact package now installed as the default, then exercises the physical controls. At **0:13–0:22**, the genuine display sleeps, wakes to the original lock screen, and slides back to Home. The [thumbnail](thumbnails/accelerated_default_phone.jpg) is the complete Home screen at **0:22.5**.

This recording used the private preview of the identical package before promotion. A separate fresh production load after promotion verifies the served manifest and WASM, requests the new snapshot through the default loader, and matches **144,000 native Home RGB pixels exactly**. No fixture import is used in either fresh-load check. [Milestone 056](056_continuous_sixteen_apps.md) supplies the complete sixteen-app walkthrough; [055](055_mbx_navigation_speed.md) supplies the measured speed comparison.

The [additional Notes recording](videos/notes_accelerated_default.mp4), **0:09–1:51**, verifies the promoted default itself: create Hello, Save/reload, edit to Hello web, Save/reload, delete, Save/reload, Home. The [Notes thumbnail](thumbnails/notes_accelerated_default.jpg) at **1:18** shows the restored edited note. All three reloads compare 144,000 native RGB pixels exactly, and the actual text/deleted states were manually reviewed. This separate five-check workflow uses deliberate input waits and is not a speed benchmark. [Raw report](../docs/traces/accelerated-default/notes.json).

## ELI5 technical explanation

The phone's real graphics driver can now submit the commands used by all sixteen original apps without our emulator rejecting them. We enable that driver by changing SpringBoard's `LK_ENABLE_MBX2D` environment setting from 0 to 1; OpenGL remains disabled. The verified work images have 5,120 file forks, and only that preference file differs. Apple's executable files are unchanged.

The new package contains the verified accelerated root filesystem and a matching Home snapshot. Both compressed multipart packages round-trip to their exact original bytes. Promotion replaces each file with a new inode, preserving old experiment copies, then publishes the matching manifest and offline bundle. The CPU engine and guarded seed remain unchanged.

The exact preview package passes **65 browser checks**: 23 persistence, six offline recovery, six physical controls, 12 clock lifecycle, and 18 firmware-upgrade checks. The upgrade test actually changes the firmware defaults from software rendering to accelerated graphics. The old phone's saved checkpoint hash, size, timestamp and version remain identical after activation, and it reopens while disconnected. Root reviewed the actual Settings screens before and after the update, plus sleep, lock screen and complete Home.

The continuous app gate completes **6,489 2D and 16,850 3D commands**, with zero rejections and all sixteen Home returns pixel-exact. Its missed Clock tab tap and existing Camera, network and media limitations are documented separately; opening an app is not full functionality acceptance.

Durable [verification and provenance](../docs/traces/accelerated-default/verification.json) link the actual manifest generations, saved-checkpoint identity and individual reports. The first upgrade harness attempt hit a POSIX same-inode staging-file cleanup issue; the fixed fresh run passed. The first production startup attempt found the development server stopped; after restarting it, the fresh production check passed. Both failed attempts remain in `.context/graphics-final/`.

## Why it matters

The measured graphics improvement is available to new phones without importing an experimental fixture: warm Home p95 is **0.92 seconds**, down from **1.27 seconds** in the paired VM test. Settings p95 improves to **3.57 seconds**, still above its two-second target. These results do not establish full speed, 30-fps animation, or working network/audio/camera services.

Existing saved phones retain their boot state and data; this update does not silently convert them. **Export** preserves a backup; **Reset** starts a fresh accelerated phone. Reimporting an older backup restores that older phone and its graphics mode. A general data-preserving graphics migration remains separate work.

Production offline bundle: `75f559b623ef8143bb518064`. Production WASM: `da43870dab0e151d6a31597bb8ddbbfb3a480f7398ddb88a9334bea9b1314c6d`.
