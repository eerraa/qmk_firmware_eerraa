"""Production AVR initialization ordering and RP2040 WL physical receipt boundaries."""
from pathlib import Path
import importlib.util
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('boot_functions', ROOT/'tests/era_boot_sequence/test_boot_sequence.py')
boot = importlib.util.module_from_spec(spec)
spec.loader.exec_module(boot)

def run(c, headers=None):
    with tempfile.TemporaryDirectory() as tmp:
        source, exe = Path(tmp)/'test.c', Path(tmp)/'test'
        source.write_text(c)
        for name, content in (headers or {}).items():
            header = Path(tmp)/name
            header.parent.mkdir(parents=True, exist_ok=True)
            header.write_text(content)
        subprocess.run(['gcc','-std=gnu11','-O2','-Wall','-Wextra','-Werror','-I'+str(ROOT/'platforms'),'-I'+tmp,str(source),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)

class StorageBackends(unittest.TestCase):
    def test_rp2040_wear_level_receipt_requires_readback(self):
        body = boot.functions('platforms/chibios/drivers/wear_leveling/wear_leveling_rp2040_flash.c',
                              'backing_store_erase backing_store_write_bulk')
        run(r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#define WEAR_LEVELING_BACKING_SIZE 4096U
#define FLASH_SECTOR_SIZE 4096U
#define WEAR_LEVELING_RP2040_FLASH_BASE 0U
#define STATIC_ASSERT _Static_assert
#define bs_dprintf(...) ((void)0)
#define wl_dump(...) ((void)0)
typedef uint16_t backing_store_int_t;
static uint8_t flash[4096];
#define XIP_BASE ((uintptr_t)flash)
static int interrupts;
static bool masked, drop_erase;
static unsigned allow_words=UINT32_MAX;
static int save_and_disable_interrupts(void) { assert(!masked); masked=true; return 0; }
static void restore_interrupts(int v) { (void)v; assert(masked); masked=false; }
static void flash_range_erase(uint32_t a, uint32_t n) { assert(masked); if(!drop_erase) memset(flash+a,255,n); }
static void pico_program_bulk(uint32_t a, backing_store_int_t *values,size_t count) {
    assert(masked);
    for(size_t i=0;i<count && i<allow_words;i++) ((backing_store_int_t *)(flash+a))[i] &= (backing_store_int_t)~values[i];
}
"""+body+r"""
int main(void) {
    drop_erase=true; assert(!backing_store_erase()); assert(!masked);
    drop_erase=false; assert(backing_store_erase());
    uint16_t values[2]={0x1234,0x5678};
    for(unsigned cut=0;cut<2;cut++) { assert(backing_store_erase()); allow_words=cut; assert(!backing_store_write_bulk(0,values,2)); assert(!masked); }
    allow_words=2; assert(backing_store_write_bulk(0,values,2));
    puts("PASS actual RP2040 WL erase/program receipt: absent/torn writes fail readback");
}
""")

    def test_native_avr_receipts_verify_physical_values_and_retain_scope_failure(self):
        source = (ROOT/'quantum/eeconfig.c').read_text()
        start = source.index('static uint16_t eeprom_native_depth;')
        owner = source[start:source.index('\n#else', start)]
        macro_source = (ROOT/'quantum/nvm/eeprom/nvm_dynamic_keymap.c').read_text()
        macro_context = macro_source[macro_source.index('#ifndef EEPROM_CUSTOM\n/* A cold marker'):
                                     macro_source.index('static inline void *dynamic_keymap_key_to_eeprom_address')]
        header = r"""
#include <stdint.h>
#include <stddef.h>
#include <string.h>
extern uint8_t storage[1024];
extern bool drop_program;
extern uintptr_t failed_address;
static inline void eeprom_busy_wait(void) {}
static inline uint8_t eeprom_read_byte(const uint8_t *p) { return storage[(uintptr_t)p]; }
static inline uint16_t eeprom_read_word(const uint16_t *p) { uint16_t v; memcpy(&v,storage+(uintptr_t)p,2); return v; }
static inline uint32_t eeprom_read_dword(const uint32_t *p) { uint32_t v; memcpy(&v,storage+(uintptr_t)p,4); return v; }
static inline void eeprom_read_block(void *d,const void *p,size_t n) { memcpy(d,storage+(uintptr_t)p,n); }
#define NATIVE(suffix,type) \
static inline void eeprom_write_##suffix(type *p,type v) { for(size_t i=0; i<sizeof(v); i++) if(!drop_program && (uintptr_t)p+i != failed_address) storage[(uintptr_t)p+i]=((uint8_t *)&v)[i]; } \
static inline void eeprom_update_##suffix(type *p,type v) { eeprom_write_##suffix(p,v); }
NATIVE(byte,uint8_t)
NATIVE(word,uint16_t)
NATIVE(dword,uint32_t)
static inline void eeprom_write_block(const void *d,void *p,size_t n) { for(size_t i=0; i<n; i++) if(!drop_program && (uintptr_t)p+i != failed_address) storage[(uintptr_t)p+i]=((const uint8_t *)d)[i]; }
static inline void eeprom_update_block(const void *d,void *p,size_t n) { eeprom_write_block(d,p,n); }
"""
        run(r"""
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
#define __AVR__
#define __AVR_ATmega32U4__
#include "eeprom.h"
uint8_t storage[1024];
bool drop_program;
uintptr_t failed_address=UINTPTR_MAX;
void eeprom_transaction_complete(bool success) { (void)success; }
"""+owner+r"""
#define DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR 256U
#define DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE 128U
"""+macro_context+boot.functions('quantum/nvm/eeprom/nvm_dynamic_keymap.c', 'nvm_dynamic_keymap_macro_read_buffer nvm_dynamic_keymap_macro_update_buffer nvm_dynamic_keymap_macro_reset')+r"""
int main(void) {
    const uint8_t block[4]={1,2,3,4};
#define VERIFY(call) do { memset(storage,0,sizeof(storage)); drop_program=true; call; assert(!eeprom_persistence_ok()); drop_program=false; call; assert(eeprom_persistence_ok()); } while(0)
    VERIFY(eeprom_write_byte((uint8_t *)0,0x12));
    VERIFY(eeprom_update_byte((uint8_t *)0,0x12));
    VERIFY(eeprom_write_word((uint16_t *)0,0x1234));
    VERIFY(eeprom_update_word((uint16_t *)0,0x1234));
    VERIFY(eeprom_write_dword((uint32_t *)0,0x12345678));
    VERIFY(eeprom_update_dword((uint32_t *)0,0x12345678));
    VERIFY(eeprom_write_block(block,(void *)0,sizeof(block)));
    VERIFY(eeprom_update_block(block,(void *)0,sizeof(block)));
    assert(eeprom_begin_transaction() && eeprom_begin_transaction());
    drop_program=true; eeprom_update_byte((uint8_t *)0,0xAB);
    drop_program=false; eeprom_update_byte((uint8_t *)1,0xCD);
    assert(!eeprom_end_transaction() && !eeprom_end_transaction() && !eeprom_persistence_ok());
    eeprom_update_byte((uint8_t *)0,0xAB); assert(eeprom_persistence_ok());
    uint8_t opened=255, closed=0, payload[28]; memset(payload,0xA7,sizeof(payload));
    nvm_dynamic_keymap_macro_update_buffer(127,1,&opened);
    failed_address=270; nvm_dynamic_keymap_macro_update_buffer(0,28,payload);
    assert(!eeprom_persistence_ok()); failed_address=UINTPTR_MAX;
    eeprom_update_byte((uint8_t *)20,0x31); // unrelated success cannot authorize CLOSE
    nvm_dynamic_keymap_macro_update_buffer(127,1,&closed);
    assert(!eeprom_persistence_ok() && storage[383] == 255);
    nvm_dynamic_keymap_macro_update_buffer(127,1,&opened);
    nvm_dynamic_keymap_macro_update_buffer(0,28,payload);
    nvm_dynamic_keymap_macro_update_buffer(127,1,&closed);
    assert(eeprom_persistence_ok() && storage[383] == 0 && !memcmp(storage+256,payload,28));
    failed_address=383; nvm_dynamic_keymap_macro_reset();
    assert(!eeprom_persistence_ok() && !memcmp(storage+256,payload,28));
    failed_address=270; nvm_dynamic_keymap_macro_reset();
    assert(!eeprom_persistence_ok() && storage[383] == 255 && storage[270] == 0xA7);
    failed_address=UINTPTR_MAX; nvm_dynamic_keymap_macro_update_buffer(127,1,&closed);
    assert(!eeprom_persistence_ok() && storage[383] == 255);
    nvm_dynamic_keymap_macro_reset();
    assert(eeprom_persistence_ok());
    for(unsigned i=256;i<384;i++) assert(storage[i] == 0);
    puts("PASS actual native AVR wrappers and macro callers: eight API failures, identical retry, nested failure, failed payload/CLOSE and RESET/retry");
}
""", {'avr/eeprom.h':header})

    def test_native_eeprom_init_cuts_never_expose_partial_defaults(self):
        body = boot.function('quantum/eeconfig.c','eeconfig_init_quantum')
        import re
        members = sorted(set(re.findall(r'\.(\w+)\s*=',body)))
        declaration = 'typedef struct { '+''.join('bool '+x+';' for x in members)+' } keymap_config_t;\n'
        run(r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#define __AVR__
#define VIA_ENABLE
#define NKRO_DEFAULT_ON false
#define EECONFIG_KB_DATA_SIZE 0
#define EECONFIG_USER_DATA_SIZE 0
#define EECONFIG_MAGIC ((uint16_t *)0)
#define EECONFIG_MAGIC_NUMBER 0xFEE7U
#define EECONFIG_MAGIC_NUMBER_OFF 0xFFFFU
typedef uint8_t layer_state_t;
typedef struct { uint8_t raw; } debug_config_t;
static layer_state_t default_layer_state;
static bool eeconfig_init_failed;
static uint8_t store[8];
static unsigned writes, cut;
static bool drop_magic, receipt=true;
static jmp_buf reset;
static void byte_write(unsigned a,uint8_t v) { if(store[a]==v) return; if(++writes==cut) longjmp(reset,1); store[a]=v; }
static void eeprom_update_word(uint16_t *addr,uint16_t v) { if(drop_magic) { receipt=false; return; } unsigned a=(unsigned)(uintptr_t)addr; byte_write(a,v); byte_write(a+1,v>>8); }
static void eeprom_busy_wait(void) {}
static bool eeprom_transaction_active(void) { return false; }
static bool eeprom_begin_transaction(void) { receipt=true; return true; }
static bool eeprom_end_transaction(void) { eeprom_busy_wait(); return true; }
static bool eeprom_persistence_ok(void) { return receipt; }
static void eeconfig_update_debug(const debug_config_t *d) { (void)d; byte_write(2,1); }
static void eeconfig_update_default_layer(layer_state_t s) { (void)s; byte_write(3,2); }
static void eeconfig_init_via_defaults(void) { byte_write(5,4); byte_write(6,5); }
static void eeconfig_init_kb(void) { byte_write(7,6); }
""" + declaration + r"""
static void eeconfig_update_keymap(const keymap_config_t *k) { (void)k; byte_write(4,3); }
""" + boot.function('quantum/nvm/eeprom/nvm_eeconfig.c','nvm_eeconfig_disable') + boot.function('quantum/nvm/eeprom/nvm_eeconfig.c','nvm_eeconfig_enable') + boot.function('quantum/eeconfig.c','eeconfig_enable') + body + r"""
int main(void) {
    for(volatile unsigned boundary=1;boundary<=12;boundary++) {
        memset(store,0x71,sizeof(store)); store[0]=0xE7; store[1]=0xFE; writes=0; cut=boundary;
        if(!setjmp(reset)) eeconfig_init_quantum();
        bool valid=store[0]==0xE7 && store[1]==0xFE;
        if(valid) {
            bool old=true,new_value=true;
            for(unsigned i=2;i<8;i++) { old &= store[i]==0x71; new_value &= store[i]==i-1; }
            assert(old || new_value);
        }
        cut=0; eeconfig_init_quantum();
        assert(!eeconfig_init_failed && store[0]==0xE7 && store[1]==0xFE);
        for(unsigned i=2;i<8;i++) assert(store[i]==i-1);
    }
    memset(store,0x71,sizeof(store)); store[0]=0xE7; store[1]=0xFE; drop_magic=true;
    eeconfig_init_quantum(); assert(eeconfig_init_failed);
    for(unsigned i=2;i<8;i++) assert(store[i] == 0x71);
    drop_magic=false; eeconfig_init_quantum(); assert(!eeconfig_init_failed);
    puts("PASS native EEPROM ordering: production initializer, byte-cut/retry and no-effect invalidation; hardware byte writer modeled");
}
""")

if __name__ == '__main__': unittest.main()
