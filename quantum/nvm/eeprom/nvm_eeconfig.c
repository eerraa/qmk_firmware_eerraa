// Copyright 2024 Nick Brassel (@tzarc)
// SPDX-License-Identifier: GPL-2.0-or-later
#include <string.h>
#include "nvm_eeconfig.h"
#include "nvm_eeprom_eeconfig_internal.h"
#include "util.h"
#include "eeconfig.h"
#include "debug.h"
#include "eeprom.h"
#include "keycode_config.h"

#ifdef EEPROM_DRIVER
#    include "eeprom_driver.h"
#endif

#ifdef AUDIO_ENABLE
#    include "audio.h"
#endif

#ifdef BACKLIGHT_ENABLE
#    include "backlight.h"
#endif

#ifdef RGBLIGHT_ENABLE
#    include "rgblight.h"
#endif

#ifdef RGB_MATRIX_ENABLE
#    include "rgb_matrix_types.h"
#endif

#ifdef LED_MATRIX_ENABLE
#    include "led_matrix_types.h"
#endif

#ifdef UNICODE_COMMON_ENABLE
#    include "unicode.h"
#endif

#ifdef HAPTIC_ENABLE
#    include "haptic.h"
#endif

#ifdef CONNECTION_ENABLE
#    include "connection.h"
#endif

__attribute__((weak)) void nvm_eeprom_changed_kb(uint16_t offset, uint16_t length) {
    (void)offset;
    (void)length;
}

__attribute__((weak)) void nvm_eeprom_write_begin_kb(uint16_t offset, uint16_t length) {
    (void)offset;
    (void)length;
}

__attribute__((weak)) void nvm_eeprom_write_end_kb(uint16_t offset, uint16_t length) {
    (void)offset;
    (void)length;
}

__attribute__((weak)) void nvm_eeprom_write_profile_kb(uint16_t offset, uint16_t requested_length, uint16_t compared_length, uint16_t written_length, uint16_t write_calls, uint16_t elapsed_ms, uint8_t flags) {
    (void)offset;
    (void)requested_length;
    (void)compared_length;
    (void)written_length;
    (void)write_calls;
    (void)elapsed_ms;
    (void)flags;
}

static void nvm_eeprom_write_byte_guarded(uint8_t *addr, uint8_t value) {
    nvm_eeprom_write_begin_kb((uint16_t)(uintptr_t)addr, sizeof(value));
    eeprom_write_byte(addr, value);
    nvm_eeprom_write_end_kb((uint16_t)(uintptr_t)addr, sizeof(value));
}

static void nvm_eeprom_write_word_guarded(uint16_t *addr, uint16_t value) {
    nvm_eeprom_write_begin_kb((uint16_t)(uintptr_t)addr, sizeof(value));
    eeprom_write_word(addr, value);
    nvm_eeprom_write_end_kb((uint16_t)(uintptr_t)addr, sizeof(value));
}

static void nvm_eeprom_write_dword_guarded(uint32_t *addr, uint32_t value) {
    nvm_eeprom_write_begin_kb((uint16_t)(uintptr_t)addr, sizeof(value));
    eeprom_write_dword(addr, value);
    nvm_eeprom_write_end_kb((uint16_t)(uintptr_t)addr, sizeof(value));
}

static void nvm_eeprom_write_block_guarded(const void *buf, void *addr, uint16_t length) {
    if (length == 0) {
        return;
    }
    nvm_eeprom_write_begin_kb((uint16_t)(uintptr_t)addr, length);
    eeprom_write_block(buf, addr, length);
    nvm_eeprom_write_end_kb((uint16_t)(uintptr_t)addr, length);
}

bool nvm_eeprom_update_changed_byte(uint8_t *addr, uint8_t value) {
    if (eeprom_read_byte(addr) == value) {
        return false;
    }
    nvm_eeprom_write_byte_guarded(addr, value);
    return true;
}

bool nvm_eeprom_update_changed_word(uint16_t *addr, uint16_t value) {
    if (eeprom_read_word(addr) == value) {
        return false;
    }
    nvm_eeprom_write_word_guarded(addr, value);
    return true;
}

