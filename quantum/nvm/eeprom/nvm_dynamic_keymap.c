// Copyright 2024 Nick Brassel (@tzarc)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "compiler_support.h"
#include "keycodes.h"
#include "eeprom.h"
#include "dynamic_keymap.h"
#include "nvm_dynamic_keymap.h"
#include "nvm_eeprom_eeconfig_internal.h"
#include "nvm_eeprom_via_internal.h"
#include "timer.h"

#if defined(ERA_DYNAMIC_MACRO_TRANSACTION_ENABLE) && (defined(EEPROM_WEAR_LEVELING) || defined(ERA_DYNAMIC_MACRO_TRANSACTION_TEST))
#    include "eeprom_driver.h"
#    define ERA_DYNAMIC_MACRO_TRANSACTION_ACTIVE
static bool s_nvm_dynamic_keymap_macro_transaction_active;
static bool s_nvm_dynamic_keymap_macro_transaction_payload_seen;
#    define NVM_DYNAMIC_KEYMAP_CACHE_ONLY_PARAMETER , bool cache_only
#    define NVM_DYNAMIC_KEYMAP_CACHE_ONLY_ARGUMENT(value) , value
#    define NVM_DYNAMIC_KEYMAP_ZERO_RESULT bool
#endif

#if !defined(ERA_DYNAMIC_MACRO_TRANSACTION_ACTIVE)
#    define NVM_DYNAMIC_KEYMAP_CACHE_ONLY_PARAMETER
#    define NVM_DYNAMIC_KEYMAP_CACHE_ONLY_ARGUMENT(value)
#    define NVM_DYNAMIC_KEYMAP_ZERO_RESULT void
#endif

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

uint32_t nvm_dynamic_keymap_eeprom_address(void) {
    return DYNAMIC_KEYMAP_EEPROM_ADDR;
}

uint32_t nvm_dynamic_keymap_macro_eeprom_address(void) {
    return DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR;
}

void nvm_dynamic_keymap_erase(void) {
    // No-op, nvm_eeconfig_erase() will have already erased EEPROM if necessary.
}

void nvm_dynamic_keymap_macro_erase(void) {
    // No-op, nvm_eeconfig_erase() will have already erased EEPROM if necessary.
}

static inline void *dynamic_keymap_key_to_eeprom_address(uint8_t layer, uint8_t row, uint8_t column) {
    return ((void *)DYNAMIC_KEYMAP_EEPROM_ADDR) + (layer * MATRIX_ROWS * MATRIX_COLS * 2) + (row * MATRIX_COLS * 2) + (column * 2);
}

#define NVM_EEPROM_WRITE_PROFILE_FLAG_OPTIMIZED 0x01
#define NVM_EEPROM_WRITE_PROFILE_FLAG_RESET 0x02
#define NVM_EEPROM_WRITE_PROFILE_FLAG_TRUNCATED 0x04

static void nvm_dynamic_keymap_write_block_guarded(uint32_t eeprom_addr, const void *buf, uint32_t length NVM_DYNAMIC_KEYMAP_CACHE_ONLY_PARAMETER) {
    if (length == 0) {
        return;
    }

#if defined(ERA_DYNAMIC_MACRO_TRANSACTION_ACTIVE)
    if (cache_only) {
        (void)eeprom_driver_write_block_cache_only(buf, (void *)(uintptr_t)eeprom_addr, length);
        return;
    }
#endif

    nvm_eeprom_write_begin_kb((uint16_t)eeprom_addr, (uint16_t)length);
    eeprom_write_block(buf, (void *)(uintptr_t)eeprom_addr, length);
    nvm_eeprom_write_end_kb((uint16_t)eeprom_addr, (uint16_t)length);
}

