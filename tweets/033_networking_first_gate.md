# Finding the first real Wi-Fi blocker

## Tweet idea

Before the original iPhone can load a webpage, its operating system needs to find a Wi-Fi card. We traced the first failure: Apple's SDIO driver asks whether the controller is ready 100,001 times and gets zero every time. Now the next implementation gate is an exact hardware transaction, not guesswork in Safari.

## What you're seeing

This is a nonvisual hardware diagnosis, not an app or connectivity demonstration. No screen recording is claimed. The matching evidence is the native cold-boot MMIO/serial trace in `.context/networking/cold-mmio.log` and `cold-serial.log`, summarized reproducibly in [the networking investigation](../plans/networking.md).

The trace writes command descriptor `0x00040045` for CMD5 at physical `0x38d00008`, then polls readiness at `0x38d00018`. The emulator has no SDIO device mapped there, so the guest times out before it can enumerate a card.

## ELI5 technical explanation

The original operating system talks to its Wi-Fi hardware through an SDIO controller. Think of it as a small mailbox: first the driver checks whether it can send a command, then sends it, then waits for a reply.

We compared a fresh boot trace with the genuine Apple driver's machine code. That identifies the mailbox addresses, the ready flag, the command-start bit, completion, acknowledgement, and response words. It also reveals the next command sequence after the failing poll. The older plan described SDIO and Wi-Fi files that had not actually been implemented; this work separates that proposal from the current device map.

The next gate is a real cold boot that completes the initial commands and reads the card's configuration. Firmware loading, a genuine network interface, packet transport and Safari HTTP rendering remain later gates. No Apple binary or app UI was changed.

## Why it matters

Networking now has a concrete first acceptance test derived from the guest itself. That keeps development focused on making the real operating system communicate through emulated hardware, while avoiding premature claims that a browser shell gives its guest internet access.
