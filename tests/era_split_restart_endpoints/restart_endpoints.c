// SPDX-License-Identifier: GPL-2.0-or-later
/* Complete production owners; physical transport, clock and final act only
 * are host seams. The scheduler's scoped restart apply branch and complete
 * response-shadow commit caller are inserted verbatim by the runner. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned test_core;
#define get_core_num() test_core
#define __DMB() __atomic_thread_fence(__ATOMIC_SEQ_CST)
#define __SEV() ((void)0)
#include "keyboards/era/common/split/era_split_restart_agreement.h"
/* ENDPOINT_DECLARATIONS */
bool era_split_restart_intent_valid(uint8_t a,uint8_t p) { return e0_era_split_restart_intent_valid(a,p); }
bool era_split_restart_authority_valid(uint8_t a,uint8_t p,bool armed) { return e0_era_split_restart_authority_valid(a,p,armed); }
bool era_split_restart_arm_valid(uint8_t a,uint8_t p,uint32_t t) { return e0_era_split_restart_arm_valid(a,p,t); }
#include "keyboards/era/common/split/era_split_input.c"
#include "keyboards/era/common/split/era_split_matrix_frame.c"
#include "keyboards/era/common/split/era_split_wire_payload.c"
#include "keyboards/era/common/split/era_host_peer_responder.c"
/* PRODUCTION_CODEC */
#include "keyboards/era/common/split/communication_core/era_split_communication_core_standing.c"
#include "keyboards/era/common/split/communication_core/era_split_communication_core_responder.c"
#include "keyboards/era/common/split/communication_core/era_split_communication_core_responder_service.c"

era_split_communication_core_state_t g_era_split_communication_core;
static timer_hw_t raw_timer;
timer_hw_t *timer_hw = &raw_timer;
static uint32_t clock_ms;
static unsigned executes, resets;
static uint16_t relation = 9;
static uint32_t standing_observed;
static era_split_wire_frame_t incoming, physical_reply;
static unsigned response_count;
static bool drop_reply, fail_exchange;
static struct { bool responder_snapshot_publish_due; } g_era_split_transport_scheduler;
uint32_t timer_read32(void) { return clock_ms; }
uint32_t timer_elapsed32(uint32_t since) { return clock_ms-since; }
uint32_t sync_timer_read32(void) { return clock_ms; }
uint32_t era_split_input_clock(void) { return raw_timer.timerawl; }
bool era_split_restart_prepare_local(era_split_restart_act_t act,uint8_t param) { (void)act;(void)param;++executes;return true; }
bool era_split_restart_arm_ready(era_split_restart_act_t act) { (void)act;return true; }
void soft_reset_keyboard(void) { ++resets; }
void era_split_communication_core_init(void) {}
void era_host_peer_matrix_link_note_ack_status_sent(void) {}
era_split_communication_core_backend_access_t era_split_communication_core_owner_backend_access(uint16_t owner) {
    (void)owner; return ERA_SPLIT_COMMUNICATION_CORE_BACKEND_ACCESS_OK;
}
void era_split_transaction_backend_arm_core1_idle_wake(uint32_t deadline) { (void)deadline; }
void era_split_transaction_engine_reset_link_state(void) {}
void era_split_transaction_engine_commit_received_frame(const era_split_wire_frame_t *frame) { (void)frame; }
bool era_split_transaction_engine_prepare_control(bool ext,uint8_t *control,uint8_t *seq) {
    *control=1|(ext?ERA_SPLIT_WIRE_CONTROL_EXT:0);*seq=1;return true;
}
era_split_transaction_engine_result_t era_split_transaction_engine_receive_idle_owned(
    era_split_wire_direction_t direction,uint16_t first,uint16_t window,uint16_t owner,
    uint8_t *wire,uint16_t capacity,era_split_wire_frame_t *frame,era_split_transaction_failure_t *failure) {
    (void)direction;(void)first;(void)window;(void)owner;(void)wire;(void)capacity;(void)failure;
    *frame=incoming;return ERA_SPLIT_TRANSACTION_RESULT_OK;
}
era_split_transaction_engine_result_t era_split_transaction_engine_send_compact_response_owned(
    era_split_wire_direction_t direction,const uint8_t *payload,uint8_t len,uint8_t seq,uint16_t owner,era_split_transaction_failure_t *failure) {
    (void)direction;(void)seq;(void)owner;(void)failure;
    ++response_count;memset(&physical_reply,0,sizeof(physical_reply));
    memcpy(physical_reply.payload,payload,len);physical_reply.payload_len=len;
    assert(era_split_wire_classify_payload(payload,len,ERA_SPLIT_WIRE_FRAME_LANE_COMPACT,&physical_reply.kind));
    return ERA_SPLIT_TRANSACTION_RESULT_OK;
}
era_split_transaction_engine_result_t era_split_transaction_engine_transact_compact_owned(
    era_split_wire_direction_t direction,const uint8_t *payload,uint8_t len,uint8_t seq,
    era_split_wire_payload_kind_t expected,era_split_wire_payload_kind_t alternate,
    uint16_t window,uint16_t owner,era_split_wire_frame_t *response,bool *sent,era_split_transaction_failure_t *failure) {
    (void)direction;(void)seq;(void)expected;(void)alternate;(void)window;(void)failure;
    if (sent!=NULL) *sent=true;
    if (fail_exchange) return ERA_SPLIT_TRANSACTION_RESULT_FAIL;
    memset(&incoming,0,sizeof(incoming));memcpy(incoming.payload,payload,len);incoming.payload_len=len;
    assert(era_split_wire_classify_payload(payload,len,ERA_SPLIT_WIRE_FRAME_LANE_COMPACT,&incoming.kind));
    unsigned before=response_count;
    assert(era_split_communication_core_responder_service_once(owner));
    assert(response_count==before+1);*response=physical_reply;
    if (drop_reply) { response->kind=ERA_SPLIT_WIRE_PAYLOAD_GRANT_ACK;response->payload_len=1;response->payload[0]=1; }
    return ERA_SPLIT_TRANSACTION_RESULT_OK;
}

