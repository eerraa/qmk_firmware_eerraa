from pathlib import Path
import hashlib, json, re, subprocess

import os, sys
ROOT = Path(os.environ.get('ERA_FIXTURE_ROOT', Path(__file__).resolve().parents[2]))
OUT = Path(sys.argv[1])
STUB = OUT/'stub'
STUB.mkdir(parents=True, exist_ok=True)
manifest = []

def src(path):
    return (ROOT/path).read_text(encoding='utf-8')

def record(path, start=1, end=None, mode='direct compile'):
    raw=(ROOT/path).read_bytes()
    manifest.append(dict(path=path, sha256=hashlib.sha256(raw).hexdigest(),
                         lines=[start, end or len(raw.splitlines())], mode=mode))

def extract(path, name):
    text=src(path)
    # Locate exact production function; do not remove comments or rewrite body.
    m=re.search(r'^[ \t]*(?:(?:static|inline)\s+)*(?:__attribute__\(\(weak\)\)\s+)?\w+[ \t*]+'+name+r'\([^;{}]*\)\s*\{', text, re.M)
    assert m, name
    # Ignore comments/strings only when counting braces, preserving exact text.
    tail=text[m.end():]
    tokens=re.finditer(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\n]*|[{}]',tail,re.S)
    depth=1
    for t in tokens:
        if t[0]=='{': depth+=1
        if t[0]=='}': depth-=1
        if not depth:
            end=m.end()+t.end()
            record(path,text[:m.start()].count('\n')+1,text[:end].count('\n')+1,'exact full function extraction')
            return text[m.start():end]+'\n'
    raise AssertionError(name)

def write(name, text):
    path=OUT/name
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(text,encoding='utf-8')

via=src('quantum/via.h')
write('stub/via.h',via)
record('quantum/via.h',mode='exact header copy')
write('stub/action.h','#pragma once\n#include "quantum.h"\n')
write('stub/quantum.h',r'''
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "eeprom.h"
#include "quantum/keycodes.h"
typedef struct { struct { bool pressed; uint32_t time; } event; } keyrecord_t;
typedef struct { uint8_t count; } tap_dance_state_t;
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
uint16_t timer_read(void);
uint16_t timer_elapsed(uint16_t t);
uint32_t timer_read32(void);
void clear_keyboard(void);
bool shutdown_user(bool jump);
''')
write('stub/eeprom.h','#pragma once\n#include "platforms/eeprom.h"\n')
write('stub/eeconfig.h',r'''
#pragma once
#include "quantum.h"
#include "keycode_config.h"
void eeconfig_read_keymap(keymap_config_t *c);
void eeconfig_update_keymap(const keymap_config_t *c);
void eeconfig_init(void);
''')
keymap=src('quantum/keycode_config.h')
start=keymap.index('typedef union keymap_config_t')
end=keymap.index('} keymap_config_t;')+len('} keymap_config_t;')
record('quantum/keycode_config.h',keymap[:start].count('\n')+1,keymap[:end].count('\n')+1,'exact type extraction')
write('stub/keycode_config.h','#pragma once\n#include <stdint.h>\n#include <stdbool.h>\n'+keymap[start:end]+'\nextern keymap_config_t keymap_config;\n')
write('stub/dynamic_keymap.h',src('quantum/dynamic_keymap.h'))
record('quantum/dynamic_keymap.h',mode='exact header copy')
write('stub/matrix.h','#pragma once\n#include <stdint.h>\ntypedef uint16_t matrix_row_t;\n#define MATRIX_ROWS 12\n#define MATRIX_COLS 11\nmatrix_row_t matrix_get_row(uint8_t r);\n')
write('stub/raw_hid.h','#pragma once\n#include <stdint.h>\nvoid raw_hid_send(uint8_t *data,uint8_t len);\n')
write('stub/usb_descriptor.h','#pragma once\n#define RAW_EPSIZE 32\n')
write('stub/atomic_util.h','#pragma once\n#define ATOMIC_BLOCK_RESTORESTATE for(int once=1;once;once=0)\n')
write('stub/util.h','#pragma once\n#include "quantum.h"\n')
write('stub/timer.h','#pragma once\n#include "quantum.h"\n')
write('stub/debounce.h','#pragma once\n')
write('stub/mousekey.h',r'''
#pragma once
#include <stdint.h>
#define MOUSEKEY_MOVE_MAX 127
#define MOUSEKEY_WHEEL_MAX 127
#define MOUSEKEY_DELAY 10
#define MOUSEKEY_WHEEL_DELAY 10
#define MOUSEKEY_WHEEL_INTERVAL 80
#define MOUSEKEY_WHEEL_DELTA 1
#define MOUSEKEY_WHEEL_MAX_SPEED 8
#define MOUSEKEY_WHEEL_TIME_TO_MAX 40
extern uint8_t mk_delay,mk_wheel_delay,mk_interval,mk_wheel_interval;
extern uint8_t mk_move_delta,mk_wheel_delta,mk_cursor_top,mk_wheel_top;
extern uint8_t mk_max_speed,mk_wheel_max_speed,mk_time_to_max,mk_wheel_time_to_max;
extern uint16_t mk_cursor_ramp_ms,mk_wheel_ramp_ms;
''')
write('stub/action_util.h','#pragma once\nvoid send_keyboard_report(void);\n')
write('stub/report.h',r'''
#pragma once
#include <stdint.h>
#define IS_BASIC_KEYCODE(k) ((k)>=4 && (k)<=0xdf)
#define IS_MODIFIER_KEYCODE(k) ((k)>=0xe0 && (k)<=0xe7)
#define MOD_BIT(k) (1U<<((k)-0xe0))
#define KEYBOARD_REPORT_KEYS 6
typedef struct {uint8_t mods;uint8_t keys[6];} report_keyboard_t;
''')

