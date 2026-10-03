#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "debug.h"
#include "eeconfig.h"
#include "action_layer.h"
#include "nvm_eeconfig.h"
#include "keycode_config.h"
#include "eeprom.h"

__attribute__((weak)) void eeprom_transaction_complete(bool success) { (void)success; }
#if defined(__AVR__) && !defined(EEPROM_DRIVER)
static uint16_t eeprom_native_depth;
static bool eeprom_native_failed, eeprom_native_ok = true;
static bool eeprom_native_commit_rejected;
void eeprom_native_note_result(bool success) {
    eeprom_native_ok = success;
    if (!success) eeprom_native_commit_rejected = true;
    if (eeprom_native_depth && !success) eeprom_native_failed = true;
}
bool eeprom_commit_failed(void) { return eeprom_native_commit_rejected; }
bool eeprom_prepare_commit(uint8_t *address, uint8_t invalid) {
    eeprom_native_commit_rejected = false;
    eeprom_update_byte(address, invalid);
    return eeprom_persistence_ok();
}
bool eeprom_commit_byte(uint8_t *address, uint8_t value) {
    if (eeprom_native_commit_rejected || !eeprom_persistence_ok()) { eeprom_native_note_result(false); return false; }
    eeprom_update_byte(address, value);
    return eeprom_persistence_ok();
}
bool eeprom_transaction_active(void) { return eeprom_native_depth != 0U; }
bool eeprom_persistence_ok(void) { return eeprom_native_ok && (!eeprom_native_depth || !eeprom_native_failed); }
bool eeprom_begin_transaction(void) {
    if (eeprom_native_depth == UINT16_MAX) return false;
    if (eeprom_native_depth++ == 0U) eeprom_native_failed = false;
    return true;
}
bool eeprom_end_transaction(void) {
    if (!eeprom_native_depth) return false;
    bool ok = eeprom_persistence_ok();
    if (--eeprom_native_depth == 0U) { eeprom_native_note_result(ok); eeprom_transaction_complete(ok); }
    return ok;
}
#else
__attribute__((weak)) bool eeprom_transaction_active(void) { return false; }
__attribute__((weak)) bool eeprom_begin_transaction(void) { return true; }
__attribute__((weak)) bool eeprom_end_transaction(void) {
    eeprom_transaction_complete(true);
    return true;
}
__attribute__((weak)) bool eeprom_persistence_ok(void) { return true; }
#endif
static bool eeconfig_init_failed;

#ifdef BACKLIGHT_ENABLE
#    include "backlight.h"
#endif // BACKLIGHT_ENABLE

#ifdef AUDIO_ENABLE
#    include "audio.h"
#endif // AUDIO_ENABLE

#ifdef RGBLIGHT_ENABLE
#    include "rgblight.h"
#endif // RGBLIGHT_ENABLE

#ifdef RGB_MATRIX_ENABLE
#    include "rgb_matrix_types.h"
#endif // RGB_MATRIX_ENABLE

#ifdef LED_MATRIX_ENABLE
#    include "led_matrix_types.h"
#endif // LED_MATRIX_ENABLE

#ifdef UNICODE_COMMON_ENABLE
#    include "unicode.h"
#endif // UNICODE_COMMON_ENABLE

#ifdef HAPTIC_ENABLE
#    include "haptic.h"
#endif // HAPTIC_ENABLE

#ifdef CONNECTION_ENABLE
#    include "connection.h"
#endif // CONNECTION_ENABLE

#ifdef VIA_ENABLE
bool via_eeprom_is_valid(void);
void via_eeprom_set_valid(bool valid);
void eeconfig_init_via(void);
#else
void dynamic_keymap_reset(void);
#endif // VIA_ENABLE

#ifndef NKRO_DEFAULT_ON
#    define NKRO_DEFAULT_ON false
#endif

__attribute__((weak)) void eeconfig_init_user(void) {
#if (EECONFIG_USER_DATA_SIZE) == 0
    // Reset user EEPROM value to blank, rather than to a set value
    eeconfig_update_user(0);
#endif // (EECONFIG_USER_DATA_SIZE) == 0
}

