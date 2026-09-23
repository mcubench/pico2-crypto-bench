/*
 * Version and build identification for the RP2350 crypto benchmark suite.
 *
 * History
 *   1.0.0  Mbed TLS only; ECDSA/ECDH/RSA timing, Arm and RISC-V builds
 *   1.1.0  Added MBEDTLS_ECP_NIST_OPTIM and MBEDTLS_HAVE_ASM; copy_to_ram option
 *   1.2.0  Multi-library shootout: p256-m, micro-ecc, Monocypher, hardware SHA-256
 *   1.3.0  Overclock sweep with QMI divider management and watchdog recovery
 *   1.4.0  Versioned build info banner and runtime state dump
 *   1.5.0  Per-library sweep suites with independent frequency ladders
 *   1.6.0  Runtime start/ceiling prompts; attempts logged before the clock moves
 *   1.7.0  Fixes: scratch[4] collision with the SDK watchdog magic, temperature
 *          reading and formatting, torture loop optimised away, duplicate steps
 *   2.0.0  Cycles/op reporting, XIP cache hit rate, RXDELAY as a swept axis,
 *          divider-aligned frequency ladder, dual-core variant, VSYS monitoring.
 *          Die temperature removed: the sensor is unusable on this board.
 *   2.1.0  ML-KEM-512/768/1024 (mlkem-native) added to both programs, run on
 *          core 1 with a 48 KiB stack; seventh sweep suite with deterministic
 *          derand known-answer checks across all three levels.
 *   2.2.0  Operations per second alongside cycles per operation everywhere;
 *          ML-KEM reports 512, 768 and 1024 as separate timed rows.
 *   2.3.0  ML-DSA-44/65/87 (mldsa-native, MLD_CONFIG_REDUCE_RAM) with the
 *          private-key operations -- deterministic key generation and signing
 *          -- timed per level, plus verification for context.
 *   2.3.1  Diagnostics: setup failures report the step that failed; core 1 is
 *          given a timeout so a fault is reported rather than hanging; core 1
 *          stack over-aligned to 32 bytes; -DUECC_NO_ASM=1 build option.
 *   2.4.0  Underclocking: rungs down to 18 MHz (the PLL floor), core voltage
 *          selectable down to 0.85 V, QMI divider floored at 2, watchdog armed
 *          only in the overclock direction.
 *   2.4.1  Menu prompts accumulate digits until Enter. With twelve voltage
 *          options a single-character read made "12" unreachable, because the
 *          leading "1" committed immediately.
 *   2.5.0  On-chip TRNG: raw throughput with the hardware health tests both
 *          enabled and bypassed, compared against pico_rand and CTR_DRBG, plus
 *          an SP 800-90B continuous health suite as a ninth sweep suite.
 *   2.5.1  TRNG reports raw throughput only: hardware health checks bypassed
 *          and a one-comparison liveness gate in place of the SP 800-90B suite,
 *          which is now opt-in via -DTRNG_FULL_HEALTH=1.
 *   2.6.0  Sweep progress journalled to a flash sector after every step, so a
 *          run survives physical removal of the board and can be resumed
 *          instead of restarting from the first suite.
 *   2.6.1  Fix: setup_all() returns a step code but was tested with "!", so a
 *          success was indistinguishable from a failure and every failure was
 *          silently ignored. Setup now prints a marker per stage and names the
 *          failing step.
 *   2.6.2  Fix: setup_all() still ended in "return true", so with the corrected
 *          caller a successful setup reported a failure at step 1. Success now
 *          returns 0, verified in the built source.
 *   2.6.3  Fix: reload and preserve all flash-journal configuration across a
 *          watchdog reset, and reject invalid legacy resume records.
 *   2.6.4  Fix: flash-only recovery reruns the recorded suite because its
 *          journal entry precedes the interrupted frequency attempt.
 *   2.6.5  Flash recovery ends the recorded hung suite and advances to the
 *          next one, avoiding repeated hangs after physical reinsertion.
 *   2.6.6  Dual-core sweep steps require core 1 to finish useful work and gain
 *          more than 1.05x; a stalled core 1 can no longer report PASS.
 *   2.6.7  Watchdog recovery waits for a serial acknowledgement before the next
 *          suite or final summary, preventing tables lost during CDC reconnect.
 */
#ifndef BENCH_VERSION_H
#define BENCH_VERSION_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define BENCH_VERSION_MAJOR  2
#define BENCH_VERSION_MINOR  6
#define BENCH_VERSION_PATCH  7
#define BENCH_VERSION_STRING "2.6.7"

/* Pinned commits of the vendored libraries, for reproducing a result later. */
#define LIB_MICROECC_COMMIT   "541b3a780264"
#define LIB_MONOCYPHER_COMMIT "1830c06d5910"
#define LIB_MLKEM_COMMIT      "438f0da19dc3"
#define LIB_MLDSA_COMMIT      "d2149e63337e"
#define LIB_MBEDTLS_COMMIT    "0bebf8b8c7f0"

/* Printed once at startup: firmware, toolchain, chip, libraries, build options.
 * program_name distinguishes the shootout from the sweep in captured logs. */
void bench_print_build_info(const char *program_name);

/* Printed at startup and on demand: clocks, voltage, QMI/flash timing,
 * temperature, reset cause. Safe to call after any clock or voltage change. */
void bench_print_runtime_state(void);

/* Single compact line, for per-step logging during a sweep. */
void bench_print_state_line(const char *prefix);

/* Individual readings, exposed so callers can log or tabulate them. */
uint32_t bench_flash_sck_khz(void);
uint32_t bench_qmi_clkdiv(void);
uint32_t bench_qmi_rxdelay(void);
uint16_t bench_vreg_mv(void);
uint16_t bench_vsys_mv(void);
void     bench_xip_reset(void);
void     bench_xip_read(uint32_t *hit, uint32_t *acc);
uint32_t bench_xip_hit_permille(uint32_t hit, uint32_t acc);

#endif /* BENCH_VERSION_H */
