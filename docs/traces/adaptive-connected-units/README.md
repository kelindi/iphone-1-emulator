# Connected IR units — Stage 2 foundation

The unit builder follows decoded branch successors across captured 1 KiB virtual
code windows. It splits basic blocks at joins and loop targets, retaining both
conditional outcomes. It admits at most 32 blocks, 512 static instructions and
eight distinct physical code granules. Uncaptured or capped targets remain
explicit external edges. Unsupported instructions remain fallback operations;
no instruction has been executed by this builder.

Input code bytes are copied synchronously. Conflicting physical aliases or
versions are rejected; consistent aliases share a physical dependency while
retaining their virtual PCs. The resulting object graph is recursively frozen.
This is compilation input ownership, not runtime code invalidation. The caller
must capture permission-checked mappings and versions; execution will still
require live validation and literal guards under ABI2.

[Nine focused tests](tests.log) cover cross-window branches, backward targets,
conditional joins and explicit BX dynamic exits, immutable output, external edges, all three limits, consistent
and conflicting aliases, alignment and duplicate windows. [Builder](unit.mjs)
and [test source](adaptive-unit.test.mjs) are preserved as milestone evidence.

No WASM emitter, guest execution, runtime dispatch or performance comparison is
claimed here. The next implementation consumes this graph and implements the
[explicit ABI2 result contract](../../adaptive-compiler-abi.md).

Review identified that physical dependency metadata is not a translation-context
witness: the installer needs a separate immutable context/mapping envelope.
Blocks now expose dynamic branch and before-instruction exits separately from
static edges so an emitter cannot accidentally treat conditional BX as merely
fallthrough. All 88 JavaScript unit tests pass.
