// SPDX-License-Identifier: GPL-2.0-or-later
#include <stdlib.h>
#define SPLIT_KEYBOARD
static unsigned test_core;
#define get_core_num() test_core
#define __DMB() __atomic_thread_fence(__ATOMIC_SEQ_CST)
#define __SEV() ((void)0)
#define ATOMIC_BLOCK_RESTORESTATE for (int once = 1; once; once = 0)
/* INPUT_OWNERS */
#include "keyboards/era/common/split/communication_core/era_split_communication_core_responder.c"
#include "keyboards/era/common/split/communication_core/era_split_communication_core_responder_service.c"
#include "keyboards/era/common/split/communication_core/era_split_communication_core_standing.c"
/* MATRIX_ENGINE */

era_split_communication_core_state_t g_era_split_communication_core;
static timer_hw_t raw_timer;
timer_hw_t *timer_hw = &raw_timer;
static era_split_wire_frame_t incoming, answer;
static unsigned sends, standing_requests;
static uint8_t last_request_length, last_response_length;
static uint16_t host_owner = 7, host_relation = 9;
static bool drop_answer;
static matrix_row_t matrix_previous[MATRIX_ROWS];
static bool input_remote_pass, input_previous_host;
static uint32_t input_hid_pressure_seen;
static uint8_t room = 66;
static bool is_keyboard_left(void) { return true; }
static uint8_t action_tapping_remote_room(void) { return room; }
static bool era_hid_report_interval_pressured(void) { return false; }
static uint32_t era_hid_report_interval_pressure_serial(void) { return 0; }
static void era_split_keyboard_note_input_edge(void) {}
void era_split_input_action_begin(bool action_open);
void era_split_input_action_end(void);
static bool era_split_transaction_engine_response_kind_matches(era_split_wire_payload_kind_t, era_split_wire_payload_kind_t, era_split_wire_payload_kind_t);

