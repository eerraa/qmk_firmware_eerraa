// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#include "era_split_sync_policy.h"

#include <stddef.h>
#include <string.h>

#include "atomic_util.h"
#include "era_split_scheduler_events.h"
#include "era_split_sync_storage.h"
#include "../storage/era_eeprom_storage.h"

/* One requested bit per family, relation-independent (Slice 9.5). */
#define ERA_SPLIT_SYNC_POLICY_FLAG_EEPROM 0x01
#define ERA_SPLIT_SYNC_POLICY_FLAG_INPUT 0x02
#define ERA_SPLIT_SYNC_POLICY_FLAG_RGB 0x04
#define ERA_SPLIT_SYNC_POLICY_FLAG_MASK 0x07
#define ERA_SPLIT_SYNC_POLICY_DIRTY_ALL_MASK ((uint8_t)((1U << ERA_SPLIT_SYNC_POLICY_FIELD_COUNT) - 1U))
/* All three families default on (owner decision 2026-08-13; RGB was the last
 * holdout and it turned on storage version 5). The defaults are a statement
 * about what the pair IS, not a preference set: a split keyboard is one
 * keyboard, and the three families are the three ways that fact has to reach
 * the second half. EEPROM and INPUT already said so. RGB's default-off rested
 * on "a lighting preference is a preference rather than a correctness fix",
 * which is true of the bit and was false of the shipped result -- a pair whose
 * halves render independently is two keyboards that happen to be cabled
 * together, and the S1 sitting of 2026-08-13 spent thirty-three minutes
 * measuring a DUAL-HOST RGB family that was never armed and produced no reading
 * of it at all. */
#define ERA_SPLIT_SYNC_POLICY_DEFAULT_FLAGS (ERA_SPLIT_SYNC_POLICY_FLAG_EEPROM | ERA_SPLIT_SYNC_POLICY_FLAG_INPUT | ERA_SPLIT_SYNC_POLICY_FLAG_RGB)
_Static_assert(ERA_SPLIT_SYNC_POLICY_DEFAULT_FLAGS == ERA_SPLIT_SYNC_POLICY_FLAG_MASK, "Every sync family defaults on (owner decision 2026-08-13).");

_Static_assert(ERA_SPLIT_SYNC_POLICY_FLAG_EEPROM == (1U << ERA_SPLIT_SYNC_POLICY_FIELD_EEPROM), "EEPROM policy flag must map to dirty bit 0.");
_Static_assert(ERA_SPLIT_SYNC_POLICY_FLAG_INPUT == (1U << ERA_SPLIT_SYNC_POLICY_FIELD_INPUT), "INPUT policy flag must map to dirty bit 1.");
_Static_assert(ERA_SPLIT_SYNC_POLICY_FLAG_RGB == (1U << ERA_SPLIT_SYNC_POLICY_FIELD_RGB), "RGB policy flag must map to dirty bit 2.");
_Static_assert(ERA_SPLIT_SYNC_POLICY_FLAG_MASK == ERA_SPLIT_SYNC_POLICY_DIRTY_ALL_MASK, "Sync policy flag mask must equal the dirty mask.");

typedef struct {
    uint32_t signature;
    uint16_t flags_generation;
    uint16_t eeprom_policy_generation;
    uint8_t  version;
    uint8_t  flags;
    /* Storage-engine territory, opaque here: the Slice 10 per-domain
     * divergence counters. This module never interprets or persists these
     * bytes outside the one fresh-block write that zeroes them. */
    uint8_t  recency_counters[14];
} era_split_sync_policy_storage_t;

