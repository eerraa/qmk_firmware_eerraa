// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "quantum.h"

#ifdef VIA_ENABLE
#    include "../../common/features/era_rgb_sleep.h"
#    include "../../common/system/era_via_system.h"

void via_custom_value_command_kb(uint8_t *data, uint8_t length) {
    if (era_rgb_sleep_handle_via_command(data, length)) {
        return;
    }
    if (data != NULL && length >= 2 && data[0] == id_custom_save && data[1] == ERA_VIA_SYSTEM_CHANNEL) {
        return;
    }
    data[0] = id_unhandled;
}
#endif
