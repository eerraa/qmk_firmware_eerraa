// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

/* One inverted bit in QMK keymap_config_t owns the user preference. Inverted
 * is intentional: QMK's eeconfig defaults zero the unused bits, so every
 * existing and CLEANed board starts with RGB Sleep enabled. */
bool era_rgb_sleep_enabled(void);

#ifdef VIA_ENABLE
bool era_rgb_sleep_handle_via_command(uint8_t *data, uint8_t length);
#endif