bool nvm_eeprom_update_changed_dword(uint32_t *addr, uint32_t value) {
    if (eeprom_read_dword(addr) == value) {
        return false;
    }
    nvm_eeprom_write_dword_guarded(addr, value);
    return true;
}

uint16_t nvm_eeprom_update_changed_block(const void *buf, void *addr, uint16_t length, uint16_t *changed_offset, uint16_t *changed_length) {
    const uint8_t *source      = (const uint8_t *)buf;
    uint8_t       *target      = (uint8_t *)addr;
    uint16_t       changed_min = length;
    uint16_t       changed_max = 0;
    uint16_t       run_start   = length;
    uint16_t       written_len = 0;

    for (uint16_t i = 0; i < length; i++) {
        if (eeprom_read_byte(&target[i]) != source[i]) {
            if (run_start == length) {
                run_start = i;
            }
            if (i < changed_min) {
                changed_min = i;
            }
            changed_max = i + 1;
        } else if (run_start != length) {
            uint16_t run_len = i - run_start;
            nvm_eeprom_write_block_guarded(&source[run_start], &target[run_start], run_len);
            written_len += run_len;
            run_start = length;
        }
    }

    if (run_start != length) {
        uint16_t run_len = length - run_start;
        nvm_eeprom_write_block_guarded(&source[run_start], &target[run_start], run_len);
        written_len += run_len;
    }

    if (changed_offset != NULL) {
        *changed_offset = changed_min < changed_max ? changed_min : 0;
    }
    if (changed_length != NULL) {
        *changed_length = changed_min < changed_max ? changed_max - changed_min : 0;
    }
    return written_len;
}

#if (EECONFIG_KB_DATA_SIZE) > 0 || (EECONFIG_USER_DATA_SIZE) > 0
static void nvm_eeprom_update_zero_block_changed(uint8_t *addr, uint16_t length, uint16_t eeprom_offset) {
    uint8_t  zeros[16]   = {0};
    uint16_t changed_min = length;
    uint16_t changed_max = 0;
    uint16_t offset      = 0;

    while (offset < length) {
        uint16_t chunk_size     = MIN(length - offset, sizeof(zeros));
        uint16_t changed_offset = 0;
        uint16_t changed_length = 0;
        nvm_eeprom_update_changed_block(zeros, &addr[offset], chunk_size, &changed_offset, &changed_length);
        if (changed_length > 0) {
            uint16_t chunk_min = offset + changed_offset;
            uint16_t chunk_max = chunk_min + changed_length;
            if (chunk_min < changed_min) {
                changed_min = chunk_min;
            }
            if (chunk_max > changed_max) {
                changed_max = chunk_max;
            }
        }
        offset += chunk_size;
    }

    if (changed_min < changed_max) {
        nvm_eeprom_changed_kb(eeprom_offset + changed_min, changed_max - changed_min);
    }
}
#endif

void nvm_eeconfig_erase(void) {
#ifdef EEPROM_DRIVER
    eeprom_driver_format(false);
#endif // EEPROM_DRIVER
}

bool nvm_eeconfig_is_enabled(void) {
    return eeprom_read_word(EECONFIG_MAGIC) == EECONFIG_MAGIC_NUMBER;
}

bool nvm_eeconfig_is_disabled(void) {
    return eeprom_read_word(EECONFIG_MAGIC) == EECONFIG_MAGIC_NUMBER_OFF;
}

void nvm_eeconfig_enable(void) {
    if (nvm_eeprom_update_changed_word(EECONFIG_MAGIC, EECONFIG_MAGIC_NUMBER)) {
        nvm_eeprom_changed_kb((uint16_t)(uintptr_t)EECONFIG_MAGIC, sizeof(uint16_t));
    }
}

