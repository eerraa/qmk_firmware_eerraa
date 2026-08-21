// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "../storage/era_eeprom_storage.h"

#define ERA_SPLIT_EEPROM_SYNC_POLICY_CONFIG_OFFSET ERA_EEPROM_LOCAL_POLICY_CONFIG_OFFSET
#define ERA_SPLIT_EEPROM_SYNC_POLICY_CONFIG_SIZE ERA_EEPROM_LOCAL_POLICY_CONFIG_SIZE
#define ERA_SPLIT_SYNC_POLICY_STORAGE_SIGNATURE 0x504E5953UL
/* The version byte has exactly one accepted value and no upgrade arm: a block
 * carrying any other value fails validity and is rewritten whole with defaults
 * (era_source_map.md, Stored-Data Compatibility). It stays a version rather
 * than becoming a second signature because it is what makes that rewrite
 * happen, and because a signature change would reset the block on a
 * signature-shaped question rather than a layout-shaped one.
 *
 * The fourteen bytes at +10 are the seven per-domain 16-bit LE divergence
 * counters of the recency layer, owned by the storage engine
 * (era_host_peer_storage_contract.md, Recency Layer) and opaque to the policy
 * module. Ordinary policy persists write only the prefix below, so a policy
 * toggle cannot clobber them. */
/* 5 since 2026-08-13, when RGB joined EEPROM and INPUT as a default-on family.
 * A default change has to ride the version, and that is the whole reason this
 * byte moved: a valid v4 block is loaded verbatim, so a board that already
 * holds one would keep RGB off forever and the new default would reach only
 * boards that had never booted. Version 4 did exactly this for INPUT (owner
 * decision 2026-08-09) and the shape is unchanged. */
#define ERA_SPLIT_SYNC_POLICY_STORAGE_VERSION 5
#define ERA_SPLIT_SYNC_POLICY_STORAGE_PREFIX_BYTES 10
#define ERA_SPLIT_SYNC_POLICY_STORAGE_COUNTER_OFFSET 10
#define ERA_SPLIT_SYNC_POLICY_STORAGE_COUNTER_BYTES 2
