// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "era_nvm.h"

/* ERA-owned, result-bearing persistence boundary. QMK's public EEPROM API is
 * intentionally void-returning; replacement Apply and CLEAN use this surface
 * instead so persistent failure is never confused with success. */
bool era_eeprom_driver_ready(void);

era_nvm_result_t era_eeprom_driver_replace(uint32_t address, const void *data, size_t length, era_nvm_origin_t origin);
era_nvm_result_t era_eeprom_driver_replay_read(uint32_t address, void *data, size_t length);

/* CLEAN PREPARE: persist one word and prove the same production replay parser
 * would recover it after a reboot before the restart agreement may publish
 * PREPARED. */
era_nvm_result_t era_eeprom_driver_prepare_reboot_word(uint32_t address, uint16_t value);

/* One inactive 4-KiB sector at most. The caller is top-level housekeeping, so
 * a pass naturally occurs between sectors and there is no recursive keyboard
 * or wire work inside flash code. */
era_nvm_result_t era_eeprom_driver_maintenance_task(bool *did_work);

/* Test/diagnostic fact only: no policy above the NVM layer should widen the
 * engine's rule that only macro-touching durable writes are refused while a
 * macro upload is staged. */
bool era_eeprom_driver_macro_transaction_open(void);
void era_eeprom_driver_get_nvm_diagnostics(era_nvm_diagnostics_t *diagnostics);
