# Original DNS-SD local-record diagnostic

The actual Weather resolver path reaches original `DNSServiceQueryRecord`.
Static original libSystem binding identifies the call at `0x3009bb64` inside
`gai_start_res_query_internal`; the original indirect-symbol table resolves its
stub `0x300e3198`. It then obtains `DNSServiceRefSockFD` and installs an async
event. A separate original-execution observer confirms the arguments three times:
name `iphone-wu.apple.com` without a trailing dot, flags `0x1000`, interface0,
typeA(1), classIN(1), callback `0x3009bde0`. The Weather failure is not merely a
missing synchronous hosts-file lookup. See `.context/app-services-current/`
`weather-query-probe/` and `weather-resolver-probe/` for the independent captures.

Original libSystem SHA-256 is
`6243ad0b202fa364ac7b243f0416013f81416e32abb3c69e30c6b020250448e2`.
Its real exported entry addresses include:

| Function | Original address | Ownership/use |
|---|---|---|
| DNSServiceCreateConnection | `0x300bd6dc` | Creates a retained registration connection. |
| DNSServiceRegisterRecord | `0x300bd778` | Registers one record on that connection. |
| DNSServiceQueryRecord | `0x30045568` | Creates a query reference/callback. |
| DNSServiceRefSockFD | `0x3003f994` | Supplies its descriptor for ordinary poll. |
| DNSServiceProcessResult | `0x30044768` | Delivers queued callbacks. |
| DNSServiceRefDeallocate | `0x3003fa20` | Ends the reference and its registrations. |

