// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdint.h>

uint32_t era_restart_test_get_core_num(void);
void     era_restart_test_dmb(void);
void     era_restart_test_sev(void);

#define get_core_num() era_restart_test_get_core_num()
#define __DMB() era_restart_test_dmb()
#define __SEV() era_restart_test_sev()
