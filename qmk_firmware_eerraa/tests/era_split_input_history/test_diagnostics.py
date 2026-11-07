#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute the minimal window/report interface with fake clocks and owners."""
import re
import unittest
from test_production import ROOT, run_c, body
import test_production


def source_without_includes(path):
    return re.sub(r"^[ \t]*#[ \t]*include[^\n]*", "", (ROOT / path).read_text(encoding="utf-8"), flags=re.M)


SPLIT = "keyboards/era/common/split/"


def cause_unit():
    unit = '#define ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE\n#define main input_owner_tests\n'
    unit += test_production.InputProduction().owners_unit("input_history.c") + '\n#undef main\n'
    unit += CAUSE_STUBS
    unit += source_without_includes(SPLIT + "diagnostics/era_split_wire_cause.c")
    unit += source_without_includes(SPLIT + "era_split_wire_frame.c")
    unit += source_without_includes(SPLIT + "era_split_transaction_io.c")
    unit += source_without_includes(SPLIT + "era_split_transaction_engine.c")
    unit += '\nbool era_split_transaction_backend_send_owned(const uint8_t *source, size_t size, uint16_t owner_epoch, era_split_transaction_backend_wait_result_t *wait_result) {\n'
    unit += body(SPLIT + "era_split_transaction_backend_rp2040.c", "era_split_transaction_backend_send_owned") + '\n}\n'
    return unit


class Diagnostics(unittest.TestCase):
    def test_window_freezes_rates_and_health_before_paced_output(self):
        source = (ROOT / "keyboards/era/common/split/diagnostics/era_split_wire_diagnostics.c").read_text(encoding="utf-8")
        source = re.sub(r"^[ \t]*#[ \t]*include[^\n]*", "", source, flags=re.M)
        ledger = source_without_includes(SPLIT + "diagnostics/era_split_wire_cause.c")
        run_c(STUBS + LEDGER_STUBS + ledger + source + CASES)

    def test_storage_disabled_report_keeps_header_before_details(self):
        source = source_without_includes(SPLIT + "diagnostics/era_split_wire_diagnostics.c")
        ledger = source_without_includes(SPLIT + "diagnostics/era_split_wire_cause.c")
        stubs = STUBS.replace("#define ERA_HOST_PEER_STORAGE_V1_ENABLE", "")
        run_c(stubs + LEDGER_STUBS + ledger + source + CASES)

    def test_first_causes_and_original_send_failure_use_production_boundaries(self):
        run_c(cause_unit() + CAUSE_CASES)

    def test_publication_retention_context_and_saturation(self):
        ledger = source_without_includes(SPLIT + "diagnostics/era_split_wire_cause.c")
        run_c('#include <assert.h>\n#include <stdio.h>\n#include <string.h>\n' + LEDGER_STUBS + ledger + PUBLICATION_CASES)


