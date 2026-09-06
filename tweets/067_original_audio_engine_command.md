# The original audio driver reaches its engine command

## Tweet idea

After uploading its own audio program, the original iPhone driver now reaches the next engine command in an isolated test. We captured its real configuration sequence and checked that every program byte stayed intact. We stop before accepting the command—executing it is the next hardware problem.

## What you're seeing

This is a **nonvisual protocol trace**, not an audio or screen demonstration. The [first preserved witness](../docs/traces/amc-run-request/first-101c.log) stops at the original input-status acknowledgement. A [second fresh replay](../docs/traces/amc-run-request/run-request.log) captures the task-manager object and nineteen idle-configuration writes, then stops at the original `0x38500000=1` command request. No recording is claimed.

Both captures preserve the full 180,224-byte SRAM image verified in065. A separate [original-kernel oracle](../docs/traces/amc-run-request/kernel-oracle.json) checks contents and exact write coverage; the [protocol oracle](../docs/traces/amc-run-request/protocol-oracle.json) checks the original sequence and actual task-manager method/argument.

## ELI5 technical explanation

Loading a program is only the beginning. The real driver next clears an old input-status indication and configures the engine. Its interrupt handler shows why the status write is an acknowledgement: the driver reads a pending bit, then writes1 before handling that input event. Our isolated latch is empty and stays empty; no event is invented.

We accept only the observed idle setup, preserve the loaded program, and reject unexpected requests before changing device state. **20,265 reset checks,418 prefix checks,13 upload-observation checks and30 write-boundary checks pass**, with eight original-file and five protocol negative controls. The original reset remains a labeled synchronous approximation, and the later ready bit stays clear.

The final command reaches the MMIO handler but is **not accepted or retired**. The interrupted probe is not a saved, resumable phone. We have not shown decoder execution, data transfer, PCM or sound.

## Why it matters

The next missing behavior is now tied to the original driver's concrete configuration and command request. This supplies evidence for a future engine model without substituting a host decoder or fabricating readiness. Production remains unchanged. [Full source, hashes, checks and limits](../docs/traces/amc-run-request/README.md) are preserved.
