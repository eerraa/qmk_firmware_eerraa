// SPDX-License-Identifier: GPL-2.0-or-later
#define get_core_num() 1U
#define __DMB() __atomic_thread_fence(__ATOMIC_SEQ_CST)
#define __SEV() ((void)0)
#define main input_owner_tests
#include "input_history.c"
#undef main
#include "keyboards/era/common/split/communication_core/era_split_communication_core_host_peer_lanes.c"
era_split_communication_core_state_t g_era_split_communication_core;
static timer_hw_t raw_timer;
timer_hw_t *timer_hw=&raw_timer;
static unsigned tx_calls,prepare_calls,standing_calls;
static uint16_t stop;
static uint32_t standing_advance,encode_advance;
static bool wrong_identity;
static era_split_communication_core_initiator_result_t published_result;
era_split_communication_core_backend_access_t era_split_communication_core_owner_backend_access(uint16_t owner) { (void)owner; return ERA_SPLIT_COMMUNICATION_CORE_BACKEND_ACCESS_OK; }
bool era_split_communication_core_standing_service_once(uint16_t owner) { (void)owner; ++standing_calls; raw_timer.timerawl+=standing_advance; ++prepare_calls; return true; }
uint16_t era_split_communication_core_standing_stop_generation(uint16_t owner,uint16_t relation) { (void)owner;(void)relation;return stop; }
bool era_split_transaction_engine_prepare_control(bool ext,uint8_t *control,uint8_t *seq) { ++prepare_calls; *seq=prepare_calls; *control=(ext?0x40:0)|(*seq&7); raw_timer.timerawl+=encode_advance; return true; }
void era_split_transaction_engine_commit_received_frame(const era_split_wire_frame_t *frame) {(void)frame;}
bool era_split_communication_core_result_push(const era_split_communication_core_queue_record_t *r) {published_result=r->data.initiator_result;return true;}
era_split_transaction_engine_result_t era_split_transaction_engine_transact_compact_owned(
 era_split_wire_direction_t direction,const uint8_t *payload,uint8_t length,uint8_t seq,
 era_split_wire_payload_kind_t expected,era_split_wire_payload_kind_t alternate,
 uint16_t window,uint16_t owner,era_split_wire_frame_t *response,bool *sent,era_split_transaction_failure_t *failure) {
 (void)direction;(void)length;(void)expected;(void)alternate;(void)window;(void)owner;(void)failure;
 ++tx_calls; assert(seq==prepare_calls); *sent=true;
 response->kind=ERA_SPLIT_WIRE_PAYLOAD_INPUT_RSP;response->payload_len=6;
 response->payload[0]=0x41;response->payload[1]=ERA_INPUT_ACCEPTED;
 memcpy(response->payload+2,payload+2,4);
 if(wrong_identity) ++response->payload[4];
 return ERA_SPLIT_TRANSACTION_RESULT_OK;
}
int main(void) {
 open(); const unsigned states[]={1}; snapshots(states,1); era_split_input_request_t input=prepare();
 era_split_communication_core_queue_record_t record={.kind=ERA_SPLIT_COMMUNICATION_CORE_QUEUE_RECORD_INITIATOR_REQUEST,
 .data.initiator_request={.owner_epoch=7,.relation_generation=9,.request_generation=1,
 .lane=ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SOURCE_PUSH,.expected_kind=ERA_SPLIT_WIRE_PAYLOAD_INPUT_RSP,
 .response_window_ms=20,.not_after_us=100000}};
 record.data.initiator_request.semantic.source_push.input=input;
 raw_timer.timerawl=clock_us; era_split_communication_core_process_initiator(&record);
 assert(tx_calls==1 && standing_calls==1 && prepare_calls==2 && published_result.result==ERA_SPLIT_TRANSACTION_RESULT_OK);
 wrong_identity=true;era_split_communication_core_process_initiator(&record); assert(published_result.result==ERA_SPLIT_TRANSACTION_RESULT_BAD);
 wrong_identity=false; stop=1; unsigned before=tx_calls; era_split_communication_core_process_initiator(&record);
 assert(tx_calls==before && published_result.failure==ERA_SPLIT_TRANSACTION_FAILURE_QUEUE_EXPIRED); // queued-before-stop
 stop=0; raw_timer.timerawl=input.born_deadline_us-1; encode_advance=1;
 era_split_communication_core_process_initiator(&record); assert(tx_calls==before); // age expires during encode
 encode_advance=0;raw_timer.timerawl=input.born_deadline_us-2;standing_advance=3;
 era_split_communication_core_process_initiator(&record);assert(tx_calls==before); // due standing wins even at deadline
 standing_advance=0;era_split_input_source_gate(false,0,0);raw_timer.timerawl=clock_us;
 era_split_communication_core_process_initiator(&record);assert(tx_calls==before); // producer revoked after enqueue
 // An old A response can never satisfy B, even if its authority facts match.
 era_split_wire_frame_t answer={.kind=ERA_SPLIT_WIRE_PAYLOAD_INPUT_FENCE_RSP,.payload_len=9};
 answer.payload[2]=ERA_SPLIT_WIRE_SESSION_STATUS_FLAG_HOST_OPEN;
 record.data.initiator_request.semantic.source_push.input.op=ERA_INPUT_BIND;
 assert(!era_split_communication_core_initiator_decode(&record.data.initiator_request,&answer,&published_result));
 answer.kind=ERA_SPLIT_WIRE_PAYLOAD_INPUT_BIND_RSP;answer.payload_len=11;
 era_split_wire_put16(answer.payload+9,input.epoch+1);
 assert(!era_split_communication_core_initiator_decode(&record.data.initiator_request,&answer,&published_result));
 era_split_wire_put16(answer.payload+9,input.epoch);
 assert(era_split_communication_core_initiator_decode(&record.data.initiator_request,&answer,&published_result));
 puts("PASS production initiator: due-standing/sequence ordering, typed stale identity, queued stop, final TX age/generation guards");
 return 0;
}
