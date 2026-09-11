# Physical code-version writer audit

Status: partial implementation. **Literal instruction guards remain required.**
The isolated ABI2 runtime can track main RAM in physical 1 KiB granules relative
to its physically aligned RAM base. Host pointers identify the underlying RAM,
so virtual aliases share a version. Zero is invalid. Each committed notification
advances every touched granule; overflow or an invalid span fails the lifetime
closed. A new `adaptive_bind` changes the epoch and destroys derived versions.
Versions, callbacks and function handles are not serialized guest state.

`adaptive_track_ram` must succeed before capture. Captures carry `code_version`;
installation rejects old captures, entry rejects/retire stale installed units,
and guards reject versions changed while a unit is active. Literal checks still
run after version checks. A version match alone does not authorize execution.

| Writer / path | Current coverage | Remaining requirement |
|---|---|---|
| `arm_interp.c` `MEM_WRITE` cached `dwrite_hit` | Notifies after direct byte/halfword/word memcpy | Custom paths must retain this post-commit contract |
| ABI2 `adaptive_write` / `arm_write_cache_try` | Qualified warm-cache helper notifies after commit; active code overlap forces an immediate exit | 51 integrated cases pass; see [store evidence](traces/adaptive-ram-stores/README.md) |
| `machine.c` `bus_write` | Notifies each committed RAM span; MMIO excluded | Custom/interposed buses must preserve the notification contract |
| `arm_interp.c` crossing/multiword/VFP stores | Commit through the two paths above | Broader fault/atomic/exclusive observer matrix before removing literals |
| `pl080.c` `bus_store` DMA | Uses notified machine bus writes | Tested widths and boundary spans; broader device scheduling qualification remains |
| `mbx.c` `mbx_gart_write` | Direct span notifies after memcpy; fallback uses notified bus | Private staging buffers are not main RAM; future commit paths must notify |
| `machine.c` `s5l8900_load` | Notifies successful in-range copy | Callers mutating returned/raw RAM afterward still need coverage |
| `tools/jit/compiler.cjs` ABI1 generated stores; `emu_jit_span` | **Not covered**: raw WASM stores bypass callbacks | Instrument/rebuild, or exclude ABI1 from a tracked lifetime; invalidate before tier transitions |
| `snapshot.c` `snap_apply` / `snap_ram` | Adaptive build calls `arm_adaptive_forget` before apply | Barrier precedes all apply reads; injected late file-I/O failure remains untested |
| `machine.c` init/free/reset | Adaptive init/free now call forget before mutation | Browser host still needs rebind/reconfigure after replacement; CPU reset also changes execution context |
| `bringup_os1.c` `s5l_os1_boot` tree edits | Adaptive boot invalidates before all boot work | Direct edits run unbound; armed boot error-path matrix remains pending |
| `firmware/loader.c` `fw_load_img3_common` | Adaptive loader invalidates before direct copy/decryption | Validate armed partial-decrypt/error cases before final writer qualification |
| Optional `bringup.c` / native kernel patch callback | Adaptive native bringup invalidates before edits | Keep callback execution unbound; optional native integration remains unqualified |
| Optional `md_bridge.c` / `md_raw_bridge.c` | **Not covered**: copies, mapped stores, bounce buffers in guest RAM | Add commit/lifetime interface; reads can partially modify destination on error |
| Optional `a64_static_engine.c` raw-RAM execution | **Not qualified** | Separate write instrumentation/exclusion audit |
| Production web `HEAP*.set` staging | Inspected writes are separate malloc input/mask buffers | No claim that arbitrary external HEAP mutations are observable |
| Test harness raw guest-RAM edits | Paused setup only in version fixtures | Notify/rebind explicitly for writes whose stale-code behavior is under test |

The opt-in scheduler uses the interpreter for ABI2 fallbacks, and the isolated
qualification build disables ABI1. That does not establish safe arbitrary tier
mixing in production. Notifications must become visible before the next affected
instruction; merely maintaining a counter without checking it is insufficient.

Evidence and exact current test scope: [code versions](traces/adaptive-code-versions/README.md).

[117 lifecycle evidence](traces/adaptive-lifecycle/README.md) verifies real
restore/free/disable barriers. Explicit disable always advances a bound epoch,
even with only an external pending capture. The existing generated stores remain
uninstrumented: engine transitions must use this API, not assign the public flag
directly. Literal guards still protect the partially integrated architecture.
