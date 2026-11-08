// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "era_nvm.h"
#include "era_install_reset_receipt.h"

typedef struct {
    void *context;
    bool (*read)(void *context, uint8_t *page);
    bool (*program)(void *context, const uint8_t *page);
} era_settings_reset_receipt_t;

bool era_settings_reset_flash(const era_nvm_flash_t *flash, uint32_t size);
bool era_settings_reset_boot(const era_nvm_flash_t *flash, const era_settings_reset_receipt_t *receipt, uint32_t size);
void era_settings_reset_bootstrap(void);