void nvm_eeconfig_disable(void) {
#if defined(EEPROM_DRIVER)
    eeprom_driver_format(false);
#endif
    if (nvm_eeprom_update_changed_word(EECONFIG_MAGIC, EECONFIG_MAGIC_NUMBER_OFF)) {
        nvm_eeprom_changed_kb((uint16_t)(uintptr_t)EECONFIG_MAGIC, sizeof(uint16_t));
    }
}

void nvm_eeconfig_read_debug(debug_config_t *debug_config) {
    debug_config->raw = eeprom_read_byte(EECONFIG_DEBUG);
}
void nvm_eeconfig_update_debug(const debug_config_t *debug_config) {
    if (nvm_eeprom_update_changed_byte(EECONFIG_DEBUG, debug_config->raw)) {
        nvm_eeprom_changed_kb((uint16_t)(uintptr_t)EECONFIG_DEBUG, sizeof(uint8_t));
    }
}

layer_state_t nvm_eeconfig_read_default_layer(void) {
    uint8_t val = eeprom_read_byte(EECONFIG_DEFAULT_LAYER);
#ifdef DEFAULT_LAYER_STATE_IS_VALUE_NOT_BITMASK
    // stored as a layer number, so convert back to bitmask
    return (layer_state_t)1 << val;
#else
    // stored as 8-bit-wide bitmask, so read the value directly - handling padding to 16/32 bit layer_state_t
    return (layer_state_t)val;
#endif
}
void nvm_eeconfig_update_default_layer(layer_state_t state) {
#ifdef DEFAULT_LAYER_STATE_IS_VALUE_NOT_BITMASK
    // stored as a layer number, so only store the highest layer
    uint8_t val = get_highest_layer(state);
#else
    // stored as 8-bit-wide bitmask, so write the value directly - handling truncation from 16/32 bit layer_state_t
    uint8_t val = (uint8_t)state;
#endif
    if (nvm_eeprom_update_changed_byte(EECONFIG_DEFAULT_LAYER, val)) {
        nvm_eeprom_changed_kb((uint16_t)(uintptr_t)EECONFIG_DEFAULT_LAYER, sizeof(uint8_t));
    }
}

void nvm_eeconfig_read_keymap(keymap_config_t *keymap_config) {
    keymap_config->raw = eeprom_read_word(EECONFIG_KEYMAP);
}
void nvm_eeconfig_update_keymap(const keymap_config_t *keymap_config) {
    if (nvm_eeprom_update_changed_word(EECONFIG_KEYMAP, keymap_config->raw)) {
        nvm_eeprom_changed_kb((uint16_t)(uintptr_t)EECONFIG_KEYMAP, sizeof(uint16_t));
    }
}

#ifdef AUDIO_ENABLE
void nvm_eeconfig_read_audio(audio_config_t *audio_config) {
    audio_config->raw = eeprom_read_byte(EECONFIG_AUDIO);
}
void nvm_eeconfig_update_audio(const audio_config_t *audio_config) {
    if (nvm_eeprom_update_changed_byte(EECONFIG_AUDIO, audio_config->raw)) {
        nvm_eeprom_changed_kb((uint16_t)(uintptr_t)EECONFIG_AUDIO, sizeof(uint8_t));
    }
}
#endif // AUDIO_ENABLE

#ifdef UNICODE_COMMON_ENABLE
void nvm_eeconfig_read_unicode_mode(unicode_config_t *unicode_config) {
    unicode_config->raw = eeprom_read_byte(EECONFIG_UNICODEMODE);
}
void nvm_eeconfig_update_unicode_mode(const unicode_config_t *unicode_config) {
    if (nvm_eeprom_update_changed_byte(EECONFIG_UNICODEMODE, unicode_config->raw)) {
        nvm_eeprom_changed_kb((uint16_t)(uintptr_t)EECONFIG_UNICODEMODE, sizeof(uint8_t));
    }
}
#endif // UNICODE_COMMON_ENABLE

