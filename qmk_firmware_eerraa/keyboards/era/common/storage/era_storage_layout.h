// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

/* ERA-owned view of the portable QMK/VIA storage geometry. QMK's private NVM
 * headers must not be an ERA notification/layout API; both the custom EEPROM
 * adapter and split storage derive their domains from these public geometry
 * inputs instead. */

#include "dynamic_keymap.h"
#include "eeconfig.h"
#include "matrix.h"
#include "via.h"

#include "era_eeprom_layout.h"

/* Stock QMK eeconfig offsets frozen by the ERA portable-storage schema. A
 * C-only tripwire in era_host_peer_storage.c compares these with QMK's private
 * layout definitions; runtime ERA code uses these assembler-safe constants and
 * therefore does not need QMK private NVM headers as an API. */
#define ERA_STORAGE_EECONFIG_MAGIC_ADDR 0U
#define ERA_STORAGE_EECONFIG_DEFAULT_LAYER_ADDR 3U
#define ERA_STORAGE_EECONFIG_KEYMAP_ADDR 4U
#define ERA_STORAGE_EECONFIG_RGB_MATRIX_ADDR 23U

#define ERA_STORAGE_VIA_MAGIC_ADDR ERA_EEPROM_CONFIG_END
#define ERA_STORAGE_VIA_LAYOUT_OPTIONS_ADDR (ERA_STORAGE_VIA_MAGIC_ADDR + 3U)
#define ERA_STORAGE_DYNAMIC_KEYMAP_ADDR (ERA_STORAGE_VIA_LAYOUT_OPTIONS_ADDR + VIA_EEPROM_LAYOUT_OPTIONS_SIZE + VIA_EEPROM_CUSTOM_CONFIG_SIZE)
#define ERA_STORAGE_DYNAMIC_KEYMAP_BYTES (DYNAMIC_KEYMAP_LAYER_COUNT * MATRIX_ROWS * MATRIX_COLS * 2U)

/* No ERA storage-adoption board reserves a dynamic encoder map. If that changes,
 * this formula and the wire schema must change together; the split storage
 * static assertions are the compile-time tripwire. */
#define ERA_STORAGE_DYNAMIC_MACRO_ADDR (ERA_STORAGE_DYNAMIC_KEYMAP_ADDR + ERA_STORAGE_DYNAMIC_KEYMAP_BYTES)

/* Per-macro playback options (system/era_macro_options.c), placed in logical
 * EEPROM that no earlier image wrote: directly after the 16-KiB macro domain,
 * outside every portable sync domain. An 8-byte header -- signature LE32,
 * version, record count, two zero bytes -- then one [flags, hold_ms LE16]
 * record per macro. The macro bytes themselves are unchanged. */
#define ERA_STORAGE_MACRO_OPTIONS_ADDR (ERA_STORAGE_DYNAMIC_MACRO_ADDR + DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE)
#define ERA_STORAGE_MACRO_OPTIONS_SIGNATURE 0x4F4D5245UL /* "ERMO" */
#define ERA_STORAGE_MACRO_OPTIONS_VERSION 1U
#define ERA_STORAGE_MACRO_OPTIONS_HEADER_BYTES 8U
#define ERA_STORAGE_MACRO_OPTIONS_RECORD_BYTES 3U
#define ERA_STORAGE_MACRO_OPTIONS_BYTES (ERA_STORAGE_MACRO_OPTIONS_HEADER_BYTES + DYNAMIC_KEYMAP_MACRO_COUNT * ERA_STORAGE_MACRO_OPTIONS_RECORD_BYTES)