eeunit='''#include "eeconfig.h"
#include "eeprom.h"
#define EECONFIG_KEYMAP ((uint16_t *)4)
'''
for p,names in [('quantum/nvm/eeprom/nvm_eeconfig.c','nvm_eeconfig_read_keymap nvm_eeconfig_update_keymap'),
                ('quantum/eeconfig.c','eeconfig_read_keymap eeconfig_update_keymap')]:
    for name in names.split(): eeunit+=extract(p,name)
write('eeconfig_owner.c',eeunit)

sysunit='''#include "quantum.h"
#include "via.h"
#include "keyboards/era/common/system/era_via_system.h"
#include "keyboards/era/common/system/era_state_sync.h"
static uint32_t via_last_raw_hid_ms;
'''
for name in 'era_via_system_set_value era_via_system_get_value era_via_system_handle_via_command via_command_kb'.split():
    sysunit+=extract('keyboards/era/common/system/era_via_system.c',name)
write('system_owner.c',sysunit)

dbunit='''#include <stddef.h>
#include <string.h>
#include "keyboards/era/common/system/era_matrix_debounce_runtime.h"
'''
for name in 'era_matrix_debounce_clamp_delay era_matrix_debounce_default_config'.split():
    dbunit+=extract('keyboards/era/common/system/era_matrix_debounce_runtime.c',name)
dbunit+='void era_matrix_debounce_apply_config(const era_matrix_debounce_config_t *c) {(void)c;}\n'
dbunit+='void era_matrix_debounce_init(void) {}\nbool era_matrix_debounce_update(matrix_row_t r[],matrix_row_t c[],bool b) {(void)r;(void)c;return b;}\n'
write('debounce_boundary.c',dbunit)

KK='keyboards/era/common/features/era_kkuk.c'
kk=src(KK);start=kk.index('enum {');end=kk.index('static uint8_t era_kkuk_clamp_ticks(')
record(KK,kk[:start].count('\n')+1,kk[:end].count('\n'),'exact stored/runtime declaration extraction')
kkunit='''#include <string.h>
#include "quantum.h"
#include "matrix.h"
#include "keyboards/era/common/features/era_kkuk.h"
#include "keyboards/era/common/storage/era_eeprom_storage.h"
#include "keyboards/era/common/system/era_state_sync.h"
'''+kk[start:end]
for name in '''era_kkuk_clamp_ticks era_kkuk_apply_defaults era_kkuk_config_is_valid
era_kkuk_normalize_config era_kkuk_reset_runtime era_kkuk_save_config
era_kkuk_load_from_eeprom era_kkuk_init era_kkuk_reload_from_eeprom
era_kkuk_publish_runtime_change era_kkuk_set_enabled era_kkuk_set_delay_ticks
era_kkuk_set_repeat_ticks era_kkuk_set_mode era_kkuk_get_enabled
era_kkuk_get_delay_ticks era_kkuk_get_repeat_ticks era_kkuk_get_mode'''.split():
    kkunit+=extract(KK,name)
