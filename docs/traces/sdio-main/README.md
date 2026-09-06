# First modeled main-header/body evidence

This isolated experiment implements the reached protocol of the original 2,140-byte helper as a high-level semantic model. It does not execute its CPU or announce main-firmware readiness. [Milestone066](../../../tweets/066_sdio_first_main_payload.md) explains the result and exact trace sections.

- [Positive trace](positive-mmio.log), [report](positive-report.json), and [model state](positive-main-model.json): original host consumes request16→512→16; stop after instruction0xc038a8c4 has committed the final pair, at0xc038a8c8 with source offset528. All39 physical transfers complete and receive genuine acknowledgements.
- [Negative trace](main-negative-mmio.log) and [report](main-negative-report.json): disabled semantic model retains the original100-zero-poll failure after the full helper receive.
- [Verification](verification.json): original input/source hashes, all actual bytes, full command/IRQ/DMA ordering, request coherence and private-DT audit.
- [Root's independent byte audit](root-byte-audit.json) and [source](root-byte-audit.py): extracts original Mach-O segments directly, without precomputed fixtures/model logic.
- [Actual main wire](actual-main-wire.bin):32-byte header frame followed by512-byte body. [Card RAM](positive-card-ram.bin):508 copied bytes plus four untouched zero bytes.
- [Contracts](contracts.log):549969 pass, including all310197 earlier checks.
- [Source provenance](source-provenance.json), [relocated entry](arm-entry-dis.txt), [ARM copy](arm-copy-dis.txt), [Thumb receiver/callbacks](thumb-receive-dis.txt), and [original host loader](original-main-loader-dis.txt). The Thumb listing includes literal pools; only reached functions identified in the contract are treated as code.
- [Reviewed source-only contract](protocol-contract.md) is preserved as the proposal, not retroactively rewritten as an execution claim. Its `.context` relative links refer to the original workspace investigation. Actual implementation/tests are retained under [source/](source/).

The initial probe tried18 instruction-hook addresses, exceeding the core limit before guest instructions ran. The corrected source selects15 relevant hooks per mode. The [failed setup source](attempt-hook-limit/probe.c), [hashes](attempt-hook-limit/sources-before.sha256), and [startup log](attempt-hook-limit/main-negative-boot.log) are retained; its explicit hook-admission return was9. The first verifier version also included a pre-upload zero requested-length pair; its final version partitions main-stage reads after the actual helper terminator and checks the earlier pair separately. Neither correction altered device behavior.

Run the durable contract suite from repository root:

```sh
cc -std=c11 -O2 -Wall -Wextra -Werror docs/traces/sdio-main/source/test_main.c docs/traces/sdio-main/source/sdio.c -o /tmp/sdio-main-contracts
/tmp/sdio-main-contracts
```

The preserved probe/verifier retain their workspace paths under `.context/sdio-first/main-first/` and require the original extracted firmware, work disk and native archive. Their complete source hashes and exact input hashes are in verification.json. The first supported card-RAM mapping is512 bytes at0xc0001000; unsupported destinations/lengths produce a separate model diagnostic. This is not a complete card-memory implementation.
