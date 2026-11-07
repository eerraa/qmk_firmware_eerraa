// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later
#include "era_settings_reset.h"

bool era_settings_reset_flash(const era_nvm_flash_t *flash) {
    if (flash == NULL || flash->init == NULL || flash->erase_sector == NULL || flash->read == NULL ||
        !flash->init(flash->context)) {
        return false;
    }

    for (uint32_t offset = 0; offset < ERA_NVM_PHYSICAL_SIZE_BYTES; offset += ERA_NVM_ERASE_SECTOR_BYTES) {
        if (!flash->erase_sector(flash->context, offset)) {
            return false;
        }
    }

    uint8_t page[ERA_NVM_PROGRAM_PAGE_BYTES];
    for (uint32_t offset = 0; offset < ERA_NVM_PHYSICAL_SIZE_BYTES; offset += sizeof(page)) {
        if (!flash->read(flash->context, offset, page, sizeof(page))) {
            return false;
        }
        for (size_t index = 0; index < sizeof(page); ++index) {
            if (page[index] != 0xFFU) {
                return false;
            }
        }
    }
    return true;
}

bool era_settings_reset_boot(const era_nvm_flash_t *flash, const era_settings_reset_receipt_t *receipt) {
    if (flash == NULL || receipt == NULL || receipt->read == NULL || receipt->program == NULL) {
        return false;
    }
    uint8_t page[ERA_INSTALL_RESET_RECEIPT_BYTES];
    if (!receipt->read(receipt->context, page)) {
        return false;
    }
    era_install_reset_receipt_state_t state = era_install_reset_receipt_state(page);
    if (state == ERA_INSTALL_RESET_DONE) {
        return true;
    }
    if (state != ERA_INSTALL_RESET_PENDING || !era_settings_reset_flash(flash)) {
        return false;
    }
    era_install_reset_receipt_make(page);
    return receipt->program(receipt->context, page) &&
           receipt->read(receipt->context, page) &&
           era_install_reset_receipt_state(page) == ERA_INSTALL_RESET_DONE;
}
