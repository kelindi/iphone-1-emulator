# Daemon lifetime observer qualification — source and synthetic tests only

No guest continuation, image edit, daemon change or production hook was run. The failed093 form and both independent graphics/daemon boundaries remain intact. This prepares a causal diagnostic; it does not explain the exit.

## Qualified original addresses

`addresses.json` binds each image SHA, LC_SEGMENT file offset, N_ARM_THUMB_DEF mode and exact32-byte window. `qualify.py` generates retained disassembly. Its linear ranges can include literal pools or unnamed following functions; they are not a complete control-flow graph. Addresses are from the actual original files, not another firmware or an assumed runtime map.

| Purpose | Actual file VA / mode | Meaning and limit |
|---|---|---|
| Kernel exit syscall wrapper | c00f7168 / Thumb | Loads caller exit argument, shifts it left8, calls exit1 |
| Kernel exit1 | c00f7094 / Thumb | r0=proc, r1=encoded status. Actual code writes r1 preserved in r10 to proc+0x128 at c00f710e |
| Fatal signal exit | c00fdf6c / Thumb | r0=proc, r1=signal; preserves them, optionally invokes coredump, then passes status to exit1 at c00fdfb0 |
| Signal request | c00fd790 / Thumb | r0=target proc, r1=signal, passes r2=1 to internal signal routine. This is a request, not delivered/terminal signal proof |
| Own final `_exit` call site | 00001a84 / ARM-USR | Calls actual saved eager import pointer; r0 is70 or71. Not proof that exit syscall completes |
| Own listener poll return site | 00001c1c / ARM-USR | Candidate return observation, requires matching real call before interpreting r0 |
| Own accept return / errno load | 00001c74 / 00001d04, ARM-USR | On negative return, actual `__error()` pointer is dereferenced;4/35 retry, other values lead to fatal path |
| Own accepted-client close return | 00001cb4 / ARM-USR | A negative close result goes to daemon failure cleanup |
| Own setsockopt return | 00002434 / ARM-USR | Caller preserves r0 before testing success; accepted-client failure ordinarily rejects only that request |
| Own error logger | 00002218 / ARM-USR | r0=bounded label, r1=result, r2=detail/errno; actual write return must be captured separately |

Native Home already binds process pointer `cc2e5d98`, PID21, task`cc285620`, exact16-byte name`iphone1-local-ht`; initial actual own USR entry records TTBR`14a04000`. These are evidence for this exact snapshot only. The initial-entry trace does not retain every CP15 address-space field needed by the new own-site observer. A future adapter must capture the full actual context on an executed own-code event and bind the live process/task, not fill missing fields from defaults.

Original library file VAs: `_exit`300099f0 ARM, `__error`30001bb0 Thumb, accept30030520 ARM, poll30067ae0 ARM, setsockopt3000e8b0 ARM, close30009880 ARM, write30009860 ARM, read300097e0 ARM. Actual relocated import pointers and executed code must match before a runtime observer uses them. The original accept/poll/close/setsockopt exports here are syscall veneers. No CRT-cancellation-wrapper failure is established.

## Socket and fatal-path review