_Static_assert(sizeof(era_split_sync_policy_storage_t) == ERA_SPLIT_EEPROM_SYNC_POLICY_CONFIG_SIZE, "ERA split sync policy storage block size changed.");
_Static_assert(offsetof(era_split_sync_policy_storage_t, signature) == 0, "ERA split sync policy signature offset changed.");
_Static_assert(offsetof(era_split_sync_policy_storage_t, flags_generation) == 4, "ERA split sync policy flags generation offset changed.");
_Static_assert(offsetof(era_split_sync_policy_storage_t, eeprom_policy_generation) == 6, "ERA split sync policy EEPROM policy generation offset changed.");
_Static_assert(offsetof(era_split_sync_policy_storage_t, version) == 8, "ERA split sync policy version offset changed.");
_Static_assert(offsetof(era_split_sync_policy_storage_t, flags) == 9, "ERA split sync policy flags offset changed.");
_Static_assert(offsetof(era_split_sync_policy_storage_t, recency_counters) == ERA_SPLIT_SYNC_POLICY_STORAGE_COUNTER_OFFSET, "ERA split sync policy counter offset changed.");
_Static_assert(offsetof(era_split_sync_policy_storage_t, recency_counters) == ERA_SPLIT_SYNC_POLICY_STORAGE_PREFIX_BYTES, "ERA split sync policy prefix must end where the counters start.");

static struct {
    uint16_t flags_generation;
    uint16_t eeprom_policy_generation;
    uint8_t  flags;
    uint8_t  dirty_flags;
    bool     initialized;
} era_split_sync_policy_state;

static uint8_t era_split_sync_policy_field_bit(era_split_sync_policy_field_t field) {
    return (uint8_t)(1U << (uint8_t)field);
}

/* Keep this cold helper out-of-line so wrap handling is not duplicated ahead of
 * the scan-bound XIP transport objects. */
static uint16_t __attribute__((noinline)) era_split_sync_policy_next_generation(uint16_t generation) {
    generation++;
    return generation == 0 ? 1 : generation;
}

static void era_split_sync_policy_mark_scheduler_dirty_if_generation_changed(uint16_t previous_generation, uint16_t current_generation) {
    if (previous_generation != current_generation) {
        era_split_transport_scheduler_mark_dirty(ERA_SPLIT_SCHEDULER_DIRTY_SYNC_POLICY);
    }
}

static uint8_t era_split_sync_policy_normalize_flags(uint8_t flags) {
    return flags & ERA_SPLIT_SYNC_POLICY_FLAG_MASK;
}

static void era_split_sync_policy_set_defaults_locked(bool mark_dirty) {
    era_split_sync_policy_state.flags_generation =
        era_split_sync_policy_state.flags_generation != 0
            ? era_split_sync_policy_next_generation(era_split_sync_policy_state.flags_generation)
            : 1;
    era_split_sync_policy_state.eeprom_policy_generation =
        era_split_sync_policy_state.eeprom_policy_generation != 0
            ? era_split_sync_policy_next_generation(era_split_sync_policy_state.eeprom_policy_generation)
            : 1;
    era_split_sync_policy_state.flags = ERA_SPLIT_SYNC_POLICY_DEFAULT_FLAGS;
    if (mark_dirty) {
        era_split_sync_policy_state.dirty_flags |= ERA_SPLIT_SYNC_POLICY_DIRTY_ALL_MASK;
    }
    era_split_sync_policy_state.initialized = true;
}

static void era_split_sync_policy_ensure_initialized_locked(void) {
    if (era_split_sync_policy_state.initialized) {
        return;
    }
    era_split_sync_policy_set_defaults_locked(true);
}

static bool era_split_sync_policy_set_flags_locked(uint8_t flags) {
    era_split_sync_policy_ensure_initialized_locked();

    uint8_t normalized = era_split_sync_policy_normalize_flags(flags);
    if (era_split_sync_policy_state.flags == normalized) {
        return false;
    }

    uint8_t changed = (uint8_t)(era_split_sync_policy_state.flags ^ normalized);
    era_split_sync_policy_state.dirty_flags |= changed;

    era_split_sync_policy_state.flags = normalized;
    era_split_sync_policy_state.flags_generation =
        era_split_sync_policy_next_generation(era_split_sync_policy_state.flags_generation);
    if ((changed & ERA_SPLIT_SYNC_POLICY_FLAG_EEPROM) != 0) {
        era_split_sync_policy_state.eeprom_policy_generation =
            era_split_sync_policy_next_generation(era_split_sync_policy_state.eeprom_policy_generation);
    }
    return true;
}

