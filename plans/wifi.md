# Plan: Marvell 88W8686 SDIO Wi-Fi + host bridging

## Goal
Emulate the original iPhone’s Marvell 88W8686 SDIO Wi-Fi module well enough that the genuine iPhone OS 1.0 SDIO host driver, Marvell driver, `wifid`, and `configd` bring up an `en0` interface, join a fake access point, and let MobileSafari load real HTTP pages over a host NAT.

## Background
- The SDIO host controller lives at `0x00d00000` on VIC line `0x2a`; the device-tree node is `/arm-io/sdio`.
- The genuine drivers and firmware live in the work image under `/System/Library/Extensions/` (Apple SDIO host and Marvell/Wireless kexts) and `/usr/share/firmware/`.
- References: Linux `drivers/net/wireless/marvell/libertas/if_sdio.c` (helper + main firmware download, `IF_SDIO_*` register layout, command/response protocol), openiBoot/iphonelinux `sdio.c`/`wlan.c` for S5L8900 host register details, and Apple driver disassembly from the rootfs.

## Approach
The guest driver’s expectations win over any Linux/Apple driver assumptions. Use `tools/firmware/hfsx_extract.py` + `capstone` to inspect the real kexts, then emulate what the guest asks for.

### 1. SDIO host controller (`core/src/soc/sdio.c`)
- Command/response registers (CMD, ARG, RSPn, status, block size, transfer count).
- Block/byte transfers with data FIFO/DMA (PL080 if the driver uses it) and completion interrupts.
- Card-detect, bus width, clock enable, and error interrupts.
- Attach only to the M68 machine.

### 2. 88W8686 SDIO device (`core/src/soc/wlan8686.c`)
- SDIO function 1: CCCR/FBR, function enable, block size, interrupt pending.
- Firmware download handshake: accept the helper firmware, then the main firmware images; manage `CMD_GET_HW_SPEC`, `CMD_802_11_MAC_ADDRESS`.
- Command/response/event/data queues and TX/RX packet descriptors.
- A fake open AP named `iphone-1` that is always visible in scans, accepts open-auth association, reports RSSI and a MAC address.
- Convert between 802.11/Marvell RX/TX descriptors and raw 802.3 Ethernet frames.
- Add both SDIO host and WLAN device state to snapshots with a version bump.

### 3. Host network seam
- Callbacks on the board context: `net_tx(ctx, frame, len)` guest → host and `s5l8900_net_rx(m, frame, len)` host → guest.
- No sockets or host IO inside `core/`.

### 4. Native backend (`tools/iphone1.c`)
- If `libslirp-dev`/`pkg-config slirp` is present, use libslirp for user-mode NAT; guard with `CMAKE` option so the build still works without it.
- Otherwise a minimal built-in NAT (ARP + DHCP + DNS forwarding + TCP proxying via host sockets).
- Harness flags: `--net slirp` and `--net pcap FILE` for a pcap of guest frames.

### 5. Browser seam (`web/emu_api.c` + `web/app.js`)
- Export `emu_net_rx` and a JS-callable `emu_net_poll_tx` so a future JS backend can forward frames.
- A no-op stub in `web/app.js` is fine for now.

## Files to touch
- New `core/src/soc/sdio.c`, `core/src/soc/sdio.h`
- New `core/src/soc/wlan8686.c`, `core/src/soc/wlan8686.h`
- `core/src/board/m68.c` — attach SDIO + WLAN for M68 only
- `core/src/soc/machine.c` or `core/include/soc.h` — host `net_tx`/`net_rx` callbacks
- `core/src/snapshot.c` — version bump and SDIO/WLAN state
- `core/tests/test_soc.c` — SDIO command round-trip, CCCR/FBR read, firmware-download state machine, one TX frame reaching `net_tx`, one RX frame becoming a Marvell RX descriptor
- `tools/iphone1.c`, `tools/CMakeLists.txt` — libslirp detection, `--net` flags
- `web/emu_api.c`, `web/app.js` — network seam exports
- `docs/iphone1_1_os1_port.md` — SDIO/MMIO discoveries, command list, remaining gaps

## Milestones
1. SDIO host driver enumerates the card (`CMD0/5/3/7`, CCCR reads, function enable); serial shows the Marvell driver starting and downloading firmware.
2. Firmware download completes; `wifid`/`configd` create `en0`. In Settings → Wi‑Fi the toggle is enabled and the fake `iphone-1` network appears.
3. Tap join → association → DHCP via host NAT → MobileSafari loads `http://example.com/` (2007 Safari may fail HTTPS because of old TLS; HTTP is the target). Screenshot the rendered page.

Use **cold boot** for Wi‑Fi verification, not the shipped snapshot, because the snapshot was taken before SDIO existed.

## Correctness gates
- All existing tests pass; add `core/tests` coverage.
- `git diff --check` clean.
- WASM build compiles (network backend may be no-op in browser).
- Screenshot of Settings → Wi‑Fi showing the fake network and, if milestone 3 is reached, Safari loading example.com.

## Deliverable
A PR from a branch `devin/<timestamp>-wifi` targeting `devin/1788299483-m68-os1-boot`.
