# Narrow integration proposal after the rendering-only gate

Root owns all production/core/web edits. Candidate source remains the reviewed stride472.patch; only BGRA8 source pitch bound changes. Native and actual WASM 2847/2847 checks pass. Baseline current RTC relink exactly reproduces released7a72add6…; candidate3b43ccf4…aa4463 uses only a replaced MBX archive member, with original API/runtime/link flags, all exports/imports, JIT ABI1/offsets and d9c seed unchanged. The archive report provides exact hashes/commands.

The separately frozen recovery-run-2 passes ten checks. All screenshots were manually reviewed; original address typing shows exactly `glyph`, two Home crops [0,30,320,450] match all144000 RGB pixels, and the full final serial including export/closure has no new graphics hang/restart. Final snapshot a4317f61…0186c is a diagnostic continuation of the failed Safari state. The HTTP daemon is absent; this is not form/server qualification. The existing page clips at the viewport, so this is not full content/layout acceptance. First launcher EOF/timer/video/export remain explicitly INCOMPLETE.

Proposed root-owned steps:

1. Apply only reviewed maintained mbx.c/test_mbx.c patch, run the existing native set and meaningful new tests. Keep all other core/source changes separately owned.
2. Stage previous/current RTC web and candidate web in separate immutable projections, with all original seed assets. Confirm candidate glue equality and exact non-WASM asset scope before replacement. Generate the candidate service-worker bundle from the actual complete package rather than copying an old cache version.
3. Run actual old saved-phone upgrade: create/save on old engine, update service worker/core, confirm byte-identical saved checkpoint and metadata, actual new candidate engine loaded, original JIT active; close origin server and recover offline from updated bundle. Preserve incompatible/missing cache fallback behavior. Existing save format remains unchanged.
4. Final default fresh-load/controls smoke must serve the exact accepted candidateWASM and unchanged seed. No default HTTP/Photos claim; both require their separately captured workflows. Keep093/094 failures, and095 rendering scope, explicit.

No expansion of arbitrary source width, 3D support, host rendering substitution, service restart, firmware graphics policy, seed or snapshot schema is proposed.