static void era_split_sync_policy_build_storage_locked(era_split_sync_policy_storage_t *storage) {
    memset(storage, 0, sizeof(*storage));
    storage->signature        = ERA_SPLIT_SYNC_POLICY_STORAGE_SIGNATURE;
    storage->flags_generation = era_split_sync_policy_state.flags_generation != 0
                                    ? era_split_sync_policy_state.flags_generation
                                    : 1;
    storage->eeprom_policy_generation = era_split_sync_policy_state.eeprom_policy_generation != 0
                                            ? era_split_sync_policy_state.eeprom_policy_generation
                                            : 1;
    storage->version = ERA_SPLIT_SYNC_POLICY_STORAGE_VERSION;
    storage->flags   = era_split_sync_policy_normalize_flags(era_split_sync_policy_state.flags);
}

static bool era_split_sync_policy_storage_prefix_is_valid(const era_split_sync_policy_storage_t *storage) {
    return storage != NULL &&
           storage->signature == ERA_SPLIT_SYNC_POLICY_STORAGE_SIGNATURE &&
           (storage->flags & ~ERA_SPLIT_SYNC_POLICY_FLAG_MASK) == 0 &&
           (storage->flags_generation != 0) &&
           (storage->eeprom_policy_generation != 0);
}

static bool era_split_sync_policy_storage_is_valid(const era_split_sync_policy_storage_t *storage) {
    /* The storage engine's divergence counters sit behind the prefix, so their
     * value carries no validity meaning here. */
    return era_split_sync_policy_storage_prefix_is_valid(storage) &&
           storage->version == ERA_SPLIT_SYNC_POLICY_STORAGE_VERSION;
}

/* Ordinary persists write the policy-owned prefix only. The counter bytes
 * behind it belong to the storage recency layer, and writing the whole block
 * from this module would zero them on every policy toggle. */
static void era_split_sync_policy_persist_current(void) {
    era_split_sync_policy_storage_t storage;
    ATOMIC_BLOCK_RESTORESTATE {
        era_split_sync_policy_ensure_initialized_locked();
        era_split_sync_policy_build_storage_locked(&storage);
    }
    era_eeprom_update_config(&storage, ERA_SPLIT_EEPROM_SYNC_POLICY_CONFIG_OFFSET, ERA_SPLIT_SYNC_POLICY_STORAGE_PREFIX_BYTES);
}

/* Fresh-block persist: only for a block whose signature/prefix did not
 * validate. Garbage counters must not survive under a freshly written
 * signature, so this is the one write that covers the whole block. */
static void era_split_sync_policy_persist_current_full(void) {
    era_split_sync_policy_storage_t storage;
    ATOMIC_BLOCK_RESTORESTATE {
        era_split_sync_policy_ensure_initialized_locked();
        era_split_sync_policy_build_storage_locked(&storage);
    }
    era_eeprom_update_config(&storage, ERA_SPLIT_EEPROM_SYNC_POLICY_CONFIG_OFFSET, sizeof(storage));
}

static void era_split_sync_policy_load_storage_locked(const era_split_sync_policy_storage_t *storage) {
    era_split_sync_policy_state.flags_generation = storage->flags_generation != 0
                                                       ? storage->flags_generation
                                                       : 1;
    era_split_sync_policy_state.eeprom_policy_generation = storage->eeprom_policy_generation != 0
                                                               ? storage->eeprom_policy_generation
                                                               : 1;
    era_split_sync_policy_state.flags       = era_split_sync_policy_normalize_flags(storage->flags);
    era_split_sync_policy_state.dirty_flags = 0;
    era_split_sync_policy_state.initialized = true;
}

void era_split_sync_policy_init(void) {
    era_split_sync_policy_storage_t storage;
    bool storage_read  = era_eeprom_read_config(&storage, ERA_SPLIT_EEPROM_SYNC_POLICY_CONFIG_OFFSET, sizeof(storage)) == sizeof(storage);
    bool storage_valid = storage_read && era_split_sync_policy_storage_is_valid(&storage);

    ATOMIC_BLOCK_RESTORESTATE {
        if (storage_valid) {
            era_split_sync_policy_load_storage_locked(&storage);
        } else {
            memset(&era_split_sync_policy_state, 0, sizeof(era_split_sync_policy_state));
            era_split_sync_policy_set_defaults_locked(true);
        }
    }

    if (!storage_valid) {
        /* Anything that is not this exact block -- erased, garbage, or written
         * by a firmware with a different layout -- is replaced whole. The full
         * persist is what makes that a reset rather than a reinterpretation:
         * it rewrites the counter bytes too, so no field of the old block
         * survives under the new signature. */
        era_split_sync_policy_persist_current_full();
    }
}