#ifdef BACKLIGHT_ENABLE
void nvm_eeconfig_read_backlight(backlight_config_t *backlight_config) {
    backlight_config->raw = eeprom_read_byte(EECONFIG_BACKLIGHT);
}
void nvm_eeconfig_update_backlight(const backlight_config_t *backlight_config) {
    if (nvm_eeprom_update_changed_byte(EECONFIG_BACKLIGHT, backlight_config->raw)) {
        nvm_eeprom_changed_kb((uint16_t)(uintptr_t)EECONFIG_BACKLIGHT, sizeof(uint8_t));
    }
}
#endif // BACKLIGHT_ENABLE

#ifdef STENO_ENABLE
uint8_t nvm_eeconfig_read_steno_mode(void) {
    return eeprom_read_byte(EECONFIG_STENOMODE);
}
void nvm_eeconfig_update_steno_mode(uint8_t val) {
    if (nvm_eeprom_update_changed_byte(EECONFIG_STENOMODE, val)) {
        nvm_eeprom_changed_kb((uint16_t)(uintptr_t)EECONFIG_STENOMODE, sizeof(uint8_t));
    }
}
#endif // STENO_ENABLE

#ifdef RGBLIGHT_ENABLE
#endif // RGBLIGHT_ENABLE

#ifdef RGB_MATRIX_ENABLE
void nvm_eeconfig_read_rgb_matrix(rgb_config_t *rgb_matrix_config) {
    eeprom_read_block(rgb_matrix_config, EECONFIG_RGB_MATRIX, sizeof(rgb_config_t));
}
void nvm_eeconfig_update_rgb_matrix(const rgb_config_t *rgb_matrix_config) {
    uint16_t changed_offset = 0;
    uint16_t changed_length = 0;
    nvm_eeprom_update_changed_block(rgb_matrix_config, EECONFIG_RGB_MATRIX, sizeof(rgb_config_t), &changed_offset, &changed_length);
    if (changed_length > 0) {
        nvm_eeprom_changed_kb((uint16_t)(uintptr_t)EECONFIG_RGB_MATRIX + changed_offset, changed_length);
    }
}
#endif // RGB_MATRIX_ENABLE

#ifdef LED_MATRIX_ENABLE
void nvm_eeconfig_read_led_matrix(led_eeconfig_t *led_matrix_config) {
    eeprom_read_block(led_matrix_config, EECONFIG_LED_MATRIX, sizeof(led_eeconfig_t));
}
void nvm_eeconfig_update_led_matrix(const led_eeconfig_t *led_matrix_config) {
    uint16_t changed_offset = 0;
    uint16_t changed_length = 0;
    nvm_eeprom_update_changed_block(led_matrix_config, EECONFIG_LED_MATRIX, sizeof(led_eeconfig_t), &changed_offset, &changed_length);
    if (changed_length > 0) {
        nvm_eeprom_changed_kb((uint16_t)(uintptr_t)EECONFIG_LED_MATRIX + changed_offset, changed_length);
    }
}
#endif // LED_MATRIX_ENABLE

#ifdef RGBLIGHT_ENABLE
void nvm_eeconfig_read_rgblight(rgblight_config_t *rgblight_config) {
    rgblight_config->raw = eeprom_read_dword(EECONFIG_RGBLIGHT);
    rgblight_config->raw |= ((uint64_t)eeprom_read_byte(EECONFIG_RGBLIGHT_EXTENDED) << 32);
}
void nvm_eeconfig_update_rgblight(const rgblight_config_t *rgblight_config) {
    if (nvm_eeprom_update_changed_dword(EECONFIG_RGBLIGHT, rgblight_config->raw & 0xFFFFFFFF)) {
        nvm_eeprom_changed_kb((uint16_t)(uintptr_t)EECONFIG_RGBLIGHT, sizeof(uint32_t));
    }
    if (nvm_eeprom_update_changed_byte(EECONFIG_RGBLIGHT_EXTENDED, (rgblight_config->raw >> 32) & 0xFF)) {
        nvm_eeprom_changed_kb((uint16_t)(uintptr_t)EECONFIG_RGBLIGHT_EXTENDED, sizeof(uint8_t));
    }
}
#endif // RGBLIGHT_ENABLE

