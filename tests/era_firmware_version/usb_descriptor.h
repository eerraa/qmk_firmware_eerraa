// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

/* QMK RAW HID is a fixed 32-byte report. Production compiles the descriptor's
   definition; the USB-less host-test platform supplies the same capacity. */
#define RAW_EPSIZE 32U
