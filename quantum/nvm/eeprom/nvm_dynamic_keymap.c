// Copyright 2024 Nick Brassel (@tzarc)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <string.h>
#include "compiler_support.h"
#include "keycodes.h"
#include "eeprom.h"
#include "dynamic_keymap.h"
#include "nvm_dynamic_keymap.h"
#include "nvm_eeprom_eeconfig_internal.h"
#include "nvm_eeprom_via_internal.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef ENCODER_ENABLE
#    include "encoder.h"
#endif

#ifdef VIA_ENABLE
#    include "via.h"
#    define DYNAMIC_KEYMAP_EEPROM_START (VIA_EEPROM_CONFIG_END)
#else
#    define DYNAMIC_KEYMAP_EEPROM_START (EECONFIG_SIZE)
#endif

#ifndef DYNAMIC_KEYMAP_EEPROM_MAX_ADDR
#    define DYNAMIC_KEYMAP_EEPROM_MAX_ADDR (TOTAL_EEPROM_BYTE_COUNT - 1)
#endif

STATIC_ASSERT(DYNAMIC_KEYMAP_EEPROM_MAX_ADDR <= (TOTAL_EEPROM_BYTE_COUNT - 1), "DYNAMIC_KEYMAP_EEPROM_MAX_ADDR is configured to use more space than what is available for the selected EEPROM driver");

// Due to usage of uint16_t check for max 65535
STATIC_ASSERT(DYNAMIC_KEYMAP_EEPROM_MAX_ADDR <= 65535, "DYNAMIC_KEYMAP_EEPROM_MAX_ADDR must be less than 65536");

// If DYNAMIC_KEYMAP_EEPROM_ADDR not explicitly defined in config.h,
#ifndef DYNAMIC_KEYMAP_EEPROM_ADDR
#    define DYNAMIC_KEYMAP_EEPROM_ADDR DYNAMIC_KEYMAP_EEPROM_START
#endif

// Dynamic encoders starts after dynamic keymaps
#ifndef DYNAMIC_KEYMAP_ENCODER_EEPROM_ADDR
#    define DYNAMIC_KEYMAP_ENCODER_EEPROM_ADDR (DYNAMIC_KEYMAP_EEPROM_ADDR + (DYNAMIC_KEYMAP_LAYER_COUNT * MATRIX_ROWS * MATRIX_COLS * 2))
#endif

// Dynamic macro starts after dynamic encoders, but only when using ENCODER_MAP
#ifdef ENCODER_MAP_ENABLE
#    ifndef DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR
#        define DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR (DYNAMIC_KEYMAP_ENCODER_EEPROM_ADDR + (DYNAMIC_KEYMAP_LAYER_COUNT * NUM_ENCODERS * 2 * 2))
#    endif // DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR
#else      // ENCODER_MAP_ENABLE
#    ifndef DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR
#        define DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR (DYNAMIC_KEYMAP_ENCODER_EEPROM_ADDR)
#    endif // DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR
#endif     // ENCODER_MAP_ENABLE

// Sanity check that dynamic keymaps fit in available EEPROM
// If there's not 100 bytes available for macros, then something is wrong.
// The keyboard should override DYNAMIC_KEYMAP_LAYER_COUNT to reduce it,
// or DYNAMIC_KEYMAP_EEPROM_MAX_ADDR to increase it, *only if* the microcontroller has
// more than the default.
STATIC_ASSERT((int64_t)(DYNAMIC_KEYMAP_EEPROM_MAX_ADDR) - (int64_t)(DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR) >= 100, "Dynamic keymaps are configured to use more EEPROM than is available.");

#ifndef TOTAL_EEPROM_BYTE_COUNT
#    error Unknown total EEPROM size. Cannot derive maximum for dynamic keymaps.
#endif
// Dynamic macros are stored after the keymaps and use what is available
// up to and including DYNAMIC_KEYMAP_EEPROM_MAX_ADDR.
#ifndef DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE
#    define DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE (DYNAMIC_KEYMAP_EEPROM_MAX_ADDR - DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR + 1)
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void nvm_dynamic_keymap_erase(void) {
    // No-op, nvm_eeconfig_erase() will have already erased EEPROM if necessary.
}

#ifndef EEPROM_CUSTOM
/* A cold marker is storage state, not proof of a request in this RAM session.
 * An opener needs received payload or a successful RESET's empty proof before
 * completion. WL retains failed desired bytes in this session; native EEPROM
 * must receive a fresh upload. */
