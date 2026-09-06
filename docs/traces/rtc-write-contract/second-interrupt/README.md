# Original RTC second-interrupt dependency

2026-09-06. Source/state evidence only; no candidate guest replay yet. The unchanged kernel constructs the pending-date event source with provider index1. Its original personality maps indices0/1 to PMU IRQ6/7. The original event-source vtable identifies slot b0 as enable and b4 as disable. The pending-date action enables IRQ7 and its eventual callback disables it. This corrects the earlier description of a software-signaled callback.

The read-only inspector executes no guest instruction. It observes the same actual event source cbaf8140 and callback c03ffa60 in both immutable Settings exports. Before the change: pending0, INT1=00, mask=f0. After: pending1, date190e00050101da07, INT1=00, mask=70. The eight semantic date bytes represent2010-01-01 00:14:25; the following stack bytes are not calendar fields.

`source-contract.py` checks original Mach-O bytes, construction, member pointer, vtable and embedded personality XML, then checks both actual inspector records. The XML hash/location and exact values are in `source-contract.json`. The existing PCF model advances its clock without generating a SECOND event. Linux's [PCF50633-family register definition](https://raw.githubusercontent.com/torvalds/linux/v6.12/include/linux/mfd/pcf50633/core.h) corroborates INT1 bit7/IRQ7; original Apple code and actual masks provide the device-specific evidence. This is not proof of every PCF50635 behavior.

Next: isolate a candidate which latches bit7 on actual emulated calendar-second advancement, using the existing mask, read-clear and IRQ wiring. Keep the existing bounded date domain/saturation unchanged and explicitly exclude saturated-clock behavior from hardware claims. Do not add RTC writes, inject a callback, or change the guest. Test edge, mask, clear and batching behavior before one bounded replay of the actual minute change. Production behavior is unchanged.


The [single bounded causal replay](causal-replay/README.md) now passes: original callback plus seven real RTC writes after the isolated SECOND latch, all13 instruction boundaries verified. Existing hardware writes still discard values and count7 unknown writes; the feature is not promoted or complete.
