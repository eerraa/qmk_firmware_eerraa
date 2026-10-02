// SPDX-License-Identifier: GPL-2.0-or-later
#include <stdlib.h>
static unsigned test_core;
#define get_core_num() test_core
#define __DMB() __atomic_thread_fence(__ATOMIC_SEQ_CST)
#define __SEV() ((void)0)
#define main input_owner_tests
#include "input_history.c"
#undef main
#include "keyboards/era/common/split/communication_core/era_split_communication_core_responder.c"
#include "keyboards/era/common/split/communication_core/era_split_communication_core_responder_service.c"
era_split_communication_core_state_t g_era_split_communication_core;
static timer_hw_t raw_timer;
timer_hw_t *timer_hw=&raw_timer;
static era_split_wire_frame_t incoming;
static unsigned ack_calls, link_resets;
static bool fail_ack, fail_receive;
static uint8_t last_ack;
void era_split_communication_core_init(void) {}
era_split_communication_core_backend_access_t era_split_communication_core_owner_backend_access(uint16_t owner) {(void)owner;return ERA_SPLIT_COMMUNICATION_CORE_BACKEND_ACCESS_OK;}
bool era_host_peer_transaction_encode_rgb_state_body(const era_host_peer_rgb_state_t *s,uint8_t p[ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_RGB_STATE_BYTES]) {(void)s;(void)p;abort();}
bool era_host_peer_transaction_decode_rgb_state_body(const uint8_t *p,era_host_peer_rgb_state_t *s) {(void)p;(void)s;abort();}
void era_split_transaction_engine_reset_link_state(void) {++link_resets;}
void era_split_transaction_engine_commit_received_frame(const era_split_wire_frame_t *f) {(void)f;}
bool era_split_transaction_engine_prepare_control(bool ext,uint8_t *ctrl,uint8_t *seq) {*ctrl=1|(ext?0x40:0);*seq=1;return true;}
era_split_transaction_engine_result_t era_split_transaction_engine_receive_idle_owned(
 era_split_wire_direction_t direction,uint16_t first,uint16_t window,uint16_t owner,uint8_t *wire,uint16_t capacity,era_split_wire_frame_t *frame,era_split_transaction_failure_t *failure) {
 (void)direction;(void)first;(void)window;(void)owner;(void)wire;(void)capacity;
 if(fail_receive) {*failure=ERA_SPLIT_TRANSACTION_FAILURE_DECODE;return ERA_SPLIT_TRANSACTION_RESULT_BAD;}
 *frame=incoming;return ERA_SPLIT_TRANSACTION_RESULT_OK;
}
era_split_transaction_engine_result_t era_split_transaction_engine_send_compact_response_owned(
 era_split_wire_direction_t direction,const uint8_t *p,uint8_t n,uint8_t seq,uint16_t owner,era_split_transaction_failure_t *failure) {
 (void)direction;(void)seq;(void)owner;(void)failure; ++ack_calls;
 if(incoming.kind==ERA_SPLIT_WIRE_PAYLOAD_INPUT) { assert(n==6);last_ack=p[1];
     if(last_ack==ERA_INPUT_ACCEPTED || last_ack==ERA_INPUT_ACCEPTED_STOP) assert(receiver.receipt_valid); }
 return fail_ack?ERA_SPLIT_TRANSACTION_RESULT_FAIL:ERA_SPLIT_TRANSACTION_RESULT_OK;
}
static unsigned publish_serial;
static void publish(bool hp) {
 era_split_communication_core_responder_snapshot_t snapshot={.owner_epoch=7,.relation_generation=9,.owner_gate_ready=true,
 .host_matrix_admitted=hp,.input_layer=++publish_serial,.heartbeat_allowed=true,.session_allowed=true,.runtime_push_allowed=true,
 .eligible_push_sections=hp?ERA_SPLIT_WIRE_SECTION_ELIGIBLE_HOST_PEER_PUSH:ERA_SPLIT_WIRE_SECTION_ELIGIBLE_DUAL_HOST_PUSH,
 .eligible_rsp_sections=hp?ERA_SPLIT_WIRE_SECTION_ELIGIBLE_HOST_PEER_RSP:ERA_SPLIT_WIRE_SECTION_ELIGIBLE_DUAL_HOST_RSP,
 .local_session_status={.accepted_host_open=true}};
 test_core=0; assert(era_split_communication_core_publish_responder_snapshot(&snapshot)); test_core=1;
}
static void inject(const era_split_input_request_t *r) {
 uint8_t length;assert(era_split_input_encode(1,r,incoming.payload,&length));incoming.payload_len=length;
 assert(era_split_wire_classify_payload(incoming.payload,length,ERA_SPLIT_WIRE_FRAME_LANE_COMPACT,&incoming.kind));
 incoming.tx_seq=1; raw_timer.timerawl=clock_us;
 assert(era_split_communication_core_responder_service_once(7));
}
int main(void) {
 open(); publish(true);
 // Runtime result ring full does not take input packet or baseline capacity.
 g_era_split_communication_core_responder_general_write=3;
 era_split_input_request_t r=delta(1,0);fail_ack=true;inject(&r);
 assert(ack_calls==1 && last_ack==ERA_INPUT_ACCEPTED && receiver.write!=receiver.read);
 assert(claim(66) && previous[0]==1);era_split_input_finish();
 fail_ack=false;inject(&r);assert(ack_calls==2 && !claim(66)); // durable receipt after ACK TX failure/consumption
 uint32_t before=receiver.write;fail_receive=true;inject(&r);assert(receiver.write==before && link_resets==1);fail_receive=false;
 g_era_split_communication_core_responder_general_read=3;
 // Pending input pin alone cannot prevent a current runtime plan publication.
 r=delta(2,0);inject(&r);assert(receiver.write!=receiver.read);
 publish(true);
 // DH parser recognizes input, but service creates no receipt/epoch/action.
 era_split_input_receiver_gate(false,7,10);g_era_split_communication_core_responder_general_read=g_era_split_communication_core_responder_general_write;
 publish(false);before=ack_calls;r=delta(3,1);inject(&r);assert(ack_calls==before);
 puts("PASS production responder: full general ring input independence, publish before ACK failure, persistent duplicate receipt, decode failure, DH closed admission");
 return 0;
}
