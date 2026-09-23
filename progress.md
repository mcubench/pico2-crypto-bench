# Progress

## 2026-09-23 — Add reproducible Pico 2 hardware workflow

- Step/experiment: `setup-wrappers`
- Change: Added repository-local doctor, dual-ISA build, safe picotool flash,
  bounded serial monitor, and end-to-end cycle wrappers; explicitly declared the
  Pico 2 4 MiB flash size and embedded the Git source identity in build options.
- Validation: `bash -n` passed for all shell wrappers; Python byte-compilation
  passed; `git diff --check` passed; `./tools/build arm` and
  `./tools/build riscv` both built `crypto_shootout` and `crypto_sweep` with
  warnings treated as errors. An initial RISC-V configure warning for an unused
  ARM-only option was fixed and the build repeated cleanly.
- Result: Both ISA toolchains produce UF2 images. The repository-local doctor
  finds SDK 2.3.1, CMake, Ninja, both compilers, picotool, Python, and pyserial;
  direct USB validation is deferred to the hardware cycle because sandboxed USB
  access is not available without escalation.
- Decision: The compile side is ready; commit this step, rebuild from the commit,
  then flash and validate each architecture on the connected board.
- Commit: `this commit (see Git history)`
- Artifacts/logs: Build outputs under ignored `build/`; pre-commit UF2 SHA-256:
  ARM shootout `6b9b4f61ab546fd0ce1e0b6fb3277ed72b55149f3cd1b36f5ee6cc76e44f1d44`,
  ARM sweep `18da2b848aaba96abffcc09b6cbec1f8cc0adbcb4334af1043ffcc06109f7c8e`,
  RISC-V shootout `b349a67ccac9818f89b244b42d40b65c05461d1771dba0aa00ef82f748940078`,
  RISC-V sweep `788cec894082c779bc764cdff1d79c30fec1c1dd6ed8762007e4219cb54de6d9`.

## 2026-09-23 — Validate ARM hardware cycle

- Step/experiment: `hardware-cycle-arm`
- Change: Rebuilt both architectures from clean commit `9b8db5b06d39`, flashed
  the ARM `crypto_shootout` UF2 with verification, and captured a bounded USB
  serial run through its completion marker.
- Validation: `./tools/cycle arm` passed. Both architecture builds were clean;
  picotool load and flash verification returned success; the serial monitor saw
  the source-matched banner, no failure marker, `done.`, and `MONITOR:PASS`.
- Result: Measured hardware identity was board ID `E0E14525E64AF6ED`, RP2350
  chip version 3, ROM version 4, ARM Cortex-M33 at 150 MHz. Every reported
  shootout operation completed, including ML-KEM, ML-DSA, TRNG, and RSA.
  Runtime reported VSYS as 9.897 V; this is not physically plausible and is
  consistent with the repository's documented unusable ADC, so it is not a
  valid supply measurement.
- Decision: ARM compile/upload/test is operational. Record the step, then flash
  and validate the RISC-V build using the same source commit and monitor gate.
- Commit: `this commit (see Git history)`
- Artifacts/logs: `logs/arm-shootout-20260923T171833Z.log` SHA-256
  `4352119c5073f3306ffbbe0ac7da7da66ece60f07de4c5e74bfa3f41a95ee587`;
  flashed UF2 SHA-256
  `3455352167cdc106105c84a39c66bcbd307c63afd31d9967dd79786f6b79894c`.

## 2026-09-23 — Validate RISC-V hardware cycle

- Step/experiment: `hardware-cycle-riscv`
- Change: Rebuilt both architectures from clean commit `a9cb729730e3`, flashed
  the Hazard3 RISC-V `crypto_shootout` UF2 with verification, captured its full
  bounded USB serial run, and repeated the repository-local environment check.
- Validation: `./tools/cycle riscv` passed. Both architecture builds were clean;
  picotool load and flash verification returned success; the serial monitor saw
  the source-matched RISC-V banner, no failure marker, `done.`, and
  `MONITOR:PASS`. A final `./tools/doctor` returned `DOCTOR:PASS`, including the
  live Pico USB serial device.
- Result: The same board ID `E0E14525E64AF6ED` ran the Hazard3 image at 150 MHz
  through all reported crypto operations. The repository can now compile both
  ISAs, upload either validated image, and enforce a finite hardware test.
- Decision: Setup is complete. Leave the last validated RISC-V shootout running;
  future platform-independent changes should use `./tools/cycle arm` and
  `./tools/cycle riscv` as required by `AGENTS.md`.
- Commit: `this commit (see Git history)`
- Artifacts/logs: `logs/riscv-shootout-20260923T172102Z.log` SHA-256
  `3632c3b36c3faaa2c55747fc575716c9a0867f581bb7f9cb91361369c6df9c30`;
  flashed UF2 SHA-256
  `1d349975689376489e2d8819b91eefb2775495ff90d30a66748f4e941383b56d`.

## 2026-09-23 — Preserve sweep configuration across reset

- Step/experiment: `resume-journal-config`
- Change: Fixed watchdog recovery to reload a matching valid flash journal,
  restore voltage/range/ladder/RXDELAY and exact results, and repopulate every
  invariant journal field before later saves. Added strict validation so legacy
  records with values such as `0-0 MHz` are refused instead of resuming at the
  voltage-table default. Bumped firmware to v2.6.3.
