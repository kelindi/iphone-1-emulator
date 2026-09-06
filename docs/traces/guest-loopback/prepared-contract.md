# One ordinary guest loopback HTTP exchange: prepared contract

Predecessor086 proves our own ARMv6 program can execute through original launchd/dyld/libSystem. This successor is prepared in a new private directory; it has **not** been installed or run in the guest. No production, Apple binary, wireless controller or kernel network state changes are included.

## Program and admission

`loopback.c` creates one TCP listener and one client in the same process, on127.0.0.1:18081, then accepts its own connection. No fork/thread/CRT initialization assumption is added. Eighteen eager imports are resolved against actual unchanged OS1 libSystem; `binary-audit.json` pins each original export and the classic ARMv6 format. Those socket/errno imports have not yet been exercised inside this guest; an actual failure remains a qualification result.

Read-only `SIOCGIFCONF` first records all returned interface address bytes (2KiB cap), validates variable record bounds and asks `SIOCGIFFLAGS` for each entry. It waits only when there is no up127.0.0.1 entry: at most30 samples/29 one-second usleep calls, with initial and final raw routing-table sysctl receipts. The original process starts early, so configd is allowed ordinary bounded time to initialize. API failure, malformed/truncated inventory and exhausted readiness remain explicit failures, not proof that networking can never work. This version does not configure lo0 or modify routes.

Historical public Apple XNU headers retained under `primary-sources/` define sockaddr_in16B, pollfd8B, Darwin32 ifconf8B and ifreq32B. The documented IOWR size/direction/group/number gives SIOCGIFCONF0xc0086924 and SIOCGIFFLAGS0xc0206911; the ARM target checks pointer/layout sizes. AF_INET2, SOCK_STREAM1, CTL_NET4/PF_ROUTE17/NET_RT_DUMP1, F_GETFL3/F_SETFL4/O_NONBLOCK4, SO_ERROR0x1007/SO_NOSIGPIPE0x1022 and Darwin errno values are source-derived. These are2007 public headers, not a claim of byte-identical complete OS1 SDK. Actual guest behavior is the next gate. No direct syscall number is used.

## Genuine bounded exchange

After real interface readiness, the server enables nonblocking mode and no-SIGPIPE, binds and verifies getsockname's exact address, then listens. The client connects nonblocking; EINPROGRESS must resolve through poll and a zero SO_ERROR. The accepted peer must be IPv4 loopback. Real socket writes and reads exchange an exact54-byte HTTP/1.0 request and a complete response whose Content-Length29 matches the independently counted body `IPHONE1_GUEST_LOOPBACK_V1_OK\n`. Actual request and response bytes are logged after reads, not inserted by the emulator. Client EOF is required; extra or wrong bytes reject. Both directions handle partial I/O, EINTR and EAGAIN with at most64 operations, each readiness wait capped at20×100ms. Every owned descriptor is closed before success; close errors fail. Log writes are also bounded.

The finite guest retries do not extend the outer boot cap. At most one new clean boot,10B instructions/900 seconds, no automatic retry. The known original168MiB source is copied into a new directory and gets only our executable and own launchd plist; existing5,178 forks/catalog records must remain exact. Current `image-preflight.json` admits four new records without moving leaf first keys. No old image or profile is modified. The first helper-only boot is not repeated.

## Observer, stop and acceptance

`prepare_probe.py` derives own entry/text/diagnostic symbol addresses from the exact own Mach-O and builds an observation-only copy of the current native CLI. At entry it compares all own text bytes and ARM user mode, then binds the real TTBR. It observes original socket/API entries plus the own diagnostic calls carrying their actual return values and read buffers. All hooks return false. Original `_exit(0)`, the process's real terminal write and actual UART terminal line must precede a normal outside-run stop; this proves the exit request, not process reaping. An observed own failure exits promptly at the next normal chunk boundary. Unknown faults and instruction/wall limits are retained failures.

Artifact acceptance requires ordered bind/connect/accept success, correct real socket names, exact actual request/response buffers, original read/write calls, original exit0 and the UART terminal line, with all source/image pins unchanged. It must report the actual initial/final interface and route bytes, genuine error values and any readiness waits. Own diagnostic values are source-instrumented observations, not an independent measurement of every syscall-return instruction. The native API-entry witnesses provide an additional process-bound check.

## Focused preflight evidence

`test.log`:1,180 sanitizer assertions across13 mocked workflows. Positive short-I/O/EINTR/EAGAIN, delayed readiness, missing readiness bound, socket/bind/connect errors, truncated interface records, route error, poll timeout, stalled reads, wrong response bytes, extra trailing bytes and write failure are covered. These are host mocks, not guest networking. The first test correctly caught an incorrect hand-counted HTTP Content-Length; two failed guesses remain in logs, corrected to the measured29 and now independently checked against the actual body. `binary-audit.json`, `build.log` and `image-preflight.json` pass. New program is13,524B with18 genuine-library eager relocations and no newer metadata load commands.

No Safari, Weather parser, external data, actual HTTP-over-Wi-Fi or production feature is claimed. A passed own TCP exchange would justify the next bounded original Safari localhost test, with the service clearly labeled as a demo where exposed to users.