void era_split_communication_core_init(void) {}
era_split_communication_core_backend_access_t era_split_communication_core_owner_backend_access(uint16_t owner) {
    (void)owner; return ERA_SPLIT_COMMUNICATION_CORE_BACKEND_ACCESS_OK;
}
bool era_host_peer_transaction_encode_rgb_state_body(const era_host_peer_rgb_state_t *s, uint8_t p[ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_RGB_STATE_BYTES]) { (void)s; (void)p; abort(); }
bool era_host_peer_transaction_decode_rgb_state_body(const uint8_t *p, era_host_peer_rgb_state_t *s) { (void)p; (void)s; abort(); }
void era_split_transaction_backend_arm_core1_idle_wake(uint32_t deadline) { (void)deadline; }
void era_split_transaction_engine_reset_link_state(void) {}
void era_split_transaction_engine_commit_received_frame(const era_split_wire_frame_t *f) { (void)f; }
bool era_split_transaction_engine_prepare_control(bool ext, uint8_t *control, uint8_t *seq) {
    *seq = 1; *control = 1 | (ext ? 0x40 : 0); return true;
}
era_split_transaction_engine_result_t era_split_transaction_engine_receive_idle_owned(
    era_split_wire_direction_t d, uint16_t first, uint16_t window, uint16_t owner,
    uint8_t *wire, uint16_t capacity, era_split_wire_frame_t *frame, era_split_transaction_failure_t *failure) {
    (void)d; (void)first; (void)window; (void)owner; (void)wire; (void)capacity; (void)failure;
    *frame = incoming; return ERA_SPLIT_TRANSACTION_RESULT_OK;
}
era_split_transaction_engine_result_t era_split_transaction_engine_send_compact_response_owned(
    era_split_wire_direction_t d, const uint8_t *p, uint8_t n, uint8_t seq, uint16_t owner, era_split_transaction_failure_t *failure) {
    (void)d; (void)seq; (void)owner; (void)failure;
    ++sends; memcpy(answer.payload, p, n); answer.payload_len = n;
    assert(era_split_wire_classify_payload(p, n, ERA_SPLIT_WIRE_FRAME_LANE_COMPACT, &answer.kind));
    return ERA_SPLIT_TRANSACTION_RESULT_OK;
}
static bool exchange(const uint8_t *p, uint8_t n) {
    incoming = (era_split_wire_frame_t){.payload_len = n, .tx_seq = 1};
    memcpy(incoming.payload, p, n);
    assert(era_split_wire_classify_payload(p, n, ERA_SPLIT_WIRE_FRAME_LANE_COMPACT, &incoming.kind));
    unsigned before = sends; test_core = 1;
    raw_timer.timerawl = clock_us;
    assert(era_split_communication_core_responder_service_once(host_owner));
    // Core0 drains general results; input owns separate capacity throughout.
    g_era_split_communication_core_responder_general_read = g_era_split_communication_core_responder_general_write;
    return sends != before;
}
era_split_transaction_engine_result_t era_split_transaction_engine_transact_compact_owned(
    era_split_wire_direction_t d, const uint8_t *p, uint8_t n, uint8_t seq,
    era_split_wire_payload_kind_t expected, era_split_wire_payload_kind_t alternate,
    uint16_t window, uint16_t owner, era_split_wire_frame_t *response, bool *sent, era_split_transaction_failure_t *failure) {
    (void)d; (void)seq; (void)window; (void)owner; (void)sent; (void)failure;
    ++standing_requests; last_request_length = n;
    assert(exchange(p, n)); last_response_length = answer.payload_len;
    if (drop_answer) return ERA_SPLIT_TRANSACTION_RESULT_MISS;
    *response = answer;
    return era_split_transaction_engine_response_kind_matches(expected, alternate, response->kind)
               ? ERA_SPLIT_TRANSACTION_RESULT_OK : ERA_SPLIT_TRANSACTION_RESULT_BAD;
}
static void publish(bool hp, bool sections) {
    era_split_communication_core_responder_snapshot_t s = {
        .owner_epoch = host_owner, .relation_generation = host_relation, .owner_gate_ready = true,
        .host_matrix_admitted = hp, .heartbeat_allowed = true, .runtime_push_allowed = true, .session_allowed = true,
        .eligible_push_sections = hp ? ERA_SPLIT_WIRE_SECTION_ELIGIBLE_HOST_PEER_PUSH : ERA_SPLIT_WIRE_SECTION_ELIGIBLE_DUAL_HOST_PUSH,
        .eligible_rsp_sections = hp ? ERA_SPLIT_WIRE_SECTION_ELIGIBLE_HOST_PEER_RSP : ERA_SPLIT_WIRE_SECTION_ELIGIBLE_DUAL_HOST_RSP,
        .local_session_status = {.accepted_host_open = true},
        .response_section_mask = sections ? ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_LOCK_STATE : 0,
        .lock_state_bits = 1,
    };
    test_core = 0; (void)era_split_communication_core_publish_responder_snapshot(&s);
    era_split_communication_core_standing_plan_t plan = {
        .owner_epoch = 7, .relation_generation = 9, .enabled = true, .poll_period_ms = 10, .liveness_period_ms = 50,
        .eligible_push_sections = hp ? ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_MATRIX : 0,
        .eligible_rsp_sections = hp ? ERA_SPLIT_WIRE_SECTION_ELIGIBLE_HOST_PEER_RSP : ERA_SPLIT_WIRE_SECTION_ELIGIBLE_DUAL_HOST_RSP,
    };
    (void)era_split_communication_core_publish_standing_plan(&plan);
}
static void action(void) {
    test_core = 0; era_split_input_action_begin(true);
    memcpy(matrix_previous, g_era_matrix_engine.composed_rows, sizeof(matrix_previous));
    era_split_input_action_end();
}
static bool input(void) {
    test_core = 0;
    era_split_input_request_t r;
    if (!era_split_input_source_prepare(&r, clock_us)) return false;
    assert(era_split_input_source_tx_allowed(&r, 7, 9, clock_us));
    uint8_t p[15], n;
    if (r.op == ERA_INPUT_FENCE || r.op == ERA_INPUT_BIND) {
        p[0] = 0x41; p[1] = r.op; memcpy(p + 2, r.body, 7); n = 9;
        if (r.op == ERA_INPUT_BIND) { era_split_wire_put16(p + 9, r.epoch); n = 11; }
    } else assert(era_split_input_encode(1, &r, p, &n));
    assert(exchange(p, n));
    test_core = 0; era_split_input_source_complete(&r, true, answer.payload[1]);
    action(); return true;
}
static void standing(void) {
    clock_us += 10000; raw_timer.timerawl = clock_us; test_core = 1;
    assert(era_split_communication_core_standing_service_once(7));
}
static void scenario(unsigned mode, bool sections) {
    host_owner = 7; host_relation = 9; room = 66; drop_answer = false;
    memset(&g_era_split_communication_core_standing_private, 0, sizeof(g_era_split_communication_core_standing_private));
    memset(&g_era_split_communication_core_standing_state, 0, sizeof(g_era_split_communication_core_standing_state));
    open();
    memset(&g_era_matrix_engine, 0, sizeof(g_era_matrix_engine));
    memset(matrix_previous, 0, sizeof(matrix_previous)); input_previous_host = false;
    g_era_matrix_engine.that_hand = 6; g_era_matrix_engine.host_peer_local_matrix_ready = true;
    publish(true, false);
    matrix_row_t rows[6] = {1}; era_split_input_source_snapshot(rows, clock_us);
    assert(input() && matrix_previous[6] == 1);
    assert(!source.count && !source.pending_valid && !era_split_input_source_pressure());
    standing(); assert(last_request_length == 1 && last_response_length == 1);
    assert(!era_split_input_source_service_needed());
    if (mode == 1 || mode == 2) {
        rows[0] = 3; era_split_input_source_snapshot(rows, clock_us);
        era_split_input_request_t pending = prepare();
        if (mode == 2) { clock_us = pending.born_deadline_us; assert(!era_split_input_source_due(clock_us)); }
    }
    // Actual HOST diagnostic-reset effects; PEER gate/USB facts stay unchanged.
    era_split_input_receiver_gate(false, 0, 0); era_matrix_engine_flush_host_peer_relation();
    host_owner = 8; host_relation = 10; era_split_input_receiver_gate(true, host_owner, host_relation);
    room = 0; action(); assert(matrix_previous[6] == 1); // quota blocks closure
    room = 66; action(); assert(matrix_previous[6] == 0); action();
    publish(true, sections);
    drop_answer = mode == 3; standing();
    if (drop_answer) {
        assert(g_era_split_communication_core_standing_state.stopped);
        uint16_t stop = era_split_communication_core_standing_stop_generation(7, 9);
        test_core = 0; assert(era_split_communication_core_resume_standing(7, 9, stop));
        drop_answer = false; standing(); // model the existing successful SESSION recovery boundary
    }
    assert(answer.payload[1] == ERA_INPUT_REBIND);
    assert(last_request_length == (mode == 2 ? 2 : 1));
    assert(last_response_length == (sections ? 4 : 2));
    if (sections) assert(g_era_split_communication_core_standing_state.peer_lock_state_valid);
    assert(era_split_input_source_service_needed());
    if (mode == 4) { room = 0; action(); }
    assert(input() && source.phase == SOURCE_BIND);
    assert(input() && source.phase == SOURCE_BASELINE);
    if (mode == 4) {
        assert(input() && source.waiting && matrix_previous[6] == 0);
        era_split_input_request_t pending = source.pending;
        room = 66; action(); standing();
        assert(answer.payload[1] == ERA_INPUT_BASELINE_READY);
        assert(era_split_input_source_due(clock_us));
        assert(!memcmp(&pending, &source.pending, sizeof(pending)));
    }
    assert(input() && matrix_previous[6] == rows[0]);
    assert(!era_split_input_source_service_needed());
    publish(true, false); standing(); assert(last_request_length == 1 && last_response_length == 1);
    rows[0] = 0; era_split_input_source_snapshot(rows, clock_us);
    assert(input() && matrix_previous[6] == 0);
}

