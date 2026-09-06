# Our own program runs inside the original iPhone OS

## Tweet idea

The real2007 iPhone OS just launched a tiny program we wrote. Its original system library returned a process ID and printed our message. Every existing Apple file stayed byte-identical. This gives us a concrete starting point for local services inside the phone.

## What you're seeing

This is a nonvisual execution milestone: [the actual guest console](../docs/traces/guest-hello/run-1/stdout.log), sections `HELLO entry` through `HELLO original_exit_entry`, and [the original UART output](../docs/traces/guest-hello/run-1/serial.log). The message is `IPHONE1_OWN_PROGRAM_V1 pid=0000000021`. No screen recording or browser feature is claimed. The console's chunk prefix is not the message's instruction timestamp; the [audited call witnesses](../docs/traces/guest-hello/run-1/independent-audit.json) record its actual execution.

## ELI5 technical explanation

Writing a little program was easy; making a2007 operating system load a modern-built file required the right old ARM format. We used historical linker output and three real library functions, then installed only our program and its startup description on a disposable clean disk. Original launchd started it, original dyld connected its imports, and the real system calls returned the expected values. A separate audit verifies all5,178 existing file forks stayed exact.

The program requested exit successfully; this test stops at original exit entry and does not prove process cleanup. The [full evidence](../docs/traces/guest-hello/README.md) preserves setup failures, source/import/build checks, exact file hashes and the single bounded run. Production files and Apple executables are unchanged.

## Why it matters

We can now test an ordinary server inside the guest without replacing Apple's apps. The next step is a real local HTTP exchange, followed by original Safari or app acceptance. Loopback networking, forecast delivery and general online functionality remain unproven.
