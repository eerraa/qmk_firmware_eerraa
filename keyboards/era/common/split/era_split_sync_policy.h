// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Persisted sync requested-policy fields, one bit per family, relation-
 * independent (owner decision 2026-07-28, Slice 9.5). **All three default on**,
 * and each default arrived with the storage version that carries it, because a
 * valid block is loaded verbatim and a default nothing rewrites reaches only a
 * board that never booted.
 *
 * EEPROM: consumed by replacement storage in every admitted relation. INPUT:
 * live since storage version 4 (owner decision 2026-08-09), gates the DUAL-HOST
 * INPUT-class runtime -- the layer byte and the ACTIVITY body, both directions.
 * RGB: live since Slice 12, gates the DUAL-HOST RGB runtime -- the config
 * sections and the visual pressed-baseline family with its reactive half, both
 * directions -- default on since storage version 5 (owner decision
 * 2026-08-13). */
typedef enum {
    ERA_SPLIT_SYNC_POLICY_FIELD_EEPROM = 0,
    ERA_SPLIT_SYNC_POLICY_FIELD_INPUT,
    ERA_SPLIT_SYNC_POLICY_FIELD_RGB,
    ERA_SPLIT_SYNC_POLICY_FIELD_COUNT,
} era_split_sync_policy_field_t;

/* Diagnostic/API snapshot. Requested policy is local-only EEPROM state.
 * eeprom_policy_generation moves only on the EEPROM bit, never on an
 * INPUT/RGB toggle, so a preference edit cannot invalidate an in-flight
 * storage transaction's generation match. That is why the module keeps it
 * apart from the all-fields generation it holds internally -- of which this
 * snapshot publishes only the low byte, as `heartbeat`. */
typedef struct {
    uint16_t eeprom_policy_generation;
    uint8_t  requested[ERA_SPLIT_SYNC_POLICY_FIELD_COUNT];
    uint8_t  dirty_flags;
    uint8_t  heartbeat;
} era_split_sync_policy_snapshot_t;

void era_split_sync_policy_init(void);
void era_split_sync_policy_reload_from_eeprom(void);
void era_split_sync_policy_reset_to_defaults(void);

/* Active EEPROM-backed requested-policy API. */
bool era_split_sync_policy_set_requested(era_split_sync_policy_field_t field, bool requested);
bool era_split_sync_policy_get_requested(era_split_sync_policy_field_t field, bool *requested);
void era_split_sync_policy_get_snapshot(era_split_sync_policy_snapshot_t *snapshot);
