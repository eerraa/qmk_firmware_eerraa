// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>

typedef struct { volatile uint32_t timerawl; } era_test_timer_registers_t;
extern era_test_timer_registers_t era_test_timer_registers;
#define timer_hw (&era_test_timer_registers)
