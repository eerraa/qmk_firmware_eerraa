// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

/* The sink the unit posts into: a record of every post in order, a room
   switch and an active switch, and the platform-side counting that the
   ChibiOS send path performs on every successful post. */
#include "platform_under_test.h"

#include <string.h>

#include "keyboards/era/common/system/era_hid_report_interval.h"

era_test_post_t era_test_posts[ERA_TEST_POST_CAPACITY];
unsigned        era_test_post_count;
bool            era_test_room   = true;
bool            era_test_active = true;
unsigned        era_test_refused;

void era_test_sink_reset(void) {
    memset(era_test_posts, 0, sizeof(era_test_posts));
    era_test_post_count = 0;
    era_test_room       = true;
    era_test_active     = true;
    era_test_refused    = 0;
}

bool era_hid_report_interval_platform_post(uint8_t lane, const uint8_t *data, uint8_t size) {
    if (!era_test_active || !era_test_room) {
        era_test_refused++;
        return false;
    }
    if (era_test_post_count < ERA_TEST_POST_CAPACITY) {
        era_test_post_t *post = &era_test_posts[era_test_post_count];
        post->lane            = lane;
        post->size            = size;
        memcpy(post->data, data, size);
    }
    era_test_post_count++;
    /* The production send path counts every post on a tracked lane. */
    era_hid_report_interval_note_posted(lane);
    return true;
}

/* A report the firmware posts directly: the hold question, then the post,
   then the keyboard-class note, exactly as send_keyboard()/send_nkro() do. */
bool era_test_send_keyboard(uint8_t lane, const uint8_t *data, uint8_t size) {
    if (era_hid_report_interval_hold(lane, data, size)) {
        return false;
    }
    if (!era_hid_report_interval_platform_post(lane, data, size)) {
        return false;
    }
    era_hid_report_interval_note_keyboard_posted(lane);
    return true;
}

/* A mouse or EXTRA report on the shared endpoint: counted, never held. */
void era_test_send_other(uint8_t lane, const uint8_t *data, uint8_t size) {
    (void)era_hid_report_interval_platform_post(lane, data, size);
}
