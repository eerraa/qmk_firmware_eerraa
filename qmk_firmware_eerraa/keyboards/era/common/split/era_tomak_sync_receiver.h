// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "era_tomak_sync_wire.h"

/* Retained shared-record binding, not a Core0 lifecycle owner.
 * It contains both endpoint channels, not equal numeric epochs. */
typedef struct {
    uint16_t local_owner;
    uint16_t local_relation;
    uint16_t peer_owner;
    uint16_t peer_relation;
    uint16_t peer_usb_epoch;
    uint16_t peer_host_open;
    uint16_t peer_host_close;
} era_tomak_sync_binding_t;