void era_split_sync_policy_reload_from_eeprom(void) {
    era_split_sync_policy_storage_t storage;
    bool storage_read  = era_eeprom_read_config(&storage, ERA_SPLIT_EEPROM_SYNC_POLICY_CONFIG_OFFSET, sizeof(storage)) == sizeof(storage);
    bool storage_valid = storage_read && era_split_sync_policy_storage_is_valid(&storage);

    uint16_t previous_generation = 0;
    uint16_t current_generation  = 0;
    ATOMIC_BLOCK_RESTORESTATE {
        previous_generation = era_split_sync_policy_state.flags_generation;
        if (storage_valid) {
            era_split_sync_policy_load_storage_locked(&storage);
        } else {
            memset(&era_split_sync_policy_state, 0, sizeof(era_split_sync_policy_state));
            era_split_sync_policy_set_defaults_locked(false);
        }
        current_generation = era_split_sync_policy_state.flags_generation;
    }
    era_split_sync_policy_mark_scheduler_dirty_if_generation_changed(previous_generation, current_generation);
}

void era_split_sync_policy_reset_to_defaults(void) {
    uint16_t previous_generation = 0;
    uint16_t current_generation  = 0;
    ATOMIC_BLOCK_RESTORESTATE {
        era_split_sync_policy_ensure_initialized_locked();
        previous_generation = era_split_sync_policy_state.flags_generation;
        era_split_sync_policy_set_defaults_locked(true);
        current_generation = era_split_sync_policy_state.flags_generation;
    }
    era_split_sync_policy_persist_current();
    era_split_sync_policy_mark_scheduler_dirty_if_generation_changed(previous_generation, current_generation);
}

bool era_split_sync_policy_set_requested(era_split_sync_policy_field_t field, bool requested) {
    if (field >= ERA_SPLIT_SYNC_POLICY_FIELD_COUNT) {
        return false;
    }

    bool changed = false;
    ATOMIC_BLOCK_RESTORESTATE {
        era_split_sync_policy_ensure_initialized_locked();

        uint8_t flags = era_split_sync_policy_state.flags;
        uint8_t bit   = era_split_sync_policy_field_bit(field);
        if (requested) {
            flags |= bit;
        } else {
            flags &= (uint8_t)~bit;
        }
        changed = era_split_sync_policy_set_flags_locked(flags);
    }

    if (changed) {
        era_split_sync_policy_persist_current();
        era_split_transport_scheduler_mark_dirty(ERA_SPLIT_SCHEDULER_DIRTY_SYNC_POLICY);
    }
    return true;
}

bool era_split_sync_policy_get_requested(era_split_sync_policy_field_t field, bool *requested) {
    if (field >= ERA_SPLIT_SYNC_POLICY_FIELD_COUNT || requested == NULL) {
        return false;
    }

    ATOMIC_BLOCK_RESTORESTATE {
        era_split_sync_policy_ensure_initialized_locked();
        uint8_t flags = era_split_sync_policy_state.flags;
        *requested = (flags & era_split_sync_policy_field_bit(field)) != 0;
    }
    return true;
}

void era_split_sync_policy_get_snapshot(era_split_sync_policy_snapshot_t *snapshot) {
    if (!snapshot) {
        return;
    }

    memset(snapshot, 0, sizeof(*snapshot));
    ATOMIC_BLOCK_RESTORESTATE {
        era_split_sync_policy_ensure_initialized_locked();
        snapshot->eeprom_policy_generation = era_split_sync_policy_state.eeprom_policy_generation;
        snapshot->dirty_flags              = era_split_sync_policy_state.dirty_flags;
        for (uint8_t i = 0; i < ERA_SPLIT_SYNC_POLICY_FIELD_COUNT; i++) {
            snapshot->requested[i] = (era_split_sync_policy_state.flags & era_split_sync_policy_field_bit((era_split_sync_policy_field_t)i)) != 0 ? 1 : 0;
        }
        snapshot->heartbeat = (uint8_t)era_split_sync_policy_state.flags_generation;
    }
}
