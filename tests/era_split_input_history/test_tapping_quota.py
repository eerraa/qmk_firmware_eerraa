#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run production FIFO accounting and direct CHORDAL/FLOW tail paths."""
from pathlib import Path
import unittest
from test_production import body, run_c, ROOT

STUBS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#define ERA_SPLIT_INPUT_HISTORY_ENABLE
#define MATRIX_ROWS_PER_HAND 6
#define MATRIX_COLS 11
#define WAITING_BUFFER_SIZE 74
#define ac_dprintf(...) ((void)0)
#define IS_NOEVENT(e) (!(e).valid)
#define KEYEQ(a,b) ((a)==(b))
typedef uint8_t keypos_t;
typedef struct {struct {bool valid,pressed; keypos_t key; uint32_t time;} event; struct {unsigned count;} tap; bool hold;} keyrecord_t;
static keyrecord_t waiting_buffer[74];
static uint8_t waiting_buffer_head,waiting_buffer_tail,waiting_origin[10];
static uint8_t waiting_local_count,waiting_remote_count;
static bool waiting_ingress_remote;
static unsigned processed,registered;
static void debug_waiting_buffer(void) {}
static bool is_tap_record(keyrecord_t *record) {return record->hold;}
static void registered_taps_add(keypos_t key) {(void)key;++registered;}
static void process_record(keyrecord_t *record) {(void)record;++processed;}
static bool flow_tap_key_if_within_term(keyrecord_t *record,uint32_t prev_time) {return record->event.time-prev_time<20;}
'''

class Quota(unittest.TestCase):
    def test_all_pop_paths_and_wrap(self):
        path='quantum/action_tapping.c'; unit=STUBS
        for name,args,ret in [('waiting_next','uint8_t index','uint8_t'),('waiting_buffer_pop','void','void'),('waiting_buffer_enq','keyrecord_t record','bool'),('waiting_buffer_clear','void','void'),('waiting_buffer_chordal_hold_taps_until','keypos_t key','void'),('waiting_buffer_process_regular','void','void')]:
            unit+='\nstatic '+ret+' '+name+'('+args+') {\n'+body(path,name)+'\n}\n'
        # The FLOW drain is embedded in process_tapping, not a helper. Extract
        # its actual balanced block, including the production pop increment.
        text=(ROOT/path).read_text(encoding='utf-8')
        start=text.index('                    for (; waiting_buffer_tail != waiting_buffer_head; waiting_buffer_pop())')
        brace=text.index('{',start); end=brace+1; depth=1
        while depth:
            depth+=(text[end]=='{')-(text[end]=='}'); end+=1
        unit+='\nstatic void flow_drain(uint32_t prev_time) {\n'+text[start:end]+'\n}\n'
        unit+=r'''
static void enqueue(bool remote,unsigned key,bool hold) {
    waiting_ingress_remote=remote;
    assert(waiting_buffer_enq((keyrecord_t){.event={.valid=true,.pressed=true,.key=key,.time=key},.hold=hold}));
}
int main(void) {
    for(unsigned round=0;round<3;++round) {
        for(unsigned i=0;i<7;++i) enqueue(false,i,false);
        waiting_ingress_remote=false;assert(!waiting_buffer_enq((keyrecord_t){.event={.valid=true}}));
        for(unsigned i=0;i<66;++i) enqueue(true,i,false);
        waiting_ingress_remote=true;assert(!waiting_buffer_enq((keyrecord_t){.event={.valid=true}}));
        assert(waiting_local_count==7 && waiting_remote_count==66);
        while(waiting_buffer_tail!=waiting_buffer_head) waiting_buffer_pop();
        assert(!waiting_local_count && !waiting_remote_count);
    }
    enqueue(false,1,false);enqueue(true,2,true);enqueue(true,3,false);
    waiting_buffer_chordal_hold_taps_until(2);
    assert(!waiting_local_count && waiting_remote_count==1 && registered==1 && processed==2);
    waiting_buffer_process_regular();assert(!waiting_remote_count && processed==3);
    enqueue(false,1,false);enqueue(true,2,true);enqueue(true,3,false);
    flow_drain(0);assert(!waiting_local_count && !waiting_remote_count && processed==5);
    enqueue(true,4,true);enqueue(false,5,false);waiting_buffer_clear();
    assert(!waiting_remote_count && !waiting_local_count && !waiting_buffer_head && !waiting_buffer_tail);
    for(unsigned i=0;i<sizeof(waiting_origin);++i) assert(!waiting_origin[i]);
    puts("PASS production origin quota: local7/remote66, non-power-of-two wrap, common/CHORDAL/FLOW pop and clear");
    return 0;
}
'''
        run_c(unit)

if __name__=='__main__': unittest.main(verbosity=2)
