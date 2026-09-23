/* On-chip TRNG: raw throughput and NIST SP 800-90B style continuous health
 * tests. The RP2350 entropy source is a ring-oscillator design with three
 * built-in checks -- von Neumann corrector, continuous RNG test and
 * autocorrelation test. The SDK's own pico_rand driver bypasses all three for
 * speed, because it only needs to seed a PRNG; a device generating keys would
 * want them on. Both configurations are measured. */
#ifndef TRNG_BENCH_H
#define TRNG_BENCH_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define TRNG_EHR_BYTES 24u   /* entropy holding register: 192 bits */

/* health_tests: false bypasses von Neumann, CRNGT and autocorrelation. */
void trng_bench_start(bool health_tests, uint32_t sample_count);

/* Fills out with len bytes; returns false if the source stalled. */
bool trng_bench_read(uint8_t *out, size_t len);

/* Cheap liveness gate used by default: confirms the source delivered and is not
 * emitting a constant. Costs one read and one comparison, so it does not
 * perturb the throughput figure. Returns 0 on success, negative on failure. */
int  trng_bench_liveness(void);

/* Full SP 800-90B continuous suite (repetition count, adaptive proportion,
 * bit balance). Not run by default: it is a quality assessment, not a speed
 * measurement, and it is slower than the source it is testing. Enable with
 * -DTRNG_FULL_HEALTH=1. */
int  trng_bench_health(void);

#endif
