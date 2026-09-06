# Original idle configuration and unaccepted engine command

Two fresh isolated replays preserve milestone065's original program upload, then establish the next command boundary. No production code, firmware, snapshot format, browser default, decoder or audio output changed.

The first replay permits the verified upload-marker callback to return as an **observation-only boundary**, without a new device completion event. It executes 58 more instructions and stops before accepting exactly `write32(0x3850101c,1)` at `0xc027814c`. The original caller's seven parameter words are `[2,2,48000,128000,0,0,0]`. Only type2 is independently tied to the AAC selection switch; other field names are not assumed. [First witness](first-101c.log), [frozen source](first-101c-source/probe.c).

Only after preserving that witness, the second frozen candidate accepts a reviewed **empty input-status acknowledgement and idle configuration**. The actual task-manager object is `0xcc999ee4`, vtable `0xc029c844`, method `0xc02782fc`, argument0. After nineteen exact configuration writes, the original instruction at `0xc0277fa0` requests `write32(0x38500000,1)`. The probe stops atomically **without accepting that command or retiring its instruction**. This is 159 instructions beyond the original upload marker. Status remains `0x100`, input-pending remains false, and no IRQ, transfer, engine execution or PCM is produced. [Run-request trace](run-request.log), [exact original-code-derived protocol oracle](protocol-oracle.json).

All SRAM writes had committed before either new stop. Both fresh captures remain byte-identical to065 across the entire **180,224-byte SRAM**, with SHA-256 `011452fdff9699442c49115354c9b0bced1b98265ba64ee5b95c1c48bbb01583`. Every one of the 59,622 descriptor bytes has actual write coverage; the exact 59,628-byte RMW footprint is unchanged. Further SRAM writes after the upload boundary are rejected before mutation.

The **in-process** gate reconstructs bytes from the original guest descriptors/source memory and checks exact values and coverage. A **separate original-Mach-O oracle** reads the supplied original kernel file and independently verifies the captured artifacts afterward; it does not run inside the MMIO callback. [Original-file oracle](kernel-oracle.json), [source](source/kernel-oracle.py). The reviewed source comment now states that distinction; the executed capture's earlier comment is preserved by its hash and differed only in wording.

## Hardware scope

The original AMC interrupt callbacks read +`0x101c` bit0 and, when set, call the +`0x101c=1` helper before handling the input event. The input handler also starts with that write. This supports an acknowledgement/clear convention. The isolated model has an explicit input-pending latch; the observed write clears an already-empty latch. **There is no guest event producer.** A unit test initializes pending state solely to verify acknowledgement; the real replays do not inject it. [Original interrupt callers](input-status-callers-dis.txt), [input-handler acknowledgement](input-irq-ack-dis.txt).

The accepted six-write input prefix is `101c=1`, then `100c=0`, `1000=0`, `1004=0`, `1008=0`, `a00=0`. Task setup next zeroes the four-word `300` and `310` groups in an interleaved loop and the two-word `320` group, writes `300=07000001`, `304=3f081603`, and `d0c=40`. These are captured **idle configuration values**, not a decoded DSP program, proven register bit fields or execution effects. Unknown order, values, widths or accesses reject before mutation. [Actual caller](post-upload-caller-dis.txt), [original command helpers](post-upload-commands-dis.txt).

The reset policy is still065's synchronous idle-reset **functional approximation** tied to genuine core→DE requests and enabled clocks3/4/power4, not measured silicon timing. Gating or reset invalidates the observed-upload/configuration state. Bit9 remains clear. The original caller does not immediately poll bit9 after this command; later transfer/reconfigure paths do. Its real producer, command execution semantics, transfer progress, sample format and audible output remain unresolved.

## Checks and limits

- [20,265 retained reset checks](idle-contracts.log), with the newly supported `101c` read checked as an empty latch rather than an unknown read.
- [418 idle-prefix checks](post-contracts.log): atomic wrong order/value/width rejection, duplicate acknowledgement, absent upload, stale reset/gates, unit-only pending acknowledgement, bit9 preservation and non-mutating run sentinel.
- [13 observation/coverage checks](observation-contracts.log): missing descriptor, wrong bytes and an expected zero without actual write coverage cannot advance the upload marker; later SRAM writes stop before mutation.
- [30 retained footprint checks](footprint-contracts.log).
- Eight original-file oracle negative controls, plus five independent protocol negatives for missing/duplicate acknowledgement, early command, reordered zero writes and wrong original literal.

Both probes use disposable callback termination. The final command instruction is not retired; no interrupted state is resumed or presented as a portable save. The first witness remains preserved separately. Sources, fixture/archive/binary hashes and stop counts are in [verification.json](verification.json). Working binaries/raw SRAM masks remain under `.context/amc-first/stage-c/`; `source/` contains the final isolated model, probe, tests and independent oracles, while `first-101c-source/` preserves the narrower first witness. Compile with the same native archive/API and include flags documented for065. Nothing from this experiment has been promoted to production.