static void nvm_dynamic_keymap_update_changed_runs(uint32_t eeprom_addr, uint32_t eeprom_size, uint32_t offset, uint32_t size, const uint8_t *data NVM_DYNAMIC_KEYMAP_CACHE_ONLY_PARAMETER) {
    if (data == NULL || size == 0 || offset >= eeprom_size) {
        return;
    }

    uint32_t started_ms   = timer_read32();
    uint32_t clipped_size = MIN(eeprom_size - offset, size);
    uint32_t written_len  = 0;
    uint16_t write_calls  = 0;
    uint8_t *target       = (uint8_t *)(uintptr_t)(eeprom_addr + offset);

    uint32_t changed_min  = clipped_size;
    uint32_t changed_max  = 0;
    uint32_t run_start    = clipped_size;

    for (uint32_t i = 0; i < clipped_size; i++) {
        uint8_t current = eeprom_read_byte(target);
        if (current != data[i]) {
            if (run_start == clipped_size) {
                run_start = i;
            }
            if (i < changed_min) {
                changed_min = i;
            }
            changed_max = i + 1;
        } else if (run_start != clipped_size) {
            uint32_t run_len = i - run_start;
            nvm_dynamic_keymap_write_block_guarded(eeprom_addr + offset + run_start, &data[run_start], run_len NVM_DYNAMIC_KEYMAP_CACHE_ONLY_ARGUMENT(cache_only));
            written_len += run_len;
            write_calls++;
            run_start = clipped_size;
        }
        target++;
    }

    if (run_start != clipped_size) {
        uint32_t run_len = clipped_size - run_start;
        nvm_dynamic_keymap_write_block_guarded(eeprom_addr + offset + run_start, &data[run_start], run_len NVM_DYNAMIC_KEYMAP_CACHE_ONLY_ARGUMENT(cache_only));
        written_len += run_len;
        write_calls++;
    }

    if (changed_min < changed_max) {
        nvm_eeprom_changed_kb((uint16_t)(eeprom_addr + offset + changed_min), (uint16_t)(changed_max - changed_min));
    }

    uint8_t flags = size != clipped_size ? NVM_EEPROM_WRITE_PROFILE_FLAG_TRUNCATED : 0;
    flags |= NVM_EEPROM_WRITE_PROFILE_FLAG_OPTIMIZED;
    nvm_eeprom_write_profile_kb((uint16_t)(eeprom_addr + offset), (uint16_t)size, (uint16_t)clipped_size, (uint16_t)written_len, write_calls, (uint16_t)timer_elapsed32(started_ms), flags);
}

static uint16_t nvm_dynamic_keymap_write_zero_run(uint32_t eeprom_addr, uint32_t length NVM_DYNAMIC_KEYMAP_CACHE_ONLY_PARAMETER) {
    static const uint8_t zeros[64] = {0};
    uint16_t write_calls = 0;
    while (length > 0) {
        uint32_t chunk_size = MIN(length, sizeof(zeros));
        nvm_dynamic_keymap_write_block_guarded(eeprom_addr, zeros, chunk_size NVM_DYNAMIC_KEYMAP_CACHE_ONLY_ARGUMENT(cache_only));
        eeprom_addr += chunk_size;
        length -= chunk_size;
        write_calls++;
    }
    return write_calls;
}

