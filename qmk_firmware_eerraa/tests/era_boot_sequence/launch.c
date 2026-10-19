// SPDX-License-Identifier: GPL-2.0-or-later
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "keyboards/era/common/split/communication_core/era_split_communication_core_owner.h"
#define SPLIT_KEYBOARD
#define BOOTMAGIC_ENABLE
#define RGB_MATRIX_ENABLE
#define BOOTMAGIC_DEBOUNCE 10
#define SERIAL_USART_SPEED 460800U
#define ERA_SPLIT_TRANSACTION_BACKEND_SERIAL_TIMEOUT_US 1000U
#define ERA_SPLIT_COMMUNICATION_CORE_OWNER_REVOKE_DEATH_STREAK 2
#define ERA_SPLIT_SCHEDULER_DIRTY_WIRE_ROLE 1
#define ERA_SPLIT_SCHEDULER_ROUTE_DUE_ATTACH_STATUS 1
#define ERA_SPLIT_SCHEDULER_ROUTE_DUE_HOST_PEER_SOURCE_PUSH 2
#define ERA_SPLIT_ROUTE_NONE 0
#define ERA_SPLIT_MODE_HOST_PEER_PEER 1
#define ERA_SPLIT_MODE_HOST_PEER_HOST 2
#define ERA_SPLIT_WIRE_SECTION_DIRECTION_PUSH 0
#define ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_INPUT_LAYER 1
#define ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_ACTIVITY 2
#define ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_VISUAL 4
#define MATRIX_ROWS_PER_HAND 1
#define __DMB() ((void)0)
#define __SEV() ((void)0)
typedef uint16_t matrix_row_t;
typedef unsigned layer_state_t;
static unsigned debug_config, keymap_config, layer_state;
static struct {
    bool initialized, communication_core_started, local_wire_available, local_wire_initiator;
    unsigned route_due_flags, last_owner_route, mode;
    uint16_t core1_initiator_relation_generation;
} g_era_split_transport_scheduler;
static struct { bool composed_changed; } g_era_matrix_engine;
static struct { bool transaction_backend_initialized; } g_era_split_transaction_backend_rp2040;
static uint32_t g_era_split_transaction_backend_baud = SERIAL_USART_SPEED;
static uint32_t g_era_split_transaction_backend_wire_scale, g_era_split_transaction_backend_byte_us;
static uint32_t g_era_split_transaction_backend_turnaround_us, g_era_split_transaction_backend_serial_timeout_us;
static unsigned starts, early_starts, serviced, scans, requests, dirty;
static bool post_init, lighting_ready, left, fail_start;
/* OWNER_STATE */
static unsigned get_core_num(void) { return 0; }
static void era_split_communication_core_wake(void) {}
static void era_split_communication_core_declare_dead(void) {}
static void era_split_communication_core_note_core1_serviced(void) { serviced++; }
static void era_split_communication_core_note_core1_service_timeout(void) {}
// Hardware launch and READY/release publication are simulated. Role transfer
// and request admission, including their epoch words, are production functions.
static bool era_split_communication_core_start(void) {
    starts++;
    if (!post_init) early_starts++;
    return !fail_start;
}
static bool era_split_communication_core_owner_wait_word(volatile uint32_t *word, uint32_t expected) {
    *word = expected;
    if (word == &g_era_split_communication_core_owner.ready_word)
        g_era_split_transaction_backend_rp2040.transaction_backend_initialized = true;
    return true;
}
/* OWNER_FUNCTIONS */
static bool era_host_peer_storage_initiator_request_pending(void) { return false; }
static bool era_host_peer_storage_route_exclusive(void) { return false; }
static void era_split_communication_core_storage_capacity_init(void) {}
static void era_split_transport_scheduler_rotate_core1_relation(void) {}
static bool era_split_transport_scheduler_cancel_core1_initiator(void) { return true; }
static void era_split_transport_scheduler_drain_communication_core_responder_results(void) {}
// The reducer/mode planner boundary supplies a valid peer-unknown half.
static void era_split_transport_scheduler_init(void) {
    g_era_split_transport_scheduler.initialized = true;
    g_era_split_transport_scheduler.local_wire_available = true;
    g_era_split_transport_scheduler.local_wire_initiator = left;
    g_era_split_transport_scheduler.route_due_flags = left ? 1 : 0;
}
static void era_split_transport_scheduler_ensure_initialized(void) {
    if (!g_era_split_transport_scheduler.initialized) era_split_transport_scheduler_init();
}
static unsigned timer_read32(void) { return 0; }
static void era_split_transport_scheduler_sample_authority(unsigned t, bool b) { (void)t; (void)b; }
static void era_split_transport_scheduler_update_mode(bool b) { (void)b; }
static unsigned era_split_link_active_level(void) { return 0; }
static unsigned era_split_link_speed(unsigned l) { (void)l; return 115200; }
static void era_split_transport_scheduler_mark_dirty(unsigned f) { dirty |= f; }
static void era_split_transport_scheduler_publish_communication_core_responder_snapshot(void) {}
static void era_split_transport_scheduler_refresh_route_due_flags(void) {}
static void era_split_transport_scheduler_update_next_deadline(void) {}
static void era_split_transport_scheduler_clear_owner_route(void) {}
static bool era_split_transport_scheduler_initiator_route_available(void) { return true; }
static void era_split_transport_scheduler_select_owner_route(void) {}
static void era_split_transport_scheduler_execute_owner_route(void) { requests++; }
static bool era_split_transport_scheduler_publish_local_matrix_if_needed(bool *first) { *first = false; return false; }
static void era_split_transport_scheduler_note_local_matrix_publish(bool changed, bool first) { (void)changed; (void)first; }
static void era_split_transport_scheduler_sync_peer_matrix_projection(void) {}
static bool era_split_transport_scheduler_local_initiator_available(void) { return g_era_split_transport_scheduler.local_wire_available && g_era_split_transport_scheduler.local_wire_initiator; }
static unsigned era_split_wire_eligible_sections(unsigned mode, unsigned direction) { (void)mode; (void)direction; return 0; }
static void era_split_transport_scheduler_publish_standing_plan(void) {}
static void era_split_transport_scheduler_note_transport_step_call(void) {}
static bool era_split_input_source_gate(bool open, uint16_t owner, uint16_t relation) { (void)open; (void)owner; (void)relation; return false; }
static void era_split_input_receiver_gate(bool open, uint16_t owner, uint16_t relation) { (void)open; (void)owner; (void)relation; }
static bool era_matrix_engine_copy_local_rows(matrix_row_t *rows) { (void)rows; return false; }
static unsigned era_split_input_clock(void) { return 0; }
static void era_split_input_source_snapshot(matrix_row_t *rows, unsigned now) { (void)rows; (void)now; }
static bool era_split_scheduler_session_host_peer_host_matrix_admitted(void) { return false; }
static bool era_matrix_engine_source_push_due(void) { return false; }
static void era_split_transport_scheduler_mark_route_due(unsigned flags) { (void)flags; }
static bool era_matrix_engine_local_changed(void) { return false; }
static void era_split_transport_scheduler_note_local_visual_change(void) {}
static void era_split_transport_scheduler_publish_host_peer_responder_visual_snapshot(void) {}
static bool era_split_transport_scheduler_scan_idle(void) { return g_era_split_transport_scheduler.route_due_flags == 0; }
/* BACKEND */
/* SCHEDULER */
static void matrix_scan_kb(void) {}
static void era_split_keyboard_note_input_edge(void) {}
static void era_matrix_engine_scan_local(void) { scans++; }
/* MATRIX */
static void wait_ms(unsigned ms) { (void)ms; }
static bool bootmagic_should_reset(void) { return false; }
static void bootmagic_reset_eeprom(void) { assert(false); }
static void bootloader_jump(void) { assert(false); }
/* BOOTMAGIC */
static bool eeconfig_is_enabled(void) { return true; }
// This fixture starts with valid settings; initialization failures use real
// adapters and initializers in tests/era_persistence_failures.
static bool eeconfig_is_init_required(void) { return false; }
static void eeconfig_init(void) { assert(false); }
static void eeconfig_read_debug(unsigned *v) { *v = 0; }
static void eeconfig_read_keymap(unsigned *v) { *v = 0; }
static unsigned eeconfig_read_default_layer(void) { return 0; }
static void default_layer_set(unsigned layer) { (void)layer; }
static void layer_state_set_kb(unsigned layer) { (void)layer; }
static void timer_init(void) {}
static void sync_timer_init(void) {}
static void split_pre_init(void) {}
static void matrix_init(void) {}
static void host_init(void) {}
static void led_init_ports(void) {}
static void rgb_matrix_init(void) { lighting_ready = true; }
static void split_post_init(void) { era_split_transport_scheduler_ensure_initialized(); }
static void keyboard_post_init_quantum(void) {
    assert(scans == 2 && lighting_ready);
    assert(early_starts == 0);
    post_init = true;
    bool started = era_split_transport_scheduler_start_communication_core();
    if (fail_start) {
        assert(!started && dirty == ERA_SPLIT_SCHEDULER_DIRTY_WIRE_ROLE);
        return;
    }
    assert(started);
    assert(g_era_split_transaction_backend_baud == 115200);
    assert(g_era_split_transaction_backend_rp2040.transaction_backend_initialized);
}
/* KEYBOARD */

