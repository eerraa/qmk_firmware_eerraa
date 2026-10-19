manifest=[]
def source(path):
    p=ROOT/path
    b=p.read_bytes()
    manifest.append({'path':path,'sha256':hashlib.sha256(b).hexdigest(),'scope':'complete direct compilation'})
    return b.decode('utf-8')
def extract(path,name):
    s=source(path)
    match=re.search(r'^[^\n]*\b'+name+r'\([^;\n]*\)\s*\{',s,re.M)
    if not match: raise ValueError((path,name))
    start=match.start(); pos=match.end(); depth=1
    # Functions used here have no brace-bearing strings/comments.
    while depth:
        depth += (s[pos]=='{')-(s[pos]=='}'); pos+=1
    out=s[start:pos]+'\n'
    manifest[-1]['scope']='function '+name
    manifest[-1]['lines']=[s.count('\n',0,start)+1,s.count('\n',0,pos)+1]
    manifest[-1]['extracted_sha256']=hashlib.sha256(out.encode()).hexdigest()
    return out
def write(name,s):
    p=OUT/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(s,encoding='utf-8')
write('stubs/dynamic_keymap.h','''#pragma once
#include <stdint.h>
#define DYNAMIC_KEYMAP_LAYER_COUNT 6U
#define DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE 16384U
static inline uint16_t dynamic_keymap_macro_get_buffer_size(void) { return DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE; }
void dynamic_keymap_reset(void);
void dynamic_keymap_macro_reset(void);
''')
write('stubs/matrix.h','#pragma once\n#define MATRIX_ROWS 5U\n#define MATRIX_COLS 8U\n')
write('stubs/eeconfig.h','''#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "eeprom.h"
#define EECONFIG_KB_DATA_SIZE 0
#define EECONFIG_USER_DATA_SIZE 0
#define EECONFIG_MAGIC ((uint16_t *)0)
#define EECONFIG_MAGIC_NUMBER 0xFEE7U
#define EECONFIG_MAGIC_NUMBER_OFF 0xFFFFU
void eeconfig_init(void);
void eeconfig_enable(void);
void eeconfig_require_reset(void);
bool eeconfig_is_init_required(void);
bool eeconfig_is_enabled(void);
''')
write('stubs/keycode_config.h','#pragma once\n#include <stdint.h>\ntypedef struct { uint16_t raw; } keymap_config_t;\n')
write('stubs/via.h','''#pragma once
#include <stdint.h>
#define VIA_EEPROM_LAYOUT_OPTIONS_SIZE 1U
#define VIA_EEPROM_CUSTOM_CONFIG_SIZE 0U
#define id_get_keyboard_value 2U
''')
write('stubs/raw_hid.h','#pragma once\n#include <stdint.h>\nvoid raw_hid_send(uint8_t *,uint8_t);\n')
write('stubs/compiler_support.h','#pragma once\n#define STATIC_ASSERT _Static_assert\n')
macro_source=source('quantum/nvm/eeprom/nvm_dynamic_keymap.c')
macro_state_start=macro_source.index('#ifndef EEPROM_CUSTOM\n/* A cold marker')
macro_state_end=macro_source.index('void nvm_dynamic_keymap_macro_reset_session(')
macro=macro_source[macro_state_start:macro_state_end]+'\n'+'\n'.join(extract('quantum/nvm/eeprom/nvm_dynamic_keymap.c',n) for n in ['nvm_dynamic_keymap_macro_reset_session','nvm_dynamic_keymap_macro_read_buffer','nvm_dynamic_keymap_macro_update_buffer','nvm_dynamic_keymap_macro_reset'])
write('macro_functions.inc',macro)
# Complete compiled sources, preserving source ownership and hashes.
for p in ['keyboards/era/common/storage/era_nvm.c','keyboards/era/common/storage/era_eeprom_driver.c','drivers/eeprom/eeprom_driver.c','drivers/eeprom/eeprom_wear_leveling.c','quantum/wear_leveling/wear_leveling.c','keyboards/era/common/system/era_state_sync.c','lib/fnv/hash_64a.c','platforms/eeprom.h']:
    source(p)
