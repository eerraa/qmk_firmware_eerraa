// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdint.h>

typedef struct {
    volatile uint32_t timerawl;
} era_restart_test_timer_hw_t;

extern era_restart_test_timer_hw_t *timer_hw;
