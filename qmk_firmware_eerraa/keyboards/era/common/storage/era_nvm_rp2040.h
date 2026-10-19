// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>

#include "era_nvm.h"

/* Bind the physical RP2040 XIP flash region reserved by
 * ERA_RP2040_SRAM_RESIDENT.ld to the generic ERA NVM engine. Production ERA
 * storage-adoption builds reach this backend only through the custom EEPROM
 * adapter; the generic engine remains independently host-testable. */
bool era_nvm_rp2040_flash_bind(era_nvm_flash_t *flash);