static bool dynamic_keymap_macro_upload_open;
static bool dynamic_keymap_macro_payload_seen;
static bool dynamic_keymap_macro_empty_reset;
#    if defined(__AVR__) && !defined(EEPROM_DRIVER)
static bool dynamic_keymap_macro_upload_failed;
#    endif
#endif

void nvm_dynamic_keymap_macro_reset_session(void) {
#ifndef EEPROM_CUSTOM
    dynamic_keymap_macro_upload_open = false;
    dynamic_keymap_macro_payload_seen = false;
    dynamic_keymap_macro_empty_reset = false;
#    if defined(__AVR__) && !defined(EEPROM_DRIVER)
    dynamic_keymap_macro_upload_failed = false;
#    endif
#endif
}

void nvm_dynamic_keymap_macro_erase(void) {
    // nvm_eeconfig_erase() will have already erased EEPROM if necessary.
    nvm_dynamic_keymap_macro_reset_session();
}

static inline void *dynamic_keymap_key_to_eeprom_address(uint8_t layer, uint8_t row, uint8_t column) {
    return ((void *)DYNAMIC_KEYMAP_EEPROM_ADDR) + (layer * MATRIX_ROWS * MATRIX_COLS * 2) + (row * MATRIX_COLS * 2) + (column * 2);
}

uint16_t nvm_dynamic_keymap_read_keycode(uint8_t layer, uint8_t row, uint8_t column) {
    if (layer >= DYNAMIC_KEYMAP_LAYER_COUNT || row >= MATRIX_ROWS || column >= MATRIX_COLS) return KC_NO;
    void *address = dynamic_keymap_key_to_eeprom_address(layer, row, column);
    // Big endian, so we can read/write EEPROM directly from host if we want
    uint16_t keycode = eeprom_read_byte(address) << 8;
    keycode |= eeprom_read_byte(address + 1);
    return keycode;
}

void nvm_dynamic_keymap_update_keycode(uint8_t layer, uint8_t row, uint8_t column, uint16_t keycode) {
    if (layer >= DYNAMIC_KEYMAP_LAYER_COUNT || row >= MATRIX_ROWS || column >= MATRIX_COLS) return;
    void *address = dynamic_keymap_key_to_eeprom_address(layer, row, column);
    // Big endian, so we can read/write EEPROM directly from host if we want
    uint8_t bytes[2] = {(uint8_t)(keycode >> 8), (uint8_t)keycode};
    eeprom_update_block(bytes, address, sizeof(bytes));
}

#ifdef ENCODER_MAP_ENABLE
static void *dynamic_keymap_encoder_to_eeprom_address(uint8_t layer, uint8_t encoder_id) {
    return ((void *)DYNAMIC_KEYMAP_ENCODER_EEPROM_ADDR) + (layer * NUM_ENCODERS * 2 * 2) + (encoder_id * 2 * 2);
}

uint16_t nvm_dynamic_keymap_read_encoder(uint8_t layer, uint8_t encoder_id, bool clockwise) {
    if (layer >= DYNAMIC_KEYMAP_LAYER_COUNT || encoder_id >= NUM_ENCODERS) return KC_NO;
    void *address = dynamic_keymap_encoder_to_eeprom_address(layer, encoder_id);
    // Big endian, so we can read/write EEPROM directly from host if we want
    uint16_t keycode = ((uint16_t)eeprom_read_byte(address + (clockwise ? 0 : 2))) << 8;
    keycode |= eeprom_read_byte(address + (clockwise ? 0 : 2) + 1);
    return keycode;
}

void nvm_dynamic_keymap_update_encoder(uint8_t layer, uint8_t encoder_id, bool clockwise, uint16_t keycode) {
    if (layer >= DYNAMIC_KEYMAP_LAYER_COUNT || encoder_id >= NUM_ENCODERS) return;
    void *address = dynamic_keymap_encoder_to_eeprom_address(layer, encoder_id);
    // Big endian, so we can read/write EEPROM directly from host if we want
    uint8_t bytes[2] = {(uint8_t)(keycode >> 8), (uint8_t)keycode};
    eeprom_update_block(bytes, address + (clockwise ? 0 : 2), sizeof(bytes));
}
#endif // ENCODER_MAP_ENABLE

