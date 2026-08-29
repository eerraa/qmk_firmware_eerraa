// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "era_nvm_format.h"

#define ERA_NVM_DYNAMIC_MACRO_SIZE_BYTES (16U * 1024U)
#define ERA_NVM_NO_ACTIVE_BANK 0xFFU

typedef enum {
    ERA_NVM_STATE_UNINITIALIZED = 0,
    ERA_NVM_STATE_READY,
    ERA_NVM_STATE_FAULTED,
} era_nvm_state_t;

typedef enum {
    ERA_NVM_RESULT_OK = 0,
    ERA_NVM_RESULT_NO_CHANGE,
    ERA_NVM_RESULT_STAGED,
    ERA_NVM_RESULT_INVALID_ARGUMENT,
    ERA_NVM_RESULT_NOT_READY,
    ERA_NVM_RESULT_BUSY,
    ERA_NVM_RESULT_PROTOCOL,
    ERA_NVM_RESULT_IO_ERROR,
    ERA_NVM_RESULT_GENERATION_EXHAUSTED,
} era_nvm_result_t;

typedef enum {
    ERA_NVM_ORIGIN_LOCAL_QMK = 0,
    ERA_NVM_ORIGIN_REMOTE_APPLY,
    ERA_NVM_ORIGIN_MACRO_TRANSACTION,
    ERA_NVM_ORIGIN_CLEAN_PREPARE,
    ERA_NVM_ORIGIN_FORMAT,
} era_nvm_origin_t;

typedef enum {
    ERA_NVM_MACRO_IDLE = 0,
    ERA_NVM_MACRO_WRITE_OPEN,
    ERA_NVM_MACRO_RESET_OPEN,
} era_nvm_macro_mode_t;

typedef struct {
    void *context;
    bool (*init)(void *context);
    bool (*read)(void *context, uint32_t offset, void *data, size_t length);
    /* Raw page-program primitive. The engine never calls this with a range
     * crossing a 256-byte physical page. */
    bool (*program)(void *context, uint32_t offset, const void *data, size_t length);
    /* Raw 4-KiB erase primitive. The engine supplies an aligned sector base. */
    bool (*erase_sector)(void *context, uint32_t offset);
} era_nvm_flash_t;

typedef void (*era_nvm_commit_notifier_t)(void *context, uint32_t address, uint32_t length, era_nvm_origin_t origin);

typedef struct {
    uint32_t                  macro_address;
    uint32_t                  macro_size;
    era_nvm_commit_notifier_t commit_notifier;
    void                     *commit_context;
} era_nvm_config_t;

/* One canonical public image is the only large permanent ERA NVM allocation.
 * Macro upload staging mutates this same image while keeping the marker
 * nonzero; RESET needs only transcript state plus the same marker byte. Durable
 * range construction streams through bounded stack scratch. */
typedef struct {
    era_nvm_flash_t flash;
    era_nvm_config_t config;

    uint8_t image[ERA_NVM_LOGICAL_SIZE_BYTES];

    era_nvm_state_t state;
    uint8_t         active_bank;
    uint32_t        generation;
    uint32_t        journal_cursor;
    uint32_t        next_sequence;
    bool            tail_sealed;

    uint8_t inactive_erase_sector;

    era_nvm_macro_mode_t macro_mode;
    bool                 macro_payload_seen;
    uint32_t             macro_reset_scan_cursor;
    bool                 macro_reset_zero_write_seen;
    bool                 macro_reset_scan_complete;
    era_nvm_macro_mode_t macro_reset_prior_mode;
    uint8_t              macro_reset_saved_marker;

    /* Physical command evidence. Counts live with the engine because only the
     * verified wrapper can distinguish a command that returned from one whose
     * immediate production readback failed. Healthy device runs require both
     * failure counters to remain zero. */
    uint32_t program_count;
    uint32_t program_failure_count;
    uint32_t erase_count;
    uint32_t erase_failure_count;
} era_nvm_t;

typedef struct {
    uint32_t program_count;
    uint32_t program_failure_count;
    uint32_t erase_count;
    uint32_t erase_failure_count;
} era_nvm_diagnostics_t;

void era_nvm_setup(era_nvm_t *nvm, const era_nvm_flash_t *flash, const era_nvm_config_t *config);
era_nvm_result_t era_nvm_mount(era_nvm_t *nvm);

era_nvm_state_t era_nvm_state(const era_nvm_t *nvm);
uint8_t         era_nvm_active_bank(const era_nvm_t *nvm);
uint32_t        era_nvm_generation(const era_nvm_t *nvm);
uint32_t        era_nvm_journal_free_bytes(const era_nvm_t *nvm);
bool            era_nvm_tail_is_sealed(const era_nvm_t *nvm);
void            era_nvm_get_diagnostics(const era_nvm_t *nvm, era_nvm_diagnostics_t *diagnostics);

era_nvm_result_t era_nvm_read(const era_nvm_t *nvm, uint32_t address, void *data, size_t length);

/* Session 2's custom EEPROM adapter must route stock QMK EEPROM reads through
 * this boundary, not just writes. Ordinary reads are identical to
 * era_nvm_read(); the stateful wrapper additionally recognizes stock QMK
 * macro_reset()'s sequential 16-byte eeprom_update_block read scan. QMK's
 * generic update helper skips writes for already-zero chunks, so observing the
 * read side is what makes RESET interceptable without a QMK Core hook. */
era_nvm_result_t era_nvm_qmk_read(era_nvm_t *nvm, uint32_t address, void *data, size_t length);

/* Result-bearing atomic replacement. This is the Session-2 boundary for
 * REMOTE_APPLY and CLEAN. It bypasses the QMK dynamic-macro transcript.
 * BUSY only when a macro transaction is open *and* the range touches the macro
 * domain; the reasoning for that scope is at the test in era_nvm.c. */
era_nvm_result_t era_nvm_replace(era_nvm_t *nvm, uint32_t address, const void *data, size_t length, era_nvm_origin_t origin);

/* Replace the whole logical image with the erased/default image and publish it.
 * This is the Session-2 seam for QMK's custom eeprom_driver_erase()/format and
 * for CLEAN's whole-store reset: era_nvm_replace() cannot serve them, because
 * a 24-KiB range needs a 24-KiB caller buffer and, through era_nvm_qmk_write(),
 * straddles the macro domain. Old QMK bytes are never parsed or migrated. */
era_nvm_result_t era_nvm_format(era_nvm_t *nvm);

/* Local QMK write boundary. Writes outside the macro domain are ordinary
 * durable replacements. Writes inside it recognize stock QMK's marker
 * transaction and, together with era_nvm_qmk_read(), macro RESET. */
era_nvm_result_t era_nvm_qmk_write(era_nvm_t *nvm, uint32_t address, const void *data, size_t length);

/* Read a range exactly as a fresh production mount would recover it, without
 * mutating the current public image. CLEAN uses this after its durable write so
 * PREPARED is a physical reboot-equivalent proof rather than a RAM equality. */
era_nvm_result_t era_nvm_replay_read(era_nvm_t *nvm, uint32_t address, void *data, size_t length);

/* Erase at most one sector of the inactive bank, then return to the caller.
 * did_work distinguishes an already-erased inactive bank from one completed by
 * this call. Mandatory rotation synchronously finishes the same primitive. */
era_nvm_result_t era_nvm_maintenance_erase_one_sector(era_nvm_t *nvm, bool *did_work);
