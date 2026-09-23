# Pico 2 development instructions

The target is a Raspberry Pi Pico 2 with an RP2350 connected by its USB port.

Supported CPU architectures:

- ARM Cortex-M33: `rp2350-arm-s`
- Hazard3 RISC-V: `rp2350-riscv`

Use the repository wrappers; do not invoke compiler binaries directly.

- Build ARM: `./tools/build arm`
- Build RISC-V: `./tools/build riscv`
- Build both, flash, and capture logs: `./tools/cycle arm` or `./tools/cycle riscv`
- Capture finite serial output: `./tools/monitor --seconds 8`
- Diagnose the host setup: `./tools/doctor`

After changing platform-independent source:

1. Build ARM and RISC-V.
2. Fix all errors and warnings.
3. Flash the requested architecture.
4. Inspect serial output.   
5. Treat `TEST:FAIL`, `FAULT`, a timeout, or any nonzero command status as failure.
6. Diagnose, edit, and repeat until the hardware test passes.

Runtime output uses `TEST:PASS`, `TEST:FAIL`, and `TEST:SUMMARY` for known-answer tests; `BENCHMARK:PASS` for measured Bitcoin double-SHA-256 throughput; `MINING:START`, `MINING:PROGRESS`, and `SHARE:FOUND` for mining work; and `FAULT` for runtime failures.

Safety constraints:

- Never run `picotool otp`, `picotool erase`, or commands that alter boot security, OTP, flash partition tables, or machine configuration.
- Do not use `sudo` from an autonomous coding cycle.
- Do not modify files outside this repository.
- Do not flash unless both architectures currently build, unless explicitly investigating an architecture-specific failure.

## Incremental commits and progress log

Work in small, logical steps. A logical step is one coherent change that can be
validated independently.

At the end of every logical step:

1. Validate the step with the relevant build, test, hardware cycle, or inspection.
2. Append a concise record to the repository-root `progress.md`; never rewrite or
   delete earlier records.
3. Create a new Git commit containing the step and its `progress.md` record. Do
   not amend or reuse the previous step's commit.
4. Commit only files that belong to that step; preserve unrelated pre-existing
   changes.

Each `progress.md` record should be structured as:

```markdown
## YYYY-MM-DD — <short step title>

- Step/experiment: `<identifier if applicable>`
- Change: <what changed and why>
- Validation: <commands/tests/hardware checks run and whether they passed>
- Result: <key measured or observed outcome; include failures or limitations>
- Decision: <what this establishes and the next action, if any>
- Commit: `<commit SHA>`
- Artifacts/logs: `<paths, hashes, or none>`
```

Include exact measurements, important runtime output, hashes, or archived log
paths when they materially explain how the step worked. State explicitly when a
metric was not measured or a validation was intentionally skipped.

## Session Continuity

This repository may be worked on by multiple Claude/Codex sessions/accounts.

For any substantial task, read and follow:
`.agent/SESSION_CONTINUITY.md`

Maintain `.agent/HANDOFF.md` as described there so another Claude/Codex
session can safely continue interrupted work.

Never discard existing uncommitted changes merely because they were
created by another session.