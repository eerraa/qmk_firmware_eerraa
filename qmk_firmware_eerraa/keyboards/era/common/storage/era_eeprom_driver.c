// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_eeprom_driver.h"
#ifdef ERA_TOMAK_SYNC_TIMING
#    include "../split/era_sync_timing.h"
#    include "timer.h"
#endif

#include <stdint.h>
#include <string.h>

#include "dynamic_keymap.h"
#include "eeconfig.h"
#include "eeprom_driver.h"
#include "matrix.h"
#include "platforms/eeprom.h"
#ifdef VIA_ENABLE
#    include "via.h"
#endif

#include "era_eeprom_layout.h"
#include "era_nvm_rp2040.h"
#ifdef VIA_ENABLE
#    include "era_storage_layout.h"
#endif

#ifdef VIA_ENABLE
#    include "../system/era_state_sync.h"
#endif
#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
#    include "../split/era_host_peer_storage.h"
#endif

#if !defined(EEPROM_CUSTOM)
#    error ERA NVM adapter requires QMK EEPROM_DRIVER=custom
#endif

_Static_assert(TOTAL_EEPROM_BYTE_COUNT == ERA_NVM_LOGICAL_SIZE_BYTES,
               "ERA NVM custom driver logical size must stay 24 KiB");

/* ERA's portable storage schema has no encoder map. Keep the layout arithmetic
 * here instead of reaching into QMK's private nvm_eeprom_* headers: the macro
 * domain starts after VIA's 3-byte magic, one layout byte, and the dynamic
 * keymap image. era_host_peer_storage.c independently pins the same formula to
 * the wire-domain schema. */
#ifdef VIA_ENABLE
_Static_assert(DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE == ERA_NVM_DYNAMIC_MACRO_SIZE_BYTES,
               "ERA NVM macro transcript requires the 16-KiB storage-adoption macro domain");
#endif

static era_nvm_t s_era_eeprom_nvm;
static bool      s_era_eeprom_ready;
static bool      s_era_eeprom_write_ok;
#ifdef VIA_ENABLE
static uint32_t s_edit_semantic_mask;
#endif
#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
static uint8_t s_edit_domain_mask;
#endif
static bool s_edit_completing;

static bool era_eeprom_driver_range_valid(uint32_t address, size_t length) {
    return length <= UINT32_MAX && address <= ERA_NVM_LOGICAL_SIZE_BYTES &&
           (uint32_t)length <= ERA_NVM_LOGICAL_SIZE_BYTES - address;
}

static bool era_eeprom_driver_range_touches_macro(uint32_t address, size_t length) {
#ifdef VIA_ENABLE
    if (length == 0U || !era_eeprom_driver_range_valid(address, length)) {
        return false;
    }
    uint32_t end       = address + (uint32_t)length;
    uint32_t macro_end = ERA_STORAGE_DYNAMIC_MACRO_ADDR + DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE;
    return address < macro_end && end > ERA_STORAGE_DYNAMIC_MACRO_ADDR;
#else
    (void)address;
    (void)length;
    return false;
#endif
}

static uint32_t macro_serial;
uint32_t era_eeprom_driver_macro_serial(void) { return macro_serial; }
static void era_eeprom_driver_note_public_macro(void *context) {
    (void)context;
    macro_serial++;
#ifdef VIA_ENABLE
    era_state_sync_note_macro_public_change();
#endif
}

static void era_eeprom_driver_note_commit(void *context, uint32_t address, uint32_t length, era_nvm_origin_t origin) {
    (void)context;
    if (era_eeprom_driver_range_touches_macro(address, length)) macro_serial++;
#ifdef ERA_TOMAK_SYNC_TIMING
    if (origin == ERA_NVM_ORIGIN_LOCAL_QMK || origin == ERA_NVM_ORIGIN_MACRO_TRANSACTION ||
        origin == ERA_NVM_ORIGIN_REMOTE_APPLY) {
        era_sync_timing_commit(length, origin == ERA_NVM_ORIGIN_REMOTE_APPLY);
    }
#endif

    /* REMOTE_APPLY owns convergence publication explicitly. CLEAN/FORMAT are
     * retiring content, not local edits. Ordinary QMK writes and a completed
     * macro transaction are the only origins that become local dirty/semantic
     * notifications here. */
    if (origin != ERA_NVM_ORIGIN_LOCAL_QMK && origin != ERA_NVM_ORIGIN_MACRO_TRANSACTION) {
        return;
    }

    if (s_edit_completing) {
#ifdef VIA_ENABLE
        era_state_sync_note_eeprom_mask(s_edit_semantic_mask);
#endif
#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
        era_host_peer_storage_note_eeprom_mask(s_edit_domain_mask);
#endif
        return;
    }

#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
    era_host_peer_storage_note_eeprom_commit(address, length);
#elif defined(VIA_ENABLE)
    era_state_sync_note_eeprom_span((uint16_t)address, (uint16_t)length);
#else
    (void)address;
    (void)length;
#endif
}

