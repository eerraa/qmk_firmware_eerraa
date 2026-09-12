// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

// Keep target selectors local: the host runner still uses its ordinary QMK
// test platform. Only the register boundary is substituted, not the sampler.
#include "quantum.h"
#define MCU_RP
#define PROTOCOL_CHIBIOS
#define SPLIT_KEYBOARD
#include "keyboards/era/common/system/era_usb_session.c"
#include "sampler_under_test.h"

era_test_usb_registers_t era_test_usb_registers;
era_test_usb_driver_t era_test_usb_driver;
era_test_timer_registers_t era_test_timer_registers;

void era_test_usb_sampler_reset(bool configured) {
    era_usb_session_sof_last_change_us = 0;
    era_usb_session_sof_last_read_us = 0;
    era_usb_session_sof_last_observe_us = 0;
    era_usb_session_sof_isr_owner_started_us = 0;
    era_usb_session_sof_count = 0;
    era_usb_session_sof_seen = false;
    era_usb_session_host_seen = false;
    era_usb_session_sof_isr_owned = false;
    era_usb_session_sampled_now_us = 0;
    era_test_usb_registers = (era_test_usb_registers_t){0};
    era_test_usb_driver.configuration = configured ? 1U : 0U;
    era_test_timer_registers.timerawl = 0;
    usb_device_state_init();
    if (configured) {
        usb_device_state_set_configuration(true, 1U);
    }
}

bool era_test_usb_sampler_observe(uint32_t now_us, uint16_t count, bool isr_owned, bool pending, uint32_t *age_ms) {
    era_test_timer_registers.timerawl = now_us;
    era_test_usb_registers.SOFRD = count;
    era_test_usb_registers.INTE = isr_owned ? USB_INTE_DEV_SOF : 0U;
    era_test_usb_registers.INTS = pending ? USB_INTS_DEV_SOF : 0U;
    return era_usb_session_sample_frame_age(age_ms);
}

uint16_t era_test_usb_sampler_observed_count(void) { return era_usb_session_sof_count; }
