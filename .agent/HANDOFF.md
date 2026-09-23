# Handoff

- Objective: fix sweep resume after a hang followed by physical reinsertion.
- Current state: source fix implemented as v2.6.6. It reloads matching journal
  configuration on watchdog recovery, reasserts invariant fields before later
  saves, rejects invalid legacy records, and advances past the suite recorded
  as hung after either reset path. Dual-safe sweep steps now require core 1 to
  finish useful work and aggregate gain above 1.05x. ARM and RISC-V builds pass.
  The board has not been flashed with v2.6.6; binaries are prepared for testing.
- Hardware: measured by host inspection as USB VID:PID `2e8a:0009`, RP2350 CDC
  serial `/dev/serial/by-id/usb-Raspberry_Pi_Pico_E0E14525E64AF6ED-if00`.
  Boot-ROM inspection reports RP2350 revision A4, QFN60, and 2048 KiB flash;
  the build now declares that measured capacity explicitly.
- Safety: no erase, OTP, security, partition, or machine-configuration command
  has been run.
- Next action: user hardware-checks v2.6.6, including a dual-core sweep step and
  the new liveness/scaling failure path; Codex must not flash unless requested.
- Prepared artifacts: v2.6.5 `crypto_shootout.uf2` exists under both
  `build/arm/` and `build/riscv/`; neither shootout image was flashed.
- Durable logs: `logs/arm-shootout-20260923T171833Z.log` SHA-256
  `4352119c5073f3306ffbbe0ac7da7da66ece60f07de4c5e74bfa3f41a95ee587`;
  `logs/riscv-shootout-20260923T172102Z.log` SHA-256
  `3632c3b36c3faaa2c55747fc575716c9a0867f581bb7f9cb91361369c6df9c30`.
