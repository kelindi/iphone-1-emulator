# Original driver commits a complete RTC date

The isolated candidate now applies the original Apple's driver's seven date bytes at the next real second pulse. Production remains unchanged. This extends [079's interrupt proof](../second-interrupt/causal-replay/README.md); browser persistence is still incomplete after the [separate autosave failure](../complete-update-browser/README.md).

## Observed result

One bounded native run completed in5.132s after45,152,141 instructions. Its trace through the first callback epilogue is byte-identical to079. The original driver writes `25 13 00 05 01 01 10`, representing2010-01-01 00:13:25 UTC. The hardware initially retains1262304843 seconds with the update pending. At the next elapsed pulse it becomes1262304805, with zero unsupported writes. The run stops after this first commit; a second request was queued but its hardware update was not observed.

The pending state was observed at guest tick5,058,000,327 with a327/6,000,000 fraction. The next pulse is5,064,000,000. Batched device advancement is observed at5,064,026,798 with fraction26,798; this is a correct post-pulse state, not evidence that interrupt delivery occurred at exactly fraction zero. Guest timer ticks remain monotonic despite the calendar moving backwards.

[Run receipt](run-1/run.json), [complete trace](run-1/trace.log), and [independent audit](run-1/audit.json) retain the actual values. No Apple code, synthetic callback, forced return or host-injected date was used in this guest run.

## Implementation and checks

The candidate stages bytes in the existing PMU register bank and pending flags, preserving active reads until the next pulse. Those fields already serialize in snapshot39; no native layout or snapshot schema changes were made. The first elapsed pulse applies the date, and any remaining lumped pulses advance normally. SECOND also latches when a write sets the same active date.

Hardware basis: [NXP PCF50633 User Manual Rev06, section8.15](https://www.freecalypso.org/pub/GSM/GTA02/PCF50633UM_6.pdf), together with the exact Apple driver and079 transaction. The family manual describes next-pulse updates, BCD date fields and binary weekday. It does not establish every PCF50635 behavior.

- 1,089 focused ASan/UBSan contracts pass, including actual bytes, date boundaries, split/lumped timing, backwards changes and unsupported inputs.
- Seven real snapshot39 roundtrips preserve all bytes exactly with one through seven pending date bytes, then behave identically at the following pulse. The unchanged linked archive was not newly sanitized.
- A deliberately wrong extra increment on the commit pulse fails the actual focused test.
- Seven mutations of the retained actual trace fail the frozen auditor: wrong active time, fraction, pulse deadline, pending mask, wire byte, retirement count and unsupported-write accounting. These checks execute no additional guest instructions.

The [build report](build-report.json) records the earlier preflight status and all source/binary/fixture hashes. It intentionally retains its historical “guest not yet run” scope; the subsequent run and audit are separate. [Artifact manifest](artifact-manifest.json) pins copied sources, receipts, logs and negative evidence. The original compiled probe and large fixture remain private and are identified by their hashes.

## Limits and next gate

Only complete, valid, calendar-consistent seven-register updates in2000–2099 are supported. Partial or invalid dates and independent weekday writes are explicitly unsupported, counted at the next pulse, and discarded without changing active time. This is an emulator limitation, not a hardware claim. Alarms, NoPower behavior, nonconforming multiple updates per second, and downgrade behavior for a pending write are not accepted. Existing artificial end-of-century saturation is preserved.

The isolated WASM build succeeds and its seed ABI matches, but the first browser workflow stopped on autosave before the second edit/reload. No completed visual milestone, production promotion, or fully working manual-time persistence is claimed.

## Broader native regression check

The complete private native build runs all26 maintained suites:25 pass unchanged; test_i2c retains one obsolete expectation that a partial RTC write is rejected immediately. The new model defers validation until the next pulse. The original failure is preserved in [native-ctest.log](native-ctest.log). Only that private test expectation was updated to require no early rejection and visible rejection at the pulse; an additional real I2C controller transaction verifies all seven date bytes, delayed active reads and SECOND read-clear. The rebuilt affected suite passes ([recovery](native-i2c-recovery.log)); the other25 binaries were unchanged and were not redundantly rerun. No production tests were edited.

The initial private native configure lacked the tools/ and web test source directories because its staging tree was prepared for WASM only. Its failure log is retained; copying those unchanged source directories resolves configuration. This is a staging correction, not a core change.

The focused update and pending-snapshot checks are also staged as maintained CTest targets (`test_pcf50635_update` and `test_pcf50635_pending_snapshot`). Both compile against the complete candidate library and pass, adding two suites to the existing26. [CTest receipt](maintained-ctest.log) and [source pins](maintained-test-pins.json) retain that integration step. This does not change the production source yet.
