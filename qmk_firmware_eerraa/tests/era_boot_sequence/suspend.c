// SPDX-License-Identifier: GPL-2.0-or-later
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#define RP2040_BOOTLOADER_DOUBLE_TAP_RESET
#define RP2040_BOOTLOADER_DOUBLE_TAP_RESET_NONBLOCKING
#define RP2040_BOOTLOADER_DOUBLE_TAP_RESET_TIMEOUT 1000U
#define USB_SUSPENDED 1
#define USB_GETSTATUS_REMOTE_WAKEUP_ENABLED 2
#define USB_SUSPEND_WAKEUP_DELAY 0
#define dprintln(x) ((void)0)
static uint32_t now_ms, double_tap_deadline_from_ms;
static uint16_t double_tap_measured_window_ms;
static bool double_tap_armed, double_tap_deadline_started;
static uint32_t magic_location, elapsed_suspend;
static unsigned timer_reads, user_calls, flush_calls;
static struct { unsigned state, status; } USB_DRIVER;
static uint32_t timer_read32(void) { timer_reads++; return now_ms; }
static uint32_t timer_elapsed32(uint32_t t) { timer_reads++; return now_ms - t; }
static void usb_event_queue_task(void) {}
static bool suspend_wakeup_condition(void) { return false; }
static void usbWakeupHost(void *d) { (void)d; }
static void era_usb_session_task(void) {}
static void era_board_persistence_flush_pending(void) { flush_calls++; }
static void suspend_power_down_user(void) { user_calls++; }
/* DEADLINE */
/* HOOK */
/* ACTIVE */
static void suspend_power_down(void) {
    suspend_power_down_kb();
    now_ms += 17;
    elapsed_suspend += 17;
    if (elapsed_suspend >= 5000) {
        assert(magic_location == 0);
        assert(!double_tap_armed);
        USB_DRIVER.state = 0;
    }
}
/* PROTOCOL */

static void arm(uint32_t now) {
    rp2040_bootloader_double_tap_reset_disarm();
    now_ms = now; magic_location = 1; double_tap_armed = true;
}
int main(void) {
    // First service can occur at timestamp zero and entirely inside suspend.
    arm(0); USB_DRIVER.state = USB_SUSPENDED; protocol_pre_task();
    assert(user_calls == flush_calls && user_calls != 0);
    // Active -> suspend -> resume shares one deadline; no re-arm at the edge.
    arm(0); era_common_features_task(); now_ms = 500; suspend_power_down_kb();
    assert(magic_location != 0); now_ms = 999; era_common_features_task();
    assert(magic_location != 0); now_ms = 1000; suspend_power_down_kb();
    assert(magic_location == 0);
    // Timer wrap is the same elapsed-time domain on both paths.
    arm(UINT32_MAX - 499); suspend_power_down_kb(); now_ms += 1000;
    era_common_features_task(); assert(magic_location == 0);
    unsigned reads = timer_reads;
    era_common_features_task(); suspend_power_down_kb();
    assert(timer_reads == reads); // disarmed service does not read the clock
}
