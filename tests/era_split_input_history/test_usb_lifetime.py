#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""ISR lifetime changes between the real HID owner and whole USB admission."""
import re
import unittest
from test_production import ROOT, body, run_c
from test_usb_admission import STUBS

GLUE = 'keyboards/era/common/system/era_hid_report_interval_chibios.c'
STUB = r'''
#define SHARED_EP_ENABLE
#define NKRO_ENABLE
#define USB_ENDPOINT_IN_KEYBOARD 0
#define USB_ENDPOINT_IN_SHARED 1
#define USB_PROTOCOL_BOOT 0
#define USB_PROTOCOL_REPORT 1
#define REPORT_ID_NKRO 5
#define USB_DRIVER usb_driver
static int usb_driver;
static usb_endpoint_in_t usb_endpoints_in[2];
static struct { bool nkro; } keymap_config;
static uint8_t protocol=USB_PROTOCOL_REPORT,era_hid_format=UINT8_MAX;
static volatile uint32_t era_hid_protocol_events;
static uint32_t era_hid_protocol_seen;
static volatile uint32_t era_hid_session_events;
static uint32_t era_hid_format_session;
static volatile bool era_hid_reopen;
static volatile bool era_hid_auxiliary_reopen;
static void host_resend_last_auxiliary_reports(void) {} /* No logical auxiliary producer in this HAL fixture. */
static volatile uint8_t era_hid_retire_pending;
static volatile uint8_t era_hid_retire_debt;
static volatile uint32_t era_hid_suspend_events;
static const uint8_t *era_hid_retire_dispatch;
static volatile bool era_hid_retire_reset;
typedef struct { uint8_t mods,reserved,keys[6]; } report_keyboard_t;
typedef struct { uint8_t report_id,mods,bits[6]; } report_nkro_t;
typedef uint8_t usb_endpoint_in_lut_t;
static volatile bool auxiliary_session_reset;
static unsigned inject,forced,clock_reads,aux_sends;
static uint32_t clock_now=10;
static uint8_t current[8];
static uint8_t usb_device_state_get_protocol(void) {return protocol;}
static bool host_can_send_nkro(void) {return protocol==USB_PROTOCOL_REPORT;}
static void era_hid_report_interval_ensure_ready(void) {}
static uint32_t era_hid_report_interval_now(void) {++clock_reads;return clock_now;}
static bool send_report(usb_endpoint_in_lut_t endpoint,void *report,size_t size);
static void force_send_keyboard_report(void) {
    ++forced;
    if(era_hid_format==2) {uint8_t nkro[8]={REPORT_ID_NKRO};era_hid_report_interval_hold(1,nkro,8);}
    else era_hid_report_interval_hold(0,current,8);
}
static void inject_before_lock(void);
'''

