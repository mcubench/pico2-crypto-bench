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

