// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>

void era_test_usb_sampler_reset(bool configured);
bool era_test_usb_sampler_observe(uint32_t now_us, uint16_t count, bool isr_owned, bool pending, uint32_t *age_ms);
uint16_t era_test_usb_sampler_observed_count(void);
