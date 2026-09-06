# The original Wi-Fi driver requests its first helper transfer

## Tweet idea

The original iPhone Wi-Fi driver now enables our isolated SDIO endpoint, waits for real modeled readiness, and requests its first firmware-helper transfer. We compared every byte of that first frame with Apple's original helper. Firmware and networking are still unready.

## What you're seeing

This is a nonvisual hardware/driver milestone. The [fresh cold-boot trace](../docs/traces/sdio-enable/candidate-mmio.log) contains three concrete sections:

1. IOEx argument `0x80000402` completes at guest tick **5,274,444**. Subsequent IORx reads return **0, 0, 2**, crossing the separate function-initialization deadline **5,276,244**. The guest consumes these responses and proceeds into function setup.
2. The original driver sets the host-power register, reads the three I/O-port bytes and capacity-derived status, then programs physical buffer **`0x13c87004`**, block length **32** and block count **2**.
3. At instruction **351,499,195**, the driver commits CMD53 descriptor **`0x000501f5`**, argument **`0x9a000002`**. The probe stops at the next instruction boundary, **`0xc0431528`**, after that write returns. There are **38 triggers and 37 responses**: CMD53 remains busy and no data completion is reported.

The [run report](../docs/traces/sdio-enable/candidate-report.json), [endpoint report](../docs/traces/sdio-enable/endpoint-report.json) and [independent verifier](../docs/traces/sdio-enable/verification.json) record zero R5 card errors, transport errors or rejected MMIO writes. All **64 captured source bytes** equal the original helper frame, SHA-256 `90f8e97b50ecbbc45c68f4fa07379ea46d02e157aa2bd0ba86aa5f3d0a72057c`; [helper provenance](../docs/traces/sdio-enable/helper-provenance.json) identifies the unchanged kernel source. Endpoint storage contains **zero received bytes** at the stop.

All **114,312 contracts pass**, including the earlier 101,589 checks. Independent review added a reset/time rollback regression: the old implementation fails exactly six new assertions, while the corrected candidate passes and was cold-booted again. Both [negative result](../docs/traces/sdio-enable/reset-review-negative.log) and [corrected result](../docs/traces/sdio-enable/contracts.log) are retained.

## ELI5 technical explanation

A driver asking to turn on a device is different from that device being ready. The endpoint now initializes and clears real bounded storage before starting its initialization deadline. Reading the readiness register repeatedly cannot make time pass. Commands and card initialization have independent deadlines, so Apple's real polling loop observes two not-ready responses before proceeding.

The endpoint's power, selection, enable state and remaining space determine whether it could receive a frame. Unknown operations return errors. Firmware scratch registers remain unready; neither a register read nor allocating a buffer pretends that Apple's helper has executed. The genuine uploader prepares a four-byte length followed by 60 original helper bytes. Comparing all 64 bytes verifies the actual request; completing its transfer is the next separate gate.

The candidate retains the explicitly reviewed [milestone 059 device policy](059_sdio_driver_function_request.md): no RF calibration, and emulated local MAC **02:00:00:00:01:01** in a private loaded device-tree copy. The audit proves exactly the same property removal and MAC change, with original kernel, device tree, work disk and native archive hashes unchanged. The 100-microsecond command delay and 300-microsecond function delay are declared model timing, not measured hardware behavior. Controller reset preserves card time; card reset, disable and backwards time cancel the appropriate pending state.

## Why it matters

We have moved from an enable request to genuine readiness polling, setup and a verified helper-transfer request. This remains an isolated native device experiment: production networking, snapshots and machine state are unchanged. CMD53 response/data completion, helper execution, main firmware, an operational interface and packets remain unproven. See [the networking plan](../plans/networking.md#isolated-function-readiness-and-first-helper-request).
