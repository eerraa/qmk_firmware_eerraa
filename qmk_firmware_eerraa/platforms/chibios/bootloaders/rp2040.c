// Copyright 2022 Stefan Kerkmann
// SPDX-License-Identifier: GPL-2.0-or-later

#include "hal.h"
#include "bootloader.h"
#include "gpio.h"
#include "wait.h"
#include "pico/bootrom.h"
#if defined(RP2040_BOOTLOADER_DOUBLE_TAP_RESET) && defined(RP2040_BOOTLOADER_DOUBLE_TAP_RESET_NONBLOCKING)
#    include "timer.h"
#endif

#if !defined(RP2040_BOOTLOADER_DOUBLE_TAP_RESET_LED)
#    define RP2040_BOOTLOADER_DOUBLE_TAP_RESET_LED_MASK 0U
#else
#    define RP2040_BOOTLOADER_DOUBLE_TAP_RESET_LED_MASK (1U << RP2040_BOOTLOADER_DOUBLE_TAP_RESET_LED)
#endif

#if defined(RP2040_BOOTLOADER_DOUBLE_TAP_RESET) && defined(RP2040_BOOTLOADER_DOUBLE_TAP_RESET_NONBLOCKING)
static void rp2040_bootloader_double_tap_reset_disarm(void);
#endif

// With the non-blocking window the firmware runs while the magic is still
// armed, so every deliberate software route out of the running image has to
// disarm first. The blocking wait reached none of these cases: it cleared the
// magic inside __late_init(), before main() started.
__attribute__((weak)) void mcu_reset(void) {
#if defined(RP2040_BOOTLOADER_DOUBLE_TAP_RESET) && defined(RP2040_BOOTLOADER_DOUBLE_TAP_RESET_NONBLOCKING)
    // A software reset inside the open window would otherwise be read as the
    // second tap and land in the bootloader.
    rp2040_bootloader_double_tap_reset_disarm();
#endif
    NVIC_SystemReset();
}
void bootloader_jump(void) {
#if defined(RP2040_BOOTLOADER_DOUBLE_TAP_RESET) && defined(RP2040_BOOTLOADER_DOUBLE_TAP_RESET_NONBLOCKING)
    // Leaving the magic armed here strands the board in the bootloader. The
    // bootmagic caller runs from quantum_init(), inside keyboard_init() and so
    // before the first loop pass that would clear it, meaning the board reaches
    // BOOTSEL still armed; the bootrom then resets into the freshly written
    // image, __late_init() reads the magic it left behind, and the new firmware
    // bounces straight back into the bootloader instead of starting.
    rp2040_bootloader_double_tap_reset_disarm();
#endif
    reset_usb_boot(RP2040_BOOTLOADER_DOUBLE_TAP_RESET_LED_MASK, 0U);
}

void enter_bootloader_mode_if_requested(void) {}

#if defined(RP2040_BOOTLOADER_DOUBLE_TAP_RESET)
#    if !defined(RP2040_BOOTLOADER_DOUBLE_TAP_RESET_TIMEOUT)
#        define RP2040_BOOTLOADER_DOUBLE_TAP_RESET_TIMEOUT 200U
#    endif

// Needs to be located in a RAM section that is never initialized on boot to
// preserve its value on reset
#    if defined(RP2040_BOOTLOADER_DOUBLE_TAP_RESET_PRE_COPY_ARM)
// Not static under that selector: the hook that arms the window and counts the
// taps runs before crt0's copy loops, so it cannot live in this translation
// unit - this one is in the copied image and does not exist yet at that point.
// It writes the word directly instead. Contract in bootloader.h.
volatile uint32_t __attribute__((section(".ram0.bootloader_magic"))) magic_location;
#    else
static volatile uint32_t __attribute__((section(".ram0.bootloader_magic"))) magic_location;
const uint32_t magic_token = RP2040_BOOTLOADER_DOUBLE_TAP_ARMED_TOKEN;
#    endif

