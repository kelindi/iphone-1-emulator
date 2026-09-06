# The original Wi-Fi driver accepts an initialized device model

## Tweet idea

The 2007 iPhone Wi-Fi driver now passes its own firmware-verification comparison against our isolated device model. We verified the uploaded image, initialized real bounded buffers and watched the original branch execute. Network commands and connectivity are still ahead.

## What you're seeing

This is a nonvisual original-driver milestone. The [actual witness log](../docs/traces/sdio-initialized-service/positive-1/milestones.log) shows the card's admission response, zero requested-length pair, real loader return, successful `0x34/0x35` verification reads and committed branch to **`0xc0380208`**. The following unsupported register6 read has not executed.

The [complete 22,919-event trace](../docs/traces/sdio-initialized-service/positive-1/transactions.bin), [independent audit](../docs/traces/sdio-initialized-service/positive-1/audit.json) and [separate root review](../docs/traces/sdio-initialized-service/positive-1/root-review.json) match the actual ordered commands and responses. The unchanged control reproduces the earlier **1,000-status8-poll failure**, including all **4,581,748 transactions** and the exact trace hash. [Control evidence](../docs/traces/sdio-initialized-service/control-1/audit.json).

The marker-disabled negative remains **incomplete**: it consumes three successful zero-valued pairs, then exhausts its fixed100-million-instruction budget before the intended100-attempt timeout. No verification acceptance occurs in that prefix, but the full negative gate is not claimed passed. Its [failed report and exact prefix audit](../docs/traces/sdio-initialized-service/no-marker-1/prefix-audit.json) remain preserved, with no retry or raised limit.

## ELI5 technical explanation

The driver asks two different questions: can the card accept work, and does its initialized service have the expected verification value? The model now answers from actual state: the received image and write coverage must match, the final handoff must be valid, and two independent guarded buffers must have been successfully allocated and initialized.

Those buffers have real reserve, byte-copy, bounded-read, release and reset operations. A declared guest-time delay precedes availability. Unknown commands still fail, and the old firmware-download parser stays closed. This is a supported high-level device model; it does not execute the uploaded card program or provide runtime command responses.

The acceptance probe also checks CPU mode and a newly observed, ordered successful low/high pair before the actual comparison branch. A matching address in Thumb mode or an old stack value cannot satisfy it. **48,451** focused checks, **1,012,343** retained SDIO checks, **36,578** lifecycle checks, **16,396** mode/pair checks and **13** auditor fixtures pass. Deliberately removed image validation fails110 assertions. [Source, tests and retained failures](../docs/traces/sdio-initialized-service/README.md).

## Why it matters

The original host has progressed beyond transferring firmware bytes into its own verification path. The verified private checkpoint lets this investigation resume after the expensive upload. The accepted scope remains narrow: no production change, card CPU execution, successful runtime commands, network interface or connectivity. Those require their own original-driver evidence.
