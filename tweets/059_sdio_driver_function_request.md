# The original Marvell driver matches and requests its data function

## Tweet idea

The original iPhone now reaches Apple's real Wi-Fi driver in an isolated cold boot. We traced two genuine startup failures, declared the virtual device's missing calibration and local MAC explicitly, and watched the driver request its SDIO data function. No pretend firmware-ready flag.

## What you're seeing

This is a nonvisual hardware/driver trace milestone; there is no UI or network-connectivity recording claim. Three separate genuine cold boots establish the sequence:

1. [Unchanged device tree](../docs/traces/sdio-identity/candidate-mmio.log): after 20 complete commands, Apple's provider cast returns a real `IOSDIODevice`; the driver then fails at `0xc0381bdc` with its invalid-calibration message `0xc038e3c4`. [Report](../docs/traces/sdio-identity/candidate-report.json).
2. [Private copy with calibration absent](../docs/traces/sdio-identity/no-calibration-mmio.log): the original missing-calibration branch `0xc0381c20` proceeds, then the zero-MAC branch `0xc0381cdc` rejects the unchanged all-zero address. [Report](../docs/traces/sdio-identity/no-calibration-report.json).
3. [Separate private copy with the declared local MAC](../docs/traces/sdio-identity/local-mac-mmio.log): the original nonzero-MAC branch `0xc0381ce0` proceeds. At instruction **348,726,693**, the driver writes execute for CMD52 argument **`0x80000402`**, requesting function-0 IOEx bit 1. The stop at `0xc0431528` occurs after the write returns to the CPU. [Report](../docs/traces/sdio-identity/local-mac-report.json).

The final run has **22 triggered commands and 21 completed responses**: the enable request is still busy. All three final stages have zero R5 card errors, controller transport errors, or rejected MMIO writes. [Independent input-hash and exact DT-byte audit](../docs/traces/sdio-identity/verification.json). Identical status polls are compressed in the text logs; report counters retain their totals.

An [initial failed attempt](../docs/traces/sdio-identity/initial-failed-report.json) and its [trace](../docs/traces/sdio-identity/initial-failed-mmio.log) remain separate: the anticipated zero-MAC checkpoint was not reached, and the then-incomplete controller rejected its four-bit bus-width write. That failure led to the actual calibration diagnosis rather than a false success claim.

## ELI5 technical explanation

The operating system first looks at each SDIO function's basic description. Apple's actual scan publishes a device when its interface-code nibble is nonzero; its Marvell personality matches the resulting `IOSDIODevice` class. This path does not parse CIS manufacturer tuples or compare a vendor ID. The declared virtual endpoint describes one WLAN function and no other functions. Its bounded common CIS uses standard tuple formats and compatible Marvell/Libertas IDs, but the trace does not claim Apple used them for matching.

The driver next checks information normally supplied by the physical phone. Its [original calibration validator](../docs/traces/sdio-identity/calibration-dis.txt) rejects an all-zero or all-FF first 128 bytes. The supplied calibration is blank, so the first unchanged-tree boot correctly fails there. The code also explicitly permits the property to be absent. The reviewed isolated policy supplies **no RF calibration**, removing that property only from a private, loaded device-tree copy. This does not manufacture calibration data or prove radio behavior.

With that path proven, the next boot exposes the original zero-MAC rejection. A separate private copy declares **02:00:00:00:01:01**, a deterministic locally administered unicast address. This is an emulated identity, not a physical phone's address. The byte audit proves only the 1,060-byte calibration-property record was removed, its node property count changed from 12 to 11, and the later copy changed the six-byte MAC property. Original kernel, device tree, work disk and native archive hashes remain unchanged.

The controller also now accepts the genuine four-bit bus-width bit derived at `0xc0431d44–0xc0431d4c`, with readback and invalid-bit contracts. All **101,589 isolated checks pass**, retaining the earlier first-gate tests. R5 errors are counted separately because Apple's read wrapper checks transport success but ignores the card error flags. No production machine structure, snapshot, user profile, or Apple executable is changed.

## Why it matters

We can now distinguish a missing controller, driver matching, bad board data and a real request to enable the device. The next step needs actual function-enable/readiness behavior and a genuine CMD53 transfer request, followed by checked firmware transport. This milestone proves none of those later completions, no loaded Wi-Fi firmware, no operational network interface and no packet delivery. Production networking remains unavailable.