STUBS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#define ERA_HOST_PEER_STORAGE_V1_ENABLE
enum { WIRE_DIAG=8, WIRE_DIAG_2=9, WIRE_QWIN=10 };
typedef struct { struct { bool pressed; } event; } keyrecord_t;
typedef struct {
    uint8_t ready, fdebug, frame_words;
    uint32_t sample_words, torn_retries, rearms;
} era_rp2040_matrix_pio_diagnostics_t;
typedef struct {
    uint8_t running, launch_capped;
    uint32_t dead_declared_count, initiator_queue_expired_count;
} era_split_communication_core_diagnostics_t;
#include "keyboards/era/common/split/era_split_transaction_types.h"
typedef struct {
    uint8_t state, status;
    uint32_t abort_count, timeout_count, integrity_reject_count;
} era_host_peer_storage_diagnostics_t;
typedef struct { uint32_t program_failure_count, erase_failure_count; } era_nvm_diagnostics_t;
static uint32_t now, era_split_wire_diagnostics_raw_matrix_scan_count;
static era_rp2040_matrix_pio_diagnostics_t pio = {.ready=1,.frame_words=8,.sample_words=1000};
static char output[4096];
static size_t used;
static uint32_t timer_read32(void) { return now; }
static uint32_t timer_elapsed32(uint32_t since) { return now-since; }
static void era_rp2040_matrix_pio_get_diagnostics(era_rp2040_matrix_pio_diagnostics_t *s) { *s=pio; }
static void era_rp2040_matrix_pio_clear_fdebug(void) { pio.fdebug=0; }
static uint8_t era_split_transport_scheduler_diagnostic_mode(void) { return 2; }
static void era_split_communication_core_get_diagnostics_snapshot(era_split_communication_core_diagnostics_t *s) {
    *s=(era_split_communication_core_diagnostics_t){1,0,3,4};
}
static void era_split_transaction_engine_get_diagnostics_snapshot(era_split_transaction_engine_diagnostics_t *s) {
    *s=(era_split_transaction_engine_diagnostics_t){10,9,1,2,3};
}
#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
static void era_host_peer_storage_get_diagnostics_snapshot(era_host_peer_storage_diagnostics_t *s) {
    *s=(era_host_peer_storage_diagnostics_t){5,6,7,8,9};
}
static void era_eeprom_driver_get_nvm_diagnostics(era_nvm_diagnostics_t *s) {
    *s=(era_nvm_diagnostics_t){10,11};
}
#endif
static void uprintf(const char *format, ...) {
    va_list args; va_start(args,format);
    int written=vsnprintf(output+used,sizeof(output)-used,format,args);
    va_end(args); assert(written>=0 && (size_t)written<sizeof(output)-used);
    used+=(size_t)written;
}
'''

LEDGER_STUBS = r'''
#define ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
#include "keyboards/era/common/split/diagnostics/era_split_wire_cause.h"
#include "keyboards/era/common/split/era_split_transaction_backend.h"
static struct { uint32_t timerawl; } raw_timer;
static const typeof(raw_timer) *timer_hw=&raw_timer;
static uint16_t owner_epoch=7;
static unsigned core=1;
static bool observe_publication;
static era_split_wire_cause_capture_t interleaved;
static void barrier(void) {
    if (observe_publication) {
        observe_publication=false;
        era_split_wire_cause_capture(&interleaved);
    }
}
#define __DMB() barrier()
#define get_core_num() core
static uint16_t era_split_communication_core_owner_epoch(void) {return owner_epoch;}
void era_split_transaction_backend_cause_facts(era_split_wire_cause_record_t *r) {r->baud=115200;r->role=2;}
'''

CASES = r'''
static void drain(void) {
    for (unsigned i=0;i<32;i++) { now+=20; era_split_wire_diagnostics_task(); }
}
int main(void) {
    keyrecord_t down={.event.pressed=true}, up={0};
    assert(era_split_wire_diagnostics_process_record(123,&down));
    assert(!era_split_wire_diagnostics_process_record(WIRE_DIAG_2,&down));
    assert(!era_split_wire_diagnostics_process_record(WIRE_QWIN,&up));
    assert(!used);
    now=100; era_split_wire_diagnostics_raw_matrix_scan_count=40;
    assert(!era_split_wire_diagnostics_process_record(WIRE_QWIN,&down));
    now=1100; era_split_wire_diagnostics_raw_matrix_scan_count=140;
    pio.sample_words=17000; pio.torn_retries=2; pio.fdebug=5;
    era_split_wire_diagnostics_task(); assert(!used);
    assert(!era_split_wire_diagnostics_process_record(WIRE_DIAG,&down));
    /* Owners continue changing after capture, but this report does not. */
    pio.sample_words=99999; pio.fdebug=0;
    era_split_wire_diagnostics_raw_matrix_scan_count=900;
    assert(!era_split_wire_diagnostics_process_record(WIRE_QWIN,&down));
    drain();
    assert(strstr(output,"wire cause v=1")!=NULL);
    const char *prefix =
        "wire scan ms=1000 count=100 hz=100 pio_valid=1 pio_hz=2000 retry=2 rearm=0 fd=5\r\n"
        "wire core mode=2 run=1 capped=0 dead=3 expired=4\r\n"
        "wire link tx=10 rx=9 miss=1 bad=2 fail=3\r\n"
#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
        "wire storage state=5 status=6 abort=7 timeout=8 reject=9 program_fail=10 erase_fail=11\r\n"
#endif
        ;
    assert(!strncmp(output,prefix,strlen(prefix)));
    assert(strstr(output,"used=0 unavailable=0\r\n")!=NULL);
    used=0; output[0]=0;
    assert(!era_split_wire_diagnostics_process_record(WIRE_QWIN,&down));
    now+=1000; era_split_wire_diagnostics_raw_matrix_scan_count+=50;
    pio.sample_words=8; pio.rearms++;
    raw_timer.timerawl=123;
    era_split_wire_cause_begin(7,ERA_CAUSE_RESPONSE_FIRST);
    era_split_wire_cause_wait(ERA_CAUSE_RESPONSE_FIRST,ERA_SPLIT_TRANSACTION_BACKEND_WAIT_TIMEOUT);
    era_split_wire_cause_finish(ERA_SPLIT_TRANSACTION_RESULT_MISS,ERA_SPLIT_TRANSACTION_FAILURE_RESPONSE_TIMEOUT);
    assert(!era_split_wire_diagnostics_process_record(WIRE_QWIN,&down));
    raw_timer.timerawl=456;
    era_split_wire_cause_begin(7,ERA_CAUSE_RESPONSE_FIRST);
    era_split_wire_cause_wait(ERA_CAUSE_RESPONSE_FIRST,ERA_SPLIT_TRANSACTION_BACKEND_WAIT_TIMEOUT);
    era_split_wire_cause_finish(ERA_SPLIT_TRANSACTION_RESULT_MISS,ERA_SPLIT_TRANSACTION_FAILURE_RESPONSE_TIMEOUT);
    drain();
    assert(strstr(output,"count=50 hz=50 pio_valid=0 pio_hz=0 retry=0 rearm=1")!=NULL);
    const char *header = strstr(output,"wire cause v=1 ");
    const char *detail = strstr(output,"wire detail ");
    assert(header && detail && header < detail);
    assert(strstr(output," n=1 us=123 ")!=NULL);
    assert(strstr(output," n=2 ")==NULL);
    puts("PASS minimal window/report: silent collection, exact rates, frozen paced health, sampler discontinuity");
    return 0;
}
'''

CAUSE_STUBS = r'''
#include "keyboards/era/common/split/diagnostics/era_split_wire_cause.h"
#include "keyboards/era/common/split/era_split_transaction_io.h"
#include "keyboards/era/common/split/era_split_transaction_engine.h"
#define get_core_num() 1U
#define __DMB() __atomic_thread_fence(__ATOMIC_SEQ_CST)
static struct { uint32_t timerawl; } raw_timer;
static const typeof(raw_timer) *timer_hw=&raw_timer;
static uint16_t era_split_communication_core_owner_epoch(void) {return 7;}
void era_split_transaction_backend_cause_facts(era_split_wire_cause_record_t *r) {r->baud=115200;r->role=2;}
static era_split_transaction_backend_wait_result_t send_outcome,cleanup_outcome,receive_outcome;
static uint8_t incoming[271];
static unsigned incoming_length,read_at,forced,clears;
static unsigned access_calls;
static bool reject_admission,reject_final;
static bool initialized=true;
static bool era_split_transaction_backend_rx_error_pending;
static struct { bool transaction_backend_initialized; } g_era_split_transaction_backend_rp2040={true};
static const int era_split_transaction_backend_rx_sm=1,era_split_transaction_backend_rx_program_offset=12,era_split_transaction_backend_pio=0;
static era_split_transaction_backend_wait_result_t era_split_transaction_backend_access_result(uint16_t epoch) {
    (void)epoch;++access_calls;
    if (reject_admission) return ERA_SPLIT_TRANSACTION_BACKEND_WAIT_OWNER;
    if (reject_final && access_calls==2) return ERA_SPLIT_TRANSACTION_BACKEND_WAIT_EPOCH;
    return ERA_SPLIT_TRANSACTION_BACKEND_WAIT_OK;
}
static void era_split_transaction_backend_rp2040_clear_fifo(void) {}
static uint32_t era_split_transaction_backend_irq_save(void) {return 0;}
static void era_split_transaction_backend_irq_restore(uint32_t state) {(void)state;}
static void pio_interrupt_clear(int pio,unsigned n) {(void)pio;(void)n;}
static void era_split_transaction_backend_leave_rx_state(void) {}
static bool era_split_transaction_backend_send_impl_owned(const uint8_t *source,size_t size,uint16_t epoch,era_split_transaction_backend_wait_result_t *r) {
    (void)source;(void)size;(void)epoch;*r=send_outcome;return *r==ERA_SPLIT_TRANSACTION_BACKEND_WAIT_OK;
}
static bool era_split_transaction_backend_enter_rx_state_owned(uint16_t epoch,era_split_transaction_backend_wait_result_t *r) {
    (void)epoch;raw_timer.timerawl+=100;*r=cleanup_outcome;return *r==ERA_SPLIT_TRANSACTION_BACKEND_WAIT_OK;
}
static unsigned pio_sm_get_pc(int pio,int sm) {(void)pio;(void)sm;return 17;}
static void era_split_transaction_backend_force_rx_state(void) {++forced;}
void era_split_transaction_backend_init(void) {}
bool era_split_transaction_backend_init_initiator(void) {return true;}
bool era_split_transaction_backend_init_responder(void) {return true;}
void era_split_transaction_backend_release(void) {}
void era_split_transaction_backend_reset_link_state(void) {}
void era_split_transaction_backend_clear(void) {++clears;}
bool era_split_transaction_backend_response_window_begin(uint16_t epoch,uint16_t ms,era_split_transaction_backend_response_window_t *window,era_split_transaction_backend_wait_result_t *r) {
    (void)ms;window->owner_epoch=epoch;*r=initialized?ERA_SPLIT_TRANSACTION_BACKEND_WAIT_OK:ERA_SPLIT_TRANSACTION_BACKEND_WAIT_IO_ERROR;return initialized;
}
bool era_split_transaction_backend_responder_idle_window_begin(uint16_t epoch,uint16_t ms,era_split_transaction_backend_response_window_t *window,era_split_transaction_backend_wait_result_t *r) {
    return era_split_transaction_backend_response_window_begin(epoch,ms,window,r);
}
bool era_split_transaction_backend_responder_frame_window_begin(era_split_transaction_backend_response_window_t *window,uint16_t ms,era_split_transaction_backend_wait_result_t *r) {
    return era_split_transaction_backend_response_window_begin(window->owner_epoch,ms,window,r);
}
bool era_split_transaction_backend_receive_response_window_until(era_split_transaction_backend_response_window_t *window,uint8_t *to,size_t size,era_split_transaction_backend_wait_result_t *r) {
    (void)window;
    *r=receive_outcome;
    if (*r!=ERA_SPLIT_TRANSACTION_BACKEND_WAIT_OK) return false;
    if (read_at+size>incoming_length) {*r=ERA_SPLIT_TRANSACTION_BACKEND_WAIT_TIMEOUT;return false;}
    memcpy(to,incoming+read_at,size);read_at+=size;return true;
}
bool era_split_transaction_backend_receive_responder_until(era_split_transaction_backend_response_window_t *window,uint8_t *to,size_t size,era_split_transaction_backend_wait_result_t *r) {
    return era_split_transaction_backend_receive_response_window_until(window,to,size,r);
}
'''

CAUSE_CASES = r'''
static era_split_wire_cause_capture_t captured;
static era_split_transaction_failure_t failure;
static era_split_wire_frame_t response;
static era_split_transaction_engine_result_t transact(uint8_t expected) {
    const uint8_t payload[]={1};
    bool sent;
    read_at=0;
    access_calls=0;
    return era_split_transaction_engine_transact_compact_owned(0,payload,1,1,expected,ERA_SPLIT_WIRE_PAYLOAD_INVALID,20,7,&response,&sent,&failure);
}
static void response_bytes(uint8_t control, uint8_t marker) {
    incoming[0]=marker;incoming[1]=control;incoming[2]=era_split_wire_crc8(incoming,2);incoming_length=3;
}
int main(void) {
    era_split_wire_cause_context(7,9,2,0x40);
    raw_timer.timerawl=500;
    send_outcome=ERA_SPLIT_TRANSACTION_BACKEND_WAIT_TIMEOUT;
    assert(transact(ERA_SPLIT_WIRE_PAYLOAD_GRANT_ACK)==ERA_SPLIT_TRANSACTION_RESULT_FAIL);
    assert(failure==ERA_SPLIT_TRANSACTION_FAILURE_IO && forced==1 && clears==1);
    era_split_wire_cause_capture(&captured);
    const era_split_wire_cause_record_t *r=&captured.slots[ERA_CAUSE_SEND_TIMEOUT].first;
    assert(captured.slots[ERA_CAUSE_SEND_TIMEOUT].count==1 && r->us==500);
    assert(r->send_wait==ERA_SPLIT_TRANSACTION_BACKEND_WAIT_TIMEOUT && r->cleanup_wait==ERA_SPLIT_TRANSACTION_BACKEND_WAIT_OK);
    assert((r->valid&(ERA_CAUSE_SEND_VALID|ERA_CAUSE_CLEANUP_VALID|ERA_CAUSE_PC_VALID))==(ERA_CAUSE_SEND_VALID|ERA_CAUSE_CLEANUP_VALID|ERA_CAUSE_PC_VALID));
    assert(r->pc==17 && r->program==12 && r->failure==ERA_SPLIT_TRANSACTION_FAILURE_IO);
    send_outcome=ERA_SPLIT_TRANSACTION_BACKEND_WAIT_CANCEL;
    for (unsigned i=0;i<100;i++) assert(transact(0)==ERA_SPLIT_TRANSACTION_RESULT_FAIL);
    send_outcome=ERA_SPLIT_TRANSACTION_BACKEND_WAIT_OK;
    reject_admission=true;
    assert(transact(0)==ERA_SPLIT_TRANSACTION_RESULT_FAIL && failure==ERA_SPLIT_TRANSACTION_FAILURE_OWNER);
    reject_admission=false;reject_final=true;
    assert(transact(0)==ERA_SPLIT_TRANSACTION_RESULT_FAIL && failure==ERA_SPLIT_TRANSACTION_FAILURE_EPOCH);
    reject_final=false;
    era_split_wire_cause_capture(&captured);
    r=&captured.slots[ERA_CAUSE_EPOCH].first;
    assert(r->phase==ERA_CAUSE_SEND_FINAL && r->send_wait==0 && r->cleanup_wait==0);
    assert((r->valid&(ERA_CAUSE_SEND_VALID|ERA_CAUSE_CLEANUP_VALID))==(ERA_CAUSE_SEND_VALID|ERA_CAUSE_CLEANUP_VALID));
    r=&captured.slots[ERA_CAUSE_OWNER].first;
    assert(r->phase==ERA_CAUSE_SEND_ADMISSION && !(r->valid&(ERA_CAUSE_SEND_VALID|ERA_CAUSE_CLEANUP_VALID)));
    response_bytes(1,0xB1); // Correct control-only kind, wrong ACK (zero, wanted one).
    assert(transact(ERA_SPLIT_WIRE_PAYLOAD_GRANT_ACK)==ERA_SPLIT_TRANSACTION_RESULT_BAD);
    assert(failure==ERA_SPLIT_TRANSACTION_FAILURE_RESPONSE_CONTRACT);
    era_split_wire_cause_capture(&captured);
    r=&captured.slots[ERA_CAUSE_ACK].first;
    assert(captured.slots[ERA_CAUSE_ACK].count==1 && r->tx==1 && r->ack==0);
    assert(r->kind==ERA_SPLIT_WIRE_PAYLOAD_GRANT_ACK && r->expected==r->kind);
    assert(transact(ERA_SPLIT_WIRE_PAYLOAD_SESSION_STATUS)==ERA_SPLIT_TRANSACTION_RESULT_BAD);
    response_bytes(9,0xB1); // matching ACK
    assert(transact(ERA_SPLIT_WIRE_PAYLOAD_SESSION_STATUS)==ERA_SPLIT_TRANSACTION_RESULT_BAD);
    assert(transact(ERA_SPLIT_WIRE_PAYLOAD_GRANT_ACK)==ERA_SPLIT_TRANSACTION_RESULT_OK);
    incoming[2]^=1;
    assert(transact(ERA_SPLIT_WIRE_PAYLOAD_GRANT_ACK)==ERA_SPLIT_TRANSACTION_RESULT_BAD);
    response_bytes(0x80,0xB1); // Valid CRC, rejected control/payload.
    assert(transact(0)==ERA_SPLIT_TRANSACTION_RESULT_BAD);
    response_bytes(9,0xA1);
    assert(transact(0)==ERA_SPLIT_TRANSACTION_RESULT_BAD);
    response_bytes(9,0x91);
    assert(transact(0)==ERA_SPLIT_TRANSACTION_RESULT_BAD);
    incoming[0]=0xB0;
    assert(transact(0)==ERA_SPLIT_TRANSACTION_RESULT_BAD);
    incoming_length=0;
    assert(transact(0)==ERA_SPLIT_TRANSACTION_RESULT_MISS);
    incoming[0]=0xB1;incoming_length=1;
    assert(transact(0)==ERA_SPLIT_TRANSACTION_RESULT_FAIL && failure==ERA_SPLIT_TRANSACTION_FAILURE_PARTIAL_FRAME);
    receive_outcome=ERA_SPLIT_TRANSACTION_BACKEND_WAIT_PIO_ERROR;
    assert(transact(0)==ERA_SPLIT_TRANSACTION_RESULT_FAIL);
    receive_outcome=ERA_SPLIT_TRANSACTION_BACKEND_WAIT_OK;
    uint8_t wire[271];
    incoming[0]=0xA0;incoming[1]=1;incoming[2]=0;incoming[3]=0x41;
    memset(incoming+4,0,4);incoming_length=8;read_at=0;
    assert(era_split_transaction_engine_receive_idle_owned(0,10,20,7,wire,sizeof(wire),&response,&failure)==ERA_SPLIT_TRANSACTION_RESULT_BAD);
    era_split_wire_put32(incoming+4,era_split_wire_crc32(incoming,4));read_at=0;
    assert(era_split_transaction_engine_receive_idle_owned(0,10,20,7,wire,sizeof(wire),&response,&failure)==ERA_SPLIT_TRANSACTION_RESULT_BAD);
    read_at=0;
    assert(era_split_transaction_engine_receive_idle_owned(0,10,20,7,wire,17,&response,&failure)==ERA_SPLIT_TRANSACTION_RESULT_BAD);
    incoming_length=0;read_at=0;
    assert(era_split_transaction_engine_receive_idle_owned(0,10,20,7,wire,sizeof(wire),&response,&failure)==ERA_SPLIT_TRANSACTION_RESULT_NONE);
    assert(era_split_transaction_engine_send_compact_response_owned(0,NULL,1,1,7,&failure)==ERA_SPLIT_TRANSACTION_RESULT_BAD);
    const uint8_t bad_control=0;
    assert(era_split_transaction_engine_send_compact_response_owned(0,&bad_control,1,1,7,&failure)==ERA_SPLIT_TRANSACTION_RESULT_BAD);
    era_split_wire_cause_capture(&captured);
    assert(captured.slots[ERA_CAUSE_CANCEL].count==100);
    assert(captured.slots[ERA_CAUSE_SEND_TIMEOUT].first.us==500);
    assert(captured.slots[ERA_CAUSE_COMPACT_CRC].count==1);
    assert(captured.slots[ERA_CAUSE_COMPACT_PAYLOAD].count==1);
    assert(captured.slots[ERA_CAUSE_DIRECTION].count==1);
    assert(captured.slots[ERA_CAUSE_RX_MARKER].count==1);
    assert(captured.slots[ERA_CAUSE_RX_LENGTH].count==1);
    assert(captured.slots[ERA_CAUSE_KIND].count==1 && captured.slots[ERA_CAUSE_KIND_ACK].count==1);
    assert(captured.slots[ERA_CAUSE_INITIAL_TIMEOUT].count==1 && captured.slots[ERA_CAUSE_PARTIAL].count==1);
    assert(captured.slots[ERA_CAUSE_PIO].count==1 && captured.slots[ERA_CAUSE_UNAVAILABLE].count==0);
    assert(captured.slots[ERA_CAUSE_BULK_CRC].count==1 && captured.slots[ERA_CAUSE_BULK_PAYLOAD].count==1);
    assert(captured.slots[ERA_CAUSE_RX_CAPACITY].count==1);
    assert(captured.slots[ERA_CAUSE_RX_CAPACITY].first.a==271 && captured.slots[ERA_CAUSE_RX_CAPACITY].first.b==17);
    assert(captured.slots[ERA_CAUSE_ENCODE_ARGS].count==1 && captured.slots[ERA_CAUSE_ENCODE_CONTROL].count==1);
    uint32_t total=0;
    for (unsigned i=0;i<ERA_CAUSE_COUNT;i++) total+=captured.slots[i].count;
    era_split_transaction_engine_diagnostics_t counts;
    era_split_transaction_engine_get_diagnostics_snapshot(&counts);
    assert(total==counts.compact_bad_count+counts.compact_fail_count+counts.compact_miss_count);
    assert(counts.compact_rx_count==4); // Includes the three response-contract rejections.
    puts("PASS real engine/IO/codec/send boundary: original failure survives cleanup, old IO result unchanged, administrative isolation, exact rejects and ACK");
}
'''

PUBLICATION_CASES = r'''
int main(void) {
    era_split_wire_cause_capture_t frozen;
    raw_timer.timerawl=10;
    era_split_wire_cause_begin(7,ERA_CAUSE_RESPONSE_FIRST);
    era_split_wire_cause_wait(ERA_CAUSE_RESPONSE_FIRST,ERA_SPLIT_TRANSACTION_BACKEND_WAIT_TIMEOUT);
    observe_publication=true;
    era_split_wire_cause_finish(2,8);
    assert(interleaved.slots[ERA_CAUSE_INITIAL_TIMEOUT].count==0);
    era_split_wire_cause_capture(&frozen);
    assert(frozen.slots[ERA_CAUSE_INITIAL_TIMEOUT].count==1);
    assert(!(frozen.slots[ERA_CAUSE_INITIAL_TIMEOUT].first.valid&ERA_CAUSE_CONTEXT_VALID));
    era_split_wire_cause_context(7,9,2,0x40);
    era_split_wire_cause_begin(7,ERA_CAUSE_SEND);
    era_split_wire_cause_wait(ERA_CAUSE_SEND,ERA_SPLIT_TRANSACTION_BACKEND_WAIT_CANCEL);
    era_split_wire_cause_finish(4,4);
    era_split_wire_cause_relation(7,10,3);
    era_split_wire_cause_owner(8);owner_epoch=8;
    raw_timer.timerawl=99;
    era_split_wire_cause_begin(8,ERA_CAUSE_SEND);
    era_split_wire_cause_wait(ERA_CAUSE_SEND,ERA_SPLIT_TRANSACTION_BACKEND_WAIT_CANCEL);
    era_split_wire_cause_finish(4,4);
    era_split_wire_cause_capture(&frozen);
    assert(frozen.slots[ERA_CAUSE_CANCEL].count==2);
    assert(frozen.slots[ERA_CAUSE_CANCEL].first.relation==9 && frozen.slots[ERA_CAUSE_CANCEL].first.attempt_epoch==7);
    assert(frozen.slots[ERA_CAUSE_CANCEL].first.valid&ERA_CAUSE_CONTEXT_VALID);
    /* Inject exhaustion, not billions of iterations. */
    ledger[ERA_CAUSE_CANCEL].count=UINT32_MAX;
    era_split_wire_cause_begin(8,ERA_CAUSE_SEND);
    era_split_wire_cause_wait(ERA_CAUSE_SEND,ERA_SPLIT_TRANSACTION_BACKEND_WAIT_CANCEL);
    era_split_wire_cause_finish(4,4);
    era_split_wire_cause_begin(7,ERA_CAUSE_SEND); // stale context epoch
    era_split_wire_cause_wait(ERA_CAUSE_SEND,ERA_SPLIT_TRANSACTION_BACKEND_WAIT_OWNER);
    era_split_wire_cause_finish(4,2);
    era_split_wire_cause_begin(8,ERA_CAUSE_SEND); // a torn same-lease publication
    context.seq|=1U;
    era_split_wire_cause_wait(ERA_CAUSE_SEND,ERA_SPLIT_TRANSACTION_BACKEND_WAIT_RESET);
    era_split_wire_cause_finish(4,5);
    era_split_wire_cause_begin(8,ERA_CAUSE_SEND); // no stale detail from RESET
    era_split_wire_cause_finish(3,10);
    era_split_wire_cause_capture(&frozen);
    assert(frozen.slots[ERA_CAUSE_CANCEL].count==UINT32_MAX);
    assert(!(frozen.slots[ERA_CAUSE_OWNER].first.valid&ERA_CAUSE_CONTEXT_VALID));
    assert(!(frozen.slots[ERA_CAUSE_RESET].first.valid&ERA_CAUSE_CONTEXT_VALID));
    assert(frozen.slots[ERA_CAUSE_UNAVAILABLE].count==1);
    assert(frozen.slots[ERA_CAUSE_UNAVAILABLE].first.failure==10);
    puts("PASS immutable publication, epoch context/missing context, capture retention, saturation and no stale pending detail");
}
'''

if __name__ == "__main__":
    unittest.main(verbosity=2)
