# Own runtime and DNS routing diagnosis

The corrected runtime-v3 original-OS diagnostic completes: LocalOnly registration succeeds and its LocalOnly query sees the exact A record, while both interfaceAny queries return NoSuchRecord. Weather uses interfaceAny. This route therefore does not connect Weather or Stocks.

The first own program omitted the executable `environ` symbol required by the original allocator. Its successor then reused r9, which this OS reserves for its thread pointer. The maintained builder now includes the owned standalone environment for default builds and reserves r9 on every own C translation unit. These are own-program corrections, not original binary patches. They do not retroactively identify the fault in the earlier Safari SIGBUS report.

The next configuration route is independently bound to the original configd: append an owned unicast supplemental resolver to IPMonitor's existing `Resolvers.plist`, preserving the old mdns dictionary. The retained JSON identifies the original instruction and resource hashes. Actual routing and app display require the next isolated guest test.