__attribute__((weak)) void eeconfig_init_kb(void) {
#if (EECONFIG_KB_DATA_SIZE) == 0
    // Reset Keyboard EEPROM value to blank, rather than to a set value
    eeconfig_update_kb(0);
#endif // (EECONFIG_KB_DATA_SIZE) == 0

    eeconfig_init_user();
}

void eeconfig_init_quantum(void) {
    eeconfig_init_failed = true;
    if (!eeprom_begin_transaction()) return;
    nvm_eeconfig_disable();
    if (!eeprom_persistence_ok()) { eeprom_end_transaction(); return; }
#if defined(__AVR__) && !defined(EEPROM_DRIVER)
    eeprom_busy_wait();
#endif

    debug_config_t debug_config = {0};
    eeconfig_update_debug(&debug_config);

    default_layer_state = (layer_state_t)1 << 0;
    eeconfig_update_default_layer(default_layer_state);

    keymap_config_t keymap_config = {
        .swap_control_capslock    = false,
        .capslock_to_control      = false,
        .swap_lalt_lgui           = false,
        .swap_ralt_rgui           = false,
        .no_gui                   = false,
        .swap_grave_esc           = false,
        .swap_backslash_backspace = false,
        .nkro                     = NKRO_DEFAULT_ON,
        .swap_lctl_lgui           = false,
        .swap_rctl_rgui           = false,
        .oneshot_enable           = true, // Enable oneshot by default
        .swap_escape_capslock     = false,
        .autocorrect_enable       = true, // Enable autocorrect by default
    };
    eeconfig_update_keymap(&keymap_config);

#ifdef BACKLIGHT_ENABLE
    backlight_config_t backlight_config = {0};
    eeconfig_update_backlight(&backlight_config);
#endif // BACKLIGHT_ENABLE

#ifdef AUDIO_ENABLE
    audio_config_t audio_config = {0};
    eeconfig_update_audio(&audio_config);
#endif // AUDIO_ENABLE

#ifdef RGBLIGHT_ENABLE
    extern void eeconfig_update_rgblight_default(void);
    eeconfig_update_rgblight_default();
#endif // RGBLIGHT_ENABLE

#ifdef UNICODE_COMMON_ENABLE
    unicode_config_t unicode_config = {0};
    eeconfig_update_unicode_mode(&unicode_config);
#endif // UNICODE_COMMON_ENABLE

#ifdef STENO_ENABLE
    eeconfig_update_steno_mode(0);
#endif // STENO_ENABLE

#ifdef RGB_MATRIX_ENABLE
    extern void eeconfig_update_rgb_matrix_default(void);
    eeconfig_update_rgb_matrix_default();
#endif

#ifdef LED_MATRIX_ENABLE
    extern void eeconfig_update_led_matrix_default(void);
    eeconfig_update_led_matrix_default();
#endif // LED_MATRIX_ENABLE

#ifdef HAPTIC_ENABLE
    haptic_config_t haptic_config = {0};
    eeconfig_update_haptic(&haptic_config);
    haptic_reset();
#endif // HAPTIC_ENABLE

#ifdef CONNECTION_ENABLE
    extern void eeconfig_update_connection_default(void);
    eeconfig_update_connection_default();
#endif // CONNECTION_ENABLE

#if (EECONFIG_KB_DATA_SIZE) > 0
    eeconfig_init_kb_datablock();
#endif // (EECONFIG_KB_DATA_SIZE) > 0

#if (EECONFIG_USER_DATA_SIZE) > 0
    eeconfig_init_user_datablock();
#endif // (EECONFIG_USER_DATA_SIZE) > 0

#if defined(VIA_ENABLE)
    // Invalidate VIA eeprom config, and then reset.
    // Just in case if power is lost mid init, this makes sure that it gets
    // properly re-initialized.
    eeconfig_init_via();
#elif defined(DYNAMIC_KEYMAP_ENABLE)
    dynamic_keymap_reset();
#endif

    eeconfig_init_kb();

#ifdef RGB_MATRIX_ENABLE
    extern void eeconfig_force_flush_rgb_matrix(void);
    eeconfig_force_flush_rgb_matrix();
#endif // RGB_MATRIX_ENABLE
#ifdef LED_MATRIX_ENABLE
    extern void eeconfig_force_flush_led_matrix(void);
    eeconfig_force_flush_led_matrix();
#endif // LED_MATRIX_ENABLE
    /* The validity word follows every default owner, including deferred RGB. */
    if (eeprom_persistence_ok()) eeconfig_enable();
    eeconfig_init_failed = !eeprom_end_transaction();
}

