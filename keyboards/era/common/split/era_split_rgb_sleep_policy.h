// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

static inline bool era_split_rgb_sleep_policy_local_requested(bool explicit_suspend,
                                                               bool frames_lost,
                                                               bool rgb_sleep_enabled,
                                                               uint16_t timeout_seconds,
                                                               uint32_t matrix_idle_ms) {
    bool idle_timeout = timeout_seconds != 0 && matrix_idle_ms >= (uint32_t)timeout_seconds * 1000U;
    return rgb_sleep_enabled && (explicit_suspend || frames_lost || idle_timeout);
}

static inline bool era_split_rgb_sleep_policy_preset_valid(uint8_t minutes) {
    return minutes == 1 || minutes == 3 || minutes == 5 || minutes == 10 || minutes == 30 || minutes == 60;
}

/* Official-VIA GET is a projection only. An exact value between presets floors
   to the nearest supported minute choice, while values outside the official
   menu clamp to its nearest endpoint. GET never mutates the exact store. */
static inline uint8_t era_split_rgb_sleep_policy_preset_minutes(uint16_t seconds) {
    static const uint8_t presets[] = {1, 3, 5, 10, 30, 60};
    uint8_t result = presets[0];
    for (uint8_t i = 0; i < sizeof(presets); i++) {
        if (seconds < (uint16_t)presets[i] * 60U) {
            break;
        }
        result = presets[i];
    }
    return result;
}
