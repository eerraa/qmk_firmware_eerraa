// Copyright 2024 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "../../common/storage/era_eeprom_layout.h"

/* Split configuration */
/*
 * Do not define USB_VBUS_PIN for split role selection. TOMAK relies on QMK's
 * bounded USB_ACTIVE detection at cold boot; raw VBUS pin sampling can race
 * Windows enumeration and disconnect USB during master selection.
 */
#define TOMAK_SPLIT_SAFE_SINGLE_WIRE GP1
#if defined(SERIAL_USART_FULL_DUPLEX)
#    error TOMAK_SPLIT_SAFE_SINGLE_WIRE requires half-duplex serial
#endif
#define SERIAL_USART_TX_PIN TOMAK_SPLIT_SAFE_SINGLE_WIRE
#define SERIAL_USART_RX_PIN TOMAK_SPLIT_SAFE_SINGLE_WIRE

#define USB_DEVICE_DESCRIPTOR_OVERRIDE_ENABLE
#define USB_PRODUCT_STRING_OVERRIDE_ENABLE
#define USB_SERIAL_NUMBER_STRING_OVERRIDE_ENABLE
#define ERA_SPLIT_USB_IDENTITY_PRODUCT_BASE "TOMAK79H"
#define ERA_SPLIT_USB_IDENTITY_PID_LEFT 0xA002
#define ERA_SPLIT_USB_IDENTITY_PID_RIGHT 0xB002

/* The first LED of the badge range this board's lock indicator paints, and the
   range the badge-only mask narrows the render domain to. Board geometry --
   9 of this board's 96 LEDs -- so it is a board fact under ERA
   build-selector rule 3 and belongs here rather than in the family unit, which
   refuses a board that does not state it.  */
#define TOMAK_BADGE_LED_MIN 87

/* The tap-dance slot keycodes start at this board's first custom keycode.
   A board fact, so it is stated here and not in make -- and stated in config.h
   rather than the board header, because a config.h is force-included into every
   translation unit and a board header is only visible to a unit that reaches
   QMK_KEYBOARD_H. */
#define ERA_TAP_DANCE_KEYCODE_BASE QK_KB_0

/* VIA-persistent tap-hold controls */
#define TAPPING_TERM_PER_KEY
#define PERMISSIVE_HOLD_PER_KEY
#define HOLD_ON_OTHER_KEY_PRESS_PER_KEY
#define RETRO_TAPPING_PER_KEY

/* Reset */
#define RP2040_BOOTLOADER_DOUBLE_TAP_RESET
/* 2000U was tried on device 2026-07-27 and changed the double-tap hit rate not
   at all, which is what identified the loss as the window's *start* rather than
   its length: on the SRAM-resident image the magic is not armed until crt0 has
   copied the load image, so a fast tweezer double tap lands before the window
   exists. Widening the far end cannot help that, so the value stays where the
   evidence leaves it. */
#define RP2040_BOOTLOADER_DOUBLE_TAP_RESET_TIMEOUT 1000U