static void reset_boot(bool is_left) {
    memset(&g_era_split_communication_core_owner, 0, sizeof g_era_split_communication_core_owner);
    memset(&g_era_split_transport_scheduler, 0, sizeof g_era_split_transport_scheduler);
    g_era_split_transaction_backend_baud = SERIAL_USART_SPEED;
    g_era_split_transaction_backend_rp2040.transaction_backend_initialized = false;
    starts = early_starts = scans = requests = dirty = 0;
    post_init = lighting_ready = fail_start = false;
    left = is_left;
}
int main(void) {
    for (unsigned side = 0; side < 2; side++) {
        reset_boot(side == 0);
        keyboard_init();
        assert(starts == 1 && requests == 0);
        matrix_scan();
        assert(requests == (left ? 1U : 0U));
        assert(starts == 1);
        // Requests cannot steal an existing responder assignment.
        if (!left) assert(!era_split_communication_core_owner_ensure_core1());
    }
    assert(era_split_communication_core_owner_transfer_role(ERA_SPLIT_COMMUNICATION_CORE_BACKEND_OWNER_CORE1, ERA_SPLIT_TRANSACTION_BACKEND_ROLE_INITIATOR));
    unsigned before = starts;
    // Readiness failure may be repaired without changing the assigned role.
    g_era_split_communication_core_owner.ready_word = 0;
    assert(era_split_communication_core_owner_ensure_core1());
    assert(starts == before + 1);
    // A revoked or epoch-mismatched grant is not request admission.
    g_era_split_communication_core_owner.revoke_word = 1;
    assert(!era_split_communication_core_owner_ensure_core1());
    g_era_split_communication_core_owner.revoke_word = 0;
    g_era_split_communication_core_owner.role_word += 0x100;
    assert(!era_split_communication_core_owner_ensure_core1());
    assert(starts == before + 1);
    // Teardown closes request admission even after the actor has run.
    assert(era_split_communication_core_owner_transfer_role(ERA_SPLIT_COMMUNICATION_CORE_BACKEND_OWNER_NONE, ERA_SPLIT_TRANSACTION_BACKEND_ROLE_DISABLED));
    assert(!era_split_communication_core_owner_ensure_core1());
    // A failed first launch is retried by policy transfer, not by a request.
    fail_start = true;
    assert(!era_split_communication_core_owner_transfer_role(ERA_SPLIT_COMMUNICATION_CORE_BACKEND_OWNER_CORE1, ERA_SPLIT_TRANSACTION_BACKEND_ROLE_INITIATOR));
    before = starts;
    assert(!era_split_communication_core_owner_ensure_core1());
    assert(starts == before);
    fail_start = false;
    assert(era_split_communication_core_owner_transfer_role(ERA_SPLIT_COMMUNICATION_CORE_BACKEND_OWNER_CORE1, ERA_SPLIT_TRANSACTION_BACKEND_ROLE_INITIATOR));
    assert(era_split_communication_core_owner_ensure_core1());
    reset_boot(true); fail_start = true;
    keyboard_init();
    before = starts;
    matrix_scan();
    assert(starts == before && requests == 0);
    fail_start = false;
    assert(era_split_transport_scheduler_reset_serial_for_transport_role(true, true, true));
    matrix_scan();
    assert(requests == 1);
}