write('kkuk_owner.c',kkunit)

TD='keyboards/era/common/features/era_tapdance.c'
td=src(TD)
start=td.index('typedef struct __attribute__((packed))')
end=td.index('typedef struct {\n    uint16_t active_keycode;')
record(TD,td[:start].count('\n')+1,td[:end].count('\n'),'exact storage types extraction')
tdunit='''#include <string.h>
#include "quantum.h"
#include "keyboards/era/common/features/era_tapdance.h"
#include "keyboards/era/common/storage/era_eeprom_storage.h"
#include "keyboards/era/common/system/era_state_sync.h"
#define ERA_TAP_DANCE_SIGNATURE 0x4E414454UL
#define ERA_TAP_DANCE_VERSION 1U
#define ERA_TAP_DANCE_MODE_TAG 0xD2U
#define ERA_TAP_DANCE_TERM_MIN_MS 100U
#define ERA_TAP_DANCE_TERM_MAX_MS 500U
#define ERA_TAP_DANCE_TERM_EXACT_MIN_MS 1U
#define ERA_TAP_DANCE_TERM_STEP_MS 20U
#define ERA_TAP_DANCE_TERM_DEFAULT_MS 200U
'''+td[start:end]+'''
static era_tapdance_storage_t tapdance_storage;
static era_tapdance_slot_state_t tapdance_state[ERA_TAP_DANCE_SLOT_COUNT];
'''
for name in '''era_tapdance_get_mode era_tapdance_normalize_term era_tapdance_note_runtime_change
era_tapdance_publish_timing era_tapdance_get_hold_term era_tapdance_get_hold_other
era_tapdance_set_hold_term era_tapdance_set_hold_other era_tapdance_set_mode
era_tapdance_set_action era_tapdance_set_slot_term_ms era_tapdance_set_slot_term_ms_exact
era_tapdance_get_action era_tapdance_get_slot_term_ms era_tapdance_apply_defaults
era_tapdance_sync_state_from_storage era_tapdance_save_config'''.split():
    tdunit+=extract(TD,name)
tdunit+='''\nvoid fixture_td_defaults(void) {
era_tapdance_apply_defaults(); era_tapdance_sync_state_from_storage();
memset(&tapdance_timing,0,sizeof(tapdance_timing));
tapdance_timing.version=1; tapdance_timing.signature=ERA_TAP_DANCE_TIMING_SIGNATURE;
}\n'''
write('td_owner.c',tdunit)

RAW='quantum/via.c'
record('quantum/keycodes.h', mode='direct header include')
rawunit='''#include <stdint.h>
#include <stdbool.h>
#include "quantum.h"
#include "via.h"
#include "dynamic_keymap.h"
#include "matrix.h"
#include "raw_hid.h"
uint32_t via_get_layout_options(void);
void via_set_layout_options(uint32_t value);
void via_set_device_indication(uint8_t value);
void via_custom_value_command_kb(uint8_t *data, uint8_t length);
bool via_command_kb(uint8_t *data, uint8_t length);
'''
rawunit+=extract(RAW,'via_custom_value_command')
rawunit+=extract(RAW,'raw_hid_receive')
write('raw_owner.c',rawunit)

DISPATCH='keyboards/era/common/split/era_split_keyboard.c'
dispatch='''#include "via.h"
#include "keyboards/era/common/system/era_common_via.h"
#include "keyboards/era/common/system/era_board_hooks.h"
#include "keyboards/era/common/split/era_split_via_sync.h"
bool era_split_via_link_handle_via_command(uint8_t *d,uint8_t n) {(void)d;(void)n;return false;}
'''+extract(DISPATCH,'era_split_keyboard_handle_via_command')
dispatch+=extract('keyboards/era/common/split/era_split_board.c','via_custom_value_command_kb')
write('dispatch.c',dispatch)

