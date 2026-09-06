# Candidate clock lifecycle — partial pass, export timeout retained

The maintained browser clock test ran against the private RTC candidate. Its only initial adaptation was to invoke hidden Pause and Export diagnostic buttons through DOM click, because the current minimalist UI hides those controls. No production script or clock assertion changed.

Nine checks pass: generated execution, foreground time, Pause acknowledgement and frozen time, Resume acknowledgement and rate, actual background-tab freezing, browser lifecycle freezing, visibility resume without catch-up, and manual Pause preservation across background/frozen states. The three measured guest/wall ratios are0.9983,1.0005 and1.0062. These short regression checks do not establish the product's stopwatch or input-latency acceptance targets.

The subsequent Export download did not arrive within the unchanged90-second bound. This stopped before paused Import/restore and final generated execution. The original harness then closed Chrome and deleted its disposable profile. It did not capture the app's export status, so the underlying cause is unknown; the adapted DOM click also lacks the original Playwright enabled/visibility wait. It is not attributed to RTC behavior or accepted as a successful full test. A later read-only attempt to inspect the profile found it already removed and changed no browser state.

The separately recorded two-edit/manual-time/autosave workflow passes and is not invalidated by this different incomplete test. Its live exports likewise do not retroactively satisfy this paused import clock check. [Actual report](report.json), [executed harness](clock-lifecycle.mjs) and [source adaptation pins](clock-harness-pins.json) preserve the boundary. No automatic guest retry occurred.

A separately instrumented successor now passes all12 original clock assertions after restoring the hidden-control enabled wait. It captures Export disabled during a real visibility-triggered save, then a complete download5.326seconds later. [Tiny negative, exact control-state receipts and successful successor](instrumented/README.md). This original nine-check run remains failed.
