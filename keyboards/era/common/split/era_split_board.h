// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "quantum.h"
#include "../system/era_board_hooks.h"

/* Split-only extension points the **split** class skeleton adds to the class-neutral
   set in system/era_board_hooks.h, which it also calls in full.

   They are here, split-named and under split/, rather than in the neutral
   header, and that is a decision rather than filing. The neutral set is one
   contract both classes call, precisely so a board cannot override a hook that
   is silently never reached. The pre/post-init pair cannot join it: a split board's content
   sits *between* keyboard_post_init_user() and era_split_keyboard_post_init()
   -- before the wire opens and the core1 launch step runs -- while the only
   non-split board with post-init content needs to run *before*
   keyboard_post_init_user(). One shared hook could not hold both positions
   without moving one board's code, which this session does not do.

   Putting them in a split-only header under a split name is what keeps that
   from becoming the silent-no-op hazard: a non-split board never sees the
   declaration, so there is nothing for it to override and nothing to be
   silently skipped. The non-split skeleton therefore takes neither
   keyboard_pre_init_kb nor keyboard_post_init_kb, and a non-split board keeps
   defining those QMK weak hooks itself. */

/* Board content that must run before era_split_keyboard_pre_init(), which is
   where USB identity and the authority reducer come up. */
void era_split_board_pre_init(void);

/* Board content that must run after keyboard_post_init_user() and before
   era_split_keyboard_post_init(), which is the single named step that opens
   the wire and launches core1 (era_invariants.md). Anything touching the wire
   pins belongs here and not after. */
void era_split_board_post_init(void);

/* Optional board-owned RGB idle-sleep setting. Zero means this split board has
   no idle-timeout feature. The split class consumes only the scalar; storage
   layout and migration remain the board/family owner's responsibility. */
uint16_t era_split_board_rgb_sleep_timeout_seconds(void);
bool     era_split_board_set_rgb_sleep_timeout_seconds(uint16_t seconds);