direct=['keyboards/era/common/storage/era_nvm.c',
        'keyboards/era/common/storage/era_eeprom_driver.c',
        'drivers/eeprom/eeprom_driver.c',
        'keyboards/era/common/storage/era_eeprom_config_io.c',
        'keyboards/era/common/system/era_state_sync.c',
        'keyboards/era/common/system/era_common_via.c',
        'keyboards/era/common/system/era_board_hooks.c',
        'keyboards/era/common/features/era_tapdance_via.c',
        'keyboards/era/common/features/era_nkro_via.c',
        'keyboards/era/common/split/era_split_sync_policy.c',
        'keyboards/era/common/split/era_split_via_sync.c',
        'keyboards/era/common/features/era_mousekey.c',
        'keyboards/era/common/features/era_mousekey_via.c',
        'keyboards/era/common/features/era_tapping.c',
        'keyboards/era/common/features/era_tapping_via.c',
        'keyboards/era/common/system/era_matrix_debounce_config.c',
        'keyboards/era/common/features/era_debounce_via.c',
        'keyboards/era/common/features/era_socd.c',
        'keyboards/era/common/features/era_socd_via.c',
        'keyboards/era/common/features/era_kkuk_via.c',
        'keyboards/era/common/features/era_lighting_sleep.c',
        'keyboards/era/common/features/era_rgb_sleep.c']
for path in direct: record(path)

