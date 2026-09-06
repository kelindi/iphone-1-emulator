# Plan: remove the `--unmatch arm-io/usb-otg` workaround

## Current investigation (2026-09-05)

This is now a direct app-speed dependency. [Transition measurements](transition-latency.md)
trace the long Home black interval to about 30 seconds of guest-time connection
retries against the absent lockdownd service. The genuine daemon exits when
`AppleUSBDeviceMux` is absent. The older statement that its failure is harmless
for SpringBoard does not hold for app-close behavior.

Revalidation with the current core, compact work image and **unmodified device
tree**, without USB unmatching, passes 500 million instructions and starts
`AppleUSBDeviceMux` at 168.6 million. The subsequent full cold boot reaches Home. lockdownd stays PID 17 across
Settings/Home, and browser Home readiness improves to 3.4–4.0 seconds.
[Milestone 036](../tweets/036_home_service_recovery.md) contains the evidence. Four reordered-tree probes
also passed, but the unmodified control establishes that reordering is not
required for this observed early-startup success. Do not promote a device-tree
ordering workaround from those probes.

The USB kexts and matching personalities are prelinked in the actual kernel's
`__PRELINK` segment. Its `__info` is an XML array using ID/IDREF references.
The provider chain is AppleARMIODevice → USBWrangler → USBNub →
AppleS5L8900XIpodHAL → IOIpodUSBDevice → IOUSBDeviceInterface → AppleUSBDeviceMux.
Removing only the wrangler would not produce a working mux.

Evidence: `.context/lockdown-root/original-dt-serial.log`, parsed personalities
in `.context/lockdown-root/prelink.json`, and the independent daemon lifecycle
trace in `.context/clock/lifecycle-resume.log`. The historical diagnosis and
possible later controller work below remain useful context, not current proof
that the early crash still occurs. The preserved origin/main native baseline
also passes this early startup gate with the original 184 MiB work image.
Thus this task retires an obsolete workaround; it does not claim to have
implemented the earlier registration fix. Both the default browser boot API
and supplied starting checkpoint now retain the original USB service.
Existing saved phones retain their old registry until explicitly reset.

## Goal
Emulate enough of the S5L8900 USB OTG controller and PHY so the genuine iPhone OS 1.0 USB stack (`AppleS5L8900XUSBWrangler` → `AppleS5L8900XUSBPhy` → `AppleUSBDeviceMux`) starts without the device-tree `compatible` workaround, and the kernel reaches SpringBoard with the USB stack registered.

## Background
Booting with `/arm-io/usb-otg` matched currently produces:
```
AppleS5L8900XUSBWrangler::start starting
AppleS5L8900XUSBPhy::start called
AppleS5L8900XUSBWrangler::phyRegistered PHY ... notified us of availability
kernel abort type 4: fault_type=0x1, fault_addr=0x0
```
Disassembly of the wrangler shows it calls `addNotification("AppleS5L8900XUSBPhy", …)`. The callback dereferences a notifier pointer that is only stored after `addNotification` returns. If the PHY is already registered when the wrangler starts, IOKit fires the callback synchronously and the wrangler null-derefs.

The true root cause is most likely service-registration/timing ordering — the PHY should register *after* the wrangler has stored its notifier, which on real hardware is probably gated by a clock, power-domain, or GPIO provider that the PHY probes later than the wrangler.

## Steps
1. **Root-cause the ordering.**
   - Extract the USB kexts from `fw/work_1A543a_act.img` with `tools/firmware/hfsx_extract.py` and disassemble (`capstone`) the real `AppleS5L8900XUSBWrangler`, `AppleS5L8900XUSBPhy`, and any `AppleS5L8900XUSB…Controller` start routines.
   - Dump the device tree personalities with `tools/firmware/dtdump.py fw/devicetree_m68ap.bin` to see `IOProbeScore`, `IOMatchCategory`, provider dependencies, and the `usb-otg` / `usb-phy` / `usb-complex` nodes.
   - Run the emulator with `/arm-io/usb-otg` matched, `--serial`, `--mmio-watch`, `--trace-devices`, and `--pc-log` around the kext start symbols (`tools/firmware/ksym.py`). Identify which MMIO, GPIO, clock-gate, or power register expectation differs from real hardware.

2. **Fix the ordering behaviour in the emulator.**
   - The fix must be a defensible hardware model change, not a patch to IOKit or an Apple binary.
   - Possibilities: delay PHY service registration until a clock-gate bit is set, or model a power-domain dependency, or adjust VIC/timer ordering so the wrangler runs before the PHY.
   - Document exactly what real hardware behaviour is missing and how the change emulates it.

3. **Model the USB OTG controller and PHY registers** (Synopsys DesignWare OTG core family, as used in S5L8900 and openiBoot/QEMU `dwc2`):
   - Controller at `0x00400000`: `GOTGCTL`, `GAHBCFG`, `GUSBCFG`, `GRSTCTL`, `GINTSTS`, `GINTMSK`, `GRXFSIZ`, `GNPTXFSIZ`, `DCFG`, `DCTL`, `DSTS`, `DIEPCTL`, `DOEPCTL`, FIFOs.
   - PHY block: find its physical address in the device tree (runtime VA `0x3c400000` on some builds).
   - Clock/power expectations in the clock-gate (`0x04500000`) and power (`0x01a00000`) blocks.
   - Goal: the genuine drivers initialize and register their services in *device* mode with no host attached (no VBUS, no enumeration); `AppleUSBDeviceMux` service becomes present and the `Can't find AppleUSBDeviceMux service` message disappears.

4. **Remove the workaround from the repo.**
   - Drop `--unmatch arm-io/usb-otg` from README run commands, harness defaults, and `web/emu_api.c`.
   - Keep the `unmatch` CLI option for debugging but stop applying it by default.

## Files to touch
- `core/src/soc/usbotg.c` — existing stub; extend into real OTG model
- New `core/src/soc/usbphy.c` (or fold into `usbotg.c` if the PHY is on the same MMIO block)
- `core/src/soc/clock.c` or `core/src/soc/power.c` — if the fix depends on gating
- `core/src/board/m68.c` — attach USB devices
- `core/tests/test_soc.c` — register reset values, reset sequencing, FIFO sizes, interrupt masking
- `tools/iphone1.c`, `web/emu_api.c`, `README.md` — remove default `--unmatch arm-io/usb-otg`
- `docs/iphone1_1_os1_port.md` — record the real ordering, MMIO addresses, and what changed

## Correctness gates
- Cold boot to home screen **without** `--unmatch arm-io/usb-otg` succeeds; attach PNG and serial log excerpts showing USB drivers starting and the mux registering.
- Snapshot resume still works; note that the shipped snapshot was taken with USB OTG unmatched.
- `ctest --test-dir build` 100% green.
- `git diff --check` clean.
- WASM build still compiles and smoke test passes.

## Deliverable
A tested change targeting `origin/main`, with genuine guest navigation and
daemon-liveness evidence. Do not rename the Conductor workspace branch.
