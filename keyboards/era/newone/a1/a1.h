// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "quantum.h"

/* The eight ERA tap-dance slots, so a source keymap can name them. VIA reaches
   the same eight through QK_KB_0..7 directly; the base is stated in config.h,
   which is force-included everywhere, rather than here. */
enum a1_keycodes {
    TD0 = QK_KB_0,
    TD1,
    TD2,
    TD3,
    TD4,
    TD5,
    TD6,
    TD7
};