void eeprom_driver_init(void) {
    era_nvm_flash_t flash;
    macro_serial++;
    memset(&s_era_eeprom_nvm, 0, sizeof(s_era_eeprom_nvm));
    s_era_eeprom_ready = false;
    s_edit_completing = false;

    if (!era_nvm_rp2040_flash_bind(&flash)) {
        return;
    }

    era_nvm_config_t config = {
        .macro_address   = 0U,
        .macro_size      = 0U,
        .public_macro_notifier = era_eeprom_driver_note_public_macro,
        .commit_notifier = era_eeprom_driver_note_commit,
        .commit_context  = NULL,
    };
#ifdef VIA_ENABLE
    config.macro_address = ERA_STORAGE_DYNAMIC_MACRO_ADDR;
    config.macro_size    = DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE;
#endif

    era_nvm_setup(&s_era_eeprom_nvm, &flash, &config);
    s_era_eeprom_ready = era_nvm_mount(&s_era_eeprom_nvm) == ERA_NVM_RESULT_OK;
    s_era_eeprom_write_ok = s_era_eeprom_ready;
}

void eeprom_driver_format(bool erase) {
    /* QMK's EEPROM_DRIVER contract uses format(false) from nvm_eeconfig_erase()
     * as the logical whole-store reset. The stock wear-level adapter ignores
     * the boolean for the same reason. ERA NVM therefore formats on either
     * value; the flag is a backend hint, not permission to preserve content. */
    (void)erase;
    eeprom_driver_erase();
}

void eeprom_driver_erase(void) {
    if (!s_era_eeprom_ready) {
        return;
    }
    /* A format fault does not imply that the previously active bank stopped
     * being readable; the engine deliberately preserves the old authority on
     * every pre-activation failure. QMK's void erase API cannot report the
     * result, but it must not make valid old public data disappear either. */
    era_nvm_result_t result = era_nvm_format(&s_era_eeprom_nvm);
    s_era_eeprom_write_ok = result == ERA_NVM_RESULT_OK || result == ERA_NVM_RESULT_STAGED;
    if (!s_era_eeprom_write_ok) s_era_eeprom_nvm.edit_failed = true;
}

void eeprom_read_block(void *buf, const void *addr, size_t len) {
    if (len == 0U) {
        return;
    }
    if (buf == NULL || !s_era_eeprom_ready ||
        era_nvm_qmk_read(&s_era_eeprom_nvm, (uint32_t)(uintptr_t)addr, buf, len) != ERA_NVM_RESULT_OK) {
        s_era_eeprom_write_ok = false;
        if (s_era_eeprom_nvm.edit_depth) s_era_eeprom_nvm.edit_failed = true;
        if (buf != NULL) {
            memset(buf, 0, len);
        }
    }
}