void nvm_dynamic_keymap_read_buffer(uint32_t offset, uint32_t size, uint8_t *data) {
    uint32_t dynamic_keymap_eeprom_size = DYNAMIC_KEYMAP_LAYER_COUNT * MATRIX_ROWS * MATRIX_COLS * 2;
    void    *source                     = (void *)(uintptr_t)(DYNAMIC_KEYMAP_EEPROM_ADDR + offset);
    uint8_t *target                     = data;
    for (uint32_t i = 0; i < size; i++) {
        if (offset + i < dynamic_keymap_eeprom_size) {
            *target = eeprom_read_byte(source);
        } else {
            *target = 0x00;
        }
        source++;
        target++;
    }
}

void nvm_dynamic_keymap_update_buffer(uint32_t offset, uint32_t size, uint8_t *data) {
    uint32_t dynamic_keymap_eeprom_size = DYNAMIC_KEYMAP_LAYER_COUNT * MATRIX_ROWS * MATRIX_COLS * 2;
    if (offset >= dynamic_keymap_eeprom_size) return;
    if (size > dynamic_keymap_eeprom_size - offset) size = dynamic_keymap_eeprom_size - offset;
    eeprom_update_block(data, (void *)(uintptr_t)(DYNAMIC_KEYMAP_EEPROM_ADDR + offset), size);
}

uint32_t nvm_dynamic_keymap_macro_size(void) {
    return DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE;
}

void nvm_dynamic_keymap_macro_read_buffer(uint32_t offset, uint32_t size, uint8_t *data) {
    if (data == NULL || size == 0U) return;
    uint32_t readable = offset < DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE ? DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE - offset : 0U;
    if (readable > size) readable = size;
    /* Preserve the public byte-read boundary: its first read aborts a private
     * RESET scan before the remaining image is copied. The remainder starts
     * past the macro base, so it cannot begin that scan itself. */
    if (readable) {
        const uint8_t *source = (const uint8_t *)(uintptr_t)(DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR + offset);
        data[0] = eeprom_read_byte(source);
        if (readable > 1U) eeprom_read_block(data + 1, source + 1, readable - 1U);
    }
    if (readable < size) memset(data + readable, 0, size - readable);
#ifndef EEPROM_CUSTOM
    const uint32_t marker = DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE - 1U;
    if (offset <= marker && readable > marker - offset && !eeprom_persistence_ok()) data[marker - offset] = 0xFFU;
#endif
}

void nvm_dynamic_keymap_macro_update_buffer(uint32_t offset, uint32_t size, uint8_t *data) {
#ifndef EEPROM_CUSTOM
    const uint32_t marker_offset = DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE - 1U;
    if (size == 0U) return;
    if (offset > marker_offset || data == NULL) { eeprom_reject_write(); return; }
    if (size > DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE - offset) size = DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE - offset;
    bool has_marker = offset + size > marker_offset;
    bool opener = has_marker && data[marker_offset - offset] != 0U;
    bool payload = offset < marker_offset;
    uint8_t *marker = (uint8_t *)(uintptr_t)(DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR + marker_offset);
    bool empty_reset_upload = opener && offset == marker_offset && size == 1U && dynamic_keymap_macro_empty_reset;
    dynamic_keymap_macro_empty_reset = false;
    if (!opener && !dynamic_keymap_macro_upload_open) { eeprom_reject_write(); return; }
    if (has_marker && !opener && !payload && !dynamic_keymap_macro_payload_seen) { eeprom_reject_write(); return; }
    /* A retained CLOSE may have been repaired by another owner's write. The
     * marker now seals that image even while the RAM upload proof remains. */
    if (!opener && payload && eeprom_read_byte(marker) == 0U) { eeprom_reject_write(); return; }
#    if defined(__AVR__) && !defined(EEPROM_DRIVER)
    if (!opener && dynamic_keymap_macro_upload_failed) { eeprom_reject_write(); return; }
#    endif
    if (!eeprom_begin_transaction()) { eeprom_reject_write(); return; }
    if (opener) {
        dynamic_keymap_macro_upload_open = false;
        dynamic_keymap_macro_payload_seen = empty_reset_upload;
    }
#    if defined(__AVR__) && !defined(EEPROM_DRIVER)
    if (opener) {
        dynamic_keymap_macro_upload_failed = false;
        if (!eeprom_prepare_commit(marker, 0xFFU)) {
            dynamic_keymap_macro_upload_failed = true;
            eeprom_end_transaction();
            return;
        }
    } else {
        if (eeprom_commit_failed()) {
            dynamic_keymap_macro_upload_failed = true;
            eeprom_reject_write();
            eeprom_end_transaction();
            return;
        }
    }
#    else
    /* A packet may include payload and its nonzero opener. Retire execution
     * first; on failure the adapter stages payload without programming it. */
    if (opener) eeprom_update_byte(marker, 0xFFU);
#    endif
    if (opener) dynamic_keymap_macro_upload_open = true;
#endif
    void    *target = (void *)(uintptr_t)(DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR + offset);
    uint8_t *source = data;
    for (uint16_t i = 0; i < size; i++) {
        if (offset + i < DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE) {
#if defined(__AVR__) && !defined(EEPROM_DRIVER)
            if (offset + i == marker_offset) {
                if (*source == 0U) eeprom_commit_byte(target, 0U);
            } else
#endif
            eeprom_update_byte(target, *source);
        }
        source++;
        target++;
    }
#ifndef EEPROM_CUSTOM
    if (payload) dynamic_keymap_macro_payload_seen = true;
    bool persisted = eeprom_end_transaction();
#    if defined(__AVR__) && !defined(EEPROM_DRIVER)
    if (!persisted) dynamic_keymap_macro_upload_failed = true;
#    endif
    if (has_marker && !opener && persisted) {
        dynamic_keymap_macro_upload_open = false;
        dynamic_keymap_macro_payload_seen = false;
    }
#endif
}

