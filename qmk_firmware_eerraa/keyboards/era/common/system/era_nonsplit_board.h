// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "era_board_hooks.h"

/* Two extension points the **non-split** class skeleton adds to the
   class-neutral set in era_board_hooks.h, which it also calls in full.

   They are here, class-named and reachable only from a non-split board's own
   content, for the reason the neutral header states: a hook declared for one
   class and silently never called on the other is a defect generator. The
   split skeleton calls neither, because it does not own the init trio -- on
   the only split family in the tree the ERA module init runs *conditionally*
   inside matrix_init_kb, and a class skeleton calling it unconditionally would
   add an EEPROM re-read on every boot (the reasoning is at the top of
   split/era_split_board.c). A split board loads its own config from the family
   unit's own matrix_init_kb instead.

   This mirrors split/era_split_board.h, which holds the two points only the
   split class calls. Between them the neutral header keeps exactly the hooks
   both classes really do call, which is what makes its promise checkable. */

/* Load this board's own persistent config into RAM, repairing an invalid
   stored copy in place. Called before era_common_features_init() in every init
   path the non-split skeleton owns, because a board's config may decide what
   the feature layer then reads. */
void era_board_config_load(void);

/* Reset this board's own persistent config to defaults and persist it. Called
   from eeconfig_init_kb only, in the position era_board_config_load() takes in
   the other init paths. */
void era_board_config_reset(void);
