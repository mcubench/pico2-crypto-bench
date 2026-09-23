/* ML-KEM needs roughly 20 KiB of stack for level 1024, which does not fit the
 * RP2350's 4 KiB SCRATCH_Y main stack. These helpers run a job on core 1 with
 * an explicitly supplied 48 KiB stack and wait for it to finish. Core 1 is
 * launched for the job and reset immediately afterwards, so it is never live
 * while the clock or the flash timing is being changed. */
#ifndef MLK_RUNNER_H
#define MLK_RUNNER_H

#include <stdint.h>
#include <stdbool.h>

/* Runs fn() once and returns its int result. */
/* Set if core 1 never signalled completion: it faulted or never started. */
extern bool mlk_runner_core1_dead;

int      mlk_call_on_core1(int (*fn)(void));
/* Repeats fn() for at least min_us and returns microseconds per call. */
uint64_t mlk_time_on_core1(int (*fn)(void), uint32_t min_us);

#endif
