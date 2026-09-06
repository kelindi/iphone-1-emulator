# Isolated verified streaming seed loader

**Persistent native-code reuse is verified in the isolated HTTP mechanism gate; production is unchanged.** The candidate preserves the original seed, ABI, generated admissions and controller lifecycle. Its only intended behavior change is how the already verified seed reaches WebAssembly compilation. The mechanism is accepted, but repeatable return-visit speed, first-ever launch gains and full-speed functionality are not established.

The current loader uses `WebAssembly.compile(binary)`. The candidate retains the actual fetched Response, reads a clone under the existing 32MiB cap, verifies both SHA256 identities and parses definitions, then calls `compileStreaming` with that still-unused original Response. A synthetic `new Response(binary)` would discard relevant browser metadata. Unsupported streaming, unavailable cloning or unusable MIME use the same verified bytes; a streaming TypeError falls back without refetching, whereas a real compilation error remains an error. [Candidate](seed-jit.js), [exact diff](loader.patch), [source identity](source-proof.json).

The 8-second deadline aborts fetches, discards late compilation and cleans up owned streams. Tee cancellation is deliberately not awaited: one branch can otherwise wait for its unread sibling. Late fetch completion after timeout cancels its newly arrived response. Native compilation itself is not cancellable by this promise; the timeout prevents installation of a late result. Buffering the verification clone can retain an additional seed-sized stream queue during verification; browser peak memory remains unmeasured. The 32 MiB input bound does not imply a32MiB total-memory bound.

The entire `class SeedJIT` suffix remains byte-identical, including 512-region cap, ABI checks, disabled/missing-export fallback, allocation cleanup, restore generation reset and table retirement. The loader candidate SHA is `3c2cc4122a9b32ab22db44d253e26920650c885c1c019b170a2951002ce597e9`; production loader remains `3ddac5082690e4ea0f0e710105ccbc7ec1a2f1afa6da74986b0c56691f173d2f`. Production core is b3237 and original seed d9c661; full hashes are in the proof.

## Exact browser source evidence

The reviewed Chromium release152.0.7977.82 resolves to commit `d04cdb24d67b081f6cf80200ffc5233f44b61109`, whose DEPS pins V8 `4323497a6a73839e6d5260f6acd7ec0212cb3321` (15.2.124.21). [Revision](chromium-revision.json), [source URLs/SHA256/lengths](source-manifest.json), [verified refetch tool](fetch-sources.py). No equal-version assumption substitutes for these exact source pins.