#if (EECONFIG_KB_DATA_SIZE) == 0
uint32_t nvm_eeconfig_read_kb(void) {
    return eeprom_read_dword(EECONFIG_KEYBOARD);
}
void nvm_eeconfig_update_kb(uint32_t val) {
    if (nvm_eeprom_update_changed_dword(EECONFIG_KEYBOARD, val)) {
        nvm_eeprom_changed_kb((uint16_t)(uintptr_t)EECONFIG_KEYBOARD, sizeof(uint32_t));
    }
}
#endif // (EECONFIG_KB_DATA_SIZE) == 0

#if (EECONFIG_USER_DATA_SIZE) == 0
uint32_t nvm_eeconfig_read_user(void) {
    return eeprom_read_dword(EECONFIG_USER);
}
void nvm_eeconfig_update_user(uint32_t val) {
    if (nvm_eeprom_update_changed_dword(EECONFIG_USER, val)) {
        nvm_eeprom_changed_kb((uint16_t)(uintptr_t)EECONFIG_USER, sizeof(uint32_t));
    }
}
#endif // (EECONFIG_USER_DATA_SIZE) == 0

#ifdef HAPTIC_ENABLE
void nvm_eeconfig_read_haptic(haptic_config_t *haptic_config) {
    haptic_config->raw = eeprom_read_dword(EECONFIG_HAPTIC);
}
void nvm_eeconfig_update_haptic(const haptic_config_t *haptic_config) {
    if (nvm_eeprom_update_changed_dword(EECONFIG_HAPTIC, haptic_config->raw)) {
        nvm_eeprom_changed_kb((uint16_t)(uintptr_t)EECONFIG_HAPTIC, sizeof(uint32_t));
    }
}
#endif // HAPTIC_ENABLE

#ifdef CONNECTION_ENABLE
void nvm_eeconfig_read_connection(connection_config_t *config) {
    config->raw = eeprom_read_byte(EECONFIG_CONNECTION);
}
void nvm_eeconfig_update_connection(const connection_config_t *config) {
    if (nvm_eeprom_update_changed_byte(EECONFIG_CONNECTION, config->raw)) {
        nvm_eeprom_changed_kb((uint16_t)(uintptr_t)EECONFIG_CONNECTION, sizeof(uint8_t));
    }
}
#endif // CONNECTION_ENABLE

bool nvm_eeconfig_read_handedness(void) {
    return !!eeprom_read_byte(EECONFIG_HANDEDNESS);
}
void nvm_eeconfig_update_handedness(bool val) {
    eeprom_update_byte(EECONFIG_HANDEDNESS, !!val);
}

#if (EECONFIG_KB_DATA_SIZE) > 0

bool nvm_eeconfig_is_kb_datablock_valid(void) {
    return eeprom_read_dword(EECONFIG_KEYBOARD) == (EECONFIG_KB_DATA_VERSION);
}

uint32_t nvm_eeconfig_read_kb_datablock(void *data, uint32_t offset, uint32_t length) {
    if (eeconfig_is_kb_datablock_valid()) {
        void *ee_start = (void *)(uintptr_t)(EECONFIG_KB_DATABLOCK + offset);
        void *ee_end   = (void *)(uintptr_t)(EECONFIG_KB_DATABLOCK + MIN(EECONFIG_KB_DATA_SIZE, offset + length));
        eeprom_read_block(data, ee_start, ee_end - ee_start);
        return ee_end - ee_start;
    } else {
        memset(data, 0, length);
        return length;
    }
}