Apple's [108.4 API header](https://github.com/apple-oss-distributions/mDNSResponder/blob/1447e4dcf5dbc1e99e27a4cd934df545e82cbb63/mDNSShared/dns_sd.h)
documents LocalOnly interface `UINT32_MAX`, unique-record flag `0x20`, raw RDATA,
record lifetime on a connection and asynchronous registration errors. The
[daemon registration path](https://github.com/apple-oss-distributions/mDNSResponder/blob/1447e4dcf5dbc1e99e27a4cd934df545e82cbb63/mDNSShared/uds_daemon.c)
passes the requested record to `mDNS_Register` and retains connection ownership.
The API supports the proposed mechanism; this alone does not prove it answers
this original application's non-`.local` Any-interface query.

**The source-version distinction matters.** The actual retained daemon reports
version118. No matching118 tag was found in Apple's published repository. Public
[108.4 core](https://github.com/apple-oss-distributions/mDNSResponder/blob/1447e4dcf5dbc1e99e27a4cd934df545e82cbb63/mDNSCore/mDNS.c)
returns early into separate unicast query handling for nonlocal names, so its
browsing documentation cannot establish this behavior. However, exact original118
`mDNS_StartQuery_internal` at `0x95a8..0x9a08` does something different: nonlocal
queries acquire a message ID and then join the same Questions list; LocalOnly
uses a separate list. The retained binary also contains its local-answer routines.
This removes the specific older-source objection, but does not replace a real
registration/query test. No claim of accepted local resolution has been made.

## Finite independently authored probe

`dns_record_probe.c` is separate from the HTTP gateway and Apple programs. It:

1. Obtains an original DNS-SD connection, with at most30 attempts separated by
   one-second sleeps.
2. Runs a baseline Any-interface query with the exact observed Weather options.
3. Registers unique LocalOnly A data `7f 00 00 01`, TTL60, under
   `iphone-wu.apple.com.` and waits for the registration callback.
4. Repeats the exact Any query, then runs a LocalOnly positive control.
5. Logs each status, callback, answer name/type/class/TTL and bounded raw RDATA,
   deallocates all owned references and exits.

Exit0 requires no matching baseline answer, a matching post-registration Any
answer and a matching LocalOnly control, with valid callback ownership and logs.
An already-positive baseline cannot qualify registration as the cause. A baseline
timeout means only no observed answer within the bound. Query creation errors,
negative callbacks and timeouts are separately logged; the control still runs
where possible. No external name is queried. A matching answer requires Add,
typeA/classIN, exactly four address bytes and the expected canonical name.
In particular, exit0 is possible when the baseline query failed to start but
both later queries succeeded. Inspect the baseline call/status before claiming a
controlled before/after attribution; exit0 alone does not establish that claim.

Each phase has at most50 polls of100ms. Only readable original descriptors are
passed to `DNSServiceProcessResult`. The original library's synchronous calls
do not expose a separate timeout API, so the outer native instruction/wall cap
remains necessary. Connection loss, conflicts, daemon lifetime, registration
permissions and actual unicast-query visibility are runtime risks to observe,
not reasons to patch Apple code. A positive probe would still require a subsequent
original Weather/Safari workflow; it is not app acceptance by itself.

The original ARMv6 build at `.context/app-services-current/mdns-local-audit/build/`
`dns-record-probe` is13468bytes, SHA-256
`e6adfdc352884c946c5bae2373505935c39db0985603fcba287fc29ad70c505f`.
Its `build.json` audits exactly11 eager original imports: six DNS-SD functions
above plus poll, write, usleep, `__error` and `_exit`. Source uses independent
freestanding memset support, with no SDK implementation copied.

`test_dns_record_probe.c` passes1606 host-mocked checks: matching options,
baseline/registration/control ordering, already-positive baseline, Any-negative
with LocalOnly-positive, conflict, timeout, malformed address, synchronous refusal,
bounded readiness and exact reference deallocation. Run with ordinary native C:

```sh
cc -std=c11 -O2 tools/legacy_services/guest_http/test_dns_record_probe.c -o /tmp/dns-probe-test
/tmp/dns-probe-test
```

Source/API excerpts, original annotated instructions, export receipt and preflight
hashes are in `.context/app-services-current/mdns-local-audit/`. This note describes
preflight evidence only. Root owns isolated provisioning and the bounded guest
diagnostic; the gateway and production phone have not been changed by this probe.

## First guest failure: missing own executable startup variable

The first bounded5B-instruction probe emits BEGIN but never returns from
DNSServiceCreateConnection. Independent inspection of its400M checkpoint finds
the own thread in exception handling, with saved user PC`0x3002266a` and r0=0.
This is the original Thumb allocator's `ldr r4,[r0]`, immediately after its call
to `_NSGetEnviron` at`0x30022658`. It is not evidence that a socket wait exhausted
the bound or that local-only DNS registration is unsupported.

Exact original `_connect_to_server` first calls malloc(24); it does not directly
call getenv or inspect a configurable DNS socket path. The allocator's environment
accessor lazily asks dyld for the executable symbol `_environ` and returns that
symbol's address. The initial freestanding probe exports no such symbol. The
original lookup string at`0x300e96c0`, accessor instructions at`0x300020e0`, probe
symbol table and actual allocator fault establish the missing startup prerequisite.
Related [Apple Csu startup source](https://github.com/apple-oss-distributions/Csu/blob/be4fbb1c9d5d48515b9c1b28b041eeba25c3bcfc/crt.c)
defines this executable global and initializes it from envp; that source is
supporting ABI context, not an exact iPhone startup binary match.

`standalone_runtime.c` now supplies an independently authored exported
`char **environ` backed by an owned null-terminated empty environment. This is
appropriate only to the explicitly fixed-configuration standalone tool; inherited
environment, arguments, program name, constructors and a full CRT are not claimed.
The HTTP gateway has not been changed. Native tests rename the export to avoid
overriding the host libc environment.

The separately built successor is
`.context/app-services-current/mdns-local-audit/build-runtime-v2/dns-record-probe`,
13520bytes, SHA-256
`2759b814c222f569de34a32c4ff6febd5f872f796fdcc3375d6d9434c2e6d898`.
Its unchanged11-import audit passes. `environ-audit.json` proves external defined
symbol `_environ` at0x302c points to mapped owned0x3064 containing a NULL terminator.
Six host-environment isolation checks and the1606 probe contracts pass. All first
failure artifacts remain separate. Runtime continuation and DNS behavior still
require root's newly provisioned bounded guest test.

## Completed corrected diagnostic: LocalOnly is insufficient

The second build exported `environ`, but its compiler used r9 for TTL60.
The original OS reserves r9 for the current thread: original `pthread_self`
returns r9 and `pthread_getspecific` subsequently faulted when r9 was60.
The maintained ARM build now reserves it with `-ffixed-r9` on every own C
translation unit. This corrects our program ABI; no original binary changed.

The runtime-v3 probe (SHA256
`49afb3180e8d39acb3c57bde2e4406ee28b35b5fcafed2e3f62acabc99199685`)
completes all calls. Registration succeeds, and the LocalOnly control receives
A127.0.0.1 for the exact hostname. Both interfaceAny queries return NoSuchRecord,
including the query made after registration. Weather uses interfaceAny, so this
is a negative routing result, not a crash or a successful Weather connection.
The end marker reports71 at guest instruction2,976,930,513; the retained snapshot
was taken earlier and does not establish a subsequently reaped process exit.

Next isolated route: append a domain-specific unicast resolver to the original
IPMonitor `Resolvers.plist`, which configd reads into its resolver configuration
without an active network interface, and serve bounded DNS on loopback port53.
Actual original app requests and display remain the acceptance gate.
