#include <string.h>
#include "pico/stdlib.h"
#include "hardware/structs/trng.h"
#include "hardware/regs/trng.h"
#include "trng_bench.h"

#define BYPASS_ALL (TRNG_TRNG_DEBUG_CONTROL_AUTO_CORRELATE_BYPASS_BITS | \
                    TRNG_TRNG_DEBUG_CONTROL_TRNG_CRNGT_BYPASS_BITS     | \
                    TRNG_TRNG_DEBUG_CONTROL_VNC_BYPASS_BITS)

static bool     g_health;
static uint32_t g_sample;

void trng_bench_start(bool health_tests, uint32_t sample_count)
{
    g_health = health_tests;
    g_sample = sample_count;
    trng_hw->rnd_source_enable = 0;
    trng_hw->sample_cnt1 = sample_count;
    trng_hw->trng_debug_control = health_tests ? 0u : BYPASS_ALL;
    trng_hw->rng_icr = ~0u;
    trng_hw->rnd_source_enable = 1;
}

/* One 192-bit entropy holding register, with a bounded wait so a stalled
 * source reports rather than hanging the sweep. */
static bool ehr_read(uint32_t w[6])
{
    absolute_time_t bail = make_timeout_time_ms(250);
    while (trng_hw->trng_busy) {
        if (absolute_time_diff_us(get_absolute_time(), bail) <= 0) return false;
    }
    for (int i = 0; i < 6; i++) w[i] = trng_hw->ehr_data[i];
    trng_hw->rng_icr = ~0u;
    trng_hw->rnd_source_enable = 1;   /* re-arm for the next block */
    return true;
}

bool trng_bench_read(uint8_t *out, size_t len)
{
    uint32_t w[6];
    while (len) {
        if (!ehr_read(w)) return false;
        size_t n = len < TRNG_EHR_BYTES ? len : TRNG_EHR_BYTES;
        memcpy(out, w, n);
        out += n; len -= n;
    }
    return true;
}

/* SP 800-90B continuous tests, byte-wise (alphabet of 256).
 *   Repetition Count Test: cutoff for H=4 bits/byte at alpha=2^-30 is 9.
 *   Adaptive Proportion Test: window 512, cutoff 200, deliberately loose so a
 *   healthy source never trips it but a stuck or badly skewed one does. */
#define APT_WINDOW 512u
#define APT_CUTOFF 200u
#define RCT_CUTOFF 9u

/* Minimal: did the source deliver, and is it moving? */
int trng_bench_liveness(void)
{
    uint8_t a[TRNG_EHR_BYTES], b[TRNG_EHR_BYTES];
    if (!trng_bench_read(a, sizeof(a))) return -1;
    if (!trng_bench_read(b, sizeof(b))) return -1;
    if (memcmp(a, b, sizeof(a)) == 0)   return -2;   /* stuck */
    return 0;
}

int trng_bench_health(void)
{
    static uint8_t buf[1024];
    if (!trng_bench_read(buf, sizeof(buf))) return -1;   /* source stalled */

    /* stuck output: an all-equal block means the source has died */
    bool all_same = true;
    for (size_t i = 1; i < sizeof(buf); i++)
        if (buf[i] != buf[0]) { all_same = false; break; }
    if (all_same) return -2;

    /* repetition count */
    unsigned run = 1;
    for (size_t i = 1; i < sizeof(buf); i++) {
        run = (buf[i] == buf[i - 1]) ? run + 1 : 1;
        if (run >= RCT_CUTOFF) return -3;
    }

    /* adaptive proportion, first window */
    unsigned same = 1;
    for (size_t i = 1; i < APT_WINDOW; i++) if (buf[i] == buf[0]) same++;
    if (same >= APT_CUTOFF) return -4;

    /* crude bit balance: a healthy 1024-byte sample sits near 4096 ones.
     * +/-15% is wide enough never to false-positive and narrow enough to catch
     * a source that has collapsed toward one rail. */
    uint32_t ones = 0;
    for (size_t i = 0; i < sizeof(buf); i++) ones += (uint32_t)__builtin_popcount(buf[i]);
    if (ones < 3482u || ones > 4710u) return -5;

    return 0;
}
