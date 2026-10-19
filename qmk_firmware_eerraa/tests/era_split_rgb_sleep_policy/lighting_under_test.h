// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdbool.h>

void era_test_lighting_reset(void);
void era_test_lighting_set_owner(bool wire_owned);
void era_test_lighting_set_local_loss(bool lost);
void era_test_lighting_rotate_relation(void);
bool era_test_lighting_publish(bool sleep);
bool era_test_lighting_resolve(void);
