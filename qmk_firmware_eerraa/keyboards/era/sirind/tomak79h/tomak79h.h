// Copyright 2024 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "quantum.h"
#include "../common/tomak_common.h"

/* What a keymap reaches through QMK_KEYBOARD_H, and this board owns none of
   it: the tomak family's keycodes, indicator types and VIA value ids are all
   in the header above, one copy for `tomak`, `tomak79h` and `tomak79s`. This
   board declares its geometry in keyboard.json and config.h and has no
   translation unit of its own -- the shape newone/odessey has had since it was
   extracted, and the reason it is reachable here is at the top of
   tomak_common.h. */
