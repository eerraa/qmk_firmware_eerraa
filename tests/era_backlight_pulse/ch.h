// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct virtual_timer virtual_timer_t;
typedef void (*vtfunc_t)(virtual_timer_t *vtp, void *p);

struct virtual_timer {
    bool     armed;
    uint32_t interval;
    vtfunc_t callback;
    void    *arg;
};

void era_backlight_test_note_timer(virtual_timer_t *vtp);

#define TIME_MS2I(ms) ((uint32_t)(ms))

static inline void chSysLock(void) {}
static inline void chSysUnlock(void) {}

static inline void chVTObjectInit(virtual_timer_t *vtp) {
    vtp->armed    = false;
    vtp->interval = 0;
    vtp->callback = 0;
    vtp->arg      = 0;
    era_backlight_test_note_timer(vtp);
}

static inline void chVTReset(virtual_timer_t *vtp) {
    vtp->armed    = false;
    vtp->interval = 0;
    vtp->callback = 0;
    vtp->arg      = 0;
}

static inline void chVTSet(virtual_timer_t *vtp, uint32_t interval, vtfunc_t callback, void *arg) {
    vtp->armed    = true;
    vtp->interval = interval;
    vtp->callback = callback;
    vtp->arg      = arg;
    era_backlight_test_note_timer(vtp);
}

static inline void chVTSetI(virtual_timer_t *vtp, uint32_t interval, vtfunc_t callback, void *arg) {
    chVTSet(vtp, interval, callback, arg);
}
