// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

/* Seeds the VIA EEPROM magic (quantum/via.c). Left to the build clock, every
 * image built on a new day reset VIA's region -- keymaps, macros -- on its first
 * boot. Pinned, equal to H7S's, and left alone: a stored-format change moves
 * ERA_EEPROM_RESET_KEY instead. Force-included into every ERA target by
 * system/era_build_variant_rules.mk; the generated version.h yields to it. */
#define QMK_BUILDDATE "2024-04-23-11:29:54"
