// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

/* The EEPROM backend and cooperative macro snapshot use one geometry.
 * Keep the existing override/default chain here; this header defines no
 * stored format of its own. */
#include "compiler_support.h"
#include "eeprom.h"
#include "dynamic_keymap.h"
#include "nvm_eeprom_eeconfig_internal.h"
#include "nvm_eeprom_via_internal.h"

#ifdef ENCODER_ENABLE
#    include "encoder.h"
#endif

#ifdef VIA_ENABLE
#    include "via.h"
#    define DYNAMIC_KEYMAP_EEPROM_START (VIA_EEPROM_CONFIG_END)
#else
#    define DYNAMIC_KEYMAP_EEPROM_START (EECONFIG_SIZE)
#endif

#ifndef DYNAMIC_KEYMAP_EEPROM_MAX_ADDR
#    define DYNAMIC_KEYMAP_EEPROM_MAX_ADDR (TOTAL_EEPROM_BYTE_COUNT - 1)
#endif

STATIC_ASSERT(DYNAMIC_KEYMAP_EEPROM_MAX_ADDR <= (TOTAL_EEPROM_BYTE_COUNT - 1), "DYNAMIC_KEYMAP_EEPROM_MAX_ADDR is configured to use more space than what is available for the selected EEPROM driver");

// Due to usage of uint16_t check for max 65535
STATIC_ASSERT(DYNAMIC_KEYMAP_EEPROM_MAX_ADDR <= 65535, "DYNAMIC_KEYMAP_EEPROM_MAX_ADDR must be less than 65536");

// If DYNAMIC_KEYMAP_EEPROM_ADDR not explicitly defined in config.h,
#ifndef DYNAMIC_KEYMAP_EEPROM_ADDR
#    define DYNAMIC_KEYMAP_EEPROM_ADDR DYNAMIC_KEYMAP_EEPROM_START
#endif

// Dynamic encoders starts after dynamic keymaps
#ifndef DYNAMIC_KEYMAP_ENCODER_EEPROM_ADDR
#    define DYNAMIC_KEYMAP_ENCODER_EEPROM_ADDR (DYNAMIC_KEYMAP_EEPROM_ADDR + (DYNAMIC_KEYMAP_LAYER_COUNT * MATRIX_ROWS * MATRIX_COLS * 2))
#endif

// Dynamic macro starts after dynamic encoders, but only when using ENCODER_MAP
#ifdef ENCODER_MAP_ENABLE
#    ifndef DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR
#        define DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR (DYNAMIC_KEYMAP_ENCODER_EEPROM_ADDR + (DYNAMIC_KEYMAP_LAYER_COUNT * NUM_ENCODERS * 2 * 2))
#    endif // DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR
#else      // ENCODER_MAP_ENABLE
#    ifndef DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR
#        define DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR (DYNAMIC_KEYMAP_ENCODER_EEPROM_ADDR)
#    endif // DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR
#endif     // ENCODER_MAP_ENABLE

// Sanity check that dynamic keymaps fit in available EEPROM
// If there's not 100 bytes available for macros, then something is wrong.
// The keyboard should override DYNAMIC_KEYMAP_LAYER_COUNT to reduce it,
// or DYNAMIC_KEYMAP_EEPROM_MAX_ADDR to increase it, *only if* the microcontroller has
// more than the default.
STATIC_ASSERT((int64_t)(DYNAMIC_KEYMAP_EEPROM_MAX_ADDR) - (int64_t)(DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR) >= 100, "Dynamic keymaps are configured to use more EEPROM than is available.");

#ifndef TOTAL_EEPROM_BYTE_COUNT
#    error Unknown total EEPROM size. Cannot derive maximum for dynamic keymaps.
#endif
// Dynamic macros are stored after the keymaps and use what is available
// up to and including DYNAMIC_KEYMAP_EEPROM_MAX_ADDR.
#ifndef DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE
#    define DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE (DYNAMIC_KEYMAP_EEPROM_MAX_ADDR - DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR + 1)
#endif
