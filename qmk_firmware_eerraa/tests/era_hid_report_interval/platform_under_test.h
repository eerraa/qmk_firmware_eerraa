// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ERA_TEST_POST_CAPACITY 64

typedef struct {
    uint8_t lane;
    uint8_t size;
    uint8_t data[32];
} era_test_post_t;

extern era_test_post_t era_test_posts[ERA_TEST_POST_CAPACITY];
extern unsigned        era_test_post_count;
extern bool            era_test_room;
extern bool            era_test_active;
extern unsigned        era_test_refused;

void era_test_sink_reset(void);
bool era_test_send_keyboard(uint8_t lane, const uint8_t *data, uint8_t size);
void era_test_send_other(uint8_t lane, const uint8_t *data, uint8_t size);

#ifdef __cplusplus
}
#endif
