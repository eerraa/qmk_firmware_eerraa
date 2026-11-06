// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Timing only. Each producer owns when its receipt starts and expires. */
static inline bool era_status_terminal_pulse(uint32_t elapsed_ms, bool *on) {
    *on = elapsed_ms < 640U;
    return elapsed_ms < 800U;
}
