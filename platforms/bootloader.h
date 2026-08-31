/*
Copyright 2011 Jun Wako <wakojun@gmail.com>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include <stdint.h>

/* give code for your bootloader to come up if needed */
void bootloader_jump(void);
void mcu_reset(void);

#if defined(RP2040_BOOTLOADER_DOUBLE_TAP_RESET) && defined(RP2040_BOOTLOADER_DOUBLE_TAP_RESET_NONBLOCKING)
/* Clears the double-tap magic once RP2040_BOOTLOADER_DOUBLE_TAP_RESET_TIMEOUT
   has passed since the first call. The keyboard must call this from a loop
   task; without a caller the window never closes. */
void rp2040_bootloader_double_tap_reset_task(void);

/* Diagnostics: 0 while the window is open, otherwise how long it stayed open
   measured from the first task call. Excludes the pre-timer_init() head, so it
   is a lower bound on the real width. */
uint16_t rp2040_bootloader_double_tap_reset_window_ms(void);
#endif

#if defined(RP2040_BOOTLOADER_DOUBLE_TAP_RESET)
/* The armed state of the double-tap magic word. Named rather than left a
   literal because a pre-copy arm has to write the same value from a second
   translation unit; see below. */
#    define RP2040_BOOTLOADER_DOUBLE_TAP_ARMED_TOKEN 0xCAFEB0BAUL

#    if defined(RP2040_BOOTLOADER_DOUBLE_TAP_RESET_PRE_COPY_ARM)
/* Opt-in for images that do not execute from flash. __late_init() runs only
   after crt0 has copied the whole image out of flash and cleared .bss, which
   on such an image is tens of milliseconds after reset - longer than a tweezer
   double tap takes. A window armed there is not open yet when the second reset
   arrives, so the tap is not counted and the board just re-arms.

   Arming and tap counting therefore move to a pre-copy hook the keyboard
   supplies, and the magic word carries three states instead of two. The hook
   owns reset eligibility and any stable-release guard; it must leave exactly
   one of these outcomes:

       pre-copy    eligible reset with armed/requested -> requested
                   eligible first reset after its guard -> armed
                   ineligible reset or guard failure    -> clear
       post-copy   requested          -> clear it, then reset_usb_boot()
                   armed              -> leave the window open and return
                   clear              -> leave the window closed and return

   Only the bootrom jump stays post-copy, and it has to: reset_usb_boot() lives
   in the copied image, so a pre-copy call to it resolves through a linker
   veneer into SRAM that has not been written yet.

   This selector is what says a pre-copy writer is linked. Defining it without
   one leaves the word at whatever the pre-copy step did not write, the
   requested state is never reached, and the window silently stops opening -
   so whatever links the writer is what should define this. */
#        define RP2040_BOOTLOADER_DOUBLE_TAP_REQUEST_TOKEN 0xCAFEB007UL

/* Written by that pre-copy hook, which cannot call into the copied image and
   so touches the object directly. It is the only cross-unit toucher, and it
   must itself be placed where no crt0 copy or clear loop reaches it. */
extern volatile uint32_t magic_location;
#    endif
#endif
