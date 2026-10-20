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
era_nvm_result_t era_eeprom_driver_macro_begin(uint32_t length);
era_nvm_result_t era_eeprom_driver_macro_data(uint32_t offset, const void *data, size_t length);
era_nvm_result_t era_eeprom_driver_macro_commit(uint32_t crc32);
era_nvm_result_t era_eeprom_driver_macro_abort(void);
bool era_eeprom_driver_macro_replace_open(void);
uint32_t era_eeprom_driver_macro_serial(void);

era_nvm_result_t era_eeprom_driver_replace(uint32_t address, const void *data, size_t length, era_nvm_origin_t origin);
era_nvm_result_t era_eeprom_driver_replay_read(uint32_t address, void *data, size_t length);
/* Protected storage-engine metadata. This is durable local state but not a QMK
 * user edit, State Sync revision, or split dirty-domain notification. */
era_nvm_result_t era_eeprom_driver_write_storage_metadata(uint32_t address, const void *data, size_t length);

/* CLEAN PREPARE: persist one word and prove the same production replay parser
 * would recover it after a reboot before the restart agreement may publish
 * PREPARED. */
era_nvm_result_t era_eeprom_driver_prepare_reboot_word(uint32_t address, uint16_t value);

/* One inactive 4-KiB sector at most. The caller is top-level housekeeping, so
 * a pass naturally occurs between sectors and there is no recursive keyboard
 * or wire work inside flash code. Admission policy belongs to
 * era_common_features_maintenance_task(); this adapter reports readiness and
 * the NVM result only. Mandatory rotation uses the NVM primitive directly. */
era_nvm_result_t era_eeprom_driver_maintenance_task(bool *did_work);

/* Existing NVM-owned transaction lifetime, also used for presentation and
 * optional maintenance admission. Unrelated mandatory durable writes remain
 * permitted while a macro upload is staged. */
bool era_eeprom_driver_macro_transaction_open(void);
void era_eeprom_driver_get_nvm_diagnostics(era_nvm_diagnostics_t *diagnostics);
#ifdef ERA_TOMAK_SYNC_TIMING
uint32_t era_eeprom_driver_timing_generation(void);
#endif
