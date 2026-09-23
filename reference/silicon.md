# Silicon

Invariants. True on every RP2350 board, regardless of vendor or toolchain. Each
one presents as a different fault, which is why they are worth knowing in advance
rather than debugging.

## Errata and hardware traps

**Erratum RP2350-E9 — internal pull-downs are unusable.** An input pad sitting
between logic levels leaks up to ~120 µA, overwhelming the internal pull-down: the
pin latches near 2 V and reads HIGH forever. Wire buttons to GND with pull-**up**,
or fit an external pull-down of **8.2 kΩ or less**. The same leakage costs ~120 µA
*per floating input* — on a battery build that is the entire sleep budget.

**PWM slice aliasing is four-wide**, and the map depends on package. From
`hardware/pwm.h`: `gpio < 32` → `slice = (gpio >> 1) & 7`; `gpio >= 32` →
`slice = 8 + ((gpio >> 1) & 3)`. Pins on one slice share the frequency; pins 16
apart share the **channel**, so writing GP2 and GP18 hits the same duty register
and the last write wins for both. Presents as a power glitch. On QFN-60 only slices
0–7 are reachable and GP14/15 are the one unaliased pair; on QFN-80 nothing is
unaliased.

**`flash_do_cmd()` stops XIP.** It must run during startup with interrupts off and
the second core not yet running. Called from a live application it hangs the board.

**Flash writes stall both cores**, because code executes from the same QSPI part
via XIP. A commit inside a timing-critical loop looks like a scheduler bug.

**The analog pins are not 5 V tolerant** and carry a reverse diode to 3V3. Above
~3.6 V they damage the chip, and any voltage on them while the board is unpowered
back-powers the whole board through the pin.

**The ADC can fail wholesale**: `ADC_CS_ERR` on every conversion including the
internal temperature diode, on all channels and clock sources — an analog supply
fault with no software fix. Never depend on a temperature reading without proving
it works, and check the channel number against the package first.

**A hung core keeps drawing power and heating** until power is pulled. It is not
idle.

**Watch for default bus pins colliding with ADC pins** — a secondary I2C instance
commonly defaults onto the first two analog pins. Corruption that begins the
moment you call an analog read is pin conflict, not noise.

## Clocking and overclocking

**Two separate questions about flash. Do not conflate them.**

- *Survival*: above ~300 MHz the QSPI clock derives from `clk_sys`, so at 400 MHz
  flash runs ~200 MHz, far past rating. The clock-change code is itself in flash,
  so an XIP build **cannot survive raising its own clock**. Fix with `copy_to_ram`,
  or by configuring the **QMI divider from SRAM before** the PLL change and keeping
  QMI SCK inside a validated range — which preserves XIP for images too large for
  SRAM.
- *Speed*: the XIP cache serves a compact hot loop well. Relocating code to SRAM is
  **not** automatically faster — SRAM is banked and shared with the other core, DMA
  and stacks, so you may trade flash stalls for bus contention. Move one resource
  at a time and record its bank, size and competing users.

**Order: voltage, settle, verify, clock, verify.**

```c
vreg_set_voltage(v);
busy_wait_us_32(1000);                     // regulator settle
if (vreg_get_voltage() != v) fail();       // verify, don't assume
set_sys_clock_khz(khz, false);
sleep_ms(20);
if (clock_get_hz(clk_sys) != khz*1000) fail();
```

**Decouple `clk_peri` from `clk_sys`** — pin it to the USB PLL at 48 MHz, so serial
failure is never confused with core instability.

**The PLL grid is not continuous.** Precompute realizable points; probe around a
request and suggest the nearest achievable value rather than failing.

**Marginal clocks survive ~17 seconds.** Short soaks mislead badly. Scale soak time
with how far you are pushing.

**Above `VREG_VOLTAGE_MAX` needs `vreg_disable_voltage_limit()`.** The ladder is
sparse (1.30, 1.35, 1.40, 1.50, 1.60 — no 1.45). The LDO is ~42% efficient at
1.40 V, so most extra input power becomes in-package heat.

