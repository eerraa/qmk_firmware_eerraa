// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void era_test_link_reset(void);
void era_test_link_select(unsigned side);
unsigned era_test_link_side(void);
void era_test_link_boot(void);
void era_test_link_clock_offset(int32_t offset);
void era_test_link_relation(bool serviced, bool initiator, bool left, bool winner);
void era_test_link_meet(bool serviced, bool initiator, bool left, bool winner, bool fresh, bool peer_searched);
void era_test_link_fault(unsigned fault);
void era_test_link_driver_ready(bool ready);
void era_test_link_full_journal(void);
void era_test_link_quiet(bool quiet);
void era_test_link_storage_busy(bool busy);
void era_test_link_anchor(bool ready);
void era_test_link_set_physical(uint8_t level, bool ready);
uint8_t era_test_link_stored(void);
bool era_test_link_stored_cached(void);
uint8_t era_test_link_physical(void);
bool era_test_link_wire_ready(void);
bool era_test_link_published(void);
bool era_test_link_fallback(void);
uint32_t era_test_link_transition_ms(void);
unsigned era_test_link_transition_count(void);
unsigned era_test_link_quiesce_count(void);
unsigned era_test_link_dirty(void);
unsigned era_test_link_notifications(void);
unsigned era_test_link_resets(void);
void era_test_link_noise(uint32_t accepted, uint32_t undecodable);
bool era_test_link_recovery_step(void);
bool era_test_link_repair(void);
uint32_t era_test_link_address(void);
#ifdef __cplusplus
}
#endif
