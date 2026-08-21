// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "quantum.h"
#include "era_board_hooks.h"
#ifdef VIA_ENABLE
#    include "era_common_via.h"
#endif

#if defined(MCU_RP)
#    include "hardware/structs/timer.h" /* timer_hw->timerawl, the tick gate's raw-microsecond clock */
#endif

/* The weak defaults behind era_board_hooks.h, in one unit rather than one copy
   per class skeleton: two skeletons that share a header and duplicate the same
   bodies is the under-split shape era_source_map.md's Source Editing Rules
   name, and it is also how a hook comes to exist on one class only. Exactly
   one skeleton is ever linked, so this unit travels with whichever one the
   board selected. */

__attribute__((weak)) void era_board_housekeeping_task(void) {}

/* The class skeletons call this, once per keyboard pass, and it forwards to
   the board hook once per millisecond. Everything a board hangs off this
   cadence is millisecond-scale by contract — deferred config saves, launch
   signal blink phases, the EEPROM SYNC lamp's visible advance — while the
   caller runs at the scan rate, tens of passes per millisecond. Re-asking
   millisecond questions every pass was a measured piece of the 2026-08-15
   scan-rate regression (qwin bisect), so the gate is one raw counter read
   here rather than a discipline every board must remember. On a platform
   without the raw counter the tick degrades to the old every-pass call. */
void era_board_housekeeping_tick(void) {
#if defined(MCU_RP)
    static uint32_t era_board_housekeeping_last_us;
    uint32_t        now_us = timer_hw->timerawl;
    if ((uint32_t)(now_us - era_board_housekeeping_last_us) < 1000) {
        return;
    }
    era_board_housekeeping_last_us = now_us;
#endif
    era_board_housekeeping_task();
}

__attribute__((weak)) bool era_board_process_record(uint16_t keycode, keyrecord_t *record) {
    (void)keycode;
    (void)record;
    return true;
}

#ifdef VIA_ENABLE
__attribute__((weak)) bool era_board_via_get_value(uint8_t *data) {
    (void)data;
    return false;
}

__attribute__((weak)) bool era_board_via_set_value(uint8_t *data) {
    (void)data;
    return false;
}

__attribute__((weak)) void era_board_via_save(void) {}

/* The keyboard channel's board half, called by whichever class skeleton is
   linked once its own router has declined. It is here rather than in
   era_common_via.c because what it dispatches is the era_board_hooks.h table
   above, and here rather than in either skeleton because both held it and the
   header states what a second copy costs.

   It is also **the one writer of id_unhandled** in the ERA VIA surface. Both
   skeletons call it unconditionally as their last statement, so every command
   no handler claimed arrives here, and every path out of it that answered
   nothing writes the byte. The rule the rest of the chain keeps so that this
   stays true is stated at era_common_via.c's router. */
void era_board_via_keyboard_channel_command(uint8_t *data, uint8_t length) {
    uint8_t *command_id        = &data[0];
    uint8_t *channel_id        = &data[1];
    uint8_t *value_id_and_data = &data[2];

    if (*channel_id == id_custom_channel) {
        if (era_common_via_handle_keyboard_channel_command(data, length)) {
            return;
        }

        switch (*command_id) {
            case id_custom_set_value:
                if (!era_board_via_set_value(value_id_and_data)) {
                    *command_id = id_unhandled;
                }
                break;
            case id_custom_get_value:
                if (!era_board_via_get_value(value_id_and_data)) {
                    *command_id = id_unhandled;
                }
                break;
            case id_custom_save:
                era_board_via_save();
                break;
            default:
                *command_id = id_unhandled;
                break;
        }
        return;
    }

    *command_id = id_unhandled;
}
#endif
