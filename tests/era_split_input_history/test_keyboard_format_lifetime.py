#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual VIA -> QMK report -> interval -> USB admission and HID idle callers.

HAL queue/storage callbacks, EEPROM and unrelated reload/cancellation are simulated.
Separate executable cases make each pre-fix counterexample independently visible.
"""
import re
import signal
import subprocess
import unittest
from test_production import ROOT, body, run_c
from test_usb_admission import STUBS

GLUE = 'keyboards/era/common/system/era_hid_report_interval_chibios.c'
DECL = r'''
#define SHARED_EP_ENABLE
#define NKRO_ENABLE
#define USB_ENDPOINT_IN_KEYBOARD 0
#define USB_ENDPOINT_IN_SHARED 1
#define USB_ENDPOINT_IN_COUNT 2
#define USB_PROTOCOL_BOOT 0
#define USB_PROTOCOL_REPORT 1
#define REPORT_ID_NKRO 5
#define REPORT_ID_COUNT 5
#define USB_DRIVER usb_driver
#define TIME_MS2I(x) (x)
#define KEYBOARD_REPORT_KEYS 6
#define KEYBOARD_REPORT_SIZE 8
#define NKRO_REPORT_BITS 30
#define KC_NO 0
#define dprintf(...) ((void)0)
#define TAP_DANCE_OWNED_ACTIONS
#define TAP_DANCE_MAX_SIMULTANEOUS 4
#define NO_ACTION_ONESHOT
typedef struct { uint8_t mods,reserved,keys[6]; } report_keyboard_t;
typedef struct { uint8_t report_id,mods,bits[30]; } report_nkro_t;
typedef struct { bool nkro; } keymap_config_t;
typedef uint8_t usb_endpoint_in_lut_t;
typedef struct { uint8_t data[32]; size_t length; } usb_fs_report_t;
typedef struct {
 void *reports;
 uint8_t (*get_idle)(void *,uint8_t);
 bool (*idle_timer_elasped)(void *,uint8_t);
 void (*get_report)(void *,uint8_t,usb_fs_report_t *);
 void (*reset_report)(void *);
} usb_report_storage_t;
static int usb_driver;
static usb_endpoint_in_t usb_endpoints_in[2];
keymap_config_t keymap_config;
static keymap_config_t durable_config;
static uint8_t protocol=USB_PROTOCOL_REPORT;
static uint32_t now=10;
static bool run_idle_task;
static usb_fs_report_t stored[2][6];
/* Host completion state survives device-side queue and GET_REPORT resets. */
static usb_fs_report_t host_shadow[2][6];
static uint8_t idle_rate[2][6];
static usb_report_storage_t storage[2];
static report_keyboard_t keyboard_buffer;
static report_nkro_t nkro_buffer={.report_id=REPORT_ID_NKRO};
report_keyboard_t *keyboard_report=&keyboard_buffer;
report_nkro_t *nkro_report=&nkro_buffer;
static uint8_t real_mods,weak_mods;
static bool era_report_force;
static bool inject_suspend_on_update;
static bool inject_protocol_before_select;
static bool inject_suspend_after_terminal_admit;
static uint8_t injected_protocol;
static void protocol_set(uint8_t value);
static void fixture_suspend(void);
void era_hid_report_interval_session_edge_i(bool keep_backlog);
static uint8_t biton(uint8_t byte) {for(uint8_t i=0;i<8;++i)if(byte&(1U<<i))return i;return 0;}
static uint32_t chVTGetSystemTimeX(void) {return now;}
static uint8_t usb_device_state_get_protocol(void) {return protocol;}
static void usb_device_state_set_protocol(uint8_t value) {protocol=value?USB_PROTOCOL_REPORT:USB_PROTOCOL_BOOT;}
static bool host_can_send_nkro(void) {return protocol==USB_PROTOCOL_REPORT;}
static bool era_usb_auxiliary_reports_pending(void) {return false;}
static void era_usb_auxiliary_reports_task(void) {}
static bool send_report(usb_endpoint_in_lut_t ep,void *report,size_t size);
static bool usb_endpoint_in_send(usb_endpoint_in_t *,const uint8_t *,size_t,sysinterval_t,bool);
static void force_send_keyboard_report(void);
static void resend_keyboard_report(void);
static void send_keyboard_report(void);
static void clear_keyboard_but_mods(void);
static void clear_keyboard_but_mods_and_keys(void);
static void clear_keys(void) {clear_keys_from_report();}
static void host_keyboard_send(report_keyboard_t *r) {send_keyboard(r);}
static void host_nkro_send(report_nkro_t *r) {send_nkro(r);}
static bool eeprom_begin_transaction(void) {return true;}
static bool eeprom_end_transaction(void) {return true;}
static void eeconfig_read_keymap(keymap_config_t *r) {*r=durable_config;}
static void eeconfig_update_keymap(keymap_config_t *r) {
 durable_config=*r;
 if(inject_suspend_on_update) {
  inject_suspend_on_update=false;
  era_hid_report_interval_session_edge_i(true);active=0;
 }
}
static uint8_t stored_idle(void *p,uint8_t id) {return idle_rate[(uintptr_t)p][id];}
static bool stored_elapsed(void *p,uint8_t id) {return stored_idle(p,id)!=0;}
static void stored_report(void *p,uint8_t id,usb_fs_report_t *out) {*out=stored[(uintptr_t)p][id];}
static void reset_storage(void *p) {memset(stored[(uintptr_t)p],0,sizeof(stored[0]));}
static bool usb_endpoint_in_is_inactive(usb_endpoint_in_t *ep) {return ep->obqueue.count==0;}
typedef uint8_t era_split_eeprom_sync_domain_t;
enum { ERA_SPLIT_EEPROM_SYNC_DOMAIN_ERA_CONFIG=1, ERA_SPLIT_EEPROM_SYNC_DOMAIN_QMK_RGB_MATRIX=2,
 ERA_SPLIT_EEPROM_SYNC_DOMAIN_QMK_DEFAULT_LAYER=3, ERA_SPLIT_EEPROM_SYNC_DOMAIN_QMK_KEYMAP_CONFIG=4,
 ERA_SPLIT_EEPROM_SYNC_DOMAIN_VIA_LAYOUT_OPTIONS=5, ERA_SPLIT_EEPROM_SYNC_DOMAIN_DYNAMIC_KEYMAP=6,
 ERA_SPLIT_EEPROM_SYNC_DOMAIN_DYNAMIC_MACRO=7 };
static void tomak_reload_config_from_era_eeprom(void) {abort();}
static void era_split_keyboard_reload_features_from_eeprom(void) {abort();}
static void tomak_reload_rgb_matrix_from_eeprom_silent(void) {abort();}
static uint32_t eeconfig_read_default_layer(void) {abort();}
static void default_layer_set(uint32_t layer) {(void)layer;abort();}
static void era_split_eeprom_sync_reload_domain_user(era_split_eeprom_sync_domain_t domain) {(void)domain;}
static void tap_dance_cancel_all(void) { /* VIA cases have no active TD outputs. */ }
static void tap_dance_clear_hid_ownership(void) {}
'''

CASES = r'''
static bool send_report(usb_endpoint_in_lut_t ep,void *report,size_t size) {
 bool accepted=usb_endpoint_in_send(&usb_endpoints_in[ep],report,size,0,false);
 if(accepted && inject_suspend_after_terminal_admit && report==era_hid_retire_dispatch) {
  inject_suspend_after_terminal_admit=false;fixture_suspend();
 }
 return accepted;
}
static void fixture_suspend(void) {
 osalSysLock();era_hid_report_interval_session_edge_i(true);active=0;
 for(unsigned ep=0;ep<2;++ep)usb_endpoint_in_suspend_cb(&usb_endpoints_in[ep]);
 osalSysUnlock();
}
static void fixture_resume(void) {
 osalSysLock();active=USB_ACTIVE;
 for(unsigned ep=0;ep<2;++ep)usb_endpoint_in_wakeup_cb(&usb_endpoints_in[ep]);
 osalSysUnlock();
}
static void complete(void) {
 for(unsigned ep=0;ep<2;++ep) {
  output_buffers_queue_t *q=&usb_endpoints_in[ep].obqueue;
  for(unsigned i=0;i<q->count;++i) {
   uint8_t id=ep?q->reports[i][0]:0;
   stored[ep][id].length=q->sizes[i];memcpy(stored[ep][id].data,q->reports[i],q->sizes[i]);
   host_shadow[ep][id]=stored[ep][id];
   era_hid_report_interval_note_completed(ep,now);
  }
  q->count=0;
 }
}
static void settle(void) {for(unsigned i=0;i<10;++i){now+=100;era_hid_report_interval_service();complete();}}
static void fresh(bool nkro) {
 memset(usb_endpoints_in,0,sizeof(usb_endpoints_in));
 memset(stored,0,sizeof(stored));memset(idle_rate,0,sizeof(idle_rate));
 memset(host_shadow,0,sizeof(host_shadow));
 memset(keyboard_report,0,sizeof(*keyboard_report));memset(nkro_report,0,sizeof(*nkro_report));nkro_report->report_id=REPORT_ID_NKRO;
 real_mods=weak_mods=0;now=10;active=USB_ACTIVE;fail_get=false;keymap_config.nkro=nkro;durable_config=keymap_config;
 protocol=USB_PROTOCOL_REPORT;era_hid_format=UINT8_MAX;era_hid_protocol_events=era_hid_protocol_seen=0;era_hid_reopen=false;
 era_hid_retire_pending=0;era_hid_retire_dispatch=NULL;era_hid_retire_reset=false;
 era_hid_retire_debt=0;era_hid_suspend_events=0;inject_suspend_after_terminal_admit=false;
 era_hid_session_events=era_hid_format_session=0;
 inject_suspend_on_update=inject_protocol_before_select=false;
 tap_dance_clear_key_ownership();
 for(unsigned ep=0;ep<2;++ep) {
  usb_endpoints_in[ep].config.buffer_size=32;usb_endpoints_in[ep].obqueue.bsize=40;
  storage[ep]=(usb_report_storage_t){(void*)(uintptr_t)ep,stored_idle,stored_elapsed,stored_report,reset_storage};
  usb_endpoints_in[ep].report_storage=&storage[ep];
 }
 era_hid_report_interval_init(1);era_hid_report_interval_select_format(nkro?2:1);force_send_keyboard_report();complete();
}
static bool has_key(unsigned ep,uint8_t key) {
 uint8_t *data=host_shadow[ep][ep?REPORT_ID_NKRO:0].data;
 if(ep)return data[2+(key>>3)]&(1U<<(key&7));
 for(unsigned i=2;i<8;++i)if(data[i]==key)return true;
 return false;
}
static void protocol_set(uint8_t value) {
 /* The production SET_PROTOCOL keyboard-interface arm is inserted here. */
 PROTOCOL_ARM
}
static void toggle_case(bool initial,bool width) {
 fresh(initial);add_key_to_report(4);real_mods=1;send_keyboard_report();complete();
 assert(has_key(initial,4));
 if(width)host_keyboard_delay(80);else fail_get=true;
 assert(era_nkro_via_set_enabled(!initial));
 fail_get=false;settle();
 assert(!has_key(initial,4));
 assert(stored[initial][initial?REPORT_ID_NKRO:0].data[initial?1:0]==0);
 assert(!has_key(!initial,4));
 puts("PASS VIA terminal release survives old interval/BUSY in both formats");
}
static void protocol_case(bool initial) {
 fresh(true);protocol_set(initial?USB_PROTOCOL_REPORT:USB_PROTOCOL_BOOT);settle();
 add_key_to_report(4);send_keyboard_report();complete();
 protocol_set(initial?USB_PROTOCOL_BOOT:USB_PROTOCOL_REPORT);settle();
 assert(has_key(!initial,4));
 del_key_from_report(4);send_keyboard_report();complete();
 protocol_set(initial?USB_PROTOCOL_REPORT:USB_PROTOCOL_BOOT);settle();
 assert(!has_key(initial,4));
 assert(!(nkro_report->bits[0]&16));
 for(unsigned i=0;i<6;++i)assert(keyboard_report->keys[i]!=4);
 puts("PASS actual QMK buffers survive protocol round-trip and release");
}
static void idle_case(void) {
 fresh(true);add_key_to_report(4);send_keyboard_report();complete();
 idle_rate[1][REPORT_ID_NKRO]=1;
 protocol_set(USB_PROTOCOL_BOOT);settle();
 run_idle_task=true;unsigned before=posted;usb_idle_task();
 assert(posted==before);
 /* A normal shared auxiliary idle resend remains admitted. */
 stored[1][2]=(usb_fs_report_t){.data={2,3},.length=8};idle_rate[1][2]=1;
 run_idle_task=true;usb_idle_task();assert(posted==before+1);
 puts("PASS actual idle rejects retired NKRO and keeps shared auxiliary");
}
static void storage_case(bool initial) {
 fresh(initial);add_key(4);real_mods=1;send_keyboard_report();complete();
 fail_get=true;durable_config.nkro=!initial;
 era_split_eeprom_sync_reload_domain_kb(ERA_SPLIT_EEPROM_SYNC_DOMAIN_QMK_KEYMAP_CONFIG);
 era_hid_report_interval_service();assert(keymap_config.nkro==!initial);
 fail_get=false;settle();
 assert(!has_key(initial,4));assert(has_key(!initial,4));
 del_key(4);real_mods=0;send_keyboard_report();settle();assert(!has_key(!initial,4));
 puts("PASS actual storage reload retires old lane before publishing held live owner");
}
static void aba_case(void) {
 fresh(false);add_key(4);send_keyboard_report();complete();
 fail_get=true;era_nkro_via_set_enabled(true);era_nkro_via_set_enabled(false);era_nkro_via_set_enabled(true);
 add_key(6);send_keyboard_report();
 fail_get=false;settle();assert(!has_key(0,4));assert(has_key(1,6));
 /* Reset cancels terminal empties with the old USB queues. */
 fail_get=true;era_nkro_via_set_enabled(false);assert(era_hid_retire_pending);
 era_hid_report_interval_session_edge_i(false);active=0;complete();
 era_hid_report_interval_service();active=USB_ACTIVE;fail_get=false;
 settle();assert(!era_hid_retire_pending);assert(!has_key(0,6));
 puts("PASS repeated local ABA and reset retire exactly bounded terminal obligations");
}
static void accepted_order_case(void) {
 fresh(false);add_key(4);send_keyboard_report();
 /* Keep A's accepted packet in the queue while the local toggle runs. */
 era_nkro_via_set_enabled(true);add_key(6);send_keyboard_report();
 assert(usb_endpoints_in[0].obqueue.count==2);
 assert(usb_endpoints_in[0].obqueue.reports[0][2]==4);
 assert(usb_endpoints_in[0].obqueue.reports[1][2]==0);
 assert(usb_endpoints_in[1].obqueue.count==0);
 complete();era_hid_report_interval_service();
 /* The terminal old-lane empty is admitted before the new active chord. */
 assert(usb_endpoints_in[0].obqueue.count==1);
 assert(usb_endpoints_in[0].obqueue.reports[0][2]==0);
 assert(usb_endpoints_in[1].obqueue.count==2);
 complete();assert(!has_key(0,4));assert(has_key(1,6));
 puts("PASS already accepted old packets retain order before terminal release and new state");
}
static void td_protocol_case(void) {
 fresh(true);add_key(4);real_mods=1;
 uint8_t prev=tap_dance_action_set_owner(0);add_key(4);add_mods(2);tap_dance_action_set_owner(prev);
 send_keyboard_report();complete();protocol_set(USB_PROTOCOL_BOOT);settle();
 assert(has_key(0,4));assert(stored[0][0].data[0]==3);
 del_key(4);real_mods=0;send_keyboard_report();complete();assert(has_key(0,4));
 prev=tap_dance_action_set_owner(0);del_key(4);del_mods(2);tap_dance_action_set_owner(prev);
 send_keyboard_report();complete();protocol_set(USB_PROTOCOL_REPORT);settle();
 assert(!has_key(1,4));assert(stored[1][REPORT_ID_NKRO].data[1]==0);
 puts("PASS actual TD/basic usage union and modifier owners survive protocol release");
}
static void chord_pulse_case(void) {
 fresh(true);for(uint8_t key=4;key<12;++key)add_key(key);real_mods=2;send_keyboard_report();complete();
 protocol_set(USB_PROTOCOL_BOOT);settle();
 for(uint8_t key=4;key<10;++key)assert(has_key(0,key));
 del_key(4);del_key(5);send_keyboard_report();complete();
 protocol_set(USB_PROTOCOL_REPORT);settle();for(uint8_t key=6;key<12;++key)assert(has_key(1,key));
 era_kkuk_start_empty_pulse();complete();for(uint8_t key=6;key<12;++key)assert(!has_key(1,key));
 del_key(6);era_kkuk_finish_restore_pulse();complete();assert(!has_key(1,6));
 for(uint8_t key=7;key<12;++key)assert(has_key(1,key));
 protocol_set(USB_PROTOCOL_BOOT);settle();assert(!has_key(0,6));assert(has_key(0,10)&&has_key(0,11));
 puts("PASS eight-key fallback and actual KKUK dual save/restore use current release state");
}
static void idle_aba_case(void) {
 fresh(true);add_key(4);send_keyboard_report();complete();idle_rate[1][REPORT_ID_NKRO]=1;
 protocol_set(USB_PROTOCOL_BOOT);settle();del_key(4);send_keyboard_report();complete();
 protocol_set(USB_PROTOCOL_REPORT);fail_get=true;era_hid_report_interval_service();fail_get=false;
 run_idle_task=true;unsigned before=posted;usb_idle_task();assert(posted==before);
 settle();run_idle_task=true;usb_idle_task();complete();assert(!has_key(1,4));
 puts("PASS same-lane protocol ABA idle waits for pending current state and resends current owners");
}
static void suspend_via_case(bool initial) {
 fresh(initial);add_key(4);real_mods=1;send_keyboard_report();complete();assert(has_key(initial,4));
 host_keyboard_delay(80);inject_suspend_on_update=true;
 assert(era_nkro_via_set_enabled(!initial));
 active=USB_ACTIVE;settle();assert(!has_key(initial,4));
 assert(stored[initial][initial?REPORT_ID_NKRO:0].data[initial?1:0]==0);
 puts("PASS VIA second-clear SUSPEND IRQ preserves old-lane terminal empty");
}
static void suspend_storage_case(bool initial) {
 fresh(initial);add_key(4);real_mods=1;send_keyboard_report();complete();assert(has_key(initial,4));
 era_hid_report_interval_session_edge_i(true);active=0;durable_config.nkro=!initial;
 era_split_eeprom_sync_reload_domain_kb(ERA_SPLIT_EEPROM_SYNC_DOMAIN_QMK_KEYMAP_CONFIG);
 active=USB_ACTIVE;settle();assert(!has_key(initial,4));assert(has_key(!initial,4));
 puts("PASS storage local format change preserves terminal empty across pending SUSPEND");
}
static void protocol_snapshot_case(bool to_boot) {
 fresh(true);add_key(4);send_keyboard_report();complete();
 if(to_boot) {protocol_set(USB_PROTOCOL_BOOT);settle();protocol_set(USB_PROTOCOL_REPORT);}
 else protocol_set(USB_PROTOCOL_BOOT);
 uint32_t seen=era_hid_protocol_seen;uint8_t selected=era_hid_format;
 /* Production action_util computes its argument, then SET_PROTOCOL IRQ
  * runs before the real select implementation reads the event serial. */
 injected_protocol=to_boot?USB_PROTOCOL_BOOT:USB_PROTOCOL_REPORT;inject_protocol_before_select=true;
 force_send_keyboard_report();
 assert(era_hid_protocol_seen==seen && era_hid_format==selected && !era_hid_retire_pending);
 settle();assert(era_hid_protocol_seen==era_hid_protocol_events && !era_hid_retire_pending);
 assert(has_key(to_boot?0:1,4));
 puts("PASS protocol IRQ between argument and serial preserves host edge classification");
}
static void service_snapshot_case(bool to_boot) {
 fresh(true);add_key(4);send_keyboard_report();complete();
 if(to_boot) {protocol_set(USB_PROTOCOL_BOOT);settle();protocol_set(USB_PROTOCOL_REPORT);}
 else protocol_set(USB_PROTOCOL_BOOT);
 unsigned retired_lane=to_boot?1:0;uint32_t before=s.posted[retired_lane];
 injected_protocol=to_boot?USB_PROTOCOL_BOOT:USB_PROTOCOL_REPORT;inject_protocol_before_select=true;
 era_hid_report_interval_service();
 assert(era_hid_protocol_seen==era_hid_protocol_events && !era_hid_retire_pending);
 assert(era_hid_format==(to_boot?0:2));
 assert(s.posted[retired_lane]==before); /* no local-toggle exception after the host IRQ */
 complete();settle();assert(has_key(to_boot?0:1,4));
 puts("PASS service argument/IRQ boundary cannot post a retired-lane terminal after host edge");
}
static void reset_selection_case(unsigned consumed,bool same) {
 fresh(false);add_key(4);send_keyboard_report();complete();
 fail_get=true;assert(era_nkro_via_set_enabled(true));assert(era_hid_retire_pending);
 era_hid_report_interval_session_edge_i(false);active=0;
 uint32_t ended_lane_completed=s.completed[same?0:1];
 if(consumed==1)era_hid_report_interval_task(now); /* interval owner already consumed reset */
 if(consumed==2)era_hid_report_interval_retire_task(); /* platform already consumed reset */
 /* RESET ends the old interface even when a local bit change follows it. */
 if(!same)keymap_config.nkro=false;
 era_hid_report_interval_select_format(same?2:1);
 assert(!era_hid_retire_pending);
 active=USB_ACTIVE;fail_get=false;settle();assert(!era_hid_retire_pending);
 assert(s.completed[same?0:1]==ended_lane_completed);
 puts("PASS pending/consumed RESET and same-format selection cannot create an old terminal");
}
static void pending_protocol_aba_case(void) {
 fresh(false);add_key(4);send_keyboard_report();complete();
 uint32_t old_completed=s.completed[0];
 fail_get=true;era_nkro_via_set_enabled(true);assert(era_hid_retire_pending);
 protocol_set(USB_PROTOCOL_BOOT);protocol_set(USB_PROTOCOL_REPORT);
 fail_get=false;settle();assert(!era_hid_retire_pending && s.completed[0]==old_completed);
 puts("PASS host protocol ABA ends pending local terminal and does not revive retired taps");
}
static void reset_fresh_prefix_case(void) {
 fresh(true);era_hid_report_interval_session_edge_i(false);
 /* The real transport producer can own new-session output before reopen. */
 add_key(6);send_nkro(nkro_report);complete();host_keyboard_delay(80);
 del_key(6);send_nkro(nkro_report);assert(s.count && s.deadline_valid);
 unsigned before=posted;now=20;era_hid_report_interval_service();
 assert(posted==before && s.count && s.deadline_valid);
 now=90;era_hid_report_interval_service();complete();assert(!has_key(1,6));
 puts("PASS same-format RESET selection preserves newly owned current-session tap prefix/width");
}
static void suspend_accepted_terminal_case(bool initial,bool via,bool race,bool twice) {
 fresh(initial);add_key(4);real_mods=1;send_keyboard_report();complete();assert(has_key(initial,4));
 uint32_t old_completed=s.completed[initial];
 inject_suspend_after_terminal_admit=race;
 if(via)assert(era_nkro_via_set_enabled(!initial));
 else {
  durable_config.nkro=!initial;
  era_split_eeprom_sync_reload_domain_kb(ERA_SPLIT_EEPROM_SYNC_DOMAIN_QMK_KEYMAP_CONFIG);
  era_hid_report_interval_service();
 }
 assert(s.completed[initial]==old_completed && has_key(initial,4));
 if(!race) {
  assert(usb_endpoints_in[initial].obqueue.count>0);
  fixture_suspend();
 }
 assert(!inject_suspend_after_terminal_admit && usb_endpoints_in[initial].obqueue.count==0);
 /* Device storage was erased, but host truth is still a held key/modifier. */
 assert(stored[initial][initial?REPORT_ID_NKRO:0].length==0 && has_key(initial,4));
 assert(host_shadow[initial][initial?REPORT_ID_NKRO:0].data[initial?1:0]==1);
 fixture_resume();
 if(twice) {era_hid_report_interval_service();fixture_suspend();fixture_resume();}
 settle();
 assert(!has_key(initial,4));
 assert(host_shadow[initial][initial?REPORT_ID_NKRO:0].data[initial?1:0]==0);
 assert(has_key(!initial,4)==!via);
 puts("PASS accepted old terminal survives actual same-session queue flush and admission-return IRQ");
}
static void suspend_new_lane_case(bool initial) {
 fresh(initial);add_key(4);send_keyboard_report();complete();
 assert(era_nkro_via_set_enabled(!initial));add_key(6);send_keyboard_report();
 assert(has_key(initial,4) && !has_key(!initial,6));
 fixture_suspend();fixture_resume();settle();
 assert(!has_key(initial,4) && has_key(!initial,6));
 /* Completion is not a reason to forget a session's inactive-interface debt.
  * Repeated suspend may resend the empty, but cannot clear the live chord. */
 fixture_suspend();fixture_resume();era_hid_report_interval_service();
 output_buffers_queue_t *q=&usb_endpoints_in[!initial].obqueue;
 assert(q->count==1);
 if(initial)assert(q->reports[0][2]==6);
 else assert(q->reports[0][2+(6>>3)]&(1U<<(6&7)));
 complete();settle();assert(has_key(!initial,6) && !has_key(initial,4));
 puts("PASS already queued new format and repeated SUSPEND restore active owner without active-lane terminal");
}
static void suspend_aba_lane_case(void) {
 fresh(false);add_key(4);send_keyboard_report();complete();fail_get=true;
 assert(era_nkro_via_set_enabled(true));assert(era_nkro_via_set_enabled(false));
 assert(era_nkro_via_set_enabled(true));add_key(8);send_keyboard_report();fail_get=false;
 fixture_suspend();fixture_resume();settle();assert(!has_key(0,4) && has_key(1,8));
 fixture_suspend();fixture_resume();era_hid_report_interval_service();
 assert(usb_endpoints_in[1].obqueue.count==1);
 assert(usb_endpoints_in[1].obqueue.reports[0][3]&1U); /* active NKRO KC8, not empty */
 complete();settle();assert(has_key(1,8));
 puts("PASS ABA debt keeps both lane identities and rearms only the inactive lane");
}
static void suspend_cancelled_debt_case(unsigned reset_consumed,bool protocol_aba) {
 fresh(false);add_key(4);send_keyboard_report();complete();
 assert(era_nkro_via_set_enabled(true));add_key(6);send_keyboard_report();settle();
 uint32_t old_posted=s.posted[0];
 if(protocol_aba) {protocol_set(USB_PROTOCOL_BOOT);protocol_set(USB_PROTOCOL_REPORT);}
 else {
  era_hid_report_interval_session_edge_i(false);
  if(reset_consumed==1)era_hid_report_interval_task(now);
  if(reset_consumed==2)era_hid_report_interval_retire_task();
 }
 fixture_suspend();fixture_resume();settle();
 assert(s.posted[0]==old_posted && !era_hid_retire_pending && !era_hid_retire_debt);
 assert(has_key(1,6));
 fixture_suspend();fixture_resume();settle();assert(s.posted[0]==old_posted);
 puts("PASS RESET/consumed RESET/host protocol ABA permanently cancel retired session debt");
}
int main(void) {
 (void)toggle_case;(void)protocol_case;(void)idle_case;(void)storage_case;(void)aba_case;
 (void)accepted_order_case;(void)td_protocol_case;(void)chord_pulse_case;(void)idle_aba_case;
 (void)suspend_via_case;(void)suspend_storage_case;(void)protocol_snapshot_case;
 (void)reset_selection_case;(void)pending_protocol_aba_case;
 (void)service_snapshot_case;(void)reset_fresh_prefix_case;
 (void)suspend_accepted_terminal_case;(void)suspend_new_lane_case;
 (void)suspend_aba_lane_case;(void)suspend_cancelled_debt_case;
 CASE return 0;
}
'''


def extracted(path, name, ret, args):
    source = re.sub(r'/\*.*?\*/|//[^\n]*', '', (ROOT / path).read_text(encoding='utf-8'), flags=re.S)
    start = re.search(r'\b' + name + r'\([^;{}]*\)\s*\{', source).end()
    depth = 1
    for end in range(start, len(source)):
        depth += (source[end] == '{') - (source[end] == '}')
        if not depth:
            return f'\n{ret} {name}({args}) {{\n{source[start:end]}\n}}\n'
    raise AssertionError(name)


def production_unit():
    stubs = re.sub(r'static (?:bool era_hid_report_interval_endpoint_allowed_i|bool era_hid_report_interval_endpoint_owned|void era_hid_report_interval_endpoint_posted_i)[^\n]+\n', '', STUBS)
    stubs = stubs.replace('q->ptr=NULL; }', 'q->ptr=NULL; ++posted; }')
    stubs = stubs.replace('bool timed_out;', 'usb_report_storage_t *report_storage; bool timed_out;')
    stubs = stubs.replace('typedef int sysinterval_t;', 'typedef struct usb_report_storage usb_report_storage_t;\ntypedef int sysinterval_t;')
    stubs = stubs.replace('static void bqSuspendI(output_buffers_queue_t *q){(void)q;abort();}',
                          'static void bqSuspendI(output_buffers_queue_t *q){assert(lock_depth==1);q->suspended=true;}')
    stubs = stubs.replace('static void obqResetI(output_buffers_queue_t *q){(void)q;++resets;abort();}',
                          'static void obqResetI(output_buffers_queue_t *q){assert(lock_depth==1);++resets;q->ptr=NULL;q->count=0;}')
    stubs = stubs.replace('static void bqResumeX(output_buffers_queue_t *q){(void)q;abort();}',
                          'static void bqResumeX(output_buffers_queue_t *q){assert(lock_depth==1);q->suspended=false;}')
    decl = DECL.replace('typedef struct {\n void *reports;', 'struct usb_report_storage {\n void *reports;').replace('} usb_report_storage_t;', '};')
    # Forward declarations precede the HAL callbacks and full platform owner.
    decl = decl.replace('static void clear_keys(void) {clear_keys_from_report();}', 'void clear_keys_from_report(void);\nstatic void clear_keys(void) {clear_keys_from_report();}')
    decl = decl.replace('static void host_keyboard_send', 'void send_keyboard(report_keyboard_t *);\nvoid send_nkro(report_nkro_t *);\nstatic void host_keyboard_send', 1)
    unit = stubs + decl + '\n#include "keyboards/era/common/system/era_hid_report_interval.c"\n'
    glue = (ROOT / GLUE).read_text(encoding='utf-8')
    glue = glue[glue.index('static bool era_hid_report_interval_ready;'):glue.rindex('#endif')]
    # Keep caller and selection bodies unchanged while injecting the allowed
    # IRQ boundary after action_util's argument calculation and before entry.
    select = 'bool era_hid_report_interval_select_format(uint8_t format) {'
    assert glue.count(select) == 1
    wrapper = r'''
bool fixture_select_format_actual(uint8_t format);
bool era_hid_report_interval_select_format(uint8_t format) {
 if(inject_protocol_before_select) {
  inject_protocol_before_select=false;protocol_set(injected_protocol);
 }
 return fixture_select_format_actual(format);
}
'''
    unit += glue.replace(select, wrapper+'\nbool fixture_select_format_actual(uint8_t format) {', 1)
    report = (ROOT / 'tmk_core/protocol/report.c').read_text(encoding='utf-8')
    unit += report[report.index('uint8_t has_anykey'):report.index('#ifdef MOUSE_ENABLE')]
    action = (ROOT / 'quantum/action_util.c').read_text(encoding='utf-8')
    unit += action[action.index('static uint8_t td_action_owner'):action.index('/* Mouse codes keep')]
    for name, ret, args in [
        ('get_mods','uint8_t','void'), ('get_weak_mods','uint8_t','void'),
        ('get_mods_for_report','uint8_t','void'),
        ('add_mods','void','uint8_t mods'), ('del_mods','void','uint8_t mods'),
        ('clear_mods','void','void'), ('clear_weak_mods','void','void'),
        ('keyboard_report_filter','void','report_keyboard_t *report'),
        ('nkro_report_filter','void','report_nkro_t *report'),
        ('send_6kro_report','void','void'), ('send_nkro_report','void','void'),
        ('send_keyboard_report','void','void'), ('resend_keyboard_report','void','void'),
        ('force_send_keyboard_report','void','void'),
    ]:
        unit += extracted('quantum/action_util.c', name, ret, args)
    for name in ['clear_keyboard_but_mods_and_keys','clear_keyboard_but_mods','clear_keyboard']:
        unit += extracted('quantum/action.c',name,'void','void')
    for name, args in [('send_keyboard','report_keyboard_t *report'),('send_nkro','report_nkro_t *report')]:
        unit += extracted('tmk_core/protocol/chibios/usb_main.c',name,'void',args)
    unit += extracted('keyboards/era/common/features/era_nkro_via.c','era_nkro_via_set_enabled','bool','bool enabled')
    unit += extracted('keyboards/era/sirind/common/tomak_common.c','era_split_eeprom_sync_reload_domain_kb','void','era_split_eeprom_sync_domain_t domain')
    unit += 'static uint8_t kkuk_pulse_state;static uint32_t kkuk_last_pulse_ms;static bool kkuk_pulse_gap_active;\n#define ERA_KKUK_PULSE_RESTORE 1\n#define ERA_KKUK_PULSE_IDLE 0\nstatic uint32_t timer_read32(void){return now;}\n'
    for name in ['era_kkuk_start_empty_pulse','era_kkuk_finish_restore_pulse']:
        unit += extracted('keyboards/era/common/features/era_kkuk.c',name,'void','void')
    unit += extracted('tmk_core/protocol/chibios/usb_driver.c','usb_endpoint_in_send','bool','usb_endpoint_in_t *endpoint,const uint8_t *data,size_t size,sysinterval_t timeout,bool buffered')
    for callback in ['usb_endpoint_in_suspend_cb','usb_endpoint_in_wakeup_cb']:
        unit += extracted('tmk_core/protocol/chibios/usb_driver.c',callback,'void','usb_endpoint_in_t *endpoint')
    unit += extracted('tmk_core/protocol/chibios/usb_report_handling.c','usb_idle_task','void','void')
    main = (ROOT / 'tmk_core/protocol/chibios/usb_main.c').read_text(encoding='utf-8')
    arm = main[main.index('case HID_REQ_SetProtocol:'):main.index('case HID_REQ_SetIdle:')]
    arm = arm[arm.index('#ifdef'):arm.index('usbSetupTransfer')]
    arm = arm.replace('setup->wValue.lbyte', 'value')
    # Remove only the enclosing keyboard-interface if's closing brace.
    arm = arm[:arm.rindex('}')]
    return unit + CASES.replace('PROTOCOL_ARM',arm)


class KeyboardFormatLifetime(unittest.TestCase):
    def execute(self, case):
        run_c(production_unit().replace('CASE',case))

    def test_via_6kro_busy(self): self.execute('toggle_case(false,false);')
    def test_via_6kro_width(self): self.execute('toggle_case(false,true);')
    def test_via_nkro_busy(self): self.execute('toggle_case(true,false);')
    def test_via_nkro_width(self): self.execute('toggle_case(true,true);')
    def test_report_boot_release_report(self): self.execute('protocol_case(true);')
    def test_boot_report_release_boot(self): self.execute('protocol_case(false);')
    def test_stale_idle(self): self.execute('idle_case();')
    def test_storage_6kro_reload(self): self.execute('storage_case(false);')
    def test_storage_nkro_reload(self): self.execute('storage_case(true);')
    def test_local_aba_reset(self): self.execute('aba_case();')
    def test_accepted_order(self): self.execute('accepted_order_case();')
    def test_td_protocol_union(self): self.execute('td_protocol_case();')
    def test_chord_and_kkuk(self): self.execute('chord_pulse_case();')
    def test_same_lane_idle_aba(self): self.execute('idle_aba_case();')
    def test_via_suspend_6kro(self): self.execute('suspend_via_case(false);')
    def test_via_suspend_nkro(self): self.execute('suspend_via_case(true);')
    def test_storage_suspend_6kro(self): self.execute('suspend_storage_case(false);')
    def test_storage_suspend_nkro(self): self.execute('suspend_storage_case(true);')
    def test_protocol_irq_to_boot(self): self.execute('protocol_snapshot_case(true);')
    def test_protocol_irq_to_report(self): self.execute('protocol_snapshot_case(false);')
    def test_service_irq_to_boot(self): self.execute('service_snapshot_case(true);')
    def test_service_irq_to_report(self): self.execute('service_snapshot_case(false);')
    def test_reset_pending_changed_format(self): self.execute('reset_selection_case(0,false);')
    def test_reset_core_consumed_changed_format(self): self.execute('reset_selection_case(1,false);')
    def test_reset_platform_consumed_changed_format(self): self.execute('reset_selection_case(2,false);')
    def test_reset_pending_same_format(self): self.execute('reset_selection_case(0,true);')
    def test_protocol_aba_pending_terminal(self): self.execute('pending_protocol_aba_case();')
    def test_reset_fresh_session_prefix(self): self.execute('reset_fresh_prefix_case();')
    def test_via_6kro_suspend_after_admit(self): self.execute('suspend_accepted_terminal_case(false,true,false,false);')
    def test_via_nkro_suspend_after_admit(self): self.execute('suspend_accepted_terminal_case(true,true,false,false);')
    def test_storage_6kro_suspend_after_admit(self): self.execute('suspend_accepted_terminal_case(false,false,false,false);')
    def test_storage_nkro_suspend_after_admit(self): self.execute('suspend_accepted_terminal_case(true,false,false,false);')
    def test_via_6kro_irq_after_admit(self): self.execute('suspend_accepted_terminal_case(false,true,true,false);')
    def test_via_nkro_irq_after_admit(self): self.execute('suspend_accepted_terminal_case(true,true,true,false);')
    def test_storage_6kro_irq_after_admit(self): self.execute('suspend_accepted_terminal_case(false,false,true,false);')
    def test_storage_nkro_irq_after_admit(self): self.execute('suspend_accepted_terminal_case(true,false,true,false);')
    def test_repeated_suspend_before_completion(self): self.execute('suspend_accepted_terminal_case(false,true,false,true);')
    def test_new_nkro_queue_suspend(self): self.execute('suspend_new_lane_case(false);')
    def test_new_6kro_queue_suspend(self): self.execute('suspend_new_lane_case(true);')
    def test_local_aba_suspend_debt(self): self.execute('suspend_aba_lane_case();')
    def test_reset_debt_cancel(self): self.execute('suspend_cancelled_debt_case(0,false);')
    def test_core_consumed_reset_debt_cancel(self): self.execute('suspend_cancelled_debt_case(1,false);')
    def test_platform_consumed_reset_debt_cancel(self): self.execute('suspend_cancelled_debt_case(2,false);')
    def test_protocol_aba_debt_cancel(self): self.execute('suspend_cancelled_debt_case(0,true);')

    def negative(self, before, after, case):
        unit = production_unit().replace('CASE', case)
        self.assertEqual(unit.count(before), 1)
        unit = unit.replace(before, after, 1)
        with self.assertRaises(subprocess.CalledProcessError) as result:
            run_c(unit)
        # Compiler failure cannot satisfy a runtime sensitivity control.
        self.assertEqual(result.exception.returncode, -signal.SIGABRT)
        self.assertNotEqual(result.exception.cmd[0], 'cc')

    def test_negative_terminal_retirement(self):
        self.negative('era_hid_retire_pending |= era_hid_format == 2 ? 2U : 1U;',
                      '(void)format;', 'toggle_case(false,false);')

    def test_negative_inactive_projection_release(self):
        self.negative('del_key_bit(nkro_report, key);\n    del_key_byte(keyboard_report, key);',
                      'if (host_can_send_nkro() && keymap_config.nkro) del_key_bit(nkro_report,key);\n'
                      '    else del_key_byte(keyboard_report,key);', 'protocol_case(true);')

    def test_negative_idle_owner_route(self):
        self.negative('if (era_hid_report_interval_handle_idle(ep, report_id)) continue;',
                      '(void)report_id;', 'idle_aba_case();')

    def test_negative_suspend_terminal_generation(self):
        self.negative('session == era_hid_format_session) {',
                      'era_hid_report_interval_session_current()) {', 'suspend_via_case(false);')

    def test_negative_protocol_snapshot_guard(self):
        self.negative('if (format != era_hid_report_interval_current_format()) {',
                      'if (false) {', 'service_snapshot_case(true);')

    def test_negative_suspend_debt_rearm(self):
        self.negative('era_hid_retire_pending |= era_hid_retire_debt & (uint8_t)~active_lane;',
                      '(void)active_lane;', 'suspend_accepted_terminal_case(false,true,false,false);')

    def test_negative_admission_return_irq_guard(self):
        self.negative('if (suspend == era_hid_suspend_events) era_hid_retire_pending &= (uint8_t)~bit;',
                      '(void)suspend;era_hid_retire_pending &= (uint8_t)~bit;',
                      'suspend_accepted_terminal_case(false,true,true,false);')

    def test_negative_active_lane_rearm_filter(self):
        self.negative('era_hid_retire_pending |= era_hid_retire_debt & (uint8_t)~active_lane;',
                      '(void)active_lane;era_hid_retire_pending |= era_hid_retire_debt;', 'suspend_aba_lane_case();')

    def test_negative_protocol_debt_cancel(self):
        self.negative('/* Host protocol changes end the old format lifetime, including ABA. */\n'
                      '        era_hid_retire_pending = 0;\n        era_hid_retire_debt = 0;',
                      'era_hid_retire_pending = 0;', 'suspend_cancelled_debt_case(0,true);')

    def test_negative_reset_debt_cancel(self):
        unit = production_unit().replace('CASE','suspend_cancelled_debt_case(2,false);')
        self.assertEqual(unit.count('era_hid_retire_debt = 0;'), 4)
        unit = unit.replace('era_hid_retire_debt = 0;', '(void)era_hid_retire_debt;')
        with self.assertRaises(subprocess.CalledProcessError) as result:
            run_c(unit)
        self.assertEqual(result.exception.returncode, -signal.SIGABRT)
        self.assertNotEqual(result.exception.cmd[0], 'cc')


if __name__ == '__main__':
    unittest.main(verbosity=2)
