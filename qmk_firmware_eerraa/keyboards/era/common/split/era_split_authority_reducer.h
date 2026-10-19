// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "usb_device_state.h"

/* The window inside which the reducer still calls a stamped SOF count fresh,
   and therefore the local host present. It lives in the header rather than in
   the reducer body because the scheduler's authority sample period is derived
   from it: the sample is the one poll in this architecture, and the period it
   polls at is the longest interval that cannot step over this window
   (era_authority_contract.md). Before Slice 11.6 the period was an independent
   5 with no recorded derivation from the 10 beside it. */
#ifndef ERA_SPLIT_AUTHORITY_SOF_FRESH_MS
#    define ERA_SPLIT_AUTHORITY_SOF_FRESH_MS 10
#endif

typedef enum {
    ERA_SPLIT_AUTHORITY_SIDE_UNKNOWN = 0,
    ERA_SPLIT_AUTHORITY_SIDE_LEFT    = 1,
    ERA_SPLIT_AUTHORITY_SIDE_RIGHT   = 2,
} era_split_authority_side_t;

typedef enum {
    ERA_AUTH_USB_UNKNOWN = 0,
    ERA_AUTH_USB_HOST_OPEN,
    ERA_AUTH_USB_NO_HOST,
} era_auth_usb_state_t;

typedef struct {
    bool                 valid;
    bool                 is_left;
    era_auth_usb_state_t usb_state;
    uint16_t             usb_epoch;
} era_authority_snapshot_t;

void era_split_authority_reducer_init(void);
bool era_split_authority_reducer_task(void);
void era_split_authority_reducer_get_snapshot(era_authority_snapshot_t *snapshot);