**Search order:** start at the authorized upper voltage, bracket the ceiling coarse
then fine on exact PLL points, and only then descend voltage at the winning clock.
Retry a failed point once after verified stock recovery.

**Classify the failure; it names the limiter.**

| symptom | usual cause |
|---|---|
| hang during a clock change | XIP, or a core running through the transition |
| USB disconnect | brownout / power, not logic |
| inter-core handshake fails, compute still correct | coordination, not datapath |
| wrong results | genuine timing failure — the rarest |

None of these is a throughput result. Overvoltage damages hardware; cooling does
not remove electrical risk. Everything above the part's rated clock is out of spec
and should be labelled so.

## Performance

**Peripheral access often dominates.** An APB register transaction costs roughly
5 cycles and is **identical on M33 and Hazard3**. If a routine is peripheral-bound,
changing architecture moves it a few percent and hand-tuning CPU code moves it
less. Count transactions before touching instructions.

**Cycles per operation should be flat across clock.** If it is, the clock is a pure
multiplier — compute the physical floor and treat anything beyond it as a broken
measurement, not a fast chip.

Patterns that pay:

- retain peripheral ownership across batches instead of repeating setup;
- **DMA: configure once, retrigger by writing the read address** — on RP2350 the
  saved transfer count reloads, so rewriting it each time is a wasted write;
- precompute invariant state and constant protocol fields;
- hoist mode dispatch out of hot loops into a function pointer;
- bound loops with a precomputed end, not a second counter;
- emit constants as immediates rather than loads;
- move rare validation, logging and error publication off the common path;
- batch accounting without weakening error detection;
- use DMA only when it removes more CPU/MMIO work than its setup and wait cost.

**Never call `putchar`/`putchar_raw` per byte.** `stdio_usb_out_chars()` takes a
mutex, runs `tud_task()` **and flushes on every call**. Batch with
`stdio_put_string(buf, len, false, false)` — one traversal, no explicit flush.

**Verify in the final linked disassembly**: instruction count, stack frame, spills,
calls, memory traffic. Reject byte-identical compiler variants without spending
device time. Keep architecture-specific implementations where measured behaviour
differs.

## Dual core

**Quiesce properly before any clock change.** A `volatile bool run = false` is not
enough — the worker only tests it at block boundaries and may still be executing.
Clear the flag, **wait for acknowledgement**, then `multicore_reset_core1()`.
Retuning `clk_sys` under a live core is a first-class cause of instability, and it
is easy to do by accident.

**Launch the second core last**, after the clock transition is verified and boot
output is flushed. The handshake is fragile exactly when the clock is unsettled or
the USB stack is busy; deferring it can be worth a full clock step.

**Never use `multicore_fifo_pop_blocking()` for a launch handshake** — if the other
core never comes up you hang forever with no diagnostic. Use a `volatile` alive
flag with a timeout and one retry.

Prefer message passing or bounded checkpoints over per-iteration shared polling.
Keep lossless events (results, faults) separate from coalescible telemetry. A
second core costs the first throughput, and costs power out of proportion to what
it returns.

## USB CDC

**A CDC write blocks up to ~500 ms when no host is draining.** Therefore **never
arm a watchdog before boot output** — fifty boot lines exceed any sane timeout and
reset the device mid-enumeration, permanently.

**The port only exists once the host opens it.** Earlier bytes are dropped, and a
`while (!Serial)`-style gate blocks startup until a monitor connects. Never gate
bring-up on the port being open. Allow an enumeration settle delay before flooding
it.

**CDC output is not UART0.** A USB-serial adapter on GP0/GP1 sees nothing from it.

**CRLF translation corrupts binary** — any `0x0A` becomes `0x0D 0x0A`. Disable per
driver: `stdio_set_translate_crlf(&stdio_usb, false)`.

**`stdio_flush()` after partial lines**, or output is lost when the device drops off
the bus.
