# Display wake investigation — 2026-09-05

Power/wake exposed a missing M68 device, separate from MBX rendering. The
browser correctly showed black when the guest disabled every CLCD window;
retaining the previous canvas image had previously hidden that state.

## Root cause

The genuine M68 device tree describes two display-related SPI endpoints:

| Controller | Device | Existing support |
| --- | --- | --- |
| SPI0, CS0 | `mpl-tx,lm2512`, display-link transmitter | Previously unattached |
| SPI1, CS0 | `lcd,merlot`, panel | Already attached |

After wake, the guest restored and started the CLCD controller, then called
the display-link transmitter's enable function while holding the display
workloop gate. The first synchronous SPI write, `{0x16, 0xff}`, remained in
SPI0's transmit FIFO because no endpoint was attached. Its receive FIFO
stayed empty, so the actual SPI completion interrupt could never occur.

The first resumed CLCD interrupt reached its filter and queued work, then
disabled VIC source 13 until that work completed. The blocked gate prevented
completion: the interrupt event source was enabled with producer `0xd73` and
consumer `0xd72`, while VIC source 13 remained disabled. The owner thread's
saved stack led through `IOCommandGate::commandSleep`,
`AppleARMSPIController::enqueueSPICommandGated`, and the wake call at
`0xc030230c`. This explains the black screen without assuming an MBX failure
or an interpreter speed problem.

## Change and compatibility

M68 now attaches a dedicated LM2512 transport endpoint to SPI0 CS0. It clocks
the measured configuration writes through the existing transmit/receive
FIFOs and interrupt logic. The guest drains the receive bytes and releases
its own workloop gate. The endpoint supplies zero for the unused full-duplex
receive bytes; it does not implement LM2512 register reads, generate pixels,
alter guest code, or set CLCD window-enable bits.

The snapshot format stays at version 39. Older loaders unconditionally
selected N82's SPI0 route 1 even for M68. The wiring validator accepts that
known legacy M68 route or the correct route 0, rejects routes 2/3 and a
Merlot pending-read phase on the transmitter, and restores the actual board
endpoint's route. Already queued transmitter bytes survive restore and
complete normally.

## Verification

- All 23 native tests pass. A focused M68 regression captures the two queued
  wake bytes on the old route, restores the snapshot, checks that both bytes
  reach the receive FIFO and raise a real SPI interrupt, and rejects invalid
  routes/read phases.
- Replaying the old browser's stuck export releases the gate, drains SPI0,
  and consumes the pending CLCD event. A subsequent explicit Power press
  renders the genuine lock screen and slide-to-unlock control. No framebuffer
  override is involved.
- Native evidence is in `.context/mbx-investigation`: `wake-trace.log`,
  `wake-vic-write.log`, `gate-owner-backtrace.txt`, `dt.txt`,
  `mpl-enable-dis.txt`, `fix-tests.log`, and `visible.png`. The visible frame
  is at instruction 10,950,000,000 in the recovery sequence.
- The native instruction-driven harness can advance idle guest time rapidly;
  a late final screenshot can therefore be black again after the genuine
  lock screen's idle timeout. Real browser recording is the acceptance check
  for user-visible wake timing.

The [real Chrome milestone](../tweets/030_sleep_wake_unlock.md) now records
volume/silent overlays, sleep, wake and slide-to-unlock. Four automated checks
pass in `npm run test:controls`; the slow unlock animation remains a separate
performance issue.

This investigation did not fix the separately observed Safari-close
`Graphics Restart during 3D Blit`. Native bookmarks-close and page-close
replays completed all submitted renders (27/27 and 22/22 respectively), so
the exact failing browser sequence or a pre-close checkpoint is still needed
to identify that packet. A complete cold boot with the additional transmitter
has not been repeated as part of this bounded wake fix.
