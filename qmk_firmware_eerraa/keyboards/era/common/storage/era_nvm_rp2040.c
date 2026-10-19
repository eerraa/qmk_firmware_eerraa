// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_nvm_rp2040.h"

#include <stdint.h>
#include <string.h>

#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"

#if !defined(ERA_SRAM_RESIDENT_IMAGE)
#    error ERA RP2040 NVM requires the ERA SRAM-resident image; XIP must not be needed while program/erase is active
#endif

_Static_assert(FLASH_PAGE_SIZE == ERA_NVM_PROGRAM_PAGE_BYTES, "ERA NVM RP2040 backend requires 256-byte program pages");
_Static_assert(FLASH_SECTOR_SIZE == ERA_NVM_ERASE_SECTOR_BYTES, "ERA NVM RP2040 backend requires 4-KiB erase sectors");

/* Absolute XIP VMAs emitted by ERA_RP2040_SRAM_RESIDENT.ld. */
extern uint8_t __era_nvm_region_base__[];
extern uint8_t __era_nvm_region_end__[];

typedef struct {
    uint32_t flash_offset;
    bool     initialized;
} era_nvm_rp2040_context_t;

static era_nvm_rp2040_context_t s_era_nvm_rp2040;

static bool era_nvm_rp2040_bounds(uint32_t offset, size_t length) {
    if (length > UINT32_MAX) {
        return false;
    }
    uint32_t length32 = (uint32_t)length;
    return offset <= ERA_NVM_PHYSICAL_SIZE_BYTES && length32 <= ERA_NVM_PHYSICAL_SIZE_BYTES - offset;
}

static bool era_nvm_rp2040_init(void *context) {
    era_nvm_rp2040_context_t *rp = (era_nvm_rp2040_context_t *)context;
    uintptr_t                 base = (uintptr_t)__era_nvm_region_base__;
    uintptr_t                 end  = (uintptr_t)__era_nvm_region_end__;

    rp->initialized = false;
    if (base < XIP_BASE || end < base || end - base != ERA_NVM_PHYSICAL_SIZE_BYTES) {
        return false;
    }
    if (end > (uintptr_t)XIP_BASE + (uintptr_t)PICO_FLASH_SIZE_BYTES) {
        return false;
    }

    rp->flash_offset = (uint32_t)(base - XIP_BASE);
    rp->initialized  = true;
    return true;
}

static bool era_nvm_rp2040_read(void *context, uint32_t offset, void *data, size_t length) {
    era_nvm_rp2040_context_t *rp = (era_nvm_rp2040_context_t *)context;
    if (!rp->initialized || (length > 0U && data == NULL) || !era_nvm_rp2040_bounds(offset, length)) {
        return false;
    }
    if (length > 0U) {
        const void *physical = (const void *)((uintptr_t)XIP_BASE + rp->flash_offset + offset);
        memcpy(data, physical, length);
    }
    return true;
}

static bool era_nvm_rp2040_program(void *context, uint32_t offset, const void *data, size_t length) {
    era_nvm_rp2040_context_t *rp = (era_nvm_rp2040_context_t *)context;
    if (!rp->initialized || data == NULL || length == 0U || length > ERA_NVM_PROGRAM_PAGE_BYTES || !era_nvm_rp2040_bounds(offset, length)) {
        return false;
    }

    uint32_t page_offset = offset % ERA_NVM_PROGRAM_PAGE_BYTES;
    if (length > ERA_NVM_PROGRAM_PAGE_BYTES - page_offset) {
        return false;
    }

    /* pico-sdk's flash_range_program() accepts complete aligned pages. Fill
     * the untouched bytes with 0xFF: NOR page programming is bitwise 1->0, so
     * those bytes preserve any already-programmed zero bits. If the requested
     * range itself attempted 0->1, the engine's immediate physical readback
     * detects the mismatch and seals the append tail. */
    uint8_t page[ERA_NVM_PROGRAM_PAGE_BYTES];
    memset(page, 0xFF, sizeof(page));
    memcpy(page + page_offset, data, length);

    uint32_t physical_page = rp->flash_offset + offset - page_offset;
    flash_range_program(physical_page, page, sizeof(page));
    return true;
}

static bool era_nvm_rp2040_erase_sector(void *context, uint32_t offset) {
    era_nvm_rp2040_context_t *rp = (era_nvm_rp2040_context_t *)context;
    if (!rp->initialized || offset % ERA_NVM_ERASE_SECTOR_BYTES != 0U || !era_nvm_rp2040_bounds(offset, ERA_NVM_ERASE_SECTOR_BYTES)) {
        return false;
    }

    /* Correctness deliberately uses only the 4-KiB sector primitive. The
     * generic engine performs the post-command erased-state readback. */
    flash_range_erase(rp->flash_offset + offset, ERA_NVM_ERASE_SECTOR_BYTES);
    return true;
}

bool era_nvm_rp2040_flash_bind(era_nvm_flash_t *flash) {
    if (flash == NULL) {
        return false;
    }
    memset(&s_era_nvm_rp2040, 0, sizeof(s_era_nvm_rp2040));
    *flash = (era_nvm_flash_t){
        .context      = &s_era_nvm_rp2040,
        .init         = era_nvm_rp2040_init,
        .read         = era_nvm_rp2040_read,
        .program      = era_nvm_rp2040_program,
        .erase_sector = era_nvm_rp2040_erase_sector,
    };
    return true;
}
