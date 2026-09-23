# Session continuity

For substantial work, keep `.agent/HANDOFF.md` current enough that another
session can continue without repeating hardware experiments.

Before editing:

1. Read `AGENTS.md`, `SKILL.md`, its routed references, `progress.md`, and
   `.agent/HANDOFF.md`.
2. Inspect `git status` and preserve all pre-existing changes.
3. Run `./tools/doctor` before relying on the hardware path.

During work, record the current objective, validated state, unresolved failures,
artifact/log paths, and exact next action in `.agent/HANDOFF.md`. At each logical
step, append to `progress.md`, commit the step, then update the handoff for the
next step. Do not turn transient build outputs under `build/` into continuity
state; preserve durable evidence under `logs/` or `artifacts/` when needed.

