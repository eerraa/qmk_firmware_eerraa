// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
void era_test_news_reset(void);
void era_test_news_receive(uint8_t news);
void era_test_news_begin_audit(void);
void era_test_news_rotate_relation(void);
void era_test_news_seed_pair_obligations(void);
uint8_t era_test_news_local_value(void);
void era_test_news_finish_unchanged_audit(void);
bool era_test_news_summary_pending(void);
bool era_test_news_pending(void);
bool era_test_news_visible(void);

void era_test_news_start_summary(void);
void era_test_news_complete_summary(uint8_t mine, uint8_t peer);
void era_test_news_abort_summary(bool terminal);
bool era_test_news_audit_if_due(uint16_t relation, uint16_t policy);
void era_test_news_adopt_transaction_identity(uint16_t relation, uint16_t policy);
void era_test_news_seed_direction(void);
bool era_test_news_idle_token(void);
bool era_test_news_select_deferred_summary(void);
uint8_t era_test_news_probe_mask(void);
uint8_t era_test_news_push_mask(void);
uint8_t era_test_news_conflict_mask(void);
void era_test_news_pending_request(void);
bool era_test_news_take_result(uint8_t changed_identity_field);
bool era_test_news_request_pending(void);

bool era_test_news_restart_wait(void);
void era_test_news_close_indicator_gate(void);
