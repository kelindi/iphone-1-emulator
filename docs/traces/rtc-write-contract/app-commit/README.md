# Original Settings commits immediately to the software calendar

The unchanged Preferences executable is extracted from parent CNID1387 (Preferences.app) in the pinned MBX work image. A leaf-name-only extraction first found the unrelated24-byte Preferences symlink; it is retained locally and was never disassembled as the app. The actual125,196-byte executable has SHA-256 `5efb9f552131284d4ca63ce5b369e953d5aa6906106fa0d94dc241637577d2a0`.

[Read-only extraction checks](commit-contract.py) bind the DateController class to its complete15-entry method list and the `datePickerChanged:` implementation at `00007bf4`. The original method reads the picker's date, asks for seconds since1970, rounds with `lrint`, constructs a timeval with zero microseconds and calls the original imported `settimeofday`. It posts the time-change notification, then on a zero return updates the display and restarts its timer. No Back action is required by this method. Selector pointers and original imported function stubs are checked, rather than assigning meaning from nearby strings alone.

Seven complete original code ranges are retained with hashes in [report.json](report.json). The kernel setter calls its calendar routine, which updates the software offset and boot-time accounting before calling `PESetGMTTimeOfDay`. The platform and RTC dispatch code contain a further path to the RTC driver's setter. This proves the software call path exists; it does not prove every asynchronous device step ran in the recorded session.

## What the actual exports establish

A separate read-only native inspector decodes both genuine exports from the existing browser recording. It executes **zero guest instructions**. The inspector copies CPU state for MMU translation and selects privileged read access only on that copy; page-table and final reads use ordinary RAM and cannot reach live device callbacks. Original snapshot bytes, CPU state and counters remain unchanged.

| Kernel field | Before minute-picker tap | After tap/export |
|---|---:|---:|
| Calendar seconds offset, low word |1262303999|1262304020|
| Calendar seconds offset, high word |0|0|
| Calendar microseconds offset |619958|377574|
| Boot-time accounting |1262304000|1262304021|
| Hardware RTC seconds |1262304842|1262304883|

The software calendar offset changed by **20.757616seconds**, distinct from ordinary monotonic-clock advance. That supports an actual software-time adjustment during the recorded interval. It does not establish the exact tap-to-call transaction, a hardware RTC write, cold-boot persistence or the failed final unpaused browser gate. Both snapshots still report zero unsupported PMU writes. The existing full recording and original failed acceptance remain [in the parent diagnostic](../README.md#actual-ui-diagnostic--acceptance-remains-incomplete).

Next, observe the real setter/platform/queued-callback boundaries during a picker change to locate why no hardware write was recorded. Repeating Back navigation alone is not justified by the newly established source path. Hardware writes remain unimplemented and production behavior is unchanged.
