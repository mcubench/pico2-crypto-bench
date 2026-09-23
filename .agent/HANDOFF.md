# Handoff

- Objective: establish a repeatable ARM/RISC-V build, flash, and finite serial
  test loop for the connected Raspberry Pi Pico 2.
- Current state: setup is complete. Repository wrappers and explicit 4 MiB flash
  declaration are implemented. ARM and RISC-V both build `crypto_shootout` and
  `crypto_sweep` cleanly with warnings as errors. Both `crypto_shootout` images
  were independently flashed, verified, and ran through `done.` with no failure
  marker. The RISC-V image is currently on the device.
- Hardware: measured by host inspection as USB VID:PID `2e8a:0009`, RP2350 CDC
  serial `/dev/serial/by-id/usb-Raspberry_Pi_Pico_E0E14525E64AF6ED-if00`.
  Package/revision were not physically inspected; board identity is supplied by
  the task and build target as Raspberry Pi Pico 2.
- Safety: no erase, OTP, security, partition, or machine-configuration command
  has been run.
- Next action: none for setup. For future source changes, build both ISAs and run
  the requested hardware cycle; use `tools/flash <arch> sweep` only when an
  interactive sweep is intentionally requested.
- Durable logs: `logs/arm-shootout-20260923T171833Z.log` SHA-256
  `4352119c5073f3306ffbbe0ac7da7da66ece60f07de4c5e74bfa3f41a95ee587`;
  `logs/riscv-shootout-20260923T172102Z.log` SHA-256
  `3632c3b36c3faaa2c55747fc575716c9a0867f581bb7f9cb91361369c6df9c30`.
