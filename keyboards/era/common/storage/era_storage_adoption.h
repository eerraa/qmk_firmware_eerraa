// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "era_eeprom_layout.h"

/* The EEPROM geometry the ERA replacement-storage engine requires, supplied
   once so that a board adopting cross-half EEPROM sync does not discover it
   one _Static_assert at a time.
 *
 * Five preconditions. This file **supplies** the four that are values and
 * **names** the fifth, with the check that catches each. They are here rather
 * than in a board family's own header because a family header cannot be the
 * authority on something the common layer owns: a board outside that family
 * then has no way to find these values except by failing one assert at a time,
 * which is the discovery this file exists to replace.
 *
 * The checks below are named rather than numbered. Every one of them is a
 * _Static_assert in split/era_host_peer_storage.c and greps by its macro; the
 * line numbers that stood here pointed at struct members roughly forty lines
 * short, which is what a line number does to a file that keeps growing above
 * the thing it cites.
 *
 * Force-included through CONFIG_H by era_storage_adoption_rules.mk, so it
 * reaches every translation unit exactly as a board config.h does. It lands
 * *after* the board's own config.h in that list, which is what lets each value
 * below refuse a conflicting board definition by name instead of colliding
 * with it.
 *
 * 1. TOTAL_EEPROM_BYTE_COUNT == 24576 -- the logical EEPROM span the schema is
 *    laid out in. Supplied below. Checked by the _Static_assert on that macro
 *    in the schema block of split/era_host_peer_storage.c.
 * 2. DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE == 16 KiB exactly. Supplied below.
 *    QMK's own default is computed from whatever is left of the EEPROM span
 *    and does not land on this number, and the engine compares it against the
 *    shared core0/core1 image size rather than bounding it, so "large enough"
 *    is not the requirement -- equality is. Checked by the two
 *    DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE asserts in the same block, against the
 *    domain size and against ERA_HOST_PEER_STORAGE_IMAGE_BYTES.
 * 3. EECONFIG_SIZE == 37, which is what puts the ERA config block at 37 and
 *    everything downstream where the schema expects it. A board gets this by
 *    declaring **no** EECONFIG_KB_DATA_SIZE and no EECONFIG_USER_DATA_SIZE;
 *    either one moves the base. Refused below where this file can see it, and
 *    backstopped by the ERA_EEPROM_CONFIG_ADDR == 37U assert in
 *    era_host_peer_storage.c -- which is the one that still fires if a
 *    *keymap* config.h sets it, because a keymap's config.h is force-included
 *    after this file.
 * 4. VIA_EEPROM_MAGIC_ADDR == ERA_EEPROM_CONFIG_END, which is what puts the
 *    VIA layout options at 296 and the dynamic keymap at 297. Supplied below,
 *    together with the non-VIA build's equivalent. Checked by the
 *    VIA_EEPROM_LAYOUT_OPTIONS_ADDR == 296U and
 *    ERA_HOST_PEER_STORAGE_DYNAMIC_KEYMAP_ADDR == 297U asserts in that block --
 *    the two addresses the equality produces, rather than the equality itself.
 * 5. The board's own keyboard config struct must fit
 *    ERA_EEPROM_KEYBOARD_CONFIG_SIZE (8 bytes). This file cannot see that
 *    type, so it stays a _Static_assert beside the struct -- see
 *    sirind/common/tomak_era_keyboard_config.h for the shape to copy.
 *
 * An include and not a set of make variables, for the residency bundle's
 * reason (system/era_sram_resident_rules.mk): these fail as a set, and only C
 * can express VIA_EEPROM_MAGIC_ADDR, whose value is a macro from
 * era_eeprom_layout.h rather than a number. What make owns is the refusal --
 * era_split_qmk_rules.mk declines EEPROM sync on a board that has not taken
 * this bundle, before any compile and naming both files.
 */

/* --- 1. The logical EEPROM span ------------------------------------------ */
#define ERA_STORAGE_EEPROM_LOGICAL_SIZE (24 * 1024)
#if defined(EEPROM_SIZE)
#    error the ERA storage adoption bundle owns EEPROM_SIZE; remove the board definition rather than restating it (keyboards/era/common/storage/era_storage_adoption.h)
#endif
#define EEPROM_SIZE ERA_STORAGE_EEPROM_LOGICAL_SIZE

/* --- 2. The dynamic macro domain ----------------------------------------- */
#if defined(DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE)
#    error the ERA storage adoption bundle owns DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE; remove the board definition rather than restating it (keyboards/era/common/storage/era_storage_adoption.h)
#endif

#define ERA_STORAGE_DYNAMIC_MACRO_EEPROM_SIZE (16 * 1024)
#define DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE ERA_STORAGE_DYNAMIC_MACRO_EEPROM_SIZE

/* --- 3. The eeconfig base ------------------------------------------------ */
#if (EECONFIG_KB_DATA_SIZE > 0) || (EECONFIG_USER_DATA_SIZE > 0)
#    error an ERA storage board may declare no EECONFIG_KB_DATA_SIZE and no EECONFIG_USER_DATA_SIZE -- either moves EECONFIG_SIZE off 37 and the whole schema with it; put per-board persistent state in the ERA keyboard config block instead (ERA_EEPROM_KEYBOARD_CONFIG_OFFSET)
#endif

/* --- 4. Where VIA's region starts ---------------------------------------- */
#if defined(VIA_EEPROM_MAGIC_ADDR)
#    error the ERA storage adoption bundle owns VIA_EEPROM_MAGIC_ADDR; remove the board definition rather than restating it (keyboards/era/common/storage/era_storage_adoption.h)
#endif

/* ERA-owned EEPROM storage sits before the VIA and dynamic-keymap ranges. */
#define VIA_EEPROM_MAGIC_ADDR ERA_EEPROM_CONFIG_END

/* A build without VIA has no magic word to place the keymap after, so the
   keymap base is stated directly. Same boundary, different route to it: this
   is what keeps a non-VIA keymap from laying its keymap over the ERA config
   block. */
#if !defined(VIA_ENABLE)
#    if defined(DYNAMIC_KEYMAP_EEPROM_ADDR)
#        error the ERA storage adoption bundle owns DYNAMIC_KEYMAP_EEPROM_ADDR; remove the board definition rather than restating it (keyboards/era/common/storage/era_storage_adoption.h)
#    endif
#    define DYNAMIC_KEYMAP_EEPROM_ADDR ERA_EEPROM_CONFIG_END
#endif
