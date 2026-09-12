// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>

typedef struct {
    volatile uint32_t INTE;
    volatile uint32_t INTS;
    volatile uint32_t SOFRD;
} era_test_usb_registers_t;
extern era_test_usb_registers_t era_test_usb_registers;
#define USB (&era_test_usb_registers)
#define USB_INTE_DEV_SOF (1UL << 17)
#define USB_INTS_DEV_SOF (1UL << 17)
#define USB_SOF_RD_COUNT_Pos 0U
#define USB_SOF_RD_COUNT_Msk 0x7FFU
