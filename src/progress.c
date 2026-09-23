#include <string.h>
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "progress.h"

#define PROGRESS_MAGIC  0x50475231u   /* "PGR1" */
#define SLOT_BYTES      FLASH_PAGE_SIZE               /* 256 */
#define SLOTS           (FLASH_SECTOR_SIZE / SLOT_BYTES)  /* 16 */

/* Last sector of flash. Override for a part with a different capacity -- an
 * RP2354A carries 2 MB, not the 4 MB a pico2 board header assumes. */
#ifndef PROGRESS_FLASH_OFFSET
#define PROGRESS_FLASH_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#endif

#define SECTOR_XIP ((const uint8_t *)(XIP_BASE + PROGRESS_FLASH_OFFSET))

static uint32_t crc32(const uint8_t *p, size_t n)
{
    uint32_t c = 0xffffffffu;
    while (n--) {
        c ^= *p++;
        for (int b = 0; b < 8; b++)
            c = (c >> 1) ^ (0xedb88320u & (uint32_t)(-(int32_t)(c & 1u)));
    }
    return ~c;
}

static bool slot_valid(const progress_t *r)
{
    if (r->magic != PROGRESS_MAGIC) return false;
    return crc32((const uint8_t *)r, offsetof(progress_t, crc)) == r->crc;
}

bool progress_load(progress_t *out)
{
    bool found = false;
    uint32_t best = 0;
    for (unsigned i = 0; i < SLOTS; i++) {
        const progress_t *r = (const progress_t *)(SECTOR_XIP + i * SLOT_BYTES);
        if (!slot_valid(r)) continue;
        if (!found || r->seq >= best) { best = r->seq; *out = *r; found = true; }
    }
    return found;
}

/* First slot that has never been written (erased flash reads 0xff). */
static int next_free_slot(void)
{
    for (unsigned i = 0; i < SLOTS; i++) {
        const uint32_t *m = (const uint32_t *)(SECTOR_XIP + i * SLOT_BYTES);
        if (*m == 0xffffffffu) return (int)i;
    }
    return -1;
}

void progress_erase(void)
{
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(PROGRESS_FLASH_OFFSET, FLASH_SECTOR_SIZE);
    restore_interrupts(ints);
}

void progress_save(progress_t *p)
{
    static uint8_t page[SLOT_BYTES];   /* must live in RAM, not XIP */

    int slot = next_free_slot();
    if (slot < 0) {                    /* sector full: start it over */
        progress_erase();
        slot = 0;
        p->seq++;
    }

    p->magic = PROGRESS_MAGIC;
    p->crc   = crc32((const uint8_t *)p, offsetof(progress_t, crc));

    memset(page, 0xff, sizeof(page));
    memcpy(page, p, sizeof(*p));

    uint32_t ints = save_and_disable_interrupts();
    flash_range_program(PROGRESS_FLASH_OFFSET + (uint32_t)slot * SLOT_BYTES,
                        page, SLOT_BYTES);
    restore_interrupts(ints);
}
