#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "mlk_runner.h"

#define MLK_STACK_WORDS (20u * 1024u)   /* 80 KiB: ML-DSA-87 needs ~19 KiB
                                         in one frame even in reduced-RAM mode */
/* ML-KEM and ML-DSA place large aligned arrays directly on the stack, and the
 * Arm AAPCS requires an 8-byte aligned SP. Over-align to 32 to be safe. */
static uint32_t __attribute__((aligned(32))) mlk_stack[MLK_STACK_WORDS];

static int      (* volatile job_fn)(void);
static volatile uint32_t job_min_us;
static volatile uint64_t job_result;
static volatile bool     job_done;
static volatile bool     job_is_timed;
bool mlk_runner_core1_dead;
#define job_failed mlk_runner_core1_dead

static void core1_job(void)
{
    if (!job_is_timed) {
        job_result = (uint64_t)(uint32_t)job_fn();
    } else {
        job_fn();                       /* warm up */
        uint32_t iters = 0;
        uint64_t us;
        absolute_time_t t0 = get_absolute_time();
        do {
            job_fn();
            iters++;
            us = (uint64_t)absolute_time_diff_us(t0, get_absolute_time());
        } while (us < job_min_us);
        job_result = iters ? us / iters : 0;
    }
    __compiler_memory_barrier();
    job_done = true;
    while (true) tight_loop_contents();
}

static uint64_t run_job(int (*fn)(void), bool timed, uint32_t min_us)
{
    job_fn = fn; job_min_us = min_us; job_is_timed = timed;
    job_done = false; job_result = 0;
    __compiler_memory_barrier();

    multicore_reset_core1();
    multicore_launch_core1_with_stack(core1_job, mlk_stack, sizeof(mlk_stack));

    /* A core 1 that faults would otherwise hang us here with no diagnosis. */
    absolute_time_t bail = make_timeout_time_ms(20000);
    while (!job_done && absolute_time_diff_us(get_absolute_time(), bail) > 0)
        tight_loop_contents();

    bool ok = job_done;
    multicore_reset_core1();
    if (!ok) { job_failed = true; return (uint64_t)(uint32_t)-999; }
    return job_result;
}

int mlk_call_on_core1(int (*fn)(void)) { return (int)(int32_t)(uint32_t)run_job(fn, false, 0); }
uint64_t mlk_time_on_core1(int (*fn)(void), uint32_t min_us) { return run_job(fn, true, min_us); }