void eeconfig_init(void) {
    eeconfig_init_quantum();
}

void eeconfig_enable(void) {
    nvm_eeconfig_enable();
}

void eeconfig_disable(void) {
    nvm_eeconfig_disable();
}

bool eeconfig_is_enabled(void) {
    bool is_eeprom_enabled = !eeconfig_init_failed && nvm_eeconfig_is_enabled();
#ifdef VIA_ENABLE
    if (is_eeprom_enabled) {
        is_eeprom_enabled = via_eeprom_is_valid();
    }
#endif // VIA_ENABLE
    return is_eeprom_enabled;
}

bool eeconfig_is_disabled(void) {
    bool is_eeprom_disabled = nvm_eeconfig_is_disabled();
#ifdef VIA_ENABLE
    if (!is_eeprom_disabled) {
        is_eeprom_disabled = !via_eeprom_is_valid();
    }
#endif // VIA_ENABLE
    return is_eeprom_disabled;
}

void eeconfig_read_debug(debug_config_t *debug_config) {
    nvm_eeconfig_read_debug(debug_config);
}
void eeconfig_update_debug(const debug_config_t *debug_config) {
    nvm_eeconfig_update_debug(debug_config);
}

layer_state_t eeconfig_read_default_layer(void) {
    return nvm_eeconfig_read_default_layer();
}
void eeconfig_update_default_layer(layer_state_t state) {
    nvm_eeconfig_update_default_layer(state);
}

void eeconfig_read_keymap(keymap_config_t *keymap_config) {
    nvm_eeconfig_read_keymap(keymap_config);
}
void eeconfig_update_keymap(const keymap_config_t *keymap_config) {
    nvm_eeconfig_update_keymap(keymap_config);
}

#ifdef AUDIO_ENABLE
void eeconfig_read_audio(audio_config_t *audio_config) {
    nvm_eeconfig_read_audio(audio_config);
}
void eeconfig_update_audio(const audio_config_t *audio_config) {
    nvm_eeconfig_update_audio(audio_config);
}
#endif // AUDIO_ENABLE

#ifdef UNICODE_COMMON_ENABLE
void eeconfig_read_unicode_mode(unicode_config_t *unicode_config) {
    return nvm_eeconfig_read_unicode_mode(unicode_config);
}
void eeconfig_update_unicode_mode(const unicode_config_t *unicode_config) {
    nvm_eeconfig_update_unicode_mode(unicode_config);
}
#endif // UNICODE_COMMON_ENABLE

#ifdef BACKLIGHT_ENABLE
void eeconfig_read_backlight(backlight_config_t *backlight_config) {
    nvm_eeconfig_read_backlight(backlight_config);
}
void eeconfig_update_backlight(const backlight_config_t *backlight_config) {
    nvm_eeconfig_update_backlight(backlight_config);
}
#endif // BACKLIGHT_ENABLE

#ifdef STENO_ENABLE
uint8_t eeconfig_read_steno_mode(void) {
    return nvm_eeconfig_read_steno_mode();
}
void eeconfig_update_steno_mode(uint8_t val) {
    nvm_eeconfig_update_steno_mode(val);
}
#endif // STENO_ENABLE

#ifdef RGB_MATRIX_ENABLE
void eeconfig_read_rgb_matrix(rgb_config_t *rgb_matrix_config) {
    nvm_eeconfig_read_rgb_matrix(rgb_matrix_config);
}
void eeconfig_update_rgb_matrix(const rgb_config_t *rgb_matrix_config) {
    nvm_eeconfig_update_rgb_matrix(rgb_matrix_config);
}
#endif // RGB_MATRIX_ENABLE

