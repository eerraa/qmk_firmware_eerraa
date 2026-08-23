// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "quantum.h"

/* The board-facing extension contract of the two class skeletons --
   era_nonsplit_board.c and split/era_split_board.c. A board that takes its
   class skeleton writes none of the QMK `_kb` hooks the skeleton owns; it
   overrides whichever of these it needs, and the skeleton calls them from
   inside those hooks.

   One set for both classes, deliberately. The strong bodies differ by class --
   a split board's task, init and process-record run through
   era_split_keyboard.c where a non-split board's run through
   era_common_features.c -- but the extension points are what a board author
   reads, and a hook declared for one class and silently never called on the
   other is exactly the shape this document layer keeps recording as a defect
   generator. **Every hook below is called by both skeletons**, and that is a
   checkable promise rather than a comment: the two points only the non-split
   class calls are in era_nonsplit_board.h, and the two only the split class
   calls are in split/era_split_board.h. A hook lands here by surviving that
   split.

   None of these is the whole of what a board may add. QMK's own weak hooks --
   rgb_matrix_indicators_kb, keyboard_post_init_kb, led_update_kb and the rest
   -- are untouched by either skeleton and a board defines them directly; a
   hook earns a place here only because the skeleton takes a *strong*
   definition of the QMK hook it would otherwise live in. */

/* Whatever the board needs on the housekeeping cadence, after the class's own
   task has run. Boards define the task; the skeletons call the tick, which
   forwards at most once per millisecond (era_board_hooks.c owns the gate and
   why). */
void era_board_housekeeping_task(void);
void era_board_housekeeping_tick(void);

/* Commit every quiet persistence gate that is holding an approved write, now,
   ignoring its timer -- and write nothing for a gate that holds none. Called
   from the two points where the pending window would otherwise be lost or
   poisoned: this unit's own `shutdown_kb()`, and the non-split suspend hook in
   `system/era_usb_session.c`. The definition carries which gates and why each
   of the two callers needs it. */
void era_board_persistence_flush_pending(void);

/* Board keycodes. Returns false to stop the record, the same contract
   process_record_kb itself has. The skeleton calls it after the ERA feature
   layer and before process_record_user(). */
bool era_board_process_record(uint16_t keycode, keyrecord_t *record);

#ifdef VIA_ENABLE
/* The board's own VIA keyboard-channel value table. `data` is
   [value_id, value_data...]. Return false for a value id this board does not
   own and the skeleton reports id_unhandled, which is the VIA protocol answer
   and is what makes the single dispatcher usable by every board. */
bool era_board_via_get_value(uint8_t *data);
bool era_board_via_set_value(uint8_t *data);

/* id_custom_save on the keyboard channel, after the common handler has
   persisted its own. The common handler returns false on that command on
   purpose so this one runs beside it. */
void era_board_via_save(void);

/* The keyboard channel's whole answer, which both class skeletons call once
   their own class router has declined: the common handler, then the three
   hooks above, then id_unhandled for a value id nobody owns and for every
   other channel.

   One copy, and the reason is history rather than tidiness. The two skeletons
   held this block character-identically, and the divergence it replaced was
   exactly this shape -- the tomak dispatcher let an unhandled value id fall
   out of its switch in silence while twelve non-split boards answered
   id_unhandled. id_unhandled is the VIA protocol answer; a second copy of the
   block is a second chance to stop giving it. */
void era_board_via_keyboard_channel_command(uint8_t *data, uint8_t length);
#endif
