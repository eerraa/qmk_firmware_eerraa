// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

/* GET_KEYBOARD_VALUE (0x02) selector. Collision audit: 0x01-0x04 GET, 0x05 SET
   only, 0x06 unused in this tree. G1 approved 2026-08-21. */
#define ERA_STATE_SYNC_KEYBOARD_VALUE 0x06U
#define ERA_STATE_SYNC_ENVELOPE_VERSION 0x01U
#define ERA_STATE_SYNC_STATUS_OK 0x00U
#define ERA_STATE_SYNC_STATUS_UNSUPPORTED_VERSION 0x01U
#define ERA_STATE_SYNC_STATUS_INVALID 0x02U

#define ERA_STATE_SYNC_DOMAIN_KEYMAP 0x01U
#define ERA_STATE_SYNC_DOMAIN_MACRO 0x02U
#define ERA_STATE_SYNC_DOMAIN_CONFIG 0x04U
#define ERA_STATE_SYNC_DOMAIN_MASK_INITIAL 0x07U

/* GET-visible CONFIG runtime changed. Call only when a setter actually altered
   a value that the corresponding VIA GET can now return. The config-relative
   span identifies which later EEPROM persist must not increment CONFIG again. */
void     era_state_sync_note_config_semantic_commit(uint16_t config_offset, uint16_t length);
/* Bracket era_eeprom_update_config(). The callback it emits is synchronous, so
   only pending semantic regions covered by this write are suppressed. */
void     era_state_sync_config_persist_begin(uint16_t config_offset, uint16_t length);
void     era_state_sync_config_persist_end(void);
void     era_state_sync_note_eeprom_span(uint16_t offset, uint16_t length);
void     era_state_sync_note_storage_domain(uint8_t domain);
bool     era_state_sync_via_command(uint8_t *data, uint8_t length);

#ifdef ERA_STATE_SYNC_TEST
uint32_t era_state_sync_keymap_revision(void);
uint32_t era_state_sync_macro_revision(void);
uint32_t era_state_sync_config_revision(void);
void era_state_sync_set_revisions_for_testing(uint32_t keymap, uint32_t macro, uint32_t config);
#endif