#ifdef LED_MATRIX_ENABLE
void eeconfig_read_led_matrix(led_eeconfig_t *led_matrix_config) {
    nvm_eeconfig_read_led_matrix(led_matrix_config);
}
void eeconfig_update_led_matrix(const led_eeconfig_t *led_matrix_config) {
    nvm_eeconfig_update_led_matrix(led_matrix_config);
}
#endif // LED_MATRIX_ENABLE

#ifdef RGBLIGHT_ENABLE
void eeconfig_read_rgblight(rgblight_config_t *rgblight_config) {
    nvm_eeconfig_read_rgblight(rgblight_config);
}
void eeconfig_update_rgblight(const rgblight_config_t *rgblight_config) {
    nvm_eeconfig_update_rgblight(rgblight_config);
}
#endif // RGBLIGHT_ENABLE

#if (EECONFIG_KB_DATA_SIZE) == 0
uint32_t eeconfig_read_kb(void) {
    return nvm_eeconfig_read_kb();
}
void eeconfig_update_kb(uint32_t val) {
    nvm_eeconfig_update_kb(val);
}
#endif // (EECONFIG_KB_DATA_SIZE) == 0

#if (EECONFIG_USER_DATA_SIZE) == 0
uint32_t eeconfig_read_user(void) {
    return nvm_eeconfig_read_user();
}
void eeconfig_update_user(uint32_t val) {
    nvm_eeconfig_update_user(val);
}
#endif // (EECONFIG_USER_DATA_SIZE) == 0

#ifdef HAPTIC_ENABLE
void eeconfig_read_haptic(haptic_config_t *haptic_config) {
    nvm_eeconfig_read_haptic(haptic_config);
}
void eeconfig_update_haptic(const haptic_config_t *haptic_config) {
    nvm_eeconfig_update_haptic(haptic_config);
}
#endif // HAPTIC_ENABLE

#ifdef CONNECTION_ENABLE
void eeconfig_read_connection(connection_config_t *config) {
    nvm_eeconfig_read_connection(config);
}
void eeconfig_update_connection(const connection_config_t *config) {
    nvm_eeconfig_update_connection(config);
}
#endif // CONNECTION_ENABLE

bool eeconfig_read_handedness(void) {
    return nvm_eeconfig_read_handedness();
}
void eeconfig_update_handedness(bool val) {
    nvm_eeconfig_update_handedness(val);
}

#if (EECONFIG_KB_DATA_SIZE) > 0
bool eeconfig_is_kb_datablock_valid(void) {
    return nvm_eeconfig_is_kb_datablock_valid();
}
uint32_t eeconfig_read_kb_datablock(void *data, uint32_t offset, uint32_t length) {
    return nvm_eeconfig_read_kb_datablock(data, offset, length);
}
uint32_t eeconfig_update_kb_datablock(const void *data, uint32_t offset, uint32_t length) {
    return nvm_eeconfig_update_kb_datablock(data, offset, length);
}
__attribute__((weak)) void eeconfig_init_kb_datablock(void) {
    nvm_eeconfig_init_kb_datablock();
}
#endif // (EECONFIG_KB_DATA_SIZE) > 0

#if (EECONFIG_USER_DATA_SIZE) > 0
bool eeconfig_is_user_datablock_valid(void) {
    return nvm_eeconfig_is_user_datablock_valid();
}
uint32_t eeconfig_read_user_datablock(void *data, uint32_t offset, uint32_t length) {
    return nvm_eeconfig_read_user_datablock(data, offset, length);
}
uint32_t eeconfig_update_user_datablock(const void *data, uint32_t offset, uint32_t length) {
    return nvm_eeconfig_update_user_datablock(data, offset, length);
}
__attribute__((weak)) void eeconfig_init_user_datablock(void) {
    nvm_eeconfig_init_user_datablock();
}
#endif // (EECONFIG_USER_DATA_SIZE) > 0