- Fetch creates a Wasm cached-metadata handler for `application/wasm` responses on HTTP-family or extension URLs and later attaches received metadata to it. This makes the proposed actual HTTP response eligible for a handler; it does not prove successful persistence or reuse. [FetchManager lines725–741 and812–815](https://chromium.googlesource.com/chromium/src/+/d04cdb24d67b081f6cf80200ffc5233f44b61109/third_party/blink/renderer/core/fetch/fetch_manager.cc#725).
- Response cloning retains URL list, response time and CacheStorage name. Body tee gives both branches the existing cached-metadata handler. Consuming the verification branch clears its own buffer's handler member; the untouched original branch retains its own reference. [FetchResponseData clone](https://chromium.googlesource.com/chromium/src/+/d04cdb24d67b081f6cf80200ffc5233f44b61109/third_party/blink/renderer/core/fetch/fetch_response_data.cc#194), [BodyStreamBuffer tee/close](https://chromium.googlesource.com/chromium/src/+/d04cdb24d67b081f6cf80200ffc5233f44b61109/third_party/blink/renderer/core/fetch/body_stream_buffer.cc#286).
- Streaming obtains the original Response URL and body metadata handler, configures serialization callbacks, and loads that buffer. A `cachedModule` trace is emitted before asynchronous cache delivery. A URL-labelled `moduleCacheHit` only means metadata was found; digest matching and V8 deserialization can subsequently reject it. These events individually are insufficient evidence of accepted persisted native code. [Blink Wasm Response extension](https://chromium.googlesource.com/chromium/src/+/d04cdb24d67b081f6cf80200ffc5233f44b61109/third_party/blink/renderer/bindings/core/v8/v8_wasm_response_extensions.cc).
- Non-service-worker and passthrough responses use the isolated code-cache route. CacheStorage responses have a separate route; synthetic or differently sourced service-worker responses can disable caching. The initial HTTP mechanism gate therefore cannot establish offline-cache compatibility. [CachedMetadataSender](https://chromium.googlesource.com/chromium/src/+/d04cdb24d67b081f6cf80200ffc5233f44b61109/third_party/blink/renderer/platform/loader/fetch/url_loader/cached_metadata_handler.cc#126).
- Current V8 source defaults are1,000 newly generated top-tier code bytes before scheduling caching, a1,000,000-byte immediate threshold, and2,000ms quiet timeout. These are defaults, not verified effective flags or a guarantee of browser cache acceptance. The old2019 blog's128KiB statement is not used as a current threshold. [Pinned flag definitions](https://chromium.googlesource.com/v8/v8/+/4323497a6a73839e6d5260f6acd7ec0212cb3321/src/flags/flag-definitions.h#2028), [compilation scheduling](https://chromium.googlesource.com/v8/v8/+/4323497a6a73839e6d5260f6acd7ec0212cb3321/src/wasm/module-compiler.cc#3959).

## Verification and retained failures

Run from the repository root:

```sh
node --test docs/traces/jit-stream-cache/loader.test.mjs docs/traces/jit-stream-cache/cache-server.test.mjs docs/traces/jit-stream-cache/lifecycle.test.mjs
```

[All 49 contracts pass](all-tests.log): 22 loader checks use actual Node Response/ReadableStream tee and tiny executable Wasm; one real loopback HTTP contract verifies stable bytes, MIME, headers, ETag/304 and HEAD; 26 controller/worker lifecycle checks use the actual production worker with mocked firmware/runtime. The controller test reads the candidate without substitutions. Node responses do not have Blink's HTTP metadata handler; these are integrity/lifecycle tests, not browser native-cache tests.

The intentionally wrong loader that ignores both integrity results fails four tests, including compilation attempted for corrupted bytes. [Negative source](wrong-integrity.js), [negative report](wrong-integrity-tests.log). The first extended test run failed twice: a diagnostic server variable shadowed the seed file metadata, producing404, and the cancellation spy observed the pre-tee body rather than the replacement original branch. The server scope and test observation were corrected; the loader did not change for these failures. [Retained initial failure](initial-extended-tests-failure.log).

## Actual two-process mechanism result

Exactly two sessions ran with the initially empty persistent profile, identical origin/seed, fixed paused normal Import and one-second resume delay. All 12 captured Chrome process identities in each session exited before reuse. The complete traces report no data loss, and both actual responses hash to the original d9c661 seed/b3237 core. [Pair and process receipts](mechanism/pair.json), [compressed full traces and hashes](mechanism/trace-pins.json), [HTTP request log](mechanism/server.log).

Session 1 records 15 serialization events and no cache hit. Session 2 consumes 21,104,920 bytes of seed-URL cache metadata. In one complete PID/TID scope, digest validation precedes `wasm.Deserialize`, whose interval contains `wasm.FinishAsyncCompile` and `wasm.OnCompilationSucceeded`; there are no invalid cache events. The seed digest/metadata source path can only reach this nested completion after successful native deserialization. It is not inferred from a faster response or an HTTP hit. [Event chain](mechanism/2-persistent/cache-analysis.json), [independent parent review](mechanism/root-review.json).

The pure analyzer passes the real trace and rejects 12 deliberate mutations: missing success/digest/finish/hit, duplicate hit, wrong process/thread/URL, out-of-scope deserialization/success, and injected invalid/invalid-digest events. [Analyzer](analyze-cache.py), [negative tests](test-analyzer.py), [results](analyzer-negatives.json). An initial refactor indentation error was caught by py_compile before analysis; its source is retained as `analyze-cache-refactor-syntax-failure.py`. No browser repeated for that source correction.

All 8 raw phone-frame comparisons pass against independently frozen original C0/H1 references: RGB<=2 below rawrow30, exact alpha, no changed threshold/crop. All 8 PNGs were manually reviewed as genuine original Settings and complete Home. [Strict comparisons](mechanism/screen-comparison.json), [manual review](mechanism/manual-screen-review.json).

| Diagnostic action | Empty-profile session | Fresh process, same profile |
|---|---:|---:|
| First Settings |5.6649s|3.4659s|
| First Home |0.8660s|0.8663s|
| Warm Settings |2.5678s|2.8666s|
| Warm Home |0.8662s|0.9163s|

These single observations establish neither a causal savings amount nor repeatable launch/p95 acceptance. The header is deliberately cacheable and service workers are bypassed; those delivery conditions differ from production. The complete first 68.48-second and returning 62.20-second recordings have unchanged durations and no cuts or speed changes. [Milestone076, both recordings and thumbnails](../../../tweets/076_compiled_seed_reuse.md), [recording identity](recordings.json).

## Next decision

[The bounded cache mechanism plan](cache-test-plan.md) specifies separate fresh Chrome processes sharing a persistent profile, a stable actual HTTP origin, unchanged normal Import and no Playwright routing. Read-only trace receipts must bind accepted native deserialization to the exact seed URL, independently of HTTP-cache hits. Both sessions require full original-speed recordings and strict real-phone screenshot/counter gates. The positive mechanism result now justifies preparing the separate [return-visit comparison](return-visit-plan.md); it has not run. That plan uses no-cache plus strong ETag/304 and a newly predeclared settled-screen phase, without rewriting older failed gates. Production delivery would additionally require no-cache plus strong validators/304 and the actual service-worker/offline upgrade gate; giving a mutable stable seed URL one-hour freshness in production could leave it inconsistent with a newer ABI.
