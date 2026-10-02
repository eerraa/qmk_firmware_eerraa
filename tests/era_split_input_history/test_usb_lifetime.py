#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""ISR lifetime changes between the real HID owner and whole USB admission."""
import re
import unittest
from test_production import body, run_c
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
static volatile bool era_hid_reopen;
static unsigned inject,forced,aux_tasks;
static uint8_t current[8];
static uint8_t usb_device_state_get_protocol(void) {return protocol;}
static bool host_can_send_nkro(void) {return protocol==USB_PROTOCOL_REPORT;}
static void era_hid_report_interval_ensure_ready(void) {}
static uint32_t era_hid_report_interval_now(void) {return 10;}
static void era_usb_auxiliary_reports_task(void) {++aux_tasks;}
static void force_send_keyboard_report(void) {++forced;era_hid_report_interval_hold(0,current,8);}
static void inject_before_lock(void);
'''

CASES = r'''
static void inject_before_lock(void) {
    unsigned edge=inject;inject=0;
    if(edge==1 || edge==2) era_hid_report_interval_session_edge_i(edge==2);
    if(edge==3) {protocol=USB_PROTOCOL_BOOT;era_hid_report_interval_protocol_edge_i();}
    if(edge==4) {era_hid_report_interval_protocol_edge_i();era_hid_report_interval_protocol_edge_i();}
}
bool era_hid_report_interval_platform_post(uint8_t lane,const uint8_t *data,uint8_t size) {
    return usb_endpoint_in_send(&usb_endpoints_in[lane],data,size,0,false);
}
static void fresh(void) {
    era_hid_report_interval_init(1);
    memset(usb_endpoints_in,0,sizeof(usb_endpoints_in));
    for(unsigned i=0;i<2;++i) {usb_endpoints_in[i].config.buffer_size=32;usb_endpoints_in[i].obqueue.bsize=40;}
    posted=forced=aux_tasks=0;protocol=USB_PROTOCOL_REPORT;
    era_hid_format=UINT8_MAX;era_hid_protocol_events=era_hid_protocol_seen=0;era_hid_reopen=false;
    keymap_config.nkro=false;
    assert(era_hid_report_interval_select_format(1));
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
}
'''


class UsbLifetime(unittest.TestCase):
    def test_isr_between_owner_and_endpoint(self):
        stubs = re.sub(r'static bool era_hid_report_interval_endpoint_allowed_i[^\n]+\n', '', STUBS)
        stubs = stubs.replace('static void osalSysLock(void)', 'static void inject_before_lock(void);\nstatic void osalSysLock(void)')
        stubs = stubs.replace('assert(!lock_depth); ++lock_depth;', 'assert(!lock_depth); inject_before_lock(); ++lock_depth;')
        stubs = stubs.replace('++posted; }', '++posted;era_hid_report_interval_note_posted(p==&usb_endpoints_in[0]?0:1); }')
        # Endpoint objects must be defined before the post callback uses them.
        callback = re.search(r'static void era_hid_report_interval_endpoint_posted_i[^\n]+\n', stubs).group()
        stubs = stubs.replace(callback, '')
        unit = '#include "keyboards/era/common/system/era_hid_report_interval.c"\n' + stubs + STUB + callback
        for name, ret, args in [
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
