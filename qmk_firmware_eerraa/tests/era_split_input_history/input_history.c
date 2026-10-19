// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later
/* Run the production source/receiver/codec. Private state is visible solely
 * to inject lifetime exhaustion and interleavings, not to model the owners. */
#include <assert.h>
#include <stdio.h>
#include "keyboards/era/common/split/era_split_input.c"
#include "keyboards/era/common/split/era_split_matrix_frame.c"
#include "keyboards/era/common/split/era_split_wire_payload.c"
static uint32_t clock_us;
static bool reset_at_clock;
uint32_t era_split_input_clock(void) {
    if (reset_at_clock) { reset_at_clock = false; era_split_input_receiver_end(); }
    return clock_us;
}
/* Runtime restart rules have their own production suite; only idle runtime
 * envelopes are exercised here. The test table supplies that unrelated seam. */
const era_split_restart_act_rules_t era_split_restart_act_rules[ERA_SPLIT_RESTART_ACT_MAX + 1] = {{0}};
static matrix_row_t previous[MATRIX_ROWS_PER_HAND];
static unsigned actions;
static void clear_owners(void) {
    memset(&source, 0, sizeof(source)); memset(&receiver, 0, sizeof(receiver));
    source_live_generation = source_identity = source_flow_wait = source_barrier = source_terminal = 0;
    flow_hint = flow_hint_generation = flow_hint_seq = flow_hint_seen = 0;
    memset(previous, 0, sizeof(previous)); clock_us = actions = 0; reset_at_clock = false;
    era_split_input_receiver_gate(true, 7, 9); era_split_input_receiver_capacity(true);
    era_split_input_source_gate(true, 7, 9);
    era_split_input_source_snapshot(previous, 0);
    era_split_wire_session_status_t facts = {.accepted_no_host=true, .matrix_ready=true, .usb_epoch=1};
    era_split_input_source_facts(&facts);
}
static era_split_input_request_t prepare(void) {
    era_split_input_request_t r; assert(era_split_input_source_prepare(&r, clock_us)); return r;
}
static uint8_t deliver(const era_split_input_request_t *r, uint8_t control) {
    uint8_t p[15], n;
    if (r->op == ERA_INPUT_FENCE || r->op == ERA_INPUT_BIND) {
        p[0] = control | ERA_SPLIT_WIRE_CONTROL_EXT; p[1] = r->op;
        memcpy(p+2,r->body,7); n=9;
        if (r->op == ERA_INPUT_BIND) { era_split_wire_put16(p+9,r->epoch); n=11; }
        era_split_wire_payload_kind_t kind;
        assert(era_split_wire_classify_payload(p,n,ERA_SPLIT_WIRE_FRAME_LANE_COMPACT,&kind));
        assert(era_split_input_control(p,n,7,9)); return 0;
    }
    assert(era_split_input_encode(control,r,p,&n));
    era_split_wire_payload_kind_t kind;
    assert(era_split_wire_classify_payload(p,n,ERA_SPLIT_WIRE_FRAME_LANE_COMPACT,&kind));
    assert(kind == ERA_SPLIT_WIRE_PAYLOAD_INPUT);
    return era_split_input_receive(p,n,7,9,clock_us);
}
static bool claim(uint8_t room) {
    matrix_row_t candidate[MATRIX_ROWS_PER_HAND];
    if (!era_split_input_claim(candidate,previous,room,true,clock_us)) return false;
    memcpy(previous,candidate,sizeof(previous)); ++actions; return true;
}
static void open(void) {
    clear_owners();
    era_split_input_request_t r = prepare(); assert(r.op==ERA_INPUT_FENCE);
    deliver(&r,1); era_split_input_source_complete(&r,true,0);
    r=prepare(); assert(r.op==ERA_INPUT_BIND); deliver(&r,2);
    /* Exact B retry after bind must not clear or install a second context. */
    uint32_t version=receiver.version; deliver(&r,3); assert(receiver.version==version);
    era_split_input_source_complete(&r,true,0);
    r=prepare(); assert(r.op==ERA_INPUT_RESET && r.record==0);
    assert(deliver(&r,4)==ERA_INPUT_ACCEPTED);
    era_split_input_source_complete(&r,true,ERA_INPUT_ACCEPTED);
    assert(claim(66)); era_split_input_finish(); actions=0;
    assert(!era_split_input_source_due(clock_us));
}
static void snapshots(const unsigned *states, unsigned n) {
    matrix_row_t rows[6]={0};
    for (unsigned i=0;i<n;++i) { rows[0]=states[i]; era_split_input_source_snapshot(rows,++clock_us); }
}
static void order_and_receipt(void) {
    const unsigned cases[][4]={{1,0,1,0},{1,3,2,0},{1,0,0,0}};
    for (unsigned c=0;c<3;++c) {
        open(); unsigned n=c==2?2:4; snapshots(cases[c],n); era_split_input_request_t r=prepare();
        assert(r.op==ERA_INPUT_DELTA && r.groups==n && r.length==n);
        assert(deliver(&r,1)==ERA_INPUT_ACCEPTED);
        uint32_t accepted=receiver.packets[receiver.read].accepted_at;
        /* ACK TX fail/loss: receiver already owns the packet. Sender retains
         * immutable bytes and born; control TX/ACK sequence is not identity. */
        era_split_input_source_complete(&r,false,0);
        era_split_input_request_t retry=prepare(); assert(!memcmp(&r,&retry,sizeof(r)));
        for (unsigned i=0;i<n;++i) {
            assert(claim(66)); assert(previous[0]==cases[c][i]); era_split_input_finish();
        }
        clock_us+=100; assert(deliver(&retry,7)==ERA_INPUT_ACCEPTED);
        assert(receiver.packets[0].accepted_at==accepted || receiver.packets[1].accepted_at==accepted || receiver.packets[2].accepted_at==accepted);
        assert(!claim(66) && actions==n);
        era_split_input_source_complete(&retry,true,ERA_INPUT_ACCEPTED);
        era_split_input_source_complete(&retry,true,ERA_INPUT_ACCEPTED); // late completion
        assert(source.count==0 && !era_split_input_source_due(clock_us));
    }
}
static era_split_input_request_t delta(uint16_t record, uint8_t token) {
    era_split_input_request_t r={.op=ERA_INPUT_DELTA,.epoch=receiver.epoch,.record=record,.length=1,.groups=1}; r.body[0]=token|0x80; return r;
}
static era_split_input_request_t baseline(uint16_t epoch, unsigned bits) {
    era_split_input_request_t r={.op=ERA_INPUT_RESET,.epoch=epoch,.length=9};
    matrix_row_t rows[6]={bits}; assert(era_split_wire_pack_matrix(rows,r.body)); return r;
}
static void capacity_and_pin(void) {
    open(); era_split_input_request_t a=delta(1,0),b=delta(2,1),c=delta(3,2);
    assert(deliver(&a,1)==ERA_INPUT_ACCEPTED); assert(deliver(&b,2)==ERA_INPUT_ACCEPTED_STOP);
    assert(deliver(&c,3)==ERA_INPUT_WAIT);
    assert(claim(66)); uint32_t read=receiver.read; assert(previous[0]==1);
    era_split_input_receiver_end(); era_split_input_request_t reset=baseline(2,8);
    assert(deliver(&reset,4)==ERA_INPUT_ACCEPTED_STOP);
    assert(receiver.read==read && receiver.packets[read].body[0]==a.body[0]);
    // One baseline holder, even when regular ring is full and pinned.
    era_split_input_request_t newer=baseline(3,16);
    assert(deliver(&newer,5)==ERA_INPUT_WAIT);
    era_split_input_finish(); assert(receiver.read!=read);
    assert(claim(66)); assert(previous[0]==8); era_split_input_finish();
    assert(!claim(66)); // old epoch packet discarded, no replay
    assert(deliver(&reset,6)==ERA_INPUT_ACCEPTED); assert(!claim(66));
    assert(deliver(&newer,7)==ERA_INPUT_ACCEPTED);
    clock_us+=ERA_INPUT_RECEIVER_AGE_US;
    assert(!claim(66)); // retired-not-applied
    assert(deliver(&newer,1)==ERA_INPUT_RESET_REQUIRED); // cannot revive same receipt
    newer=baseline(4,0); assert(deliver(&newer,2)==ERA_INPUT_ACCEPTED);
    assert(claim(66) && previous[0]==0); era_split_input_finish();
}
static void full_group_and_reset_race(void) {
    open(); era_split_input_request_t full8={.op=ERA_INPUT_FULL,.epoch=receiver.epoch,.record=1,.length=9,.groups=1};
    matrix_row_t eight[6]={255};assert(era_split_wire_pack_matrix(eight,full8.body));
    assert(deliver(&full8,1)==ERA_INPUT_ACCEPTED);assert(!claim(7) && !actions);
    assert(claim(8) && previous[0]==255);era_split_input_finish();

    open(); matrix_row_t rows[6]; for(unsigned i=0;i<6;++i) rows[i]=0x7ff;
    era_split_input_source_snapshot(rows,++clock_us); era_split_input_request_t r=prepare();
    assert(r.op==ERA_INPUT_FULL && r.groups==1 && r.length==9);
    assert(deliver(&r,1)==ERA_INPUT_ACCEPTED);
    assert(!claim(65) && previous[0]==0 && actions==0); // no partial FULL
    reset_at_clock=true; assert(!claim(66) && actions==0); // reset during final age sample
    era_split_input_request_t reset=baseline(2,3); assert(deliver(&reset,2)==ERA_INPUT_ACCEPTED);
    assert(claim(66) && previous[0]==3); era_split_input_finish();
}
static void deadlines_and_recovery(void) {
    open(); const unsigned states[]={1,0}; snapshots(states,2); era_split_input_request_t r=prepare();
    clock_us=r.born_deadline_us-1; assert(era_split_input_source_tx_allowed(&r,7,9,clock_us));
    ++clock_us; assert(!era_split_input_source_tx_allowed(&r,7,9,clock_us));
    assert(!era_split_input_source_due(clock_us) && era_split_input_source_terminal());
    era_split_input_receiver_end();
    era_split_input_source_flow(era_split_input_receiver_flow(clock_us),era_split_input_source_generation());
    r=prepare(); assert(r.op==ERA_INPUT_RESET && r.epoch==2);
    // RESET2 never arrives. Its abandoned epoch is not reused; RESET3 can
    // install into a terminal epoch1 without a permanent exact +1 deadlock.
    clock_us+=ERA_INPUT_SENDER_AGE_US; assert(!era_split_input_source_due(clock_us));
    era_split_input_receiver_end();
    era_split_input_source_flow(era_split_input_receiver_flow(clock_us),era_split_input_source_generation());
    r=prepare(); assert(r.epoch==3 && deliver(&r,1)==ERA_INPUT_ACCEPTED);
    era_split_input_source_complete(&r,true,ERA_INPUT_ACCEPTED); assert(claim(66)); era_split_input_finish();
    // HISTORY saturation retains latest separately and cancels old TX generation.
    for(unsigned i=0;i<19;++i) { matrix_row_t rows[6]={i&1?1:2}; era_split_input_source_snapshot(rows,++clock_us); }
    assert(source.count==0 && source.latest[0]==2 && era_split_input_source_terminal());
    uint32_t old=era_split_input_source_generation(); era_split_input_source_gate(false,0,0); era_split_input_source_gate(true,7,10);
    era_split_input_source_flow(ERA_INPUT_BASELINE_READY,old); (void)era_split_input_source_due(clock_us);
    assert(source.phase==SOURCE_FENCE); // late FLOW cannot relabel new lifetime
    // Receiver gate/version saturation never reopens a reused token.
    receiver.gate_seq=UINT32_MAX-1; era_split_input_receiver_gate(true,7,10); assert(!receiver.gate_enabled);
}
static void wait_and_wrap(void) {
    clear_owners(); era_split_input_request_t r=prepare(); deliver(&r,1); era_split_input_source_complete(&r,true,0);
    r=prepare(); deliver(&r,2); era_split_input_source_complete(&r,true,0);
    era_split_input_receiver_capacity(false); r=prepare(); assert(deliver(&r,3)==ERA_INPUT_WAIT);
    era_split_input_source_complete(&r,true,ERA_INPUT_WAIT); era_split_input_receiver_capacity(true);
    era_split_input_source_flow(era_split_input_receiver_flow(clock_us),era_split_input_source_generation());
    era_split_input_request_t retry=prepare(); assert(!memcmp(&r,&retry,sizeof(r)) && retry.epoch==1);
    assert(deliver(&retry,4)==ERA_INPUT_ACCEPTED); era_split_input_source_complete(&retry,true,ERA_INPUT_ACCEPTED);
    assert(claim(66)); era_split_input_finish();
    source.next_record=UINT16_MAX; receiver.expected=UINT16_MAX;
    unsigned states[]={1,0}; snapshots(states,2); r=prepare(); assert(r.groups==1);
    assert(deliver(&r,5)==ERA_INPUT_ACCEPTED); era_split_input_source_complete(&r,true,ERA_INPUT_ACCEPTED);
    assert(source.phase==SOURCE_FRESH); assert(claim(66)); era_split_input_finish();
    source.epoch=receiver.epoch=UINT16_MAX; era_split_input_receiver_end();
    era_split_input_source_flow(ERA_INPUT_BASELINE_READY,era_split_input_source_generation()); r=prepare(); assert(r.op==ERA_INPUT_FENCE && r.epoch==1);
    deliver(&r,6); era_split_input_source_complete(&r,true,0); r=prepare(); deliver(&r,7); era_split_input_source_complete(&r,true,0);
    r=prepare(); assert(deliver(&r,1)==ERA_INPUT_ACCEPTED); era_split_input_source_complete(&r,true,ERA_INPUT_ACCEPTED);
    assert(claim(66)); era_split_input_finish();
    clock_us=UINT32_MAX-20; snapshots(states,2); r=prepare(); clock_us=r.born_deadline_us-1;
    assert(era_split_input_source_tx_allowed(&r,7,9,clock_us)); ++clock_us; assert(!era_split_input_source_tx_allowed(&r,7,9,clock_us));
}
static void strict_schema_and_pressure(void) {
    open(); uint8_t p[16]={0x41,ERA_INPUT_DELTA,1,0,1,0,0x80};
    assert(era_split_input_validate(p,7)); p[6]=66|0x80; assert(!era_split_input_validate(p,7));
    p[6]=1; p[7]=1|0x80; assert(!era_split_input_validate(p,8)); // repeated position
    p[6]=2; p[7]=1|0x80; assert(!era_split_input_validate(p,8)); // unsorted
    p[6]=1; assert(!era_split_input_validate(p,7)); // no end-group
    p[6]=0x80; p[7]=0x80; p[4]=p[5]=0xff; assert(!era_split_input_validate(p,8));
    era_split_wire_payload_kind_t kind; uint8_t session[9],len;
    era_split_wire_session_status_t status={.accepted_no_host=true,.matrix_ready=true};
    assert(era_split_wire_encode_session_status(1,&status,session,&len)); assert(session[1]==ERA_INPUT_SESSION);
    session[1]=0x11; assert(!era_split_wire_classify_payload(session,len,ERA_SPLIT_WIRE_FRAME_LANE_COMPACT,&kind));
    session[1]=0x10; assert(!era_split_wire_classify_payload(session,9,ERA_SPLIT_WIRE_FRAME_LANE_COMPACT,&kind));
    p[1]=ERA_INPUT_FLOW_QUERY; assert(era_split_wire_classify_payload(p,2,ERA_SPLIT_WIRE_FRAME_LANE_COMPACT,&kind));
    assert(!era_split_wire_classify_payload(p,16,ERA_SPLIT_WIRE_FRAME_LANE_COMPACT,&kind));
    era_split_input_request_t r=delta(1,0); assert(deliver(&r,1)==ERA_INPUT_ACCEPTED);
    r.body[0]=0x81; assert(deliver(&r,2)==ERA_INPUT_MISMATCH && !claim(66));
    open(); era_split_input_receiver_pressure_event(); era_split_input_receiver_pressure(false);
    assert(era_split_input_receiver_flow(clock_us)==ERA_INPUT_BASELINE_READY); // sticky first overflow ends epoch
    assert(!era_split_input_claim(previous,previous,66,false,clock_us));
    era_split_input_receiver_gate(false,7,9); r=delta(1,0); assert(deliver(&r,1)==ERA_INPUT_RESET_REQUIRED); // closed/DH gate
}
static void receiver_lifetime_recovery(void) {
    for (unsigned waiting=0; waiting<2; ++waiting) {
        open(); const unsigned states[]={1}; snapshots(states,1);
        era_split_input_request_t old=prepare();
        if (waiting) {
            clock_us=old.born_deadline_us;
            assert(!era_split_input_source_due(clock_us));
        }
        /* A receiver-only lease/relationship renewal leaves the remote
         * source gate and USB facts unchanged. Delayed old input observes it. */
        era_split_input_receiver_gate(false,7,9);
        era_split_input_receiver_gate(true,7,9);
        uint8_t status=deliver(&old,1);
        assert(status==ERA_INPUT_MISMATCH && !claim(66));
        if (!waiting) era_split_input_source_complete(&old,true,status);
        else {
            era_split_input_receiver_end();
            era_split_input_receiver_capacity(false);
            assert(era_split_input_receiver_flow(clock_us)==ERA_INPUT_REBIND);
            era_split_input_receiver_capacity(true);
            status=era_split_input_receiver_flow(clock_us);
            assert(status==ERA_INPUT_REBIND);
            era_split_input_source_flow(status,era_split_input_source_generation());
            assert(era_split_input_source_due(clock_us));
        }
        assert(!era_split_input_source_tx_allowed(&old,7,9,clock_us));
        /* Core1 can sample the replacement generation before Core0 raises
         * the A/B barrier. Its late FLOW cannot end that control install. */
        const uint8_t late[] = {ERA_INPUT_FLOW_RESET, ERA_INPUT_REBIND};
        era_split_input_request_t r;
        for (unsigned i=0; i<sizeof(late); ++i) {
            era_split_input_source_flow(late[i],era_split_input_source_generation());
            r=prepare(); assert(r.op==ERA_INPUT_FENCE);
        }
        deliver(&r,3); era_split_input_source_complete(&r,true,0);
        for (unsigned i=0; i<sizeof(late); ++i) {
            era_split_input_source_flow(late[i],era_split_input_source_generation());
            r=prepare(); assert(r.op==ERA_INPUT_BIND);
        }
        deliver(&r,4); era_split_input_source_complete(&r,true,0);
        r=prepare(); assert(r.op==ERA_INPUT_RESET && r.epoch==1);
        assert(deliver(&r,5)==ERA_INPUT_ACCEPTED);
        era_split_input_source_complete(&r,true,ERA_INPUT_ACCEPTED);
        assert(claim(66) && previous[0]==1); era_split_input_finish();
        assert(source.phase==SOURCE_LIVE && !era_split_input_source_due(clock_us));
    }
}
int main(void) {
    order_and_receipt(); capacity_and_pin(); full_group_and_reset_race(); deadlines_and_recovery(); wait_and_wrap(); strict_schema_and_pressure(); receiver_lifetime_recovery();
    printf("PASS production input owners: history=%zu packet=%zu source=%zu receiver=%zu request=%zu\n",sizeof(input_snapshot_t),sizeof(input_packet_t),sizeof(source),sizeof(receiver),sizeof(era_split_input_request_t));
    return 0;
}