static NVM_DYNAMIC_KEYMAP_ZERO_RESULT nvm_dynamic_keymap_zero_changed_runs(uint32_t eeprom_addr, uint32_t eeprom_size NVM_DYNAMIC_KEYMAP_CACHE_ONLY_PARAMETER) {
    uint32_t started_ms  = timer_read32();
    uint32_t written_len = 0;
    uint16_t write_calls = 0;

    uint32_t changed_min = eeprom_size;
    uint32_t changed_max = 0;
    uint32_t run_start   = eeprom_size;
    uint8_t *target      = (uint8_t *)(uintptr_t)eeprom_addr;

    for (uint32_t i = 0; i < eeprom_size; i++) {
        uint8_t current = eeprom_read_byte(target);
        if (current != 0) {
            if (run_start == eeprom_size) {
                run_start = i;
            }
            if (i < changed_min) {
                changed_min = i;
            }
            changed_max = i + 1;
        } else if (run_start != eeprom_size) {
            uint32_t run_len = i - run_start;
            write_calls += nvm_dynamic_keymap_write_zero_run(eeprom_addr + run_start, run_len NVM_DYNAMIC_KEYMAP_CACHE_ONLY_ARGUMENT(cache_only));
            written_len += run_len;
            run_start = eeprom_size;
        }
        target++;
    }

    if (run_start != eeprom_size) {
        uint32_t run_len = eeprom_size - run_start;
        write_calls += nvm_dynamic_keymap_write_zero_run(eeprom_addr + run_start, run_len NVM_DYNAMIC_KEYMAP_CACHE_ONLY_ARGUMENT(cache_only));
        written_len += run_len;
    }

    if (changed_min < changed_max) {
        nvm_eeprom_changed_kb((uint16_t)(eeprom_addr + changed_min), (uint16_t)(changed_max - changed_min));
    }

    uint8_t flags = NVM_EEPROM_WRITE_PROFILE_FLAG_RESET;
    flags |= NVM_EEPROM_WRITE_PROFILE_FLAG_OPTIMIZED;
    nvm_eeprom_write_profile_kb((uint16_t)eeprom_addr, (uint16_t)eeprom_size, (uint16_t)eeprom_size, (uint16_t)written_len, write_calls, (uint16_t)timer_elapsed32(started_ms), flags);
#if defined(ERA_DYNAMIC_MACRO_TRANSACTION_ACTIVE)
    return changed_min < changed_max;
#endif
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
    // Big endian, so we can read/write EEPROM directly from host if we want
    uint8_t keycode_data[2] = {(uint8_t)(keycode >> 8), (uint8_t)(keycode & 0xFF)};
    nvm_dynamic_keymap_update_changed_runs((uint32_t)(uintptr_t)dynamic_keymap_key_to_eeprom_address(layer, row, column), sizeof(keycode_data), 0, sizeof(keycode_data), keycode_data NVM_DYNAMIC_KEYMAP_CACHE_ONLY_ARGUMENT(false));
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
    uint8_t keycode_data[2] = {(uint8_t)(keycode >> 8), (uint8_t)(keycode & 0xFF)};
    nvm_dynamic_keymap_update_changed_runs((uint32_t)(uintptr_t)(address + (clockwise ? 0 : 2)), sizeof(keycode_data), 0, sizeof(keycode_data), keycode_data NVM_DYNAMIC_KEYMAP_CACHE_ONLY_ARGUMENT(false));
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
    nvm_dynamic_keymap_update_changed_runs(DYNAMIC_KEYMAP_EEPROM_ADDR, dynamic_keymap_eeprom_size, offset, size, data NVM_DYNAMIC_KEYMAP_CACHE_ONLY_ARGUMENT(false));
}

uint32_t nvm_dynamic_keymap_macro_size(void) {
    return DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE;
}

#if defined(ERA_DYNAMIC_MACRO_TRANSACTION_ENABLE)
bool nvm_dynamic_keymap_macro_transaction_in_progress(void) {
#if defined(ERA_DYNAMIC_MACRO_TRANSACTION_ACTIVE)
    return s_nvm_dynamic_keymap_macro_transaction_active;
#else
    return false;
#endif
}
#endif

void nvm_dynamic_keymap_macro_read_buffer(uint32_t offset, uint32_t size, uint8_t *data) {
    void    *source = (void *)(uintptr_t)(DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR + offset);
    uint8_t *target = data;
    for (uint16_t i = 0; i < size; i++) {
        if (offset + i < DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE) {
            *target = eeprom_read_byte(source);
        } else {
            *target = 0x00;
        }
        source++;
        target++;
    }
}

