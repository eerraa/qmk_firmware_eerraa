// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Per-macro playback options on the ERA Custom Values channel MACRO_OPTIONS,
 * built with the storage-adoption geometry that holds their table
 * (storage/era_storage_layout.h). The executor's accessors and the reset that
 * VIA's storage initializer calls are declared in quantum/era_macro.h, the core
 * seam that uses them. */

/* Load the stored table into RAM; any unsaved SET is discarded. */
void era_macro_options_init(void);
bool era_macro_options_handle_via(uint8_t *data, uint8_t length);
