// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#include "split_common/transport.h"

#include "era_split_transport_scheduler.h"

/* SPLIT_TRANSPORT = custom's whole live surface, and it is these two names:
   split_util.c calls whichever one is_keyboard_master() selects -
   transport_master_init() from split_pre_init(), transport_slave_init() from
   split_post_init(). On ERA that selection is deliberately inert. Nothing
   samples the reducer between keyboard_pre_init_kb and the launch step, so
   is_keyboard_master() answers false throughout keyboard_init() whatever USB
   is doing, transport_slave_init() runs on both halves every boot, and the
   boot path is identical with or without a cable. Both names reach the same
   planner init, and neither opens the wire - the launch stays the one named
   step, era_split_transport_scheduler_start_communication_core()
   (era_invariants.md).

   The row-array transport_master()/transport_slave() pair and
   transport_execute_transaction() are deliberately absent: their only callers
   are quantum/matrix.c and quantum/split_common/transactions.c, and
   CUSTOM_MATRIX=yes / SPLIT_TRANSPORT=custom compile neither into any ERA
   image. Re-adding either caller to a build makes the link fail here by name
   instead of silently pulling in a stub. */
void transport_master_init(void) {
    era_split_transport_scheduler_init();
}

void transport_slave_init(void) {
    era_split_transport_scheduler_init();
}
