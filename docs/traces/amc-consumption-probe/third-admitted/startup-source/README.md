# The actual audio client is MeCCA, with a direct explicit-start sequence

This is source evidence, not a new guest run. The previously captured original driver's client type is 1, named MeCCA by Apple's own assertion text. The separate IOAudio2 transformer selector 1 resolves to an AMC virtual override at c02732a0; it is not the direct AMC output-start method.

The original MeCCA framework supplies the matching path: `restartCodec` prebuffers, calls scalar selector 2 to initialize the decoder, checks its return and four output values, obtains output-buffer information, then calls scalar selector 3. Original kernel method 3 reaches c0272224 and the existing +4/output-mask wrapper. Input refills are handled by a separate task, whose modulo-three loop explains the observed buffer-index wrap. Source does not require a fourth transfer before explicit start.

The [original-file oracle](start-path-source-oracle.json) independently checks symbols, instruction bytes, import-stub identity, virtual-table encoding and the real captured client type. [Original source extracts](mecca-restart-dis.txt) and the [bounded next observation contract](next-mecca-return-contract.md) identify exact user/kernel return sites needed to distinguish an error or output-count mismatch from the setup thread not returning to user mode before the fixed fourth-request stop.

No new start/PCM consumption is claimed. No DMA timing, input admission, ready state, engine, browser or production behavior changed. Full original framework files remain private and are not included here. Original SHA256 hashes and all captured evidence boundaries are recorded in the oracle and contract.
