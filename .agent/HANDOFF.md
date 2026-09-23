# Handoff

- Objective: fix sweep resume after a hang followed by physical reinsertion.
- Current state: source fix implemented as v2.6.4. It reloads matching journal
  configuration on watchdog recovery, reasserts invariant fields before later
  saves, rejects invalid legacy records, and reruns the recorded suite on a
  flash-only recovery instead of skipping an attempt that may not have
  completed. ARM and RISC-V builds pass. The board still has the prior v2.6.3
  RISC-V image until v2.6.4 is flashed.
- Hardware: measured by host inspection as USB VID:PID `2e8a:0009`, RP2350 CDC
  serial `/dev/serial/by-id/usb-Raspberry_Pi_Pico_E0E14525E64AF6ED-if00`.
  Boot-ROM inspection reports RP2350 revision A4, QFN60, and 2048 KiB flash;
  the build now declares that measured capacity explicitly.
- Safety: no erase, OTP, security, partition, or machine-configuration command
  has been run.
- Next action: flash v2.6.4 RISC-V, run the interactive sweep, physically
  interrupt during a suite, reinsert, and confirm that flash recovery reruns
  that recorded suite before continuing.
- Durable logs: `logs/arm-shootout-20260923T171833Z.log` SHA-256
  `4352119c5073f3306ffbbe0ac7da7da66ece60f07de4c5e74bfa3f41a95ee587`;
  `logs/riscv-shootout-20260923T172102Z.log` SHA-256
  `3632c3b36c3faaa2c55747fc575716c9a0867f581bb7f9cb91361369c6df9c30`.
