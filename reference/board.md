# Board

Premises. Verify once per piece of hardware; everything downstream inherits them.
**A wrong board definition compiles cleanly and then drives the wrong pins** —
that failure looks like broken hardware, never like a config error.

## Identify the part

**Do not infer the package from `SYSINFO_PACKAGE_SEL`.** The SDK headers give no
meaning for the bit and the SDK never uses it to distinguish A from B. Use vendor
documentation, the schematic, or the physical pin count. A wrong guess here
silently invalidates every pin, ADC-channel and PWM-slice conclusion that follows,
and those conclusions will still look internally consistent.

| | RP2350A (QFN-60) | RP2350B / RP2354B (QFN-80) |
|---|---|---|
| GPIO | 30 (GP0–29) | 48 (GP0–47) |
| ADC channels | 5, base pin GP26 | 9, base pin GP40 |
| analog inputs | GP26–29 | GP40–47 |
| temperature sensor | channel 4 | channel 8 |
| not 5 V tolerant | GP26–29 | GP40–47 |
| PWM slices reachable | 0–7 | all 12 |

Code written against "the ADC pins" or "channel 4 is temperature" reads the wrong
thing on the other package. **RP2354** is an RP2350B with stacked flash in
package: fixed size, no external part to inspect.

Third-party boards also ship in **incompatible revisions under one name** — pin
function, power input and ADC reference move between them. Establish the revision,
then make it a compile-time constant the build refuses to proceed without.

Chip revision, board, flash part, cooling and power integrity are all part of the
test identity. **One sample is not a device rating.**

## Declare the real flash size

A `pico2`-derived build claims 4 MiB. On a smaller die, **addresses past the end
alias silently onto the start of flash** rather than faulting: a write near the
top lands on the vector table, and the failure appears much later as firmware that
no longer boots.

```cmake
set(PICO_FLASH_SIZE_BYTES 2097152)   # before pico_sdk_init()
```

Rust: `FLASH : ORIGIN = 0x10000000, LENGTH = 2048K` in `memory.x`.

**You may have to measure it.** `FLASH_DEVINFO` is commonly unprogrammed on
third-party boards, so `picotool` cannot size the chip and `picotool erase -a`
fails with "cannot determine the flash size". Write a pattern at the suspected top
and read it back at 0 — if it appears, you have found the real size. Reading the
JEDEC ID also works where the flash is an external part.

Keep persistent data in the top sectors of the *real* region and derive offsets
from the declared size. Erase granularity is 4 KiB.

## Toolchains

**pico-sdk, C, both ISAs.** RISC-V needs assembly: Ubuntu's
`riscv64-unknown-elf-gcc` ships **no C library**. Install
`picolibc-riscv64-unknown-elf`, symlink the tools under `riscv32-unknown-elf-*`
(the SDK looks for those), then:

```bash
cmake -B build-riscv -DPICO_PLATFORM=rp2350-riscv \
  -DPICO_TOOLCHAIN_PATH=/opt/rv32bin -DPICO_CLIB=picolibc \
  -DPICOLIBC_ROOT=/usr/lib/picolibc/riscv64-unknown-elf -DNO_CXX_RUNTIME=1
```

`PICOLIBC_ROOT` adds `-nostartfiles` (the SDK has its own crt0).
`NO_CXX_RUNTIME=1` drops the SDK's `new`/`delete` shim, which needs libstdc++
headers this toolchain lacks. A newlib-only build may also need an empty
`nosys.specs` to exist.

**Rust** (`rp235x-hal`) builds on stable for `thumbv8m.main-none-eabihf`. The boot
ROM scans the first 4 KiB for an `IMAGE_DEF` block — without one the image never
starts — and the UF2 family must match the image type (`rp2350-arm-s` or
`rp2350-riscv`) or the ROM rejects it.

**Arduino**: PlatformIO + arduino-pico (earlephilhower core). Plain
`framework = arduino` selects the Arduino **Mbed** core, which does not support
RP2350 at all, and the failure reads as a broken toolchain rather than a missing
ini line. Hazard3 is not reachable this way; RISC-V means the SDK or Rust.

Build both ISAs with warnings as errors when source is shared. Never assume one
code shape suits both — register allocation, branch cost and code size differ, and
measured divergence between them is normal.

## Flashing and getting back

**BOOTSEL** is reached by holding the button while plugging in. On boards whose
button sits on `QSPI_SS`, the boot ROM sees it before any firmware runs, so this
works whatever is in flash — a hung image, one that never brings USB up, one built
for the wrong architecture. Boards without a reset button need a full unplug.

**Reflash a running application without the button.** pico-sdk USB stdio adds a
vendor reset interface, which is what makes `picotool load -f -x image.uf2` work.
Opening the CDC port at 1200 baud does the same. Writing your own TinyUSB
descriptors removes the interface — plan a route back before you do. A Rust
application gets this only if you wire `hal::reboot` to a command yourself. None
of it helps once the firmware has hung; use the button.

**RP2350 UF2 files carry a different family ID from RP2040** and are rejected
rather than run.

```bash
picotool info -a                                   # attached device
picotool info -a image.uf2                         # what a file will do first
picotool save -r 0x10000000 0x10200000 b.bin -t bin  # back up before risk
picotool erase -r 0x10000000 0x10200000            # explicit range
picotool load -x image.uf2                         # write and run
```

`picotool` decides file type by extension; a Cargo ELF has none, so pass `-t elf`
or copy it to `name.elf`.

## Never write OTP

Every bit is one-way: secure boot, boot key hashes, the declared flash size, and
whether the BOOTSEL USB interfaces exist at all. Stay out of the entire
`picotool otp` subcommand, and **never program `FLASH_DEVINFO`** "so picotool knows
the size" — the value people reach for is often wrong and the fuse cannot be
cleared.

The boot ROM is mask ROM, so the part **cannot be bricked by software**. OTP is the
one exception, which is why the rule is absolute. Reading OTP is harmless.

## Board-level traps

**Mixed-ISA images leave state behind.** A firmware that switches the second core
to RISC-V at runtime leaves that setting in place, and the boot ROM does not undo
it — the next plain Arm image hangs before USB comes up, looking exactly like a bug
in whatever you just flashed. From BOOTSEL, `picotool reboot -a -c arm` clears it.
Plain Arm and plain RISC-V images alternate freely; only mixed ones set the trap.

**Some boards boot dark intermittently** — roughly one flash in nine coming up
dead, independent of the image. Before debugging your code: BOOTSEL, erase the
range, load *the same image* again. Only if the second attempt also fails is the
problem yours.

**A board that looks dead may be held in reset** — a pulled-low enable pin kills
the rail with USB still attached. The 3V3 pin is an output; never back-feed it.

**Suspect the host.** Enumeration flakiness can follow a board swap and need a host
reboot. Runtime and BOOTSEL are different USB identities; match vendor ID `2e8a`
rather than one product ID. Devices re-enumerate during flash — use bounded
discovery and verify the vendor before opening.

**No debug pins is common** on token and dongle form factors: no UART, no SWD, USB
CDC is the only console. Plan telemetry accordingly.