void eeprom_write_block(const void *buf, void *addr, size_t len) {
    if (len == 0U) return;
    if (buf == NULL || !era_eeprom_driver_ready()) {
        s_era_eeprom_write_ok = false;
        s_era_eeprom_nvm.edit_failed = true;
        return;
    }

    uint32_t       address = (uint32_t)(uintptr_t)addr;
    const uint8_t *source  = (const uint8_t *)buf;
    if (!era_eeprom_driver_range_valid(address, len)) {
        s_era_eeprom_write_ok = false;
        s_era_eeprom_nvm.edit_failed = true;
        return;
    }

#ifdef ERA_TOMAK_SYNC_TIMING
    era_sync_timing_write_begin(len);
    uint32_t timing_start = timer_read32();
#endif
    /* Preserve macro writes byte-for-byte as stock QMK issued them. In
     * particular RESET is recognized by the exact sequential 16-byte
     * eeprom_update_block transcript, so changed-span coalescing must never
     * rewrite that transcript above the engine.
     *
     * Outside the macro domain the adapter owns authoritative changed-span
     * notification. QMK's update helpers avoid wholly-equal calls, but a block
     * can still contain an unchanged prefix/suffix; narrow those here so the
     * committed-span notifier publishes exactly the changed envelope. No
     * caller buffer is allocated -- the mounted 24-KiB image is already the
     * public comparison source. */
    if (!s_era_eeprom_nvm.edit_depth && !s_era_eeprom_nvm.tail_sealed && !era_eeprom_driver_range_touches_macro(address, len)) {
        size_t first = 0U;
        while (first < len && s_era_eeprom_nvm.image[address + first] == source[first]) {
            first++;
        }
        if (first == len) {
            s_era_eeprom_write_ok = true;
#ifdef ERA_TOMAK_SYNC_TIMING
            era_sync_timing_write_end(timer_read32() - timing_start);
#endif
            return;
        }
        size_t last = len;
        while (last > first + 1U && s_era_eeprom_nvm.image[address + last - 1U] == source[last - 1U]) {
            last--;
        }
        address += (uint32_t)first;
        source += first;
        len = last - first;
    }

    /* Write failures are transaction failures, not mount failures. In
     * particular a failed macro close must keep the public nonzero marker and
     * the previous durable image readable. Let era_nvm_state() decide whether
     * the engine itself is still mounted/ready. */
    if (s_era_eeprom_nvm.edit_depth) {
        /* Record meanings of actual changed runs, independently of the single
         * contiguous record the NVM engine will commit. This runs only at a
         * synchronous write boundary, never on the scan-bound idle path. */
        for (size_t cursor = 0; cursor < len;) {
            if (s_era_eeprom_nvm.image[address + cursor] == source[cursor]) { cursor++; continue; }
            size_t first = cursor++;
            while (cursor < len && s_era_eeprom_nvm.image[address + cursor] != source[cursor]) cursor++;
#ifdef VIA_ENABLE
            s_edit_semantic_mask |= era_state_sync_eeprom_span_mask((uint16_t)(address + first), (uint16_t)(cursor - first));
#endif
#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
            s_edit_domain_mask |= era_host_peer_storage_eeprom_domain_mask(address + first, cursor - first);
#endif
            (void)first;
        }
    }
#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
    bool legacy_macro_open = s_era_eeprom_nvm.macro_mode == ERA_NVM_MACRO_WRITE_OPEN;
#endif
    era_nvm_result_t result = era_nvm_qmk_write(&s_era_eeprom_nvm, address, source, len);
#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
    /* The stock VIA final marker is an explicit boundary too, but only after
     * a successful durable close. RESET has its own mode and retains its
     * independent publication and ordinary quiet period. */
    if (legacy_macro_open && result == ERA_NVM_RESULT_OK &&
        s_era_eeprom_nvm.macro_mode == ERA_NVM_MACRO_IDLE) {
        era_host_peer_storage_note_macro_commit_ready();
    }
#endif
#ifdef ERA_TOMAK_SYNC_TIMING
    era_sync_timing_write_end(timer_read32() - timing_start);
#endif
    s_era_eeprom_write_ok = result == ERA_NVM_RESULT_OK || result == ERA_NVM_RESULT_NO_CHANGE || result == ERA_NVM_RESULT_STAGED;
    if (!s_era_eeprom_write_ok) s_era_eeprom_nvm.edit_failed = true;
}

bool eeprom_driver_can_skip_write(uint32_t address, size_t length) {
    /* An equal macro byte is still a received transcript event (including
     * CLOSE). Ordinary updates retain their no-op optimization. */
    return !era_eeprom_driver_range_touches_macro(address, length) &&
           era_eeprom_driver_ready() && !s_era_eeprom_nvm.tail_sealed && s_era_eeprom_write_ok;
}

bool eeprom_begin_transaction(void) {
    bool ok = era_nvm_begin(&s_era_eeprom_nvm) == ERA_NVM_RESULT_OK;
    if (ok && s_era_eeprom_nvm.edit_depth == 1U) {
        s_era_eeprom_write_ok = true;
#ifdef VIA_ENABLE
        s_edit_semantic_mask = 0;
#endif
#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
        s_edit_domain_mask = 0;
#endif
    }
    return ok;
}

bool eeprom_transaction_active(void) { return s_era_eeprom_nvm.edit_depth != 0U; }

void eeprom_reject_write(void) {
    s_era_eeprom_write_ok = false;
    if (eeprom_transaction_active()) s_era_eeprom_nvm.edit_failed = true;
}

bool eeprom_end_transaction(void) {
    s_edit_completing = s_era_eeprom_nvm.edit_depth == 1U;
    era_nvm_result_t result = era_nvm_end(&s_era_eeprom_nvm);
    s_edit_completing = false;
    s_era_eeprom_write_ok = result == ERA_NVM_RESULT_OK || result == ERA_NVM_RESULT_NO_CHANGE || result == ERA_NVM_RESULT_STAGED;
    if (!eeprom_transaction_active()) eeprom_transaction_complete(s_era_eeprom_write_ok);
    return s_era_eeprom_write_ok;
}

