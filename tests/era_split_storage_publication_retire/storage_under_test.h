// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

void era_test_storage_boot_reset(void);
void era_test_storage_set_time_us(uint32_t now_us);
void era_test_storage_watch_initiator_publish(void);
void era_test_storage_watch_responder_publish(void);
bool era_test_storage_ready_observed(void);
bool era_test_storage_ready_seq_was_even(void);
bool era_test_storage_source_claim_was_held_at_ready(void);
void era_test_storage_inject_late_initiator_claim(void);
void era_test_storage_inject_late_responder_claim(void);
bool era_test_storage_late_claim_was_injected(void);
uint32_t era_test_storage_initiator_publication_seq(void);
uint32_t era_test_storage_responder_publication_seq(void);
void era_test_storage_release_late_claim(void);
