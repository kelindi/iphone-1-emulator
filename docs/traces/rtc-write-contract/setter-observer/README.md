# Actual original setter path reaches the queued action

One isolated native replay starts from the genuine before-picker export and applies the same minute-wheel touch through the existing input API. The original kernel, emulator archive, input state, date conversion and device behavior are unchanged. [Exact executable/source/input identities](pins.json), [uncut trace](trace.log), [bounds/result](run.json), and [independent date/order audit](audit.json) are retained.

The replay observes **two original requests exactly60seconds apart**,1262304804 and1262304864, with zero microseconds. Each passes through the original software-calendar setter, platform lookup, RTC dispatch, RTC date setter and queued-action entry. All18 observed first instructions retire to their expected successor and ISA. A pre-step hook alone is insufficient because an IRQ can intervene; the recorded post-step successor/mode/cycle checks distinguish these cases. There were no diverted observations in this run.

The original RTC subclass adds one second. Its actual date structures therefore contain2010-01-01 00:13:25 UTC and00:14:25 UTC, with the expected weekday/month/year. The audit independently converts the actual timeval bytes and compares the eight semantic date bytes at both setter and queue entry. It does not interpret stack padding or supply any values to the guest.

**The hardware-write gate fails:** the fixed400million-instruction run ends with zero RTC I²C writes and no observed asynchronous callback. It exits10 in33.46seconds without reaching its60-second wall or50-second CPU limits. There is no automatic repeat. Reaching the queue action's first instruction does not prove that the complete action, event signal or work-loop callback ran.

The source was prepared after the previous Back-only probe: checked original app metadata had established immediate picker commits, so this replay tests a different, specific call path from the before-change export. It is not a larger rerun of Back navigation. No clock-write implementation, callback invocation, return-value override or device success was injected. Existing unsupported RTC writes remain unsupported.

Next useful boundary is the queue action's real completion, event-source state and work-loop dispatch. Merely adding register writes cannot resolve an asynchronous callback that has not been observed. Manual Settings end-to-end acceptance and hardware persistence remain open.
