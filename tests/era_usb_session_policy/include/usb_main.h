// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>

typedef struct { uint8_t configuration; } era_test_usb_driver_t;
extern era_test_usb_driver_t era_test_usb_driver;
#define USB_DRIVER era_test_usb_driver
