# Project update tweets

Each `.md` file is a single tweet-able milestone:
- **Tweet idea** — ready-to-post copy (or a thread hook).
- **What you're seeing** — the matching screen recording file and timestamp.
- **ELI5 technical explanation** — what actually happened and why it took work.
- **Why it matters** — why this moves the project forward.

The recordings and thumbnails live in `videos/` and `thumbnails/`.

| # | Update | Recording | Thumbnail |
|---|--------|-----------|-----------|
| 001 | [Genuine 2007 home screen](001_home_screen.md) | `browser_home_settings_safari_cpi1.mp4` | [thumb](thumbnails/browser_home_settings_safari_cpi1.jpg) |
| 002 | [Touch input opens real apps](002_touch_input.md) | `browser_home_settings_safari_cpi1.mp4` | [thumb](thumbnails/browser_home_settings_safari_cpi1.jpg) |
| 003 | [Home button returns from Safari](003_home_button.md) | `browser_home_settings_safari_cpi1.mp4` | [thumb](thumbnails/browser_home_settings_safari_cpi1.jpg) |
| 004 | [It runs in the browser](004_browser_port.md) | `browser_home_settings_safari_cpi1.mp4` | [thumb](thumbnails/browser_home_settings_safari_cpi1.jpg) |
| 005 | [CPI knob vs real speed](005_cpi_lesson.md) | `browser_cpi2_timing.mp4` | [thumb](thumbnails/browser_cpi2_timing.jpg) |
| 006 | [Interpreter speed pass](006_speed_pass.md) | *(to be recorded)* | *(placeholder)* |
| 007 | [Parallel subagents take on the hard parts](007_parallel_subagents.md) | *(in progress)* | *(placeholder)* |
| 008 | [First pixels / lock screen](008_first_pixels.md) | `browser_home_settings_safari_cpi1.mp4` | [thumb](thumbnails/browser_home_settings_safari_cpi1.jpg) |
| 009 | [M68 / iPhone1,1 machine target](009_m68_board.md) | *(architecture docs only)* | *(placeholder)* |
| 010 | [Reverse-engineering Z1 touch](010_z1_touch.md) | `browser_home_settings_safari_cpi1.mp4` | [thumb](thumbnails/browser_home_settings_safari_cpi1.jpg) |
| 011 | [Why USB is currently unmatched](011_usb_workaround.md) | *(kernel race docs only)* | *(placeholder)* |
| 012 | [Browser end-to-end test](012_browser_e2e.md) | `browser_home_settings_safari_cpi1.mp4` | [thumb](thumbnails/browser_home_settings_safari_cpi1.jpg) |
| 013 | [Timer4 one-shot down-counter fix](013_timer4_fix.md) | *(boot trace recording TBD)* | *(placeholder)* |
| 014 | [MBX 2D packet / app-close animation fix](014_mbx_2d.md) | `browser_home_settings_safari_cpi1.mp4` | [thumb](thumbnails/browser_home_settings_safari_cpi1.jpg) |
| 015 | [Activation without patching Apple binaries](015_activation.md) | `browser_home_settings_safari_cpi1.mp4` | [thumb](thumbnails/browser_home_settings_safari_cpi1.jpg) |
| 016 | [M68 NOR/NVRAM CHRP partition](016_nor_nvram.md) | *(cold boot recording TBD)* | *(placeholder)* |
| 017 | [Privileged tick batching speed gain](017_interpreter_tick_batching.md) | *(terminal benchmark TBD)* | *(placeholder)* |
| 018 | [Browser CI + GitHub Pages deployment](018_browser_ci.md) | `browser_home_settings_safari_cpi1.mp4` | [thumb](thumbnails/browser_home_settings_safari_cpi1.jpg) |

Not every milestone has a unique recording yet — several share the same browser end-to-end clip because the work was captured during that run. New recordings will be added for speed passes, SDL desktop, and the in-progress subagent work as they finish.
