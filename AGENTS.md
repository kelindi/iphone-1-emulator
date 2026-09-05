# Agent instructions

## Documentation and project updates

Whenever we hit a significant milestone, or whenever asked to summarize progress, create a new entry in the `tweets/` folder and keep `tweets/README.md` up to date.

Each entry must be one of:
- `tweets/NNN_short_slug.md` (one file per milestone)
- an update to `tweets/README.md`

Every milestone file must contain:
1. **Tweet idea** — concise, ready-to-post copy or thread hook.
2. **What you're seeing** — the matching screen recording file and timestamp/section.
3. **ELI5 technical explanation** — explain like the reader is learning: what we did, what hardware/software behavior made it hard, and how it was solved.
4. **Why it matters** — why this moves the project forward.

Screen recordings are required for visual milestones (home screen, app launches, browser UI, speed benchmarks, etc.). If a matching recording does not exist, generate one by running the emulator/web build, recording the screen, and placing it at `tweets/videos/<slug>.mp4` with a corresponding `tweets/thumbnails/<slug>.jpg`. Update `tweets/README.md` to link both.

When asked to "backfill" updates, scan the git history, prior session messages, and existing recordings/PR comments, then create missing milestone files for all major achievements and update the index.

Prefer reusing existing recordings if they already cover the milestone, but add a note if a dedicated recording would be clearer.