CASES = r'''
static void inject_before_lock(void) {
    unsigned edge=inject;inject=0;
    if(edge==1 || edge==2) {era_hid_report_interval_session_edge_i(edge==2);if(edge==1)auxiliary_session_reset=true;}
    if(edge==3) {protocol=USB_PROTOCOL_BOOT;era_hid_report_interval_protocol_edge_i();}
    if(edge==4) {era_hid_report_interval_protocol_edge_i();era_hid_report_interval_protocol_edge_i();}
}
bool era_hid_report_interval_platform_post(uint8_t lane,const uint8_t *data,uint8_t size) {
    return usb_endpoint_in_send(&usb_endpoints_in[lane],data,size,0,false);
}
static bool send_report(usb_endpoint_in_lut_t endpoint,void *report,size_t size) {
    ++aux_sends;return usb_endpoint_in_send(&usb_endpoints_in[endpoint],report,size,0,false);
}
static void fresh(void) {
    era_hid_report_interval_init(1);
    memset(usb_endpoints_in,0,sizeof(usb_endpoints_in));
    for(unsigned i=0;i<2;++i) {usb_endpoints_in[i].config.buffer_size=32;usb_endpoints_in[i].obqueue.bsize=40;}
    posted=forced=lock_calls=clock_reads=aux_sends=inject=0;protocol=USB_PROTOCOL_REPORT;
    active=USB_ACTIVE;fail_get=false;clock_now=10;
    auxiliary_pending=0;auxiliary_session_reset=false;
    era_hid_format=UINT8_MAX;era_hid_protocol_events=era_hid_protocol_seen=0;era_hid_reopen=era_hid_auxiliary_reopen=false;
    era_hid_retire_pending=0;era_hid_retire_dispatch=NULL;era_hid_retire_reset=false;
    era_hid_retire_debt=0;era_hid_suspend_events=0;
    era_hid_session_events=era_hid_format_session=0;
    keymap_config.nkro=false;
    assert(era_hid_report_interval_select_format(1));
}
static void idle(void) {
    unsigned locks=lock_calls,clocks=clock_reads,sends=aux_sends,reports=posted;
    for(unsigned i=0;i<32;++i) era_hid_report_interval_service();
    assert(lock_calls==locks && clock_reads==clocks && aux_sends==sends && posted==reports);
}
static void service_cases(void) {
    uint8_t down[8]={4},up[8]={0},extra[8]={2,1};
    fresh();idle();
    /* Reports already accepted by USB do not keep the task awake. */
    era_hid_report_interval_hold(0,down,8);idle();
    auxiliary_report(3,1,extra,8);assert(!era_usb_auxiliary_reports_pending());idle();

    fresh();
    usb_endpoints_in[1].obqueue.count=2;
    for(unsigned i=0;i<4;++i) {extra[1]=10+i;auxiliary_report(i,1,extra,8);}
    extra[1]=99;auxiliary_report(2,1,extra,8);
    assert(auxiliary_pending==15);
    era_hid_report_interval_service();assert(auxiliary_pending==15 && !forced);
    usb_endpoints_in[1].obqueue.count=0;
    era_hid_report_interval_service();
    assert(auxiliary_pending==12 && usb_endpoints_in[1].obqueue.reports[0][1]==10 && usb_endpoints_in[1].obqueue.reports[1][1]==11);
    usb_endpoints_in[1].obqueue.count=0;
    era_hid_report_interval_service();
    assert(!auxiliary_pending && usb_endpoints_in[1].obqueue.reports[0][1]==99 && usb_endpoints_in[1].obqueue.reports[1][1]==13);
    idle();

    fresh();
    /* An auxiliary-only retry wakes the service even during a keyboard width. */
    era_hid_report_interval_hold(0,down,8);
    era_hid_report_interval_request(80,10);
    era_hid_report_interval_hold(0,up,8);
    usb_endpoints_in[1].obqueue.count=2;auxiliary_report(0,1,extra,8);
    usb_endpoints_in[1].obqueue.count=0;
    era_hid_report_interval_note_completed(0,10);
    clock_now=89;era_hid_report_interval_service();
    assert(!auxiliary_pending && s.count==1 && usb_endpoints_in[0].obqueue.count==1);
    clock_now=90;era_hid_report_interval_service();
    assert(!s.count && usb_endpoints_in[0].obqueue.reports[1][0]==0);idle();

    fresh();
    /* Reset retires auxiliary retries, even if no keyboard backlog exists. */
    usb_endpoints_in[1].obqueue.count=2;auxiliary_report(1,1,extra,8);
    unsigned sends=aux_sends;
    era_hid_report_interval_session_edge_i(false);auxiliary_session_reset=true;active=0;
    era_hid_report_interval_service();assert(auxiliary_pending && !forced);
    active=USB_ACTIVE;usb_endpoints_in[1].obqueue.count=0;
    era_hid_report_interval_service();assert(!auxiliary_pending && aux_sends==sends && forced==1);idle();

    fresh();
    /* Suspend preserves both retry owners until active resume. */
    fail_get=true;era_hid_report_interval_hold(0,down,8);auxiliary_report(0,1,extra,8);fail_get=false;
    era_hid_report_interval_session_edge_i(true);active=0;
    era_hid_report_interval_service();assert(s.count==1 && auxiliary_pending && !forced);
    active=USB_ACTIVE;era_hid_report_interval_service();
    assert(!s.count && !auxiliary_pending && forced==1 && usb_endpoints_in[0].obqueue.reports[0][0]==4);idle();

    fresh();
    /* Idle NKRO changes can come from QMK/VIA/storage, without a USB ISR. */
    keymap_config.nkro=true;era_hid_report_interval_service();
    assert(era_hid_format==2 && forced==1 && usb_endpoints_in[1].obqueue.reports[0][0]==REPORT_ID_NKRO);idle();
    keymap_config.nkro=false;era_hid_report_interval_service();assert(era_hid_format==1 && forced==2);idle();
    fresh();
    era_hid_report_interval_protocol_edge_i();era_hid_report_interval_protocol_edge_i();
    era_hid_report_interval_service();assert(forced==1 && era_hid_protocol_seen==2);idle();
    puts("PASS idle service: zero locks/clocks/posts, four auxiliary retries, latest replacement, widths, reset/suspend and idle format edges");
}
int main(void) {
    uint8_t old[8]={4},nkro[8]={REPORT_ID_NKRO,4},mouse[8]={2,1};
    fresh();
    /* The task has sampled the old session, then RESET/configure occurs at
     * its first endpoint lock. USB is ACTIVE again: state alone is too weak. */
    fail_get=true;assert(era_hid_report_interval_hold(0,old,8));fail_get=false;
    inject=1;era_hid_report_interval_task(1);
    assert(posted==0 && s.count==1 && !era_hid_report_interval_session_current());
    era_hid_report_interval_service();
    assert(posted==1 && forced==1 && !s.count && usb_endpoints_in[0].obqueue.reports[0][0]==0);
    fresh();
    /* The direct void-producer path owes the same check. */
    inject=1;assert(era_hid_report_interval_hold(0,old,8));assert(!posted && s.count==1);
    era_hid_report_interval_service();assert(posted==1 && forced==1 && !s.count);
    fresh();
    fail_get=true;era_hid_report_interval_hold(0,old,8);fail_get=false;
    inject=2;era_hid_report_interval_task(1);assert(!posted && s.count==1);
    era_hid_report_interval_task(10);assert(posted==1 && !s.count);
    fresh();
    keymap_config.nkro=true;assert(era_hid_report_interval_select_format(2));
    fail_get=true;era_hid_report_interval_hold(1,nkro,8);fail_get=false;
    inject=3;era_hid_report_interval_task(1);assert(!posted && s.count==1);
    /* A keyboard format edge does not hold auxiliary reports or erase them. */
    assert(usb_endpoint_in_send(&usb_endpoints_in[1],mouse,8,0,false));
    era_hid_report_interval_service();
    assert(posted==2 && forced==1 && !s.count && usb_endpoints_in[1].obqueue.reports[0][0]==2);
    fresh();
    fail_get=true;era_hid_report_interval_hold(0,old,8);fail_get=false;
    inject=4;era_hid_report_interval_task(1);assert(!posted && s.count==1);
    /* REPORT -> BOOT -> REPORT is still a new format lifetime. */
    era_hid_report_interval_service();assert(posted==1 && forced==1 && !s.count);
    puts("PASS production HID/USB ISR reset, suspend, protocol/ABA, auxiliary and reopen interleavings");
    service_cases();
}
'''


