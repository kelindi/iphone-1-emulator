# Agent instructions

> New agents should read `plans/README.md` first to understand the project, current status, and what has been implemented before writing any code.

## Documentation and project updates

Record significant implementation and verification updates in `plans/README.md` and the relevant engineering log. Keep generated screenshots, recordings, and temporary test output in `.context/`; do not commit generated media.

## Subagent mode

When spawning parallel child sessions for this repo, prefer `devin_mode: "swe"` if the API accepts it. If the API only allows `normal`/`fast`/`lite`/`ultra`/`fusion`, create them as `ultra` and immediately flip each session to **SWE-1.7 Lightning** in the Devin webapp UI before work begins.

## Project context

For a full project overview, architecture, implemented systems, workarounds, and current blockers, see `plans/README.md`.
