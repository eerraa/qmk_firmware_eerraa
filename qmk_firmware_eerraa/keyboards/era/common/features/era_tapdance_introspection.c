// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_tapdance.h"

#if defined(ERA_TAP_DANCE_ENABLE)

tap_dance_action_t tap_dance_actions[ERA_TAP_DANCE_SLOT_COUNT] = {
    [0] = ERA_TAP_DANCE_ACTION(0),
    [1] = ERA_TAP_DANCE_ACTION(1),
    [2] = ERA_TAP_DANCE_ACTION(2),
    [3] = ERA_TAP_DANCE_ACTION(3),
    [4] = ERA_TAP_DANCE_ACTION(4),
    [5] = ERA_TAP_DANCE_ACTION(5),
    [6] = ERA_TAP_DANCE_ACTION(6),
    [7] = ERA_TAP_DANCE_ACTION(7),
};

#endif
