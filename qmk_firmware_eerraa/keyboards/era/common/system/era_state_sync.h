// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "era_wire.h"

/* Private GET_KEYBOARD_VALUE selector; allocations belong to the registry. */
#define ERA_STATE_SYNC_KEYBOARD_VALUE ERA_WIRE_STATE_SYNC_SELECTORS_CURRENT
#define ERA_STATE_SYNC_ENVELOPE_VERSION ERA_WIRE_STATE_SYNC_ENVELOPE_VERSION
#define ERA_STATE_SYNC_STATUS_OK ERA_WIRE_STATE_SYNC_STATUS_OK
#define ERA_STATE_SYNC_STATUS_UNSUPPORTED_VERSION ERA_WIRE_STATE_SYNC_STATUS_UNSUPPORTED_VERSION
#define ERA_STATE_SYNC_STATUS_INVALID ERA_WIRE_STATE_SYNC_STATUS_INVALID

#define ERA_STATE_SYNC_DOMAIN_KEYMAP ERA_WIRE_STATE_SYNC_DOMAINS_KEYMAP
#define ERA_STATE_SYNC_DOMAIN_MACRO ERA_WIRE_STATE_SYNC_DOMAINS_MACRO
#define ERA_STATE_SYNC_DOMAIN_CONFIG ERA_WIRE_STATE_SYNC_DOMAINS_CONFIG
#define ERA_STATE_SYNC_DOMAIN_MASK_INITIAL ERA_WIRE_STATE_SYNC_DOMAINS_MASK_INITIAL

/* GET-visible CONFIG runtime changed. Call only when a setter actually altered
   a value that the corresponding VIA GET can now return. The config-relative
   span identifies which later EEPROM persist must not increment CONFIG again. */
void     era_state_sync_note_config_semantic_commit(uint16_t config_offset, uint16_t length);
/* Bracket era_eeprom_update_config(). Retain the SET snapshot through outer
   completion; a newer SET must keep its own debt when an older write repairs. */
void     era_state_sync_config_persist_begin(uint16_t config_offset, uint16_t length);
void     era_state_sync_config_persist_end(void);
void     era_state_sync_note_macro_public_change(void);
void     era_state_sync_note_eeprom_span(uint16_t offset, uint16_t length);
uint32_t era_state_sync_eeprom_span_mask(uint16_t offset, uint16_t length);
void     era_state_sync_note_eeprom_mask(uint32_t mask);
void     era_state_sync_note_storage_domain(uint8_t domain);
bool     era_state_sync_via_command(uint8_t *data, uint8_t length);

#ifdef ERA_STATE_SYNC_TEST
uint32_t era_state_sync_keymap_revision(void);
uint32_t era_state_sync_macro_revision(void);
uint32_t era_state_sync_config_revision(void);
void era_state_sync_set_revisions_for_testing(uint32_t keymap, uint32_t macro, uint32_t config);
#endif