int main(void) {
    for (unsigned mode = 0; mode < 5; ++mode) for (unsigned sections = 0; sections < 2; ++sections) scenario(mode, sections);
    era_split_input_receiver_gate(false, 0, 0); publish(false, false);
    standing(); assert(last_request_length == 1 && last_response_length == 1);
    uint8_t flow_query[] = {0x41, ERA_INPUT_FLOW_QUERY};
    assert(!exchange(flow_query, sizeof(flow_query))); // DH cannot admit FLOW
    publish(true, false); // HP with a disabled receiver cannot request REBIND
    standing(); assert(last_response_length == 1);
    assert(exchange(flow_query, sizeof(flow_query)) && answer.payload[1] == ERA_INPUT_FLOW_RESET);
    assert(!era_split_transaction_engine_response_kind_matches(ERA_SPLIT_WIRE_PAYLOAD_GRANT_ACK,
        ERA_SPLIT_WIRE_PAYLOAD_HOST_PEER_HOST_SOURCE_RSP, ERA_SPLIT_WIRE_PAYLOAD_FLOW_RSP));
    assert(!era_split_transaction_engine_response_kind_matches(ERA_SPLIT_WIRE_PAYLOAD_HP_STANDING_RSP,
        ERA_SPLIT_WIRE_PAYLOAD_INVALID, ERA_SPLIT_WIRE_PAYLOAD_INPUT_RSP));
    puts("PASS standing-to-action recovery: idle held, active, FRESH, lost answer, sections, closure quota, final release; HP idle/DH stay bare");
    return 0;
}
