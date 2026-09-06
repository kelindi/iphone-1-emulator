# The original audio driver supplies real ringtone bytes

## Tweet idea

The original iPhone audio driver now reaches its first real input-transfer request in an isolated test. All16KiB match the original Alarm ringtone. We also found a packet split across the transfer boundary—exactly the kind of detail a working audio engine must handle. Sound is still ahead.

## What you're seeing

This is a **nonvisual protocol and byte-verification milestone**, not a screen recording or audio demonstration. The [actual driver trace](../docs/traces/amc-input/trace.log) reaches the transfer-start write; [earlier prefixes](../docs/traces/amc-input/prefixes/) retain every first unsupported request. No recording is claimed.

The [input oracle](../docs/traces/amc-input/input-oracle.json) and [independent review](../docs/traces/amc-input/root-input-audit.json) compare the supplied bytes and packet metadata against the original file. The loaded audio program remains independently verified and unchanged.

## ELI5 technical explanation

The driver has loaded its audio program and configured the engine. It next points the hardware at a real memory buffer. We record that request without pretending the program has executed or the transfer has completed.

The buffer contains49 complete encoded packets and part of the50th. Its metadata describes52 additional bytes that have not arrived. Treating that incomplete packet as complete—or filling it with invented data—would hide the next missing behavior.

The isolated model passes bounded configuration, input, lifecycle and malformed-descriptor checks. Two separate file parsers confirm all16,384 supplied bytes. The final write reaches the handler but is **not accepted or retired**; this interrupted probe is never resumed or saved. No ready bit, input IRQ, PCM or sound is generated.

## Why it matters

A future audio engine now has a concrete original input protocol and independently verified media to consume. Packet continuation, output buffers, completion signals and sample timing remain necessary. Production remains unchanged. [Sources, hashes, checks and exact limits](../docs/traces/amc-input/README.md) are documented.
