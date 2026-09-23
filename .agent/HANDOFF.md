# Handoff

- Objective: establish a repeatable ARM/RISC-V build, flash, and finite serial
  test loop for the connected Raspberry Pi Pico 2.
- Current state: repository wrappers and explicit 4 MiB flash declaration are
  implemented. ARM and RISC-V both build `crypto_shootout` and `crypto_sweep`
  cleanly with warnings as errors.
- Hardware: measured by host inspection as USB VID:PID `2e8a:0009`, RP2350 CDC
  serial `/dev/serial/by-id/usb-Raspberry_Pi_Pico_E0E14525E64AF6ED-if00`.
  Package/revision were not physically inspected; board identity is supplied by
  the task and build target as Raspberry Pi Pico 2.
- Safety: no erase, OTP, security, partition, or machine-configuration command
  has been run.
- Next action: commit the setup step, then run `./tools/cycle arm` with hardware
  access. Diagnose any finite-monitor or firmware failure, then validate RISC-V.
- Durable logs: none yet; build outputs are intentionally ignored.

