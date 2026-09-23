# RP2350 Cryptographic Benchmark Suite

Benchmarks classical and post-quantum cryptography on the Raspberry Pi RP2350 —
on **both** of its CPU architectures, across a **19× clock range**, with
**byte-exact stability validation** at every step.

> [!WARNING]
> This project is experimental with source code mostly created by Opus 5 model. 
> However, the crypto code is taken from existing cryptographic libraries, the results were 
> measured on a real device and make "sense" when compared together and directionally. 
> But use at your own risk and re-run selected library directly from its original repository.

The RP2350 is unusual: the same die carries two Arm Cortex-M33 cores and two
RISC-V Hazard3 cores, and you choose which pair boots. That makes it a rare
chance to compare two instruction sets on identical silicon, identical memory,
identical peripherals, and an identical compiler version. This project does
that, and then keeps going - into overclocking, undervolting, dual-core
scaling, flash timing, and post-quantum schemes.



---

## Contents

- [What it measures](#what-it-measures)
- [Two programs](#two-programs)
- [Quick start](#quick-start)
- [Building](#building)
- [Build options](#build-options)
- [Libraries under test](#libraries-under-test)
- [How stability is judged](#how-stability-is-judged)
- [Selected findings](#selected-findings)
- [Known issues](#known-issues)
- [Caveats](#caveats)
- [Licensing](#licensing)

---

## What it measures

| Category | Primitives |
|---|---|
| Hashing | SHA-256 (software and the on-chip accelerator), BLAKE2b |
| Symmetric | AES-128-CBC |
| Classical PK | ECDSA P-256 (four implementations), ECDH P-256, ECDSA P-384, X25519, Ed25519, RSA-2048 |
| Post-quantum | ML-KEM-512/768/1024 (FIPS 203), ML-DSA-44/65/87 (FIPS 204) |
| Entropy | On-chip TRNG raw throughput, `pico_rand` PRNG, Mbed TLS CTR_DRBG |

Reported per operation: **cycles**, **operations per second**, **cost relative
to the baseline clock**, **XIP cache hit rate**, and **single- versus dual-core
throughput**.

---

## Two programs

### `crypto_shootout`

A single-pass comparison at one clock. Answers *which library should I use?* —
each operation is run through every implementation that provides it, with a
relative factor against the fastest.

### `crypto_sweep`

Nine independent suites, each walking a frequency ladder from 18 MHz to 800 MHz
while validating correctness at every step. Answers *how far can I push this
part, and what breaks first?*

Suites: core (ALU + SRAM), Mbed TLS, p256-m, micro-ecc, Monocypher,
SHA-256 hardware, ML-KEM, ML-DSA, TRNG.

Interactive prompts choose core voltage (0.85–1.60 V), the frequency ladder,
the flash RXDELAY strategy, and the start and ceiling clocks.

---

## Quick start

1. Hold **BOOTSEL**, plug the board in, drag a `.uf2` onto the `RP2350` drive. (start with `shootout_arm_v2.5.1.uf2`).
2. Open the USB serial port at any baud rate. Output is mirrored on UART0
   (GPIO 0/1, 115200 8N1), which survives USB re-enumeration after a reset.
3. For the sweep, answer the prompts. Each is terminated by **Enter**.

Prebuilt images are provided for both architectures. The bootrom switches
architecture automatically based on the image, so no configuration is needed.

> **Do not flash this onto a device holding real keys.** It erases the
> firmware and every credential stored with it.

---

## Building

Requires **Pico SDK 2.x** with the `mbedtls` and `tinyusb` submodules
initialised.

```bash
export PICO_SDK_PATH=/path/to/pico-sdk

# Arm Cortex-M33
cmake -B build-arm -DCMAKE_BUILD_TYPE=Release \
      -DPICO_BOARD=pico2 -DPICO_PLATFORM=rp2350-arm-s .
cmake --build build-arm

# RISC-V Hazard3 (needs a Hazard3-capable GCC)
export PICO_TOOLCHAIN_PATH=/path/to/riscv-toolchain
cmake -B build-rv -DCMAKE_BUILD_TYPE=Release \
      -DPICO_BOARD=pico2 -DPICO_PLATFORM=rp2350-riscv .
cmake --build build-rv
```

Use a separate build directory per architecture — CMake will not switch
compilers inside an existing one. A RISC-V toolchain built for Hazard3 is
available from the [`pico-sdk-tools`](https://github.com/raspberrypi/pico-sdk-tools)
releases.

For a like-for-like architecture comparison, use the **same GCC major version**
on both targets. A compiler mismatch moved results by up to 3.4% in testing,
which is small but comparable to some of the margins being measured.

---

## Build options

| Option | Effect |
|---|---|
| `-DBENCH_IN_RAM=1` | Run the shootout from SRAM instead of XIP flash, removing flash-cache effects |
| `-DBENCH_NO_MBEDTLS_ASM=1` | Disable Mbed TLS's assembly bignum paths (pure C on both architectures) |
| `-DUECC_NO_UMAAL=1` | Keep micro-ecc's Thumb-2 assembly but drop the UMAAL multiply/square path — **required on Cortex-M33**, see [Known issues](#known-issues) |
| `-DUECC_NO_ASM=1` | Disable micro-ecc's assembly entirely |
| `-DTRNG_FULL_HEALTH=1` | Swap the TRNG liveness gate for the full SP 800-90B continuous suite |
| `-DSWEEP_MAX_KHZ=` / `-DSWEEP_START_KHZ=` | Compile-time defaults for the sweep range |
| `-DQMI_SCK_MAX_MHZ=` / `-DQMI_SCK_MIN_MHZ=` | Flash SCK window (default 100–115 MHz) |
| `-DSWEEP_QMI_RXDELAY=` | Force a flash RXDELAY instead of keeping the bootrom's |

Every build stamps its configuration into the banner, so a captured log is
self-describing: firmware version, build ID, compiler, chip revision, board ID,
library commits, and the options above.

---

## Libraries under test

| Library | Role | Vendored commit |
|---|---|---|
| [Mbed TLS](https://github.com/Mbed-TLS/mbedtls) 3.6.6 | Baseline: ECC, RSA, hashing | bundled with the Pico SDK |
| [p256-m](https://github.com/mpg/p256-m) | Minimal constant-time P-256 | shipped inside Mbed TLS |
| [micro-ecc](https://github.com/kmackay/micro-ecc) | Embedded ECC with Cortex-M assembly | `541b3a780264` |
| [Monocypher](https://github.com/LoupVaillant/Monocypher) | X25519, Ed25519, BLAKE2b | `1830c06d5910` |
| [mlkem-native](https://github.com/pq-code-package/mlkem-native) | ML-KEM 512/768/1024 | `438f0da19dc3` |
| [mldsa-native](https://github.com/pq-code-package/mldsa-native) | ML-DSA 44/65/87 | `d2149e63337e` |
| RP2350 hardware | SHA-256 accelerator, TRNG | via Pico SDK |

Both post-quantum libraries use their **monolithic multi-level build**, so all
three parameter sets live in one binary with namespaced symbols.

### Stack requirements

ML-DSA-87 signing needs a **107 KiB stack frame** by default, and the RP2350's
main stack lives in a 4 KiB scratch bank that cannot grow. Two things address
this:

- `MLD_CONFIG_REDUCE_RAM` cuts that frame to **14 KiB** at some cost in speed.
- Both PQC libraries run on **core 1 with an explicitly supplied 80 KiB stack**,
  launched for the job and reset immediately afterwards.

This is why ML-KEM and ML-DSA have no dual-core figure.

---

## How stability is judged

Overclocking that produces *wrong answers* is more dangerous than overclocking
that crashes. Every suite is validated by **byte-exact comparison** against
references captured at the safe default clock:

- **Deterministic by construction**: SHA-256, X25519, Ed25519 signatures,
  RSA PKCS#1 v1.5 signatures, `mbedtls_ecp_mul` results.
- **Made deterministic**: ML-KEM via `keypair_derand` / `enc_derand` and ML-DSA
  via `keypair_internal` / `signature_internal`, both with fixed coins. One
  SHA-256 digest over every output per level.
- **Randomised signatures**: anchored on a fixed ECDH shared secret instead,
  which is constant for a fixed key pair.
- **Entropy**: a liveness gate, since entropy has no stored answer.

A hardware watchdog catches hangs; the failing frequency and suite survive the
reset in scratch registers, and the sweep resumes past it. Each step is checked
twice — cold, and again after sustained load — because passing cold and failing
hot is the failure mode that matters.

---

## Selected findings

Measured on Raspberry Pi Pico 2 hardware. **Single board, single run** unless
noted — see [Caveats](#caveats).

**Post-quantum key establishment is cheaper than classical ECC.** ML-KEM-768
encapsulation costs 1.21 Mcycles against 7.03 for an ECDSA P-256 signature.
ML-KEM-512 is the fastest public-key operation measured.

**Post-quantum signing splits.** ML-DSA-44 signs faster than every P-256
implementation; ML-DSA-65 and -87 do not.

**The library matters more than the architecture.** One ECDSA P-256 signature
spans 4.56 to 30.11 Mcycles across implementations — a 6.6× range. The same
Mbed TLS code differs by 2% between Cortex-M33 and Hazard3.

**The Arm advantage tracks 64-bit multiply-and-carry content**, not
"Arm versus RISC-V" in general:

| Workload | Hazard3 ÷ Arm cycles |
|---|---|
| RSA-2048 | 2.03× |
| Ed25519 (64-bit limbs) | 1.42× |
| ML-DSA (32-bit coeffs) | 1.13–1.21× |
| ML-KEM (16-bit coeffs) | 1.01–1.04× |
| Pure ALU loop | **0.93×** (Hazard3 faster) |

**Two cores are not two cores.** A register-only loop gains 1.91× on
Cortex-M33 but only 1.55× on Hazard3 — 5% versus 33% per-core contention, on
code whose only shared resource is instruction fetch.

**The flash divider, not the clock, drives memory-bound cost.** Below 150 MHz
the QMI divider is pinned and every library is flat to within 0.5%. Above it,
cost steps at each divider change. Mbed TLS's ECC pays up to 18%; ML-KEM and
ML-DSA pay nothing. At one point 480 MHz measured *slower* than 460 MHz,
because the divider steps from 4 to 5 and flash SCK drops from 115 to 96 MHz.

**The SHA-256 accelerator is ~9.5× software** and identical on both
architectures, at 5.3 cycles per byte.

---

## Known issues

### micro-ecc UMAAL assembly is broken on Cortex-M33

`asm_arm_mult_square_umaal.inc` causes key generation to fail during setup.
micro-ecc auto-enables the UMAAL path on any ARMv8-M Mainline target at
optimisation level 3, so this affects any Cortex-M33 user of the library.

Build with **`-DUECC_NO_UMAAL=1`**, which keeps the rest of the Thumb-2
assembly. Note this costs roughly 2.4× on P-256 — without the UMAAL path
micro-ecc runs at portable-C speed.

The same assembly worked in an earlier binary of this project and failed in a
later one with no change to micro-ecc, which points at register allocation
rather than anything frequency-dependent.

### ADC unusable on the test boards

Both the die temperature sensor and VSYS read full scale (4095) on the boards
tested. No thermal or supply monitoring is available, which matters when
running 45% over the datasheet voltage.

### Dual-core figures are unreliable for slow operations

The measurement uses a fixed time window. Operations taking a large fraction of
it complete only 1–3 times, and core 1's final operation is counted with no time
attached — producing reported gains above the physical maximum of 2.00 (2.56×
was observed at 18 MHz). Trust the column only where many operations complete
per window. A fixed operation count with both cores joined would fix it.

---

## Caveats

These limit what the numbers support. They are not hypothetical — each was
observed during testing.

| | |
|---|---|
| **Stochastic ceilings** | Six hangs across runs landed on five different suites at 460–558 MHz. Which suite fails first is largely chance. Treat any ceiling as one sample. |
| **Part-to-part spread** | Two Pico 2 boards differed by roughly 20% in maximum stable clock. |
| **Code layout** | Identical library code shifted 5–26% in cost between binaries purely from placement in flash. Any single-binary flash benchmark carries an error bar that cannot be quoted. |
| **ML-DSA reduced-RAM** | Figures come from a memory-constrained build, not the library's best speed. |
| **Rejection sampling** | ML-DSA signing uses one fixed key and message per level: a single draw from the retry distribution, not an average. |
| **Self-tests disabled** | Pairwise consistency tests are available in both PQC libraries and are off. A FIPS-validated deployment would pay substantially more for key generation. |
| **Input validation included** | ML-KEM encapsulation runs the FIPS 203 public-key modulus check on every call. That cost is inside the numbers. |
| **Side channels unmeasured** | This is a speed comparison. p256-m is slowest partly because it spends cycles on uniform constant-time behaviour — which may be the right trade for a device holding keys. |

**Overclocking and overvolting are outside the RP2350 datasheet.** Core
voltages above 1.30 V are out of spec; expect heat, and accept that sustained
use may shorten the life of the part.

---

## Licensing

The benchmark code in this repository is provided under the terms in
[`LICENSE`](LICENSE). Vendored libraries keep their own terms:

| Library | Licence |
|---|---|
| micro-ecc | BSD 2-clause (Kenneth MacKay) |
| Monocypher | Dual: BSD 2-clause or CC0 |
| p256-m | Apache-2.0 OR GPL-2.0-or-later (Mbed TLS Contributors) |
| mlkem-native | Apache-2.0 OR ISC OR MIT |
| mldsa-native | Apache-2.0 OR ISC OR MIT |
| Mbed TLS | Apache-2.0 OR GPL-2.0-or-later |

Each vendored tree retains its own licence file. Check per-file
`SPDX-License-Identifier` tags for anything you intend to reuse.

The embedded RSA-2048 key is a throwaway generated for benchmarking.
**Never use it for anything.**

---

## Acknowledgements

Built on the [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk).
The post-quantum implementations come from the
[PQ Code Package](https://github.com/pq-code-package) project, whose monolithic
multi-level builds made hosting three parameter sets in one binary
straightforward.