/* PRODUCTION_RESPONSE_COMMIT */
static void apply_restart(const era_split_communication_core_responder_result_t *result) {
    uint8_t eligible_push=ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_RESTART_ARM;
    /* PRODUCTION_RESTART_APPLY */
}
static void drain_peer(void) {
    test_core=0;
    era_split_communication_core_responder_result_t result;
    while (era_split_communication_core_drain_responder_result(&result)) {
        assert(era_split_communication_core_responder_result_matches_current(&result));
        apply_restart(&result);
        era_split_transport_scheduler_commit_communication_core_host_response(&result);
    }
}
static void publish_peer(void) {
    test_core=0;
    era_split_wire_authority_section_t authority={.accepted_host_open=true};
    e1_era_split_restart_agreement_fill_authority(&authority);
    era_host_peer_transaction_responder_response_plan_t plan;
    era_host_peer_transaction_prepare_responder_response(0,0,0,&authority,NULL,
        ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_AUTHORITY,&plan);
    era_split_communication_core_responder_snapshot_t snapshot={
        .owner_epoch=7,.relation_generation=relation,.owner_gate_ready=true,
        .heartbeat_allowed=true,.runtime_push_allowed=true,.runtime_allowed=true,
        .eligible_push_sections=ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_AUTHORITY|ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_RESTART_ARM,
        .eligible_rsp_sections=ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_AUTHORITY,
        .authority=plan.authority,
        .response_section_mask=plan.send_authority?ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_AUTHORITY:0};
    assert(era_split_communication_core_publish_responder_snapshot(&snapshot));
}
static void publish_initiator(void) {
    test_core=0;
    era_split_communication_core_standing_plan_t plan={.owner_epoch=7,.relation_generation=relation,
        .enabled=true,.poll_period_ms=1,.liveness_period_ms=1,
        .eligible_push_sections=ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_AUTHORITY|ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_RESTART_ARM,
        .eligible_rsp_sections=ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_AUTHORITY,
        .authority_valid=true,.authority={.accepted_host_open=true}};
    e0_era_split_restart_agreement_fill_authority(&plan.authority);
    e0_era_split_restart_agreement_arm_section(&plan.restart_act,&plan.restart_param,&plan.restart_commit_ms);
    e0_era_split_restart_agreement_restart_generation(&plan.restart_generation);
    assert(era_split_communication_core_publish_standing_plan(&plan));
}
static void apply_initiator(void) {
    test_core=0;uint32_t seq;
    era_split_communication_core_standing_state_t state;
    assert(era_split_communication_core_read_standing_state(&state,&seq));
    if (seq==standing_observed) return;
    standing_observed=seq;
    if (state.owner_epoch!=7 || state.relation_generation!=relation) return;
    if (state.peer_authority_valid) {
        e0_era_split_restart_agreement_note_peer_authority(&state.peer_authority);
        e0_era_split_restart_agreement_note_restart_receipt(state.restart_receipt_generation);
    }
}
static void pump(uint32_t now) {
    clock_ms=now;raw_timer.timerawl=now*1000;test_core=1;
    assert(era_split_communication_core_standing_service_once(7));
    apply_initiator();
}
static uint32_t arm(void) {
    uint8_t act,param;uint32_t deadline;
    e0_era_split_restart_agreement_arm_section(&act,&param,&deadline);
    return deadline;
}
static void fresh(void) {
    e0_era_split_restart_agreement_test_reset();e1_era_split_restart_agreement_test_reset();
    memset(&g_era_split_communication_core,0,sizeof(g_era_split_communication_core));
    memset(&g_era_split_communication_core_standing_plan,0,sizeof(g_era_split_communication_core_standing_plan));
    memset(&g_era_split_communication_core_standing_state,0,sizeof(g_era_split_communication_core_standing_state));
    memset(&g_era_split_communication_core_standing_private,0,sizeof(g_era_split_communication_core_standing_private));
    memset(&g_era_split_communication_core_responder_snapshot,0,sizeof(g_era_split_communication_core_responder_snapshot));
    memset(&g_era_split_communication_core_responder_restart_fence,0,sizeof(g_era_split_communication_core_responder_restart_fence));
    g_era_split_communication_core_standing_plan_seq=g_era_split_communication_core_standing_state_seq=g_era_split_communication_core_standing_state_change_seq=0;
    g_era_split_communication_core_responder_snapshot_publish_seq=g_era_split_communication_core_responder_snapshot_claim_generation=0;
    g_era_split_communication_core_responder_general_read=g_era_split_communication_core_responder_general_write=0;
    g_era_split_communication_core_responder_refresh_due=false;
    era_host_peer_transaction_forget_responder_input_layer();
    standing_observed=executes=resets=response_count=0;drop_reply=fail_exchange=false;relation=9;clock_ms=1000;
    e0_era_split_restart_agreement_note_relation(true,true,true);
    e1_era_split_restart_agreement_note_relation(true,false,false);
    publish_peer();publish_initiator();pump(1000);drain_peer();publish_peer();pump(1001);drain_peer();
    /* Leave the immutable AUTHORITY=IDLE snapshot installed even after the
       response's sent shadow is committed. This is the pre-arm cached body. */
    assert(g_era_split_communication_core_standing_state.peer_authority_valid);
    assert(g_era_host_peer_transaction_responder_authority_sent_valid);
    g_era_split_communication_core_responder_snapshot.authority =
        (era_split_wire_authority_section_t){.accepted_host_open=true};
    g_era_split_communication_core_responder_snapshot.response_section_mask = ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_AUTHORITY;
}
static uint32_t start_lost_echo(bool drain, bool retry) {
    clock_ms=1010;
    assert(e0_era_split_restart_agreement_request(ERA_SPLIT_RESTART_ACT_LINK_SPEED,1));
    e0_era_split_restart_agreement_task();uint32_t first=arm();assert(first==1130);
    publish_initiator();drop_reply=true;pump(1010);drop_reply=false;
    if (drain) { drain_peer();publish_peer(); }
    clock_ms=1070;e0_era_split_restart_agreement_task();assert(arm()==0);
    clock_ms=1071;
    if (retry) {
        assert(e0_era_split_restart_agreement_request(ERA_SPLIT_RESTART_ACT_LINK_SPEED,1));
        e0_era_split_restart_agreement_task();assert(arm()==0);
    }
    return first;
}
static void finish_retry(uint32_t first) {
    uint32_t before=era_split_communication_core_standing_state_seq();
    publish_peer();pump(1074);
    assert(era_split_communication_core_standing_state_seq()!=before);
    e0_era_split_restart_agreement_task();assert(arm()!=0 && arm()!=first);
    uint32_t next=arm();
    era_split_wire_authority_section_t delayed={.accepted_host_open=true,
        .restart_act=ERA_SPLIT_RESTART_ACT_LINK_SPEED,.restart_param=1,.restart_armed=true};
    e0_era_split_restart_agreement_note_peer_authority(&delayed);
    assert(!e0_era_split_restart_agreement_commit_agreed());
    publish_initiator();pump(1075);
    assert(!e0_era_split_restart_agreement_commit_agreed());
    drain_peer();publish_peer();pump(1076);
    uint32_t a,b;assert(e0_era_split_restart_agreement_agreed_deadline(&a));
    assert(e1_era_split_restart_agreement_agreed_deadline(&b));assert(a==b && a==next);
    /* Raw cached/delayed authority alone cannot create another confirmation. */
    drain_peer();publish_peer();clock_ms=next;
    e0_era_split_restart_agreement_task();e1_era_split_restart_agreement_task();assert(executes==2);
    puts("PASS same act/param retry: transmitted IDLE -> applied fresh authority -> one shared deadline");
}
static void coalesced_idle_and_cached_prearm(void) {
    fresh();uint32_t first=start_lost_echo(false,true);
    publish_initiator();pump(1072); // ARM and IDLE now queued under the same pre-arm snapshot.
    uint16_t before=g_era_split_communication_core_responder_snapshot.snapshot_generation;
    pump(1073);e0_era_split_restart_agreement_task();assert(arm()==0);
    assert(g_era_split_communication_core_standing_state.restart_receipt_generation==0);
    drain_peer();assert(!e1_era_split_restart_agreement_commit_agreed() || !e1_era_split_restart_agreement_timed_window());
    publish_peer();assert(g_era_split_communication_core_responder_snapshot.snapshot_generation!=before);
    finish_retry(first);
    puts("PASS cached pre-arm IDLE cannot prove queued retirement; equal final body gets fresh generation and shadow");
}
static void dropped_idle_and_cached_echo(void) {
    fresh();uint32_t first=start_lost_echo(true,true);
    era_split_wire_authority_section_t old={.accepted_host_open=true};e1_era_split_restart_agreement_fill_authority(&old);
    e0_era_split_restart_agreement_note_peer_authority(&old);e0_era_split_restart_agreement_task();assert(arm()==0);
    publish_initiator();pump(1072);pump(1073);e0_era_split_restart_agreement_task();assert(arm()==0);
    drain_peer();finish_retry(first);
}
static void rotation_and_standalone(void) {
    fresh();start_lost_echo(true,false);
    uint32_t retired;e0_era_split_restart_agreement_restart_generation(&retired);
    e0_era_split_restart_agreement_note_relation_rotation();
    e0_era_split_restart_agreement_note_restart_receipt(retired);e0_era_split_restart_agreement_task();assert(arm()==0);
    e0_era_split_restart_agreement_note_relation(false,false,true);
    /* A fresh request after unplug retains its existing standalone behavior. */
    clock_ms=1072;
    assert(e0_era_split_restart_agreement_request(ERA_SPLIT_RESTART_ACT_LINK_SPEED,1));
    e0_era_split_restart_agreement_task();assert(e0_era_split_restart_agreement_timed_window());
    clock_ms=1192;e0_era_split_restart_agreement_task();assert(executes==1);
    assert(e0_era_split_restart_agreement_request(ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN,0));
    e0_era_split_restart_agreement_task();assert(resets==1);
    puts("PASS retired receipt fenced across rotation; unserviced LINK/CLEAN retain standalone policy");
}
static void fence_wrap_retirement(void) {
    fresh();start_lost_echo(false,true);publish_initiator();pump(1072);drain_peer();publish_peer();
    uint16_t old=g_era_split_communication_core_responder_restart_fence.snapshot_generation;
    pump(1073);assert(g_era_split_communication_core_responder_restart_fence.snapshot_generation==0);
    drain_peer();
    /* The old identity can recur after normal 16-bit wrap; an already retired
       fence must not be resurrected by that alias. No wire value is changed. */
    g_era_split_communication_core_responder_snapshot.snapshot_generation=old;
    g_era_split_communication_core.responder_snapshot_generation=old;
    g_era_split_communication_core_responder_snapshot.response_section_mask=ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_AUTHORITY;
    pump(1074);
    era_host_peer_transaction_result_t decoded={0};assert(era_host_peer_transaction_extract_sections(&physical_reply,&decoded));
    assert(decoded.host_source_authority_valid);
    puts("PASS observed new snapshot retires old fence before snapshot-generation alias");
}
static void completed_same_tuple_with_delayed_peer_task(void) {
    const uint8_t acts[]={ERA_SPLIT_RESTART_ACT_LINK_SPEED,ERA_SPLIT_RESTART_ACT_LINK_RECOVERED};
    for (unsigned i=0;i<2;++i) {
        fresh();clock_ms=1010;
        uint8_t param=acts[i]==ERA_SPLIT_RESTART_ACT_LINK_SPEED?1:0;
        assert(e0_era_split_restart_agreement_request(acts[i],param));e0_era_split_restart_agreement_task();
        publish_initiator();pump(1010);drain_peer();publish_peer();pump(1011);drain_peer();
        assert(e0_era_split_restart_agreement_commit_agreed());
        clock_ms=1130;e0_era_split_restart_agreement_task(); // Peer's due cold task is deliberately delayed.
        assert(e0_era_split_restart_agreement_request(acts[i],param));e0_era_split_restart_agreement_task();assert(arm()==0);
        publish_initiator();pump(1131);drain_peer();publish_peer();pump(1132);
        e0_era_split_restart_agreement_task();assert(arm()==0);drain_peer();
        clock_ms=1133;e1_era_split_restart_agreement_task();publish_peer();pump(1134);
        e0_era_split_restart_agreement_task();assert(arm()==1254);
        publish_initiator();pump(1135);drain_peer();publish_peer();pump(1136);
        uint32_t a,b;assert(e0_era_split_restart_agreement_agreed_deadline(&a));
        assert(e1_era_split_restart_agreement_agreed_deadline(&b));assert(a==b && a==1254);
    }
    puts("PASS same tuple after local completion waits for peer's delayed due task; RATE and RECOVERED");
}
static void role_flip_receipt_preserves_confirmed_arm(void) {
    fresh();uint32_t first=start_lost_echo(true,false);
    clock_ms=1071;
    e0_era_split_restart_agreement_note_relation_rotation();
    e0_era_split_restart_agreement_note_relation(true,false,true);
    e1_era_split_restart_agreement_note_relation_rotation();
    e1_era_split_restart_agreement_note_relation(true,true,false);
    uint8_t act,param;uint32_t carried;
    e1_era_split_restart_agreement_arm_section(&act,&param,&carried);assert(carried==first);
    e0_era_split_restart_agreement_note_peer_arm(act,param,carried);
    uint32_t a,b;assert(e0_era_split_restart_agreement_agreed_deadline(&a));
    assert(e1_era_split_restart_agreement_agreed_deadline(&b));assert(a==b && a==first);
    // Flip back before the accepted instant. The same deadline is now carried
    // by an initiator that still had a retirement obligation before adoption.
    e0_era_split_restart_agreement_note_relation_rotation();
    e0_era_split_restart_agreement_note_relation(true,true,true);
    e1_era_split_restart_agreement_note_relation_rotation();
    e1_era_split_restart_agreement_note_relation(true,false,false);
    relation=10;
    publish_initiator();publish_peer();pump(1072);drain_peer();publish_peer();pump(1073);
    e0_era_split_restart_agreement_arm_section(&act,&param,&carried);
    assert(act==ERA_SPLIT_RESTART_ACT_LINK_SPEED && param==1 && carried==first);
    assert(era_split_restart_arm_valid(act,param,carried));
    assert(e0_era_split_restart_agreement_agreed_deadline(&a));
    assert(e1_era_split_restart_agreement_agreed_deadline(&b));assert(a==b && a==first);
    clock_ms=first;e0_era_split_restart_agreement_task();e1_era_split_restart_agreement_task();assert(executes==2);
    puts("PASS confirmed adoption retires prior disarm wait; role flip receipt preserves valid arm and original pair deadline");
}
static void clean_prepare_pair(void) {
    fresh();clock_ms=1010;
    assert(e0_era_split_restart_agreement_request(ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN,0));
    e0_era_split_restart_agreement_task();
    assert(e0_era_split_restart_agreement_storage_quarantined());
    assert(arm()==0 && resets==0);
    publish_initiator();pump(1010);drain_peer();
    assert(e1_era_split_restart_agreement_storage_quarantined());
    e1_era_split_restart_agreement_task();e0_era_split_restart_agreement_task();
    assert(executes==2 && resets==0);
    publish_peer();pump(1011);drain_peer();
    assert(!e0_era_split_restart_agreement_commit_agreed());
    e0_era_split_restart_agreement_task();
    uint8_t act,param;uint32_t deadline;
    e0_era_split_restart_agreement_arm_section(&act,&param,&deadline);
    assert(act==ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN);
    assert(param==ERA_SPLIT_RESTART_CLEAN_PARAM_COMMIT && deadline==1131);
}
static void clean_actual_receipt_commits(void) {
    clean_prepare_pair();uint32_t first=arm();
    publish_initiator();pump(1012);
    assert(!e0_era_split_restart_agreement_commit_agreed() && resets==0);
    drain_peer();publish_peer();pump(1013);
    uint32_t a,b;
    assert(e0_era_split_restart_agreement_agreed_deadline(&a));
    assert(e1_era_split_restart_agreement_agreed_deadline(&b));
    assert(a==b && a==first && resets==0);
    clock_ms=first-1;e0_era_split_restart_agreement_task();e1_era_split_restart_agreement_task();
    assert(resets==0);
    clock_ms=first;e0_era_split_restart_agreement_task();e1_era_split_restart_agreement_task();
    assert(resets==2 && executes==2);
    puts("PASS CLEAN actual PREPARED/COMMIT receipt: no sending-exchange confirmation, one shared deadline, two resets");
}
static void clean_coalesced_commit_and_idle(void) {
    clean_prepare_pair();uint32_t first=arm();
    publish_initiator();drop_reply=true;pump(1012);drop_reply=false;
    /* The responder's COMMIT result is queued, not yet applied. Its immutable
       PREPARED vote must not retire IDLE on a later exchange until cold apply. */
    clock_ms=1071;e0_era_split_restart_agreement_task();assert(arm()==0);
    uint32_t retired;e0_era_split_restart_agreement_restart_generation(&retired);
    era_split_wire_authority_section_t old={.accepted_host_open=true,
        .restart_act=ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN,
        .restart_param=ERA_SPLIT_RESTART_CLEAN_PARAM_PREPARED,.restart_armed=true};
    e0_era_split_restart_agreement_note_peer_authority(&old);
    e0_era_split_restart_agreement_task();assert(arm()==0);
    publish_initiator();pump(1072);
    pump(1073);e0_era_split_restart_agreement_task();assert(arm()==0);
    assert(g_era_split_communication_core_standing_state.restart_receipt_generation!=retired);
    drain_peer();publish_peer();pump(1074);
    assert(g_era_split_communication_core_standing_state.restart_receipt_generation==retired);
    e0_era_split_restart_agreement_task();uint32_t next=arm();
    assert(next==1194 && next!=first && resets==0);
    publish_initiator();pump(1075);
    assert(!e0_era_split_restart_agreement_commit_agreed());
    drain_peer();publish_peer();pump(1076);
    uint32_t a,b;
    assert(e0_era_split_restart_agreement_agreed_deadline(&a));
    assert(e1_era_split_restart_agreement_agreed_deadline(&b));
    assert(a==b && a==next);
    clock_ms=first;e0_era_split_restart_agreement_task();e1_era_split_restart_agreement_task();
    assert(resets==0);
    clock_ms=next;e0_era_split_restart_agreement_task();e1_era_split_restart_agreement_task();
    assert(resets==2 && executes==2);
    puts("PASS CLEAN lost COMMIT echo: unapplied COMMIT/IDLE cannot reuse cached PREPARED; fresh IDLE receipt rearms once");
}

int main(void) {
#ifdef ERA_RESTART_ROLE_FLIP_PROBE
    role_flip_receipt_preserves_confirmed_arm();
#elif defined(ERA_RESTART_CLEAN_PROBE)
    clean_actual_receipt_commits();clean_coalesced_commit_and_idle();
    puts("Hardware/clock/transport/final CLEAN prepare/reset simulated. No physical boot-replay or NVM guarantee is claimed.");
#else
    coalesced_idle_and_cached_prearm();dropped_idle_and_cached_echo();rotation_and_standalone();fence_wrap_retirement();
    completed_same_tuple_with_delayed_peer_task();
#endif
    puts("Hardware/clock/transport simulated; no electrical timing or real hardware result claimed.");return 0;
}