uint32_t nvm_eeconfig_update_kb_datablock(const void *data, uint32_t offset, uint32_t length) {
    if (nvm_eeprom_update_changed_dword(EECONFIG_KEYBOARD, (EECONFIG_KB_DATA_VERSION))) {
        nvm_eeprom_changed_kb((uint16_t)(uintptr_t)EECONFIG_KEYBOARD, sizeof(uint32_t));
    }

    void *ee_start = (void *)(uintptr_t)(EECONFIG_KB_DATABLOCK + offset);
    void *ee_end   = (void *)(uintptr_t)(EECONFIG_KB_DATABLOCK + MIN(EECONFIG_KB_DATA_SIZE, offset + length));
    uint16_t changed_offset = 0;
    uint16_t changed_length = 0;
    nvm_eeprom_update_changed_block(data, ee_start, (uint16_t)(ee_end - ee_start), &changed_offset, &changed_length);
    if (changed_length > 0) {
        nvm_eeprom_changed_kb((uint16_t)(uintptr_t)(EECONFIG_KB_DATABLOCK + offset) + changed_offset, changed_length);
    }
    return ee_end - ee_start;
}

void nvm_eeconfig_init_kb_datablock(void) {
    if (nvm_eeprom_update_changed_dword(EECONFIG_KEYBOARD, (EECONFIG_KB_DATA_VERSION))) {
        nvm_eeprom_changed_kb((uint16_t)(uintptr_t)EECONFIG_KEYBOARD, sizeof(uint32_t));
    }

    nvm_eeprom_update_zero_block_changed((uint8_t *)(uintptr_t)EECONFIG_KB_DATABLOCK, EECONFIG_KB_DATA_SIZE, (uint16_t)(uintptr_t)EECONFIG_KB_DATABLOCK);
}

#endif // (EECONFIG_KB_DATA_SIZE) > 0

#if (EECONFIG_USER_DATA_SIZE) > 0

bool nvm_eeconfig_is_user_datablock_valid(void) {
    return eeprom_read_dword(EECONFIG_USER) == (EECONFIG_USER_DATA_VERSION);
}

uint32_t nvm_eeconfig_read_user_datablock(void *data, uint32_t offset, uint32_t length) {
    if (eeconfig_is_user_datablock_valid()) {
        void *ee_start = (void *)(uintptr_t)(EECONFIG_USER_DATABLOCK + offset);
        void *ee_end   = (void *)(uintptr_t)(EECONFIG_USER_DATABLOCK + MIN(EECONFIG_USER_DATA_SIZE, offset + length));
        eeprom_read_block(data, ee_start, ee_end - ee_start);
        return ee_end - ee_start;
    } else {
        memset(data, 0, length);
        return length;
    }
}

uint32_t nvm_eeconfig_update_user_datablock(const void *data, uint32_t offset, uint32_t length) {
    if (nvm_eeprom_update_changed_dword(EECONFIG_USER, (EECONFIG_USER_DATA_VERSION))) {
        nvm_eeprom_changed_kb((uint16_t)(uintptr_t)EECONFIG_USER, sizeof(uint32_t));
    }

    void *ee_start = (void *)(uintptr_t)(EECONFIG_USER_DATABLOCK + offset);
    void *ee_end   = (void *)(uintptr_t)(EECONFIG_USER_DATABLOCK + MIN(EECONFIG_USER_DATA_SIZE, offset + length));
    uint16_t changed_offset = 0;
    uint16_t changed_length = 0;
    nvm_eeprom_update_changed_block(data, ee_start, (uint16_t)(ee_end - ee_start), &changed_offset, &changed_length);
    if (changed_length > 0) {
        nvm_eeprom_changed_kb((uint16_t)(uintptr_t)(EECONFIG_USER_DATABLOCK + offset) + changed_offset, changed_length);
    }
    return ee_end - ee_start;
}

void nvm_eeconfig_init_user_datablock(void) {
    if (nvm_eeprom_update_changed_dword(EECONFIG_USER, (EECONFIG_USER_DATA_VERSION))) {
        nvm_eeprom_changed_kb((uint16_t)(uintptr_t)EECONFIG_USER, sizeof(uint32_t));
    }

    nvm_eeprom_update_zero_block_changed((uint8_t *)(uintptr_t)EECONFIG_USER_DATABLOCK, EECONFIG_USER_DATA_SIZE, (uint16_t)(uintptr_t)EECONFIG_USER_DATABLOCK);
}

#endif // (EECONFIG_USER_DATA_SIZE) > 0
