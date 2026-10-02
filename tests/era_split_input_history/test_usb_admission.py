#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute production USB whole-buffer admission with controlled queue callbacks.

This checks firmware acceptance, not physical USB timing.
"""
import unittest
from test_production import body, run_c

STUBS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define ERA_HID_REPORT_INTERVAL_ENABLE
#define USB_ACTIVE 1
#define TIME_IMMEDIATE 0
#define TIME_INFINITE -1
#define MSG_OK 0
#define osalDbgCheck(x) assert(x)
typedef int sysinterval_t;
typedef struct { uint8_t *ptr; size_t bsize; bool suspended; unsigned count; uint8_t empty[40]; uint8_t reports[2][32]; size_t sizes[2]; } output_buffers_queue_t;
typedef struct { struct { void *usbp; size_t buffer_size; } config; output_buffers_queue_t obqueue; bool timed_out; } usb_endpoint_in_t;
static unsigned lock_depth,posted,resets; static int active=USB_ACTIVE; static bool fail_get;
static void osalSysLock(void) { assert(!lock_depth); ++lock_depth; }
static void osalSysUnlock(void) { assert(lock_depth==1); --lock_depth; }
static int usbGetDriverStateI(void *p) { (void)p; assert(lock_depth==1); return active; }
static bool bqIsSuspendedX(output_buffers_queue_t *q) { assert(lock_depth==1); return q->suspended; }
static bool obqIsFullI(output_buffers_queue_t *q) { assert(lock_depth==1); return q->count==2; }
static int obqGetEmptyBufferTimeoutS(output_buffers_queue_t *q,sysinterval_t t) { assert(lock_depth==1 && t==TIME_IMMEDIATE && !q->ptr && q->count<2); if(fail_get)return -1; q->ptr=q->empty;return MSG_OK; }
static void obqPostFullBufferS(output_buffers_queue_t *q,size_t n) { assert(lock_depth==1 && q->ptr && q->count<2); memcpy(q->reports[q->count],q->ptr,n);q->sizes[q->count++]=n;q->ptr=NULL; }
static void era_hid_report_interval_endpoint_posted_i(const void *p) { (void)p;assert(lock_depth==1);++posted; }
static bool era_hid_report_interval_endpoint_allowed_i(const void *p,const uint8_t *d) { (void)p;(void)d;assert(lock_depth==1);return true; }
/* No shared HID producer may enter these old blocking/buffered fallbacks. */
static size_t obqWriteTimeout(output_buffers_queue_t *q,const uint8_t *d,size_t n,sysinterval_t t){(void)q;(void)d;(void)n;(void)t;abort();}
static void bqSuspendI(output_buffers_queue_t *q){(void)q;abort();}
static void obqResetI(output_buffers_queue_t *q){(void)q;++resets;abort();}
static void bqResumeX(output_buffers_queue_t *q){(void)q;abort();}
static void osalOsRescheduleS(void){abort();}
static void obqFlush(output_buffers_queue_t *q){(void)q;abort();}
'''

CASES = r'''
int main(void) {
    usb_endpoint_in_t ep={.config={.buffer_size=32},.obqueue={.bsize=40}};
    uint8_t a[8]={1,2,3},b[8]={4,5,6},mouse[8]={7,8,9};
    assert(usb_endpoint_in_send(&ep,a,8,100,false)); assert(usb_endpoint_in_send(&ep,b,8,100,false));
    assert(posted==2 && ep.obqueue.count==2);
    assert(!usb_endpoint_in_send(&ep,mouse,8,100,false));
    assert(posted==2 && resets==0 && !memcmp(ep.obqueue.reports[0],a,8) && !memcmp(ep.obqueue.reports[1],b,8));
    ep.obqueue.count=0; ep.obqueue.ptr=ep.obqueue.empty;
    ep.obqueue.empty[0]=99; assert(!usb_endpoint_in_send(&ep,a,8,100,false)); assert(ep.obqueue.empty[0]==99 && posted==2);
    ep.obqueue.ptr=NULL; ep.obqueue.suspended=true; assert(!usb_endpoint_in_send(&ep,a,8,100,false));
    ep.obqueue.suspended=false; active=0; assert(!usb_endpoint_in_send(&ep,a,8,100,false));
    active=USB_ACTIVE;fail_get=true;assert(!usb_endpoint_in_send(&ep,a,8,100,false)); assert(posted==2);
    fail_get=false;ep.obqueue.bsize=12;assert(!usb_endpoint_in_send(&ep,a,8,100,false));
    ep.obqueue.bsize=40;assert(usb_endpoint_in_send(&ep,mouse,8,100,false));assert(posted==3 && !lock_depth);
    puts("PASS production USB whole-buffer/shared BUSY/partial/suspend/inactive/refusal guards");
}
'''

class UsbAdmission(unittest.TestCase):
    def test_whole_usb_acceptance(self):
        function = 'bool usb_endpoint_in_send(usb_endpoint_in_t *endpoint,const uint8_t *data,size_t size,sysinterval_t timeout,bool buffered) {\n' + body('tmk_core/protocol/chibios/usb_driver.c', 'usb_endpoint_in_send') + '\n}\n'
        run_c(STUBS + function + CASES)

if __name__ == '__main__':
    unittest.main(verbosity=2)
