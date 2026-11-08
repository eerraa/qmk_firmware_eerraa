// SPDX-License-Identifier: GPL-2.0-or-later
/* Injected into matrix_startup.c: real main/setup/init and EEPROM adapter
 * entry, with hardware/storage boundaries that count or refuse entry. */
static unsigned platform_ready, storage_binds, receipts, mounts, usb_attaches;
static unsigned engine_inits, feature_inits, post_inits, via_inits, timer_inits;
static bool storage_fails, settings_valid;
static void halInit(void) {platform_ready=1;}
static void chSysInit(void) {assert(platform_ready==1);platform_ready=2;}
/* PLATFORM */
static void protocol_setup(void) {assert(platform_ready==2);}
static void protocol_pre_init(void) {usb_attaches++;}
static void protocol_post_init(void) {longjmp(boot_exit,2);}
static void protocol_pre_task(void) {assert(false);}
static void protocol_keyboard_task(void) {assert(false);}
static void protocol_post_task(void) {assert(false);}
static void housekeeping_task(void) {assert(false);}
static void print_set_sendchar(void (*sender)(void)) {(void)sender;}
static void sendchar(void) {}
static void matrix_setup(void) {}
static void keyboard_pre_init_quantum(void) {}

typedef unsigned era_nvm_flash_t;
typedef struct {unsigned unused;} era_nvm_t;
typedef struct {
    unsigned macro_address,macro_size;
    void (*public_macro_notifier)(void), (*commit_notifier)(void);
    void *commit_context;
} era_nvm_config_t;
static unsigned macro_serial;
static era_nvm_t s_era_eeprom_nvm;
static bool s_era_eeprom_ready,s_edit_completing,s_era_eeprom_write_ok;
#define ERA_NVM_RESULT_OK 0
#define ERA_STORAGE_DYNAMIC_MACRO_ADDR 100
#define DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE 16384
static bool era_nvm_rp2040_flash_bind(era_nvm_flash_t *flash) {
#ifndef BOOTMAGIC_ENABLE
    assert(!era_rp2040_matrix_pio_state.initialized);
#endif
    (void)flash;storage_binds++;return !storage_fails;
}
void era_settings_reset_bootstrap(void) {
    receipts++;
}
static void era_eeprom_driver_note_public_macro(void) {}
static void era_eeprom_driver_note_commit(void) {}
static void era_nvm_setup(era_nvm_t *n,const era_nvm_flash_t *f,const era_nvm_config_t *c) {(void)n;(void)f;(void)c;}
static unsigned era_nvm_mount(era_nvm_t *n) {(void)n;mounts++;return 0;}
/* STORAGE */

typedef unsigned layer_state_t;
static unsigned debug_config,keymap_config,layer_state;
static bool eeconfig_is_init_required(void) {return !settings_valid;}
static bool eeconfig_is_enabled(void) {return settings_valid;}
static void eeconfig_init(void) {assert(!storage_fails);settings_valid=true;}
static void eeconfig_init_via(void) {eeconfig_init();}
static void eeconfig_read_debug(unsigned *v) {*v=0;}
static void eeconfig_read_keymap(unsigned *v) {*v=0;}
static unsigned eeconfig_read_default_layer(void) {return 0;}
static void default_layer_set(unsigned layer) {(void)layer;}
static void layer_state_set_kb(unsigned layer) {(void)layer;}
static void timer_init(void) {assert(usb_attaches==1);timer_inits++;}
static void sync_timer_init(void) {}
static void via_init(void) {assert(mounts==1);via_inits++;}
static void split_pre_init(void) {cached_left=physical_left;}
static void split_post_init(void) {}
static void era_matrix_engine_init(void) {
    assert(timer_inits==1);era_rp2040_matrix_init_pins();engine_inits++;
}
static void matrix_init_kb(void) {assert(mounts==1);feature_inits++;}
/* MATRIX_INIT */
static void host_init(void) {}
static void led_init_ports(void) {}
static void keyboard_post_init_quantum(void) {post_inits++;}
#ifdef BOOTMAGIC_ENABLE
/* BOOTMAGIC_ENTRY */
#endif
/* KEYBOARD */

#define RP2040_BOOTLOADER_DOUBLE_TAP_RESET
#define RP2040_BOOTLOADER_DOUBLE_TAP_RESET_NONBLOCKING
#define RP2040_BOOTLOADER_DOUBLE_TAP_RESET_LED_MASK 0
static unsigned magic_location=123,double_tap_deadline_from_ms;
static bool double_tap_armed=true,double_tap_deadline_started=true;
static void reset_usb_boot(unsigned led,unsigned disable) {
    (void)led;(void)disable;
    assert(magic_location==0 && !double_tap_armed && !double_tap_deadline_started);
    assert(storage_binds==0 && receipts==0 && usb_attaches==0 && via_inits==0);
    assert(engine_inits==0 && feature_inits==0 && post_inits==0);
    fixture_bootrom();
}
/* BOOTLOADER */
/* MAIN */

static void check_boot(unsigned delivery,bool fail_storage) {
    fresh(delivery);jump_exits=true;storage_fails=fail_storage;settings_valid=!fail_storage;
    platform_ready=storage_binds=receipts=mounts=usb_attaches=0;
    engine_inits=feature_inits=post_inits=via_inits=timer_inits=0;
    magic_location=123;double_tap_armed=double_tap_deadline_started=true;
    int result=setjmp(boot_exit);
    if(!result) firmware_main();
    if(delivery==2) {
        assert(result==1 && jumps==1 && resets==0);
        assert(storage_binds==0 && receipts==0 && mounts==0 && usb_attaches==0);
    } else {
        assert(result==2 && jumps==0 && resets==0);
        assert(storage_binds==1 && mounts==1 && usb_attaches==1);
#ifdef ERA_SETTINGS_RESET_IMAGE
        assert(receipts==1);
#endif
        assert(engine_inits==1 && feature_inits==1 && post_inits==1);
#ifdef VIA_ENABLE
        assert(via_inits==1);
#endif
    }
    assert(allocation==2 && programs==1 && claims==1);
}
