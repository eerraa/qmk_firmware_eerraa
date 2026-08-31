// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdint.h>

typedef struct {
    uint32_t timerawl;
} timer_hw_t;

extern timer_hw_t *timer_hw;
