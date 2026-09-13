// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later
#include "quantum.h"
#include "sync_timer.h"
#include <string.h>
#include "link_under_test.h"

static int32_t test_shared_offset;
static uint32_t era_test_shared_clock(void) { return timer_read32() + test_shared_offset; }
#undef sync_timer_read32
#define sync_timer_read32() era_test_shared_clock()
// Storage admission is a supplied peer-service boundary, not an NVM substitute.
#define ERA_HOST_PEER_STORAGE_V1_ENABLE
#include "keyboards/era/common/split/era_split_restart_agreement.c"
#include "keyboards/era/common/split/era_split_link.c"
#include "keyboards/era/common/split/era_split_via_link.c"
#include "keyboards/era/common/storage/era_eeprom_driver.c"

static struct {
    bool communication_core_started;
    bool local_wire_available;
    bool local_wire_initiator;
    bool responder_snapshot_publish_due;
} g_era_split_transport_scheduler;
enum { ERA_SPLIT_SCHEDULER_DIRTY_WIRE_ROLE = 1U << 6 };
static struct {
    unsigned fault, dirty, transitions, quiesces, notifications, resets;
    uint8_t physical, selected;
    bool ready, published, quiet, storage_busy, anchor;
    uint32_t transitioned_ms, accepted, undecodable;
    bool accept_after_next_read;
} test_hw;

static bool era_split_transport_scheduler_stop_communication_core_for_flash_write(void) {
    test_hw.quiesces++;
    if (test_hw.fault == 1) return false;
    test_hw.ready = test_hw.published = false;
    return true;
}
bool era_split_transaction_backend_set_speed(uint32_t baud) {
    uint8_t level = baud == ERA_SPLIT_LINK_SPEED_LOW ? ERA_SPLIT_LINK_LEVEL_LOW :
                    baud == ERA_SPLIT_LINK_SPEED_MEDIUM ? ERA_SPLIT_LINK_LEVEL_MEDIUM : ERA_SPLIT_LINK_LEVEL_HIGH;
    bool changed = test_hw.selected != level;
    test_hw.selected = level;
    return changed;
}
static bool era_split_transport_scheduler_reset_serial_for_transport_role(bool available, bool initiator, bool rotate) {
    (void)initiator; (void)rotate;
    if (test_hw.fault == 2) return false;
    if (available && !test_hw.ready) {
        test_hw.transitions++;
        test_hw.transitioned_ms = timer_read32();
        test_hw.physical = test_hw.selected;
    }
    test_hw.ready = available;
    return true;
}
static void era_split_transport_scheduler_mark_dirty(uint8_t flags) { test_hw.dirty |= flags; }
bool era_split_transport_scheduler_publish_communication_core_responder_snapshot(void) {
    g_era_split_transport_scheduler.responder_snapshot_publish_due = test_hw.fault == 3;
    return !g_era_split_transport_scheduler.responder_snapshot_publish_due;
}
static bool era_split_transport_scheduler_publish_standing_plan(void) {
    test_hw.published = test_hw.ready && test_hw.fault != 3 && test_hw.fault != 4;
    return test_hw.published;
}
bool era_split_transport_scheduler_standing_plan_stale(void) { return test_hw.fault == 4; }
#include "keyboards/era/common/split/scheduler/era_split_transport_scheduler_link.inc"