bool eeprom_persistence_ok(void) {
    return era_eeprom_driver_ready() && s_era_eeprom_write_ok && (!s_era_eeprom_nvm.edit_depth || !s_era_eeprom_nvm.edit_failed);
}

bool era_eeprom_driver_ready(void) {
    return s_era_eeprom_ready && era_nvm_state(&s_era_eeprom_nvm) == ERA_NVM_STATE_READY;
}

era_nvm_result_t era_eeprom_driver_macro_begin(uint32_t length) {
    return era_nvm_macro_replace_begin(&s_era_eeprom_nvm, length);
}
era_nvm_result_t era_eeprom_driver_macro_data(uint32_t offset, const void *data, size_t length) {
    return era_nvm_macro_replace_data(&s_era_eeprom_nvm, offset, data, length);
}
era_nvm_result_t era_eeprom_driver_macro_commit(uint32_t crc32) {
    era_nvm_result_t result = era_nvm_macro_replace_commit(&s_era_eeprom_nvm, crc32);
#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
    if (result == ERA_NVM_RESULT_OK) era_host_peer_storage_note_macro_commit_ready();
#endif
    return result;
}
era_nvm_result_t era_eeprom_driver_macro_abort(void) {
    return era_nvm_macro_replace_abort(&s_era_eeprom_nvm);
}
bool era_eeprom_driver_macro_replace_open(void) {
    return s_era_eeprom_nvm.macro_mode == ERA_NVM_MACRO_REPLACE_OPEN;
}

era_nvm_result_t era_eeprom_driver_replace(uint32_t address, const void *data, size_t length, era_nvm_origin_t origin) {
    if (!era_eeprom_driver_ready()) {
        return ERA_NVM_RESULT_NOT_READY;
    }
#ifdef ERA_TOMAK_SYNC_TIMING
    uint32_t timing_start = timer_read32();
#endif
    era_nvm_result_t result = era_nvm_replace(&s_era_eeprom_nvm, address, data, length, origin);
#ifdef ERA_TOMAK_SYNC_TIMING
    if (origin == ERA_NVM_ORIGIN_REMOTE_APPLY) {
        era_sync_timing_apply_end(timer_read32() - timing_start);
    }
#endif
    return result;
}

era_nvm_result_t era_eeprom_driver_write_storage_metadata(uint32_t address, const void *data, size_t length) {
    return era_eeprom_driver_replace(address, data, length, ERA_NVM_ORIGIN_STORAGE_METADATA);
}

era_nvm_result_t era_eeprom_driver_replay_read(uint32_t address, void *data, size_t length) {
    if (!era_eeprom_driver_ready()) {
        return ERA_NVM_RESULT_NOT_READY;
    }
    return era_nvm_replay_read(&s_era_eeprom_nvm, address, data, length);
}

era_nvm_result_t era_eeprom_driver_prepare_reboot_word(uint32_t address, uint16_t value) {
    era_nvm_result_t result = era_eeprom_driver_replace(address, &value, sizeof(value), ERA_NVM_ORIGIN_CLEAN_PREPARE);
    if (result != ERA_NVM_RESULT_OK && result != ERA_NVM_RESULT_NO_CHANGE) {
        return result;
    }

    uint16_t replayed = 0U;
    result            = era_eeprom_driver_replay_read(address, &replayed, sizeof(replayed));
    if (result != ERA_NVM_RESULT_OK) {
        return result;
    }
    return replayed == value ? ERA_NVM_RESULT_OK : ERA_NVM_RESULT_IO_ERROR;
}

era_nvm_result_t era_eeprom_driver_maintenance_task(bool *did_work) {
    if (did_work != NULL) {
        *did_work = false;
    }
    if (!era_eeprom_driver_ready()) {
        return ERA_NVM_RESULT_NOT_READY;
    }
    return era_nvm_maintenance_erase_one_sector(&s_era_eeprom_nvm, did_work);
}

bool era_eeprom_driver_macro_transaction_open(void) {
    return era_eeprom_driver_ready() && s_era_eeprom_nvm.macro_mode != ERA_NVM_MACRO_IDLE;
}

void era_eeprom_driver_get_nvm_diagnostics(era_nvm_diagnostics_t *diagnostics) {
    era_nvm_get_diagnostics(&s_era_eeprom_nvm, diagnostics);
}
#ifdef ERA_TOMAK_SYNC_TIMING
uint32_t era_eeprom_driver_timing_generation(void) { return era_nvm_generation(&s_era_eeprom_nvm); }
#endif
