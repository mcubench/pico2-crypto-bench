/* Sweep progress journalled to flash so it survives a power cycle.
 *
 * The watchdog scratch registers hold state across a watchdog reset, but not
 * across physical removal of the board. An overclock sweep that wedges hard
 * enough to need unplugging would otherwise lose everything and restart from
 * the first suite. This writes a record to the last flash sector after every
 * step, and offers to resume from it on the next boot.
 *
 * Records are 256 bytes (one flash page) and are appended to a 4 KiB sector,
 * giving 16 slots before an erase is needed -- roughly one erase per 16 steps
 * rather than one per step.
 */
#ifndef SWEEP_PROGRESS_H
#define SWEEP_PROGRESS_H

#include <stdint.h>
#include <stdbool.h>

#define PROGRESS_MAX_SUITES 9

typedef struct {
    uint32_t magic;
    uint32_t seq;            /* newest valid record wins */
    uint8_t  suite;          /* suite in progress when this was written */
    uint8_t  vsel;           /* index into the voltage table */
    uint8_t  ladder_mode;
    uint8_t  rxd_mode;
    uint16_t rxd_force;
    uint16_t start_mhz;
    uint16_t max_mhz;
    uint16_t cur_mhz;        /* frequency being attempted */
    uint16_t result[PROGRESS_MAX_SUITES];
    uint8_t  complete;       /* 1 once every suite has finished */
    uint8_t  pad[3];
    uint32_t crc;
} progress_t;

/* Newest valid record, or false if the journal is empty or corrupt. */
bool progress_load(progress_t *out);

/* Append a record. Erases the sector first if it is full. Must be called at a
 * clock the flash is known good at -- never while overclocked. */
void progress_save(progress_t *p);

/* Wipe the journal. */
void progress_erase(void);

#endif