Historical public Apple headers identify SO_NOSIGPIPE0x1022 and EINTR4/EAGAIN35/ECONNABORTED53. The [2007 socket implementation](https://raw.githubusercontent.com/apple-oss-distributions/xnu/xnu-1228/bsd/kern/uipc_socket.c) copies an integer option and updates a socket flag. This public version is provenance, not assumed byte-identical OS1 source.

The actual OS1 `_sosetopt` independently confirms dispatch: at c01125d6 literal c0112850=1004, then c011263c literal c0112858=1023, and c01127a6 literal c0112870=1021 plus1 selects1022. c01127b4–27bc copies exactly4 bytes; nonzero value follows c01127ce–27dc and ORs bit0 at socket+0x114. Thus this option exists in the actual kernel. We have no actual failed accepted-client setsockopt result.

Our listener setup must pass `nonblock` before the verified bind/listen. On an accepted client, a nonblock/SO_NOSIGPIPE failure makes `ok=false`, closes that client, logs HTTP_REQUEST_REJECTED and continues if close succeeds. It alone does not terminate the daemon. Fatal candidates are listener poll error/unexpected events, nonretry accept errno, or negative client close, plus independent kernel termination. No observed errno selects one. Quiet mode leaves failure logs enabled; its bounded console write can itself fail.

The public [exit](https://raw.githubusercontent.com/apple-oss-distributions/xnu/xnu-1228/bsd/kern/kern_exit.c) and [signal](https://raw.githubusercontent.com/apple-oss-distributions/xnu/xnu-1228/bsd/kern/kern_sig.c) sources provide interpretation context. Actual original instructions above bind the diagnostic arguments. Signal request, fatal-signal entry, exit1 entry, status store and removal from allproc are distinct observations.

## Focused proof actually completed

`observer.c/h`, generated `bindings.h` and `test_observer.c` pass **613 checks** under AddressSanitizer/UBSan for the observer/helper code, linked to the unchanged current native archive. The archive itself was not rebuilt with sanitizers. Tests cover actual pinned code windows, wrong ARM/Thumb/privilege, PID/task/name mismatch, missing mapping, all32 individual code-byte corruptions, all six address-space fields, denied USR permissions, overflow and unchanged output on refusal. CPU, RAM and live bus callbacks remain unchanged.

These functions deliberately return **entry observations only**. They do not yet own a call/return state machine, prove retirement, trace errno or install hooks. A matching post-call address alone cannot prove a syscall returned. Before any guest run, a reviewed adapter must pair actual call site→relocated target→return with exact owner context and mode; return observations preserve r0 before later instructions modify it. Kernel exit-status completion needs an actual before/after store witness, separate from entry. No longjmp or pre-step fake yield may publish a snapshot.

The first binding-generator attempt contained a Python f-string syntax error, preserved as `generate-bindings-initial-syntax-failure.py`; no guest ran. Corrected generation and the focused build/test passed. `check-report.json` pins these results.

## Native scripted Safari feasibility and proposed cap

A native reproduction is technically feasible: the existing probe already uses `s5l8900_set_touch` for genuine Z1 contacts and `s5l8900_set_button` for Home, and captures actual CLCD scanout. It must restore the exact listener/Home snapshot and use180ms contact holds measured in guest timer ticks. Stage advances need retained screen/readiness predicates, not a translation of browser video timestamps into instruction counts. Dynamic input settling and graphics failure can prevent reaching a later stage; stop and retain that boundary.

A single proposed native diagnostic would follow only Home→Safari→numeric loopback URL→landing→Next→first form, with ordinary touches and no HTML/device injection. It is a new diagnostic continuation, not an exact replay of093: browser host-clock catchup, JIT scheduling and background autosaves differ. A failed native reproduction cannot rule out a browser-only cause.

The actual093 exported instruction delta is6,019,550,735 from native listener to failed form. Native coldboot took385.88s for9.197B including serialization (~23.8M instructions/s as a rough scheduling estimate only). Proposed limits are **8B additional instructions /450s run wall, then separate120s serialization**, within the earlier10B/900s ceilings; no automatic extension or retry. Estimated250–337s before observation overhead is not a performance prediction or assurance of completing the UI path. Every stage also needs a finite instruction/tick deadline and a total event-ring bound. Parent must review final adapter, source pins, stage predicates and schedule before execution.

Stop first at a bound own-process fatal signal/exit-status witness, unavailable observer identity, failed graphics/readiness stage, or cap. Preserve normal outside-run state and exact trace even if no causal exit is found. No KeepAlive, daemon source, graphics mode or service-policy change accompanies this proposal.


## Final adapter preparation, awaiting guest scheduling

The [separate adapter packet](adapter-1/README.md) now implements actual-owner call/return/errno and committed-status observations, with **461 focused adapter checks and 20 UI checks**. The previous 311-check adapter fails the added shared-process regression and remains preserved. Other processes executing common library addresses no longer contaminate the daemon's pending call. The daemon and old renderer remain unchanged.

The first zero-instruction image check failed on a browser screenshot projection. Exact artifact comparison identified one extra top row; the full cropped browser frame equals all460,800 native bytes. The final alert reference instead uses actual native frame17 and matches its127,500 selected bytes exactly. The [final zero-step report](adapter-1/check-report.json) keeps cycles at9,197,000,000 and records source/owner admission without executing guest instructions. Both the original failure and corrected preflights are archived.

The frozen one-attempt proposal remains8B additional instructions /450s run /120s serialization, normal outside-run snapshots only, actual finite touchscreen and scanout stages, and no automatic retry or default promotion. [Pins](adapter-1/pins.json) bind the immutable pre-stride archive/source and final runner. **No diagnostic guest continuation has run and the original daemon exit cause remains unknown.**