write('callers.c',r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "quantum.h"
#include "via.h"
#include "dynamic_keymap.h"
#include "keycode_config.h"
#include "eeprom_driver.h"
#include "keyboards/era/common/storage/era_nvm.h"
#include "keyboards/era/common/storage/era_eeprom_driver.h"
#include "keyboards/era/common/storage/era_storage_layout.h"
#include "keyboards/era/common/features/era_tapdance.h"
#include "keyboards/era/common/features/era_mousekey.h"
#include "keyboards/era/common/features/era_tapping.h"
#include "keyboards/era/common/features/era_kkuk.h"
#include "keyboards/era/common/features/era_socd.h"
#include "keyboards/era/common/features/era_lighting_sleep.h"
#include "keyboards/era/common/system/era_matrix_debounce_config.h"
#include "keyboards/era/common/system/era_state_sync.h"
#include "keyboards/era/common/system/era_board_hooks.h"
#include "keyboards/era/common/split/era_split_sync_policy.h"
void raw_hid_receive(uint8_t *d,uint8_t n);
void fixture_td_defaults(void);
static uint8_t flash_image[ERA_NVM_PHYSICAL_SIZE_BYTES];
static bool fail_program;
static uint32_t now;
static uint8_t response[32];
static unsigned writes,dirty_calls,approved_saves;
keymap_config_t keymap_config;
uint8_t mk_delay,mk_wheel_delay,mk_interval,mk_wheel_interval;
uint8_t mk_move_delta,mk_wheel_delta,mk_cursor_top,mk_wheel_top;
uint8_t mk_max_speed,mk_wheel_max_speed,mk_time_to_max,mk_wheel_time_to_max;
uint16_t mk_cursor_ramp_ms,mk_wheel_ramp_ms;
static bool flash_init(void *ctx) { (void)ctx; return true; }
static bool flash_read(void *ctx,uint32_t a,void *d,size_t n) { (void)ctx; memcpy(d,flash_image+a,n);return true; }
static bool flash_program(void *ctx,uint32_t a,const void *d,size_t n) {
  (void)ctx; writes++; if(fail_program) return false;
  for(size_t i=0;i<n;i++) flash_image[a+i]&=((const uint8_t *)d)[i]; return true;
}
static bool flash_erase(void *ctx,uint32_t a) { (void)ctx;memset(flash_image+a,255,4096);return true; }
bool era_nvm_rp2040_flash_bind(era_nvm_flash_t *f) { *f=(era_nvm_flash_t){NULL,flash_init,flash_read,flash_program,flash_erase};return true; }
void raw_hid_send(uint8_t *d,uint8_t n) {assert(n==32);memcpy(response,d,n);}
uint16_t timer_read(void) {return now;}
uint16_t timer_elapsed(uint16_t t) {return (uint16_t)(now-t);}
uint32_t timer_read32(void) {return now;}
void clear_keyboard(void) {}
bool shutdown_user(bool jump) {(void)jump;return true;}
bool era_firmware_version_handle_via_command(uint8_t *d,uint8_t n) {(void)d;(void)n;return false;}
void era_split_transport_scheduler_mark_dirty(uint8_t f) {(void)f;dirty_calls++;}
void send_keyboard_report(void) {}
void era_board_via_save(void) { approved_saves++; }
void eeconfig_init(void) {}
void via_set_device_indication(uint8_t v) {(void)v;}
uint32_t via_get_layout_options(void) {return eeprom_read_byte((const uint8_t *)ERA_STORAGE_VIA_LAYOUT_OPTIONS_ADDR);}
void via_set_layout_options(uint32_t v) {eeprom_update_byte((uint8_t *)ERA_STORAGE_VIA_LAYOUT_OPTIONS_ADDR,v);}
uint16_t dynamic_keymap_macro_get_buffer_size(void) {return DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE;}
uint8_t dynamic_keymap_macro_get_count(void) {return 16;}
uint8_t dynamic_keymap_get_layer_count(void) {return DYNAMIC_KEYMAP_LAYER_COUNT;}
uint16_t dynamic_keymap_get_keycode(uint8_t l,uint8_t r,uint8_t c) {(void)l;(void)r;(void)c;return 0;}
void dynamic_keymap_set_keycode(uint8_t l,uint8_t r,uint8_t c,uint16_t v) {(void)l;(void)r;(void)c;eeprom_update_word((uint16_t *)ERA_STORAGE_DYNAMIC_KEYMAP_ADDR,v);}
void dynamic_keymap_reset(void) {}
void dynamic_keymap_get_buffer(uint16_t o,uint16_t n,uint8_t *d) {eeprom_read_block(d,(void *)(uintptr_t)(ERA_STORAGE_DYNAMIC_KEYMAP_ADDR+o),n);}
void dynamic_keymap_set_buffer(uint16_t o,uint16_t n,uint8_t *d) {eeprom_update_block(d,(void *)(uintptr_t)(ERA_STORAGE_DYNAMIC_KEYMAP_ADDR+o),n);}
void dynamic_keymap_macro_get_buffer(uint16_t o,uint16_t n,uint8_t *d) {eeprom_read_block(d,(void *)(uintptr_t)(ERA_STORAGE_DYNAMIC_MACRO_ADDR+o),n);}
void dynamic_keymap_macro_set_buffer(uint16_t o,uint16_t n,uint8_t *d) {eeprom_update_block(d,(void *)(uintptr_t)(ERA_STORAGE_DYNAMIC_MACRO_ADDR+o),n);}
void dynamic_keymap_macro_reset(void) {}
static void cmd(uint8_t a,uint8_t b,uint8_t c,uint8_t d,uint8_t e) {uint8_t p[32]={a,b,c,d,e};raw_hid_receive(p,32);}
static void fresh(void) {memset(flash_image,255,sizeof(flash_image));fail_program=false;now=0;eeprom_driver_init();assert(eeprom_persistence_ok());era_state_sync_set_revisions_for_testing(1,1,1);}
int main(int argc,char **argv) {
  if(argc>1 && strcmp(argv[1],"bridge")==0) {
    fresh(); fixture_td_defaults(); era_tapdance_save_config();
    era_split_sync_policy_init(); era_state_sync_set_revisions_for_testing(1,1,1);
    assert(eeprom_persistence_ok()); fail_program=true;
    char line[256];
    while(fgets(line,sizeof(line),stdin)) {
      if(strncmp(line,"STATE",5)==0) {
        printf("{\"runtime\":%u,\"durable\":%u,\"receipt\":%u,\"configRevision\":%lu}\n",
          era_tapdance_get_slot_term_ms(0),eeprom_read_word((const uint16_t *)(ERA_EEPROM_CONFIG_ADDR+8)),
          eeprom_persistence_ok(),(unsigned long)era_state_sync_config_revision()); fflush(stdout);continue;
      }
      uint8_t p[32]={0}; unsigned v;
      for(unsigned i=0;i<32;i++){assert(sscanf(line+2*i,"%2x",&v)==1);p[i]=v;}
      raw_hid_receive(p,32);
      for(unsigned i=0;i<32;i++) printf("%02x",response[i]);puts("");fflush(stdout);
    }
    return 0;
  }
  fresh();
  cmd(0x01,0,0,0,0);
  assert(response[0]==0x01 && response[1]==0x00 && response[2]==0x0D);
  cmd(0x02,0x06,0,0,0);
  assert(response[0]==0x02 && response[1]==0x06);
  assert(response[2]==0x00 && response[3]==0x00 && response[4]==0x00 && response[5]==0x08);
  cmd(0x02,0x80,0x01,0,0xAB);
  assert(response[0]==0x02 && response[1]==0x80 && response[2]==0x01 && response[3]==0x00);
  assert(response[4]==0xAB && response[5]==0x00 && response[6]==0x07 && response[7]==0x00);
  for(unsigned i=8;i<20;i++) assert(response[i]==((i%4)==3 ? 1 : 0));
  for(unsigned i=20;i<32;i++) assert(response[i]==0);
  puts("PASS VIA protocol 13: core keycodes version 0x06 and ERA State Sync 0x80");
  fresh();fixture_td_defaults();era_tapdance_save_config();assert(eeprom_persistence_ok());
  era_state_sync_set_revisions_for_testing(1,1,1);
  uint16_t durable=eeprom_read_word((const uint16_t *)(ERA_EEPROM_CONFIG_ADDR+8));
  cmd(id_custom_set_value,0,72,0,250);assert(response[0]==7);assert(era_state_sync_config_revision()==2);
  cmd(id_custom_set_value,0,88,1,44);assert(response[0]==7);assert(era_state_sync_config_revision()==3);
  fail_program=true;unsigned before=writes;
  cmd(id_custom_save,0,0,0,0);
  assert(writes>before);assert(!eeprom_persistence_ok());assert(response[0]==id_unhandled);
  assert(eeprom_read_word((const uint16_t *)(ERA_EEPROM_CONFIG_ADDR+8))==durable);
  printf("PASS TD immediate SAVE: reply=%02x receipt=%u runtime=%u durable=%u CONFIG=%lu\n",response[0],eeprom_persistence_ok(),era_tapdance_get_slot_term_ms(0),durable,(unsigned long)era_state_sync_config_revision());
  fail_program=false;now=501;unsigned approvals=approved_saves;era_board_housekeeping_tick();
  assert(approved_saves==approvals+1); // TD failure still approves deferred owners
  assert(eeprom_read_word((const uint16_t *)(ERA_EEPROM_CONFIG_ADDR+8))==durable);
  puts("PASS keyboard deferred retry does not retry TD owner's failed immediate SAVE");
  cmd(id_custom_save,0,0,0,0);assert(eeprom_persistence_ok());assert(response[0]==9);
  assert(eeprom_read_word((const uint16_t *)(ERA_EEPROM_CONFIG_ADDR+8))==250);
  assert(eeprom_read_word((const uint16_t *)(ERA_EEPROM_CONFIG_ADDR+172))==300);
  assert(era_state_sync_config_revision()==3);
  puts("PASS TD compound SAVE retry: durable term/timing, CONFIG duplicate suppressed");
  fresh(); fail_program=true;cmd(id_custom_set_value,0,5,1,0);
  assert(response[0]==id_unhandled);assert(!eeprom_persistence_ok());assert(keymap_config.nkro);assert(eeprom_read_word((const uint16_t *)4)==0);
  printf("PASS NKRO synchronous SET: reply=%02x echo=%u receipt=%u runtime=%u durable=%u\n",response[0],response[3],eeprom_persistence_ok(),keymap_config.nkro,eeprom_read_word((const uint16_t *)4));
  fail_program=false;cmd(id_custom_set_value,0,5,1,0);assert(eeprom_persistence_ok());
  keymap_config_t nkro_saved;eeconfig_read_keymap(&nkro_saved);assert(nkro_saved.nkro);
  puts("PASS NKRO explicit SET retry: healthy backend persists same requested ON");
  fresh();era_split_sync_policy_init();assert(eeprom_persistence_ok());
  uint8_t saved[10];eeprom_read_block(saved,(void *)(ERA_EEPROM_CONFIG_ADDR+200),10);assert(saved[9]==7);
  fail_program=true;cmd(id_custom_set_value,9,5,0,0);
  bool requested=true;era_split_sync_policy_get_requested(ERA_SPLIT_SYNC_POLICY_FIELD_EEPROM,&requested);
  assert(response[0]==id_unhandled);assert(!eeprom_persistence_ok());assert(!requested);
  eeprom_read_block(saved,(void *)(ERA_EEPROM_CONFIG_ADDR+200),10);assert(saved[9]==7);
  printf("PASS sync-policy synchronous SET: reply=%02x receipt=%u runtime=%u saved_flags=%u\n",response[0],eeprom_persistence_ok(),requested,saved[9]);
  fail_program=false;
  era_tapping_init();era_tapping_set_term_ms_exact(211);era_tapping_save_config();assert(eeprom_persistence_ok());
  eeprom_read_block(saved,(void *)(ERA_EEPROM_CONFIG_ADDR+200),10);assert(saved[9]==7);
  before=writes;cmd(id_custom_set_value,9,5,0,0);assert(writes>before); assert(response[0]==7);
  eeprom_read_block(saved,(void *)(ERA_EEPROM_CONFIG_ADDR+200),10);assert(saved[9]==6);
  puts("PASS policy receipt survives unrelated healthy TAPPING save");
  eeprom_driver_init();era_split_sync_policy_reload_from_eeprom();era_split_sync_policy_get_requested(ERA_SPLIT_SYNC_POLICY_FIELD_EEPROM,&requested);assert(!requested);
  puts("PASS same-value policy retry persists OFF across cold mount");
  cmd(id_custom_set_value,9,5,0,0);assert(eeprom_persistence_ok());
  eeprom_read_block(saved,(void *)(ERA_EEPROM_CONFIG_ADDR+200),10);assert(saved[9]==6);
  puts("PASS healthy policy changed SET: receipt1 saved_flags6");

  for(unsigned which=0;which<6;which++) {
    fresh();uint8_t channel=0,field=0,value=0;uint32_t address=0;const char *name="";
    switch(which) {
      case 0:era_mousekey_init();channel=13;field=8;value=23;address=ERA_EEPROM_CONFIG_ADDR+100+5;name="MOUSE exact";break;
      case 1:era_tapping_init();channel=15;field=5;value=211;address=ERA_EEPROM_CONFIG_ADDR+88;name="TAPPING exact";break;
      case 2:era_matrix_debounce_config_init();channel=14;field=2;value=11;address=ERA_EEPROM_CONFIG_ADDR+116+1;name="DEBOUNCE";break;
      case 3:era_socd_init();channel=10;field=1;value=1;address=ERA_EEPROM_CONFIG_ADDR+124;name="SOCD-LR";break;
      case 4:era_socd_init();channel=11;field=1;value=1;address=ERA_EEPROM_CONFIG_ADDR+132;name="SOCD-UD";break;
      case 5:era_kkuk_init();channel=12;field=1;value=1;address=ERA_EEPROM_CONFIG_ADDR+140;name="KKUK";break;
    }
    assert(eeprom_persistence_ok());uint8_t old=eeprom_read_byte((const uint8_t *)(uintptr_t)address);
    if(which<2) cmd(7,channel,field,0,value);else cmd(7,channel,field,value,0);
    assert(response[0]==7);fail_program=true;before=writes;cmd(9,channel,0,0,0);
    assert(writes>before);assert(!eeprom_persistence_ok());assert(response[0]==id_unhandled);
    assert(eeprom_read_byte((const uint8_t *)(uintptr_t)address)==old);
    printf("PASS %s synchronous SAVE replyff receipt0 old=%u requested=%u\n",name,old,value);
    fail_program=false;cmd(9,channel,0,0,0);assert(eeprom_persistence_ok());
    assert(eeprom_read_byte((const uint8_t *)(uintptr_t)address)==value);
    printf("PASS %s explicit SAVE retry durable=%u\n",name,value);
  }
  for(unsigned which=0;which<4;which++) {
    fresh();era_lighting_sleep_init();uint8_t field=which==0?11:which==1?15:which==2?13:12;
    uint32_t addr=which==0?ERA_EEPROM_CONFIG_ADDR+164:which==1?ERA_EEPROM_CONFIG_ADDR+166:which==2?ERA_EEPROM_CONFIG_ADDR+168:4;
    uint8_t old=eeprom_read_byte((const uint8_t *)(uintptr_t)addr);
    fail_program=true;cmd(7,9,field,which<2?0:0,which<2?250:0);
    assert(response[0]==id_unhandled);assert(!eeprom_persistence_ok());
    assert(eeprom_read_byte((const uint8_t *)(uintptr_t)addr)==old);
    printf("PASS SYSTEM field%u synchronous SET replyff receipt0 old=%u\n",field,old);
    fail_program=false;
    eeprom_update_byte((uint8_t *)30,(uint8_t)(which+1));assert(eeprom_persistence_ok());
    // An unrelated successful receipt must not consume SLEEP's pending intent.
    before=writes;cmd(7,9,field,0,which<2?250:0);
    if(which<3) {assert(writes>before);assert(eeprom_read_byte((const uint8_t *)(uintptr_t)addr)==(which<2?250:1));}
    else {assert(eeprom_persistence_ok());assert(eeprom_read_word((const uint16_t *)4)&(1U<<13));}
    if(which<3) {
      era_lighting_sleep_reload_from_eeprom();cmd(7,9,field,0,which<2?250:0);
      assert(eeprom_persistence_ok());assert(eeprom_read_byte((const uint8_t *)(uintptr_t)addr)==(which<2?250:1));
    }
    printf("PASS SYSTEM field%u same-value SET retry is durable; owner reload preserves value\n",field);
  }
  fresh();fail_program=true;cmd(id_dynamic_keymap_set_keycode,0,0,0,4);assert(response[0]==id_unhandled);assert(!eeprom_persistence_ok());
  puts("PASS raw_hid_receive stock keymap failure returns ff through same NVM adapter");
}
''')

flags=['-std=gnu11','-O2','-Wall','-Wextra','-Werror=implicit-function-declaration',
       '-DEEPROM_CUSTOM','-DEEPROM_SIZE=24576','-DVIA_ENABLE','-DERA_STATE_SYNC_TEST',
       '-DERA_TAP_DANCE_ENABLE','-DTAP_DANCE_ENABLE','-DERA_TAP_DANCE_KEYCODE_BASE=0x7e00',
       '-DERA_NKRO_VIA_ENABLE','-DERA_STORAGE_QUIET_DEFER_MS=500',
       '-DERA_MOUSEKEY_CONFIG_ENABLE','-DERA_MOUSEKEY_RUNTIME_DELTA','-DERA_TAPPING_CONFIG_ENABLE',
       '-DERA_DEBOUNCE_ENABLE','-DERA_SOCD_ENABLE','-DERA_KKUK_ENABLE','-DERA_VIA_SYSTEM_ENABLE',
       '-DERA_LIGHTING_SLEEP_ENABLE','-DERA_RGB_SLEEP_MASTER_ENABLE','-DERA_BACKLIGHT_SLEEP_ENABLE','-DSPLIT_KEYBOARD',
       '-DERA_LIGHTING_SLEEP_TEST','-include',str(ROOT/'keyboards/era/common/storage/era_eeprom_layout.h'),
       '-DDYNAMIC_KEYMAP_LAYER_COUNT=6','-DDYNAMIC_KEYMAP_MACRO_EEPROM_SIZE=16384',
       '-DQMK_KEYBOARD_H="quantum.h"','-I'+str(STUB),'-I'+str(ROOT),
       '-I'+str(ROOT/'platforms'),'-I'+str(ROOT/'drivers/eeprom')]
command=['gcc',*flags,*[str(ROOT/p) for p in direct],*[str(OUT/p) for p in ['td_owner.c','raw_owner.c','dispatch.c','eeconfig_owner.c','system_owner.c','debounce_boundary.c','kkuk_owner.c','callers.c']],'-o',str(OUT/'callers.exe')]
write('source-manifest.json',json.dumps(manifest,indent=2))
write('compile-command.json',json.dumps(command,indent=2))
result=subprocess.run(command,capture_output=True,text=True)
write('compile.log',result.stdout+result.stderr)
print('compile return',result.returncode)
if result.returncode:
    print(result.stderr[-10000:]); raise SystemExit(result.returncode)
result=subprocess.run([str(OUT/'callers.exe')],capture_output=True,text=True)
write('callers-result.txt',result.stdout+result.stderr)
print(result.stdout+result.stderr)
raise SystemExit(result.returncode)
