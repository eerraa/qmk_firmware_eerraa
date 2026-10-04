#define main nor_tests_main
#include "nor.c"
#undef main
#include <setjmp.h>
#include "keyboards/era/common/storage/era_via_magic.h"
#include "keyboards/era/common/storage/era_reset_guard.h"
#define VIA_EEPROM_MAGIC_ADDR 317U
#define VIA_EEPROM_LAYOUT_OPTIONS_ADDR 320U
#define VIA_EEPROM_LAYOUT_OPTIONS_DEFAULT 0U
#define NKRO_DEFAULT_ON false
#define DYNAMIC_KEYMAP_ENABLE
static bool eeconfig_init_failed;
typedef uint8_t layer_state_t;
typedef struct { uint8_t raw; } debug_config_t;
typedef struct { bool swap_control_capslock,capslock_to_control,swap_lalt_lgui,swap_ralt_rgui,no_gui,swap_grave_esc,swap_backslash_backspace,nkro,swap_lctl_lgui,swap_rctl_rgui,oneshot_enable,swap_escape_capslock,autocorrect_enable; } keymap_config_t;
static layer_state_t default_layer_state,layer_state;
static debug_config_t debug_config;
static keymap_config_t keymap_config;
static unsigned init_kb_calls,keymap_calls,macro_calls,ready_calls;
static jmp_buf stalled;
void eeconfig_update_debug(const debug_config_t *c) { eeprom_update_byte((void*)2,c->raw); }
void eeconfig_update_default_layer(layer_state_t c) { eeprom_update_byte((void*)3,c); }
void eeconfig_update_keymap(const keymap_config_t *c) { (void)c;eeprom_update_word((void*)4,0x0100); }
void eeconfig_init_kb(void) { init_kb_calls++;uint8_t cfg=0x51;era_eeprom_update_config(&cfg,156,1);era_reset_guard_write(); }
void via_init_kb(void) { era_reset_guard_check(); }
void via_set_layout_options_kb(uint32_t v) { (void)v; }
void nvm_eeconfig_disable(void);void nvm_eeconfig_enable(void);bool nvm_eeconfig_is_enabled(void);bool nvm_eeconfig_is_disabled(void);
void eeconfig_init_via(void);void via_eeprom_set_valid(bool);bool via_eeprom_is_valid(void);
void eeconfig_init_via_defaults(void);bool via_eeprom_is_ready(void);
void via_set_layout_options(uint32_t);uint32_t via_get_layout_options(void);
void nvm_via_erase(void);void nvm_via_update_magic(uint8_t,uint8_t,uint8_t);void nvm_via_read_magic(uint8_t*,uint8_t*,uint8_t*);
uint32_t nvm_via_read_layout_options(void);void nvm_via_update_layout_options(uint32_t);
void dynamic_keymap_reset(void) { keymap_calls++;assert(eeprom_begin_transaction());const uint8_t bytes[2]={0x12,0x34};eeprom_update_block(bytes,(void*)321,2);eeprom_end_transaction(); }
void dynamic_keymap_macro_reset(void) { macro_calls++;nvm_dynamic_keymap_macro_reset(); }
void eeconfig_read_debug(debug_config_t *c) { c->raw=eeprom_read_byte((void*)2); }
void eeconfig_read_keymap(keymap_config_t *c) { memset(c,0,sizeof(*c)); }
uint8_t eeconfig_read_default_layer(void) { return eeprom_read_byte((void*)3); }
void default_layer_set(layer_state_t v) { (void)v;ready_calls++; }
void layer_state_set_kb(layer_state_t v) { (void)v; }
void wait_ms(unsigned n) { (void)n;longjmp(stalled,1); }
#include "boot_functions.inc"
static void counts_clear(void) { init_kb_calls=keymap_calls=macro_calls=ready_calls=0; }
static void full(void) { assert(init_kb_calls==1 && keymap_calls==1 && macro_calls==1);assert(eeconfig_is_enabled()); }
static void boot(void) { via_init();era_reset_guard_check();quantum_init(); }
int main(void) {
    fresh();eeconfig_init_failed=false;counts_clear();boot();full();assert(ready_calls==1);
    counts_clear();boot();assert(init_kb_calls==0 && keymap_calls==0 && macro_calls==0 && ready_calls==1);
    eeprom_update_word((void*)0,0x1010);counts_clear();boot();full();
    via_eeprom_set_valid(false);counts_clear();boot();assert(init_kb_calls==0 && keymap_calls==1 && macro_calls==1);
    assert(era_eeprom_driver_prepare_reboot_word(0,0xFFFF)==ERA_NVM_RESULT_OK);reboot();counts_clear();boot();full();
    uint8_t bad=0;era_eeprom_update_config(&bad,228,1);counts_clear();boot();full();
    counts_clear();eeconfig_init();full();
    puts("PASS stock default initializer/VIA branch bodies: empty, valid, invalid QMK, invalid VIA, CLEAN prepare/reboot, reset-key mismatch and default restore; model default payload owners invoked once");
    // A full initializer may not acknowledge an unrelated caller's open scope.
    counts_clear();
    assert(eeprom_begin_transaction());
    unsigned healthy_before = programs;
    eeconfig_init();
    assert(eeprom_transaction_active() && eeconfig_is_init_required());
    assert(programs == healthy_before && init_kb_calls == 0 && keymap_calls == 0 && macro_calls == 0);
    assert(eeprom_end_transaction());
    assert(eeconfig_is_init_required() && !eeconfig_is_enabled());
    eeconfig_init(); full();
    counts_clear();
    assert(eeprom_begin_transaction());
    unsigned before = programs;
    eeconfig_init();
    assert(eeprom_transaction_active() && eeconfig_is_init_required() && !eeconfig_is_enabled());
    assert(programs == before && init_kb_calls == 0 && keymap_calls == 0 && macro_calls == 0);
    eeprom_update_byte((void *)2, 0x39);
    fail_mode = 0; fail_all = true;
    assert(!eeprom_end_transaction() && !eeconfig_is_enabled());
    if (!setjmp(stalled)) quantum_init();
    assert(ready_calls == 0 && !eeconfig_is_enabled());
    fail_all = false; counts_clear(); eeconfig_init(); full();
    reboot(); counts_clear(); boot();
    assert(ready_calls == 1 && init_kb_calls == 0 && keymap_calls == 0 && macro_calls == 0);
    puts("PASS nested full defaults refuse before mutation, preserve boot gate through either outer result, and recover by top-level retry");
    // Persistently failed reset guard invalidation leaves old validity bytes intact.
    for (volatile unsigned mode=0; mode<4; mode++) {
        bad=0;era_eeprom_update_config(&bad,228,1);counts_clear();eeconfig_init_failed=false;fail_mode=mode;fail_all=true;
        if (!setjmp(stalled)) boot();
        assert(!eeprom_persistence_ok() && !eeconfig_is_enabled() && ready_calls==0);
        fail_all=false; counts_clear(); boot(); full(); assert(ready_calls==1);
    }
    puts("PASS reset-key no-effect/torn/ambiguous invalidation blocks normal loop until checked defaults complete");
    return 0;
}
