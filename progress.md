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
