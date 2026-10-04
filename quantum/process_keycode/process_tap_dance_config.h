// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

/* Capacity shared by the TD runtime and its output-owner registration.
 * Keep this header free of action/runtime types. */
#ifdef ERA_TAP_DANCE_ENABLE
/* VIA may map the same configuration slot to every physical switch. Keep a
 * direct seat per matrix position and the existing bounded non-matrix pool. */
#    define TAP_DANCE_MATRIX_STATES (MATRIX_ROWS * MATRIX_COLS)
#endif
#ifndef TAP_DANCE_MAX_SIMULTANEOUS
#    ifdef ERA_TAP_DANCE_ENABLE
#        define TAP_DANCE_MAX_SIMULTANEOUS (TAP_DANCE_MATRIX_STATES + 8)
#    else
#        define TAP_DANCE_MAX_SIMULTANEOUS 3
#    endif
#endif
