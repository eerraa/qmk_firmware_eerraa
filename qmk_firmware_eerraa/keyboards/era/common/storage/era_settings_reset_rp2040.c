// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later
#include "era_settings_reset.h"

#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"
#include "wait.h"

#if !defined(ERA_SRAM_RESIDENT_IMAGE) || !defined(ERA_SETTINGS_RESET_IMAGE)
#    error Install reset requires the ERA RP2040 SRAM-resident reset image
#endif

_Static_assert(FLASH_PAGE_SIZE == ERA_INSTALL_RESET_RECEIPT_BYTES, "Install receipt must occupy one program page");

__attribute__((section(".era_install_reset_receipt"), used, aligned(256)))
const uint8_t era_settings_reset_install_page[ERA_INSTALL_RESET_RECEIPT_BYTES] = {
    [0 ... ERA_INSTALL_RESET_RECEIPT_BYTES - 1] = 0xFF,
};

extern uint8_t __era_install_reset_receipt_base__[];
extern uint8_t __era_install_reset_receipt_end__[];
extern uint8_t __flash_startup_base__[];
extern uint8_t __flash_startup_end__[];
extern uint8_t __era_nvm_region_base__[];

static bool receipt_bounds(void) {
    uintptr_t base = (uintptr_t)__era_install_reset_receipt_base__;
    uintptr_t end  = (uintptr_t)__era_install_reset_receipt_end__;
    return base == (uintptr_t)era_settings_reset_install_page &&
           base >= (uintptr_t)XIP_BASE + FLASH_PAGE_SIZE &&
           base % FLASH_PAGE_SIZE == 0U &&
           end >= base && end - base == ERA_INSTALL_RESET_RECEIPT_BYTES &&
           base >= (uintptr_t)__flash_startup_base__ &&
           end <= (uintptr_t)__flash_startup_end__ &&
           end <= (uintptr_t)__era_nvm_region_base__;
}

static bool receipt_read(void *context, uint8_t *page) {
    (void)context;
    if (page == NULL || !receipt_bounds()) {
        return false;
    }
    /* Read physical flash, not the initializer or an XIP cache alias. */
    const volatile uint8_t *physical = (const volatile uint8_t *)(
        (uintptr_t)XIP_NOCACHE_NOALLOC_BASE + (uintptr_t)__era_install_reset_receipt_base__ - XIP_BASE);
    for (size_t index = 0; index < ERA_INSTALL_RESET_RECEIPT_BYTES; ++index) {
        page[index] = physical[index];
    }
    return true;
}

static bool receipt_program(void *context, const uint8_t *page) {
    uint8_t current[ERA_INSTALL_RESET_RECEIPT_BYTES];
    if (era_install_reset_receipt_state(page) != ERA_INSTALL_RESET_DONE ||
        !receipt_read(context, current) ||
        era_install_reset_receipt_state(current) != ERA_INSTALL_RESET_PENDING) {
        return false;
    }
    flash_range_program((uint32_t)((uintptr_t)__era_install_reset_receipt_base__ - XIP_BASE),
                        page, ERA_INSTALL_RESET_RECEIPT_BYTES);
    return true;
}

void era_settings_reset_bootstrap(const era_nvm_flash_t *flash) {
    const era_settings_reset_receipt_t receipt = {
        .read    = receipt_read,
        .program = receipt_program,
    };
    if (!era_settings_reset_boot(flash, &receipt)) {
        /* Do not mount or expose partly reset settings. BOOTSEL and the
         * pre-copy RUN entry remain independent of this failed boot. */
        for (;;) {
            wait_ms(250);
        }
    }
}