void nvm_dynamic_keymap_macro_update_buffer(uint32_t offset, uint32_t size, uint8_t *data) {
#if defined(ERA_DYNAMIC_MACRO_TRANSACTION_ACTIVE)
    bool    marker_written = false;
    uint8_t marker_value   = 0;
    bool    payload_written = false;
    if (data != NULL && size > 0 && offset < DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE) {
        uint32_t clipped_size  = MIN(DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE - offset, size);
        uint32_t marker_offset = DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE - 1;
        if (marker_offset >= offset && marker_offset - offset < clipped_size) {
            marker_written = true;
            marker_value   = data[marker_offset - offset];
        }
        payload_written = offset < marker_offset && clipped_size > 0;
    }

    // QMK's public dynamic-macro contract defines a nonzero final byte as the
    // transaction opener and zero as the completed image. Between those two
    // writes, update the wear-leveling cache only; ordinary QMK reads already
    // use that cache, so the invalid marker continues to block execution.
    if (!s_nvm_dynamic_keymap_macro_transaction_active) {
        /* The marker is the opener, not merely another byte. An unbracketed
         * payload or a zero before an opener remains unopened and changes
         * nothing, so a reordered host transcript cannot publish fragments. */
        if (!marker_written || marker_value == 0) {
            return;
        }
        s_nvm_dynamic_keymap_macro_transaction_active = true;
        s_nvm_dynamic_keymap_macro_transaction_payload_seen = false;
    }

    nvm_dynamic_keymap_update_changed_runs(DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR, DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE, offset, size, data NVM_DYNAMIC_KEYMAP_CACHE_ONLY_ARGUMENT(s_nvm_dynamic_keymap_macro_transaction_active));
    if (payload_written) {
        s_nvm_dynamic_keymap_macro_transaction_payload_seen = true;
    }

    if (s_nvm_dynamic_keymap_macro_transaction_active && marker_written && marker_value == 0) {
        if (!s_nvm_dynamic_keymap_macro_transaction_payload_seen) {
            /* Zero is publication authority only after at least one payload
             * write. Keep the cache visibly invalid and the transaction open. */
            uint8_t invalid = 0xFF;
            (void)eeprom_driver_write_block_cache_only(&invalid, (void *)(uintptr_t)(DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR + DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE - 1), sizeof(invalid));
            return;
        }
        bool committed = eeprom_driver_commit_cache();
        if (committed) {
            s_nvm_dynamic_keymap_macro_transaction_active = false;
            s_nvm_dynamic_keymap_macro_transaction_payload_seen = false;
        } else {
            // A zero marker is publication authority. If durability failed,
            // put the cache back into QMK's invalid state so neither macro
            // execution nor ERA storage capture can publish the staged image.
            uint8_t invalid = 0xFF;
            (void)eeprom_driver_write_block_cache_only(&invalid, (void *)(uintptr_t)(DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR + DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE - 1), sizeof(invalid));
        }

        // Anchor the storage quiet interval after the physical commit attempt,
        // not before the full-cache consolidation it brackets.
        nvm_eeprom_changed_kb((uint16_t)(DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR + DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE - 1), 1);
    }
#else
    nvm_dynamic_keymap_update_changed_runs(DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR, DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE, offset, size, data);
#endif
}

void nvm_dynamic_keymap_macro_reset(void) {
#if defined(ERA_DYNAMIC_MACRO_TRANSACTION_ACTIVE)
    bool transaction_was_active                   = s_nvm_dynamic_keymap_macro_transaction_active;
    s_nvm_dynamic_keymap_macro_transaction_active = true;
    s_nvm_dynamic_keymap_macro_transaction_payload_seen = true;
    bool changed                                  = nvm_dynamic_keymap_zero_changed_runs(DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR, DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE NVM_DYNAMIC_KEYMAP_CACHE_ONLY_ARGUMENT(true));
    bool commit_needed                            = changed || transaction_was_active;
    bool committed                                = !commit_needed || eeprom_driver_commit_cache();
    if (!committed) {
        uint8_t invalid = 0xFF;
        (void)eeprom_driver_write_block_cache_only(&invalid, (void *)(uintptr_t)(DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR + DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE - 1), sizeof(invalid));
        s_nvm_dynamic_keymap_macro_transaction_active = true;
    } else {
        s_nvm_dynamic_keymap_macro_transaction_active = false;
        s_nvm_dynamic_keymap_macro_transaction_payload_seen = false;
    }
    if (commit_needed) {
        // The first notification raises the storage indicator before the
        // consolidation; this one makes its quiet deadline trailing.
        nvm_eeprom_changed_kb((uint16_t)DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR, (uint16_t)DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE);
    }
#else
    nvm_dynamic_keymap_zero_changed_runs(DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR, DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE);
#endif
}

#undef NVM_DYNAMIC_KEYMAP_CACHE_ONLY_PARAMETER
#undef NVM_DYNAMIC_KEYMAP_CACHE_ONLY_ARGUMENT
#undef NVM_DYNAMIC_KEYMAP_ZERO_RESULT