- Validation: `git diff --check`, `./tools/build arm`, and
  `./tools/build riscv` passed with warnings treated as errors; both shootout
  and sweep UF2 targets linked on each ISA.
- Result: The zero-overwrite path is removed at both causes: `g_prog` is restored
  after a matching watchdog reset and configuration fields are unconditionally
  reasserted before any subsequent suite can journal a record.
- Decision: Commit the source fix, then reproduce a power-cycle-style resume on
  hardware at the stock 1.10 V / 150 MHz operating point.
- Commit: `this commit (see Git history)`
- Artifacts/logs: Build outputs under ignored `build/`; hardware validation not
  yet performed for this step.

## 2026-09-23 — Prepare manual reinsertion test image

- Step/experiment: `manual-resume-image`
- Change: Corrected the declared flash capacity from 4 MiB to the 2048 KiB
  reported by this board's boot ROM, placing the sweep journal in the actual
  final sector rather than relying on address aliasing. Built and flashed the
  normal RISC-V sweep image without an automatic-reset test seam.
- Validation: `./tools/build arm` and `./tools/build riscv` passed with warnings
  treated as errors; `./tools/flash riscv sweep` returned `FLASH:PASS`.
- Result: The connected RP2350 A4/QFN60 board, ID `E0E14525E64AF6ED`, now has
  the v2.6.3 RISC-V manual-test firmware. A controlled internal-reset experiment
  reached the first durable journal save; physical interruption/reinsertion is
  intentionally left to the user.
- Decision: Use the interactive sweep at the desired 1.60 V settings, remove
  and reinsert USB after a result has been journaled, then confirm the recovery
  banner retains the selected voltage and frequency range before resuming.
- Commit: `this commit (see Git history)`
- Artifacts/logs: ARM sweep UF2 SHA-256
  `5a62777a83a4acbc5810985fb30e802004f8c80cf48829b8e555930a48f8bdb6`;
  RISC-V sweep UF2 SHA-256
  `1d687d094b1047f2041a262c2a0de8f7cb39bb5165ec712b4d20841151312241`.

## 2026-09-23 — Install source-matched manual test firmware

- Step/experiment: `manual-resume-image-clean`
- Change: Rebuilt both architectures from committed source `1edd0410f83b` and
  installed its normal RISC-V sweep firmware on the connected Pico 2.
- Validation: `./tools/build arm`, `./tools/build riscv`, and
  `./tools/flash riscv sweep` all passed; the flash wrapper reported embedded
  source ID `1edd0410f83b`.
- Result: The board is ready for a user-controlled interruption and physical
  reinsertion test, with no automatic reboot behavior in the image.
- Decision: Await the manual test outcome; the expected recovery settings are
  the exact voltage, start/max frequencies, ladder, and RXDELAY mode selected
  before interruption.
- Commit: `this commit (see Git history)`
- Artifacts/logs: ARM sweep UF2 SHA-256
  `32261cd513fa36a2c6ce178029772e810a01b3a3187909b141d02bc41a5fe3bb`;
  RISC-V sweep UF2 SHA-256
  `354d2ecc318efd9ca936f1bcc8053eff01ae611597b4e6faa7fc02dd22d9ee1a`.

## 2026-09-23 — Retry interrupted suite on flash resume

- Step/experiment: `resume-current-suite`
- Change: Changed flash-only recovery to restart the journal's recorded suite
  from its first frequency rather than treating a pre-attempt record as proof
  that the suite completed. Matching watchdog recovery still advances past the
  positively identified hung suite. Updated the recovery prompt and bumped the
  firmware to v2.6.4.
- Validation: `git diff --check`, `./tools/build arm`, and
  `./tools/build riscv` passed with warnings treated as errors.
- Result: In the supplied log, suites 5–8 had in fact run while USB serial
  output was absent, as shown by saved results of 558, 564, 460, and 460 MHz.
  The new policy nevertheless ensures that a flash-only record for suite 8
  resumes at suite 8, not suite 9.
- Decision: Commit the semantic fix, rebuild from clean source, and prepare the
  RISC-V image for a manual interruption/reinsertion check.
- Commit: `this commit (see Git history)`
- Artifacts/logs: user-supplied serial log at attachment path; build outputs
  under ignored `build/`.

## 2026-09-23 — Advance past hung suite after reinsertion

- Step/experiment: `skip-recorded-hang`
- Change: Updated flash recovery to end the suite recorded at the hang and
  continue with the following suite, preventing reinsertion from repeating the
  same unstable frequency. Updated prompt text and bumped firmware to v2.6.5.
- Validation: `git diff --check`, `./tools/build arm`, and
  `./tools/build riscv` passed with warnings treated as errors.
- Result: Both matching-watchdog and flash-journal recovery now advance exactly
  one suite past the recorded hang.
- Decision: Prepare source-matched ARM and RISC-V UF2 files for the user's
  manual hardware check; do not flash the attached device.
- Commit: `this commit (see Git history)`
- Artifacts/logs: build outputs under ignored `build/`; hardware validation
  intentionally left to the user.