#    if defined(RP2040_BOOTLOADER_DOUBLE_TAP_RESET_NONBLOCKING)
// Opt-in: the selected startup path arms the window without waiting, and the
// keyboard clears the magic from rp2040_bootloader_double_tap_reset_task() once
// RP2040_BOOTLOADER_DOUBLE_TAP_RESET_TIMEOUT has passed, so boot no longer
// stops for it. On the SRAM-resident ERA image the selected startup path is the
// pre-copy hook; other users may still arm in __late_init().
//
// There is no timebase readable at __late_init that survives to the main loop.
// The QMK millisecond timer does not exist yet and keyboard_init() reruns
// timer_init()/timer_clear() afterwards; the RP2040 realtime counter looks like
// it would work - the busy wait below counts on it - but ChibiOS zeroes it in
// st_lld_init() (TIMER->TIMELW/TIMEHW, hal_st_lld.c) during chSysInit(), which
// also runs after this. A stamp taken here and compared later is unsound on
// either one.
//
// So the deadline is stamped on the first task call instead, which is
// unambiguously after timer_init(). A separate started bit carries whether that
// stamp exists: zero is a valid first timer sample and must not be overloaded
// as the unset sentinel, or a second task call in the same millisecond computes
// 0 - 1 and expires the window immediately. The window is therefore [arm,
// first keyboard-loop pass + TIMEOUT]: never shorter than TIMEOUT, longer by
// however long boot takes to reach the loop. TIMEOUT is a guaranteed minimum
// here, not an exact width.
static uint32_t double_tap_deadline_from_ms;
static uint16_t double_tap_measured_window_ms;
static bool     double_tap_armed;
static bool     double_tap_deadline_started;

static void rp2040_bootloader_double_tap_reset_disarm(void) {
    double_tap_armed            = false;
    double_tap_deadline_started = false;
    double_tap_deadline_from_ms = 0;
    magic_location              = 0;
}

void rp2040_bootloader_double_tap_reset_task(void) {
    if (!double_tap_armed) {
        return;
    }

    if (!double_tap_deadline_started) {
        double_tap_deadline_from_ms = timer_read32();
        double_tap_deadline_started = true;
        return;
    }

    uint32_t elapsed_ms = timer_elapsed32(double_tap_deadline_from_ms);
    if (elapsed_ms < RP2040_BOOTLOADER_DOUBLE_TAP_RESET_TIMEOUT) {
        return;
    }

    // Diagnostics only: how long the window stayed open measured from the first
    // loop pass. It excludes the pre-timer_init() head of the window, so it is a
    // lower bound on the real width - which is the number that says whether the
    // window is the size the macro claims.
    double_tap_measured_window_ms = elapsed_ms > UINT16_MAX ? UINT16_MAX : (uint16_t)elapsed_ms;
    rp2040_bootloader_double_tap_reset_disarm();
}

uint16_t rp2040_bootloader_double_tap_reset_window_ms(void) {
    return double_tap_armed ? 0 : double_tap_measured_window_ms;
}
#    endif

#    if defined(RP2040_BOOTLOADER_DOUBLE_TAP_RESET_PRE_COPY_ARM)
// The pre-copy hook has already armed the window and counted this reset, so
// what reaches here is a bootloader request rather than the armed state.
#        define RP2040_BOOTLOADER_DOUBLE_TAP_ENTER_TOKEN RP2040_BOOTLOADER_DOUBLE_TAP_REQUEST_TOKEN
#    else
#        define RP2040_BOOTLOADER_DOUBLE_TAP_ENTER_TOKEN magic_token
#    endif

// The bootrom jump can not move to the __early_init /
// enter_bootloader_mode_if_requested hook, because it depends on an already
// initialized system with usable memory regions and populated function pointer
// tables to the optimized math functions in the bootrom. This function is
// called just prior to main. Arming can move, and under
// RP2040_BOOTLOADER_DOUBLE_TAP_RESET_PRE_COPY_ARM it has, because on an image
// that is copied out of flash first this point is far too late to open a
// window a hand can hit.
void __late_init(void) {
    // All clocks have to be enabled before jumping to the bootloader function,
    // otherwise the bootrom will be stuck infinitely.
    clocks_init();

    if (magic_location != RP2040_BOOTLOADER_DOUBLE_TAP_ENTER_TOKEN) {
#        if defined(RP2040_BOOTLOADER_DOUBLE_TAP_RESET_PRE_COPY_ARM)
        /*
         * The pre-copy hook has already classified this reset and applied its
         * stable-release guard. Only an eligible physical RUN may leave ARMED
         * behind; POR, recovery and software resets arrive CLEAR and must not
         * be silently re-armed here.
         */
        double_tap_armed = magic_location == RP2040_BOOTLOADER_DOUBLE_TAP_ARMED_TOKEN;
#        else
        magic_location = magic_token;
#    if defined(RP2040_BOOTLOADER_DOUBLE_TAP_RESET_NONBLOCKING)
        double_tap_armed = true;
#    else
        // ChibiOS is not initialized at this point, so sleeping is only
        // possible via busy waiting.
        wait_us(RP2040_BOOTLOADER_DOUBLE_TAP_RESET_TIMEOUT * 1000U);
        magic_location = 0;
#    endif
#        endif
        return;
    }

    magic_location = 0;
    reset_usb_boot(RP2040_BOOTLOADER_DOUBLE_TAP_RESET_LED_MASK, 0U);
}

#endif
