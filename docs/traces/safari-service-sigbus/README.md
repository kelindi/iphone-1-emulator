# Safari current-renderer retest and actual daemon exit status

Current production browser assets restore the same original quiet-service native
phone used for093. Settings and Contacts render; Safari loads HOME-1 and NEXT-1,
submits the actual keyboard token `go`, and renders ECHO-1. A following navigation
fails with lost network connection; reload reports server unavailable. Bookmark,
autosave/reload and default distribution are not accepted. The browser workflow
ends at that failure; its report marks the unfinished bookmark requirement.

The complete read-only process list shows no local-service daemon. The requested
name in that audit was truncated incorrectly (`iphone1-local-h` rather than
historical `iphone1-local-ht`), but neither spelling nor any iphone1 process is
present in the complete list. Absence alone does not identify an exit cause.

A separate read-only walk of original launchd's complete18-job tree identifies
our exact label once, PID0, LastExitStatus0x0000000a: Darwin SIGBUS10. Original
launchd job_reap stores the raw wait status at job+0xb4; job_export2 reads that
field as LastExitStatus. The inspected original guest code matches pinned
original bytes, and the loaded CPU is unchanged. See launchd-status.json.

This establishes a memory-access signal, not the faulting instruction or an
emulator root cause. The retained crash report describes crashdump itself at
0x3188d51c, not our service; do not attribute that address to the daemon.

The current source-stride renderer fix permits the form response in this run,
but does not establish graphics completion: serial still contains3D restart and
pending2D hang messages. No compiler changes are part of this experiment.

The next service candidate uses original launchd's supported KeepAlive behavior
and retains service logs in an own file. Original launchd has a10-guest-second
minimum-run throttle; configurable ThrottleInterval was not established and is
not added. Recovery is not a fix for SIGBUS, and new app acceptance must report
any observed restarts. Existing saved phones and product assets remain unchanged.