// Exactly the production dispatch binding; neither LINK work nor NVM is stubbed.
const era_split_restart_act_rules_t era_split_restart_act_rules[ERA_SPLIT_RESTART_ACT_MAX + 1] = {
    {false, false, false, 0, false, false}, {true, true, false, 2, false, true}, {true, true, true, 0, false, false},
    {true, true, false, 0, true, true},
};
bool era_split_restart_prepare_local(era_split_restart_act_t act, uint8_t param) {
    if (act == ERA_SPLIT_RESTART_ACT_LINK_RECOVERED) return era_split_link_reconcile_success_report_commit();
    return act == ERA_SPLIT_RESTART_ACT_LINK_SPEED && era_split_link_apply(param);
}
bool era_split_restart_arm_ready(era_split_restart_act_t act) { (void)act; return test_hw.anchor; }
bool era_via_system_restart_quiet_ok(uint32_t requested_ms) { (void)requested_ms; return test_hw.quiet; }
bool era_host_peer_storage_restart_should_wait(void) { return test_hw.storage_busy; }
bool era_host_peer_storage_restart_quarantine_ready(void) { return true; }
void era_host_peer_storage_note_eeprom_commit(uint32_t address, uint32_t length) {
    (void)address; (void)length; test_hw.notifications++;
}
uint32_t era_split_communication_core_responder_accepted_rx_count(void) {
    uint32_t observed = test_hw.accepted;
    if (test_hw.accept_after_next_read) {
        test_hw.accept_after_next_read = false;
        test_hw.accepted++;
    }
    return observed;
}
uint32_t era_split_communication_core_responder_undecodable_rx_count(void) { return test_hw.undecodable; }
void __wrap_soft_reset_keyboard(void) { test_hw.resets++; }

