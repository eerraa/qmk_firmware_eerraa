// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
void era_test_standing_reset(void);
void era_test_standing_publish(uint16_t owner, uint16_t relation, bool pending, bool enabled);
void era_test_standing_reply(bool news_valid, uint8_t news, bool success);
bool era_test_standing_run(uint16_t owner);
void era_test_standing_revoke(void);
void era_test_standing_rotate_during_transaction(uint16_t owner, uint16_t relation);
uint16_t era_test_standing_relation(void);
bool era_test_standing_news_valid(void);
uint8_t era_test_standing_news(void);
uint32_t era_test_standing_change_seq(void);
uint32_t era_test_standing_exchanges(void);
uint32_t era_test_standing_transactions(void);
uint8_t era_test_standing_last_sections(void);
bool era_test_standing_stopped(void);
uint16_t era_test_standing_capture_stop(uint16_t owner, uint16_t relation);
bool era_test_standing_resume(uint16_t owner, uint16_t relation, uint16_t stop);
bool era_test_standing_snapshot_consistent(void);
void era_test_standing_visual_reply(void);
uint8_t era_test_standing_visual_seq(void);
