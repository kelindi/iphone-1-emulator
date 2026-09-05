## Tweet idea

> We added a proper `m68ap` / iPhone1,1 machine target to the shared S5L8900 SoC, instead of hardcoding N82/iPhone 3G differences everywhere.

## What you're seeing

No dedicated recording yet (this was bring-up work in the native emulator). The architecture is documented in `docs/iphone1_1_os1_port.md`.

## ELI5 technical explanation

- **SoC (System on Chip)** = what is shared between original iPhone and later phones: the S5L8900 (ARM1176 CPU, VIC interrupt controller, timers, DMA, NAND/SPI, LCD controller, USB, etc.).
- **Machine / board** = the specific phone model: M68AP for iPhone1,1, N82AP for iPhone 3G.
- The existing S5LBox code was heavily N82-flavored: some values, GPIOs, and device tree assumptions matched the iPhone 3G rather than the 2007 iPhone.
- We split the codebase like this:
  - `core/src/soc/` = S5L8900 SoC emulation.
  - `core/src/board/m68.c` = M68 machine: which GPIOs map to Home/hold/volume, which SPI bus has the LCD and touch, which NOR/NVRAM layout, and the M68 device tree.
  - `core/src/soc/machine.c` dispatches between machine targets.
- This means future board support (e.g., N45 iPod touch) can be added without touching the shared SoC code.

## Why it matters

A clean machine split is what lets the project be faithful to the original iPhone rather than accidentally the iPhone 3G. It also makes the codebase portable to future targets.