void nvm_dynamic_keymap_macro_reset(void) {
#ifndef EEPROM_CUSTOM
    dynamic_keymap_macro_upload_open = false;
    dynamic_keymap_macro_payload_seen = false;
    dynamic_keymap_macro_empty_reset = false;
    bool changed = false;
    for (uint32_t i = 0; i < DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE; ++i) {
        if (eeprom_read_byte((const uint8_t *)(uintptr_t)(DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR + i)) != 0U) { changed = true; break; }
    }
#if defined(__AVR__) && !defined(EEPROM_DRIVER)
    if (!changed && eeprom_persistence_ok() && !eeprom_commit_failed()) {
        dynamic_keymap_macro_upload_failed = false;
        dynamic_keymap_macro_empty_reset = true;
        return;
    }
#else
    if (!changed && eeprom_persistence_ok()) { dynamic_keymap_macro_empty_reset = true; return; }
#endif
    if (!eeprom_begin_transaction()) { eeprom_reject_write(); return; }
    /* Native/WL stores retire execution before changing the payload. */
    uint8_t *marker = (uint8_t *)(uintptr_t)(DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR + DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE - 1U);
#if defined(__AVR__) && !defined(EEPROM_DRIVER)
    dynamic_keymap_macro_upload_failed = false;
    if (!eeprom_prepare_commit(marker, 0xFFU)) { dynamic_keymap_macro_upload_failed = true; eeprom_end_transaction(); return; }
#else
    eeprom_write_byte(marker, 0xFFU);
#endif
#endif
    void   *start     = (void *)(uintptr_t)(DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR);
    void   *end       = (void *)(uintptr_t)(DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR + DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE);
    size_t  remaining = end - start;
    uint8_t dummy[16] = {0};
    for (size_t i = 0; i < DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE; i += sizeof(dummy)) {
        size_t this_loop = remaining < sizeof(dummy) ? remaining : sizeof(dummy);
#if defined(__AVR__) && !defined(EEPROM_DRIVER)
        if (start + this_loop == end) --this_loop;
#endif
        eeprom_update_block(dummy, start, this_loop);
        start += this_loop;
        remaining -= this_loop;
    }
#if defined(__AVR__) && !defined(EEPROM_DRIVER)
    eeprom_commit_byte(marker, 0U);
#endif
#ifndef EEPROM_CUSTOM
    bool persisted = eeprom_end_transaction();
    dynamic_keymap_macro_empty_reset = persisted;
#    if defined(__AVR__) && !defined(EEPROM_DRIVER)
    dynamic_keymap_macro_upload_failed = !persisted;
#    else
    /* A failed reset can repair its retained image on a fresh RESET. */
    (void)persisted;
#    endif
#endif
}
