// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later
#include <stdbool.h>

/* Private names keep the shared matrix fixture's no-op led_set separate.
 * Execute QMK's real LED task/selection; only the electrical pin is adapted.
 * This is the dedicated Caps-pin policy used by the 7B75 configuration. */
static bool caps_pin;
static void capture_caps_pin(bool on) { caps_pin = on; }
bool era_td_caps_gpio_state(void) { return caps_pin; }
#define LED_CAPS_LOCK_PIN 1
#define gpio_write_pin(pin, value) ((void)(pin), capture_caps_pin(value))
#define gpio_set_pin_output(pin) ((void)(pin))
#define last_led_activity_time era_td_caps_last_activity_time
#define last_led_activity_elapsed era_td_caps_last_activity_elapsed
#define led_update_user era_td_caps_update_user
#define led_update_kb era_td_caps_update_kb
#define led_update_ports era_td_caps_update_ports
#define led_init_ports era_td_caps_init_ports
#define led_set era_td_caps_set
#define led_suspend era_td_caps_suspend
#define led_wakeup era_td_caps_wakeup
#define led_task era_td_caps_led_task
#include "../../quantum/led.c"