// Two independent firmware singleton images. Selecting an endpoint only swaps
// its RAM: it never copies either endpoint's physical NOR or advances its clock.
static struct endpoint {
    __typeof__(g_era_split_link) link;
    __typeof__(g_era_split_link_fallback_report) fallback_report;
    __typeof__(g_era_split_restart_agreement) agreement;
    __typeof__(g_era_split_transport_scheduler) scheduler;
    __typeof__(test_hw) hw;
    era_nvm_t nvm;
    bool nvm_ready;
    int32_t offset;
} endpoints[2];
static unsigned test_side;
static void save_endpoint(void) {
    endpoints[test_side].link = g_era_split_link;
    endpoints[test_side].fallback_report = g_era_split_link_fallback_report;
    endpoints[test_side].agreement = g_era_split_restart_agreement;
    endpoints[test_side].scheduler = g_era_split_transport_scheduler;
    endpoints[test_side].hw = test_hw;
    endpoints[test_side].nvm = s_era_eeprom_nvm;
    endpoints[test_side].nvm_ready = s_era_eeprom_ready;
    endpoints[test_side].offset = test_shared_offset;
}
void era_test_link_select(unsigned side) {
    if (side > 1 || side == test_side) return;
    save_endpoint(); test_side = side;
    g_era_split_link = endpoints[side].link;
    g_era_split_link_fallback_report = endpoints[side].fallback_report;
    g_era_split_restart_agreement = endpoints[side].agreement;
    g_era_split_transport_scheduler = endpoints[side].scheduler;
    test_hw = endpoints[side].hw;
    s_era_eeprom_nvm = endpoints[side].nvm;
    s_era_eeprom_ready = endpoints[side].nvm_ready;
    test_shared_offset = endpoints[side].offset;
}
unsigned era_test_link_side(void) { return test_side; }
void era_test_link_reset(void) {
    memset(endpoints, 0, sizeof(endpoints));
    memset(&g_era_split_link, 0, sizeof(g_era_split_link));
    memset(&g_era_split_restart_agreement, 0, sizeof(g_era_split_restart_agreement));
    memset(&g_era_split_link_fallback_report, 0, sizeof(g_era_split_link_fallback_report));
    memset(&s_era_eeprom_nvm, 0, sizeof(s_era_eeprom_nvm));
    memset(&test_hw, 0, sizeof(test_hw));
    memset(&g_era_split_transport_scheduler, 0, sizeof(g_era_split_transport_scheduler));
    s_era_eeprom_ready = false; test_side = 0; test_shared_offset = 0;
}
void era_test_link_boot(void) {
    memset(&g_era_split_link, 0, sizeof(g_era_split_link));
    memset(&g_era_split_link_fallback_report, 0, sizeof(g_era_split_link_fallback_report));
    memset(&g_era_split_restart_agreement, 0, sizeof(g_era_split_restart_agreement));
    memset(&test_hw, 0, sizeof(test_hw));
    test_hw.physical = test_hw.selected = ERA_SPLIT_LINK_LEVEL_LOW;
    test_hw.ready = test_hw.published = test_hw.quiet = test_hw.anchor = true;
    g_era_split_transport_scheduler = (__typeof__(g_era_split_transport_scheduler)){true, true, true, false};
    eeprom_driver_init();
}
void era_test_link_clock_offset(int32_t offset) { test_shared_offset = offset; }
// Every explicit relation edge in the original cases is a fresh meeting whose
// peer answer says nothing about a search; the same-pair reopen the scheduler
// reports after a storage close, and the answer's rate_searched fact, are
// era_test_link_meet.
void era_test_link_relation(bool serviced, bool initiator, bool left, bool winner) {
    era_test_link_meet(serviced, initiator, left, winner, true, false);
}
void era_test_link_meet(bool serviced, bool initiator, bool left, bool winner, bool fresh, bool peer_searched) {
    g_era_split_transport_scheduler.local_wire_initiator = initiator;
    era_split_restart_agreement_note_relation(serviced, initiator, left);
    era_split_link_note_relation(serviced, !serviced && !initiator, winner, fresh, peer_searched, initiator);
}
void era_test_link_fault(unsigned fault) { test_hw.fault = fault; }
void era_test_link_driver_ready(bool ready) { s_era_eeprom_ready = ready; }
void era_test_link_full_journal(void) { s_era_eeprom_nvm.journal_cursor = ERA_NVM_BANK_SIZE_BYTES; }
void era_test_link_quiet(bool quiet) { test_hw.quiet = quiet; }
void era_test_link_storage_busy(bool busy) { test_hw.storage_busy = busy; }
void era_test_link_anchor(bool ready) { test_hw.anchor = ready; }
void era_test_link_set_physical(uint8_t level, bool ready) {
    test_hw.physical = test_hw.selected = level; test_hw.ready = ready;
    era_split_link_note_level_selected(level);
}
uint8_t era_test_link_stored(void) { return g_era_split_link.stored_level; }
bool era_test_link_stored_cached(void) { return g_era_split_link.stored_cached; }
uint8_t era_test_link_physical(void) { return test_hw.physical; }
bool era_test_link_wire_ready(void) { return test_hw.ready; }
bool era_test_link_published(void) { return test_hw.published; }
bool era_test_link_fallback(void) { return g_era_split_link.fallback_latched; }
uint32_t era_test_link_transition_ms(void) { return test_hw.transitioned_ms; }
unsigned era_test_link_transition_count(void) { return test_hw.transitions; }
unsigned era_test_link_quiesce_count(void) { return test_hw.quiesces; }
unsigned era_test_link_dirty(void) { return test_hw.dirty; }
unsigned era_test_link_notifications(void) { return test_hw.notifications; }
unsigned era_test_link_resets(void) { return test_hw.resets; }
void era_test_link_noise(uint32_t accepted, uint32_t undecodable) { test_hw.accepted = accepted; test_hw.undecodable = undecodable; }
bool era_test_link_recovery_step(void) {
    uint8_t level;
    return era_split_link_step_due(&level) && era_split_transport_scheduler_apply_link_step(level, true);
}
bool era_test_link_repair(void) {
    bool result = era_split_transport_scheduler_apply_link_step(era_split_link_active_level(), false);
    if (result) test_hw.dirty = 0;
    return result;
}
uint32_t era_test_link_address(void) { return ERA_EEPROM_CONFIG_ADDR + ERA_EEPROM_LINK_CONFIG_OFFSET; }

void era_test_link_accept_after_next_read(void) { test_hw.accept_after_next_read = true; }