class UsbLifetime(unittest.TestCase):
    def test_isr_between_owner_and_endpoint(self):
        stubs = re.sub(r'static bool era_hid_report_interval_endpoint_allowed_i[^\n]+\n', '', STUBS)
        stubs = stubs.replace('static void osalSysLock(void)', 'static void inject_before_lock(void);\nstatic void osalSysLock(void)')
        stubs = stubs.replace('assert(!lock_depth); ++lock_depth;', 'assert(!lock_depth); inject_before_lock(); ++lock_depth; ++lock_calls;')
        stubs = 'static unsigned lock_calls;\n' + stubs
        stubs = stubs.replace('++posted; }', '++posted;era_hid_report_interval_note_posted(p==&usb_endpoints_in[0]?0:1); }')
        # Endpoint objects must be defined before the post callback uses them.
        callback = re.search(r'static void era_hid_report_interval_endpoint_posted_i[^\n]+\n', stubs).group()
        stubs = stubs.replace(callback, '')
        unit = '#include "keyboards/era/common/system/era_hid_report_interval.c"\n' + stubs + STUB + callback
        # Execute the actual auxiliary owner and its pending predicate too.
        main = (ROOT / 'tmk_core/protocol/chibios/usb_main.c').read_text(encoding='utf-8')
        start = main.index('static struct {', main.index('/* Each void shared-endpoint producer'))
        unit += main[start:main.index('\n#endif', start)]
        for name, ret, args in [
            ('era_hid_report_interval_retire_apply_reset','void','void'),
            ('era_hid_report_interval_retire_one','void','uint8_t bit,uint8_t endpoint,const void *report,size_t size'),
            ('era_hid_report_interval_retire_task','void','void'),
            ('era_hid_report_interval_session_edge_i','void','bool keep_backlog'),
            ('era_hid_report_interval_protocol_edge_i','void','void'),
            ('era_hid_report_interval_current_format','uint8_t','void'),
            ('era_hid_report_interval_endpoint_allowed_i','bool','const void *endpoint,const uint8_t *data'),
            ('era_hid_report_interval_select_format','bool','uint8_t format'),
            ('era_hid_report_interval_service','void','void'),
        ]:
            unit += f'\n{ret} {name}({args}) {{\n{body(GLUE,name)}\n}}\n'
        unit += 'bool usb_endpoint_in_send(usb_endpoint_in_t *endpoint,const uint8_t *data,size_t size,sysinterval_t timeout,bool buffered) {\n'
        unit += body('tmk_core/protocol/chibios/usb_driver.c','usb_endpoint_in_send') + '\n}\n'
        run_c(unit + CASES)


if __name__ == '__main__':
    unittest.main(verbosity=2)
