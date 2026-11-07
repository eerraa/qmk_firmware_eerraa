// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#include "era_split_wire_diagnostics.h"
#include "era_split_wire_cause.h"
#include "../communication_core/era_split_communication_core_diagnostics.h"
#include "../era_split_transport_scheduler.h"
#include "../era_split_transaction_engine.h"
#include "../../system/era_rp2040_matrix.h"
#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
#    include "../era_host_peer_storage.h"
#    include "../../storage/era_eeprom_driver.h"
#endif
#include "timer.h"

typedef struct {
    uint32_t elapsed_ms;
    uint32_t scans;
    uint32_t samples;
    uint32_t torn_retries;
    uint32_t rearms;
    uint8_t  sampler_valid;
    uint8_t  fdebug;
    uint8_t  relation;
    era_split_communication_core_diagnostics_t core;
    era_split_transaction_engine_diagnostics_t link;
#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
    uint32_t aborts;
    uint32_t timeouts;
    uint32_t rejects;
    uint32_t program_failures;
    uint32_t erase_failures;
    uint8_t  storage_state;
    uint8_t  storage_status;
#endif
} era_split_wire_report_t;

static bool armed;
static uint8_t print_line;
static uint32_t start_ms, start_count, last_line_ms;
static era_rp2040_matrix_pio_diagnostics_t start_pio;
static era_split_wire_report_t report;
static era_split_wire_cause_capture_t causes;
static uint8_t cause_cursor;

static uint32_t rate_hz(uint32_t count, uint32_t elapsed_ms) {
    return elapsed_ms ? (uint32_t)((uint64_t)count * 1000U / elapsed_ms) : 0;
}

static void capture(void) {
    era_rp2040_matrix_pio_diagnostics_t pio;
    report.elapsed_ms = timer_read32() - start_ms;
    report.scans = era_split_wire_diagnostics_raw_matrix_scan_count - start_count;
    era_rp2040_matrix_pio_get_diagnostics(&pio);
    report.sampler_valid = pio.ready && start_pio.ready && pio.frame_words != 0 &&
                           pio.frame_words == start_pio.frame_words && pio.rearms == start_pio.rearms;
    report.samples = report.sampler_valid ? (pio.sample_words - start_pio.sample_words) / pio.frame_words : 0;
    report.torn_retries = pio.torn_retries - start_pio.torn_retries;
    report.rearms = pio.rearms - start_pio.rearms;
    report.fdebug = pio.fdebug;
    report.relation = era_split_transport_scheduler_diagnostic_mode();
    era_split_communication_core_get_diagnostics_snapshot(&report.core);
    era_split_transaction_engine_get_diagnostics_snapshot(&report.link);
    era_split_wire_cause_capture(&causes);
    cause_cursor = UINT8_MAX;
#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
    era_host_peer_storage_diagnostics_t storage;
    era_nvm_diagnostics_t nvm;
    era_host_peer_storage_get_diagnostics_snapshot(&storage);
    era_eeprom_driver_get_nvm_diagnostics(&nvm);
    report.storage_state = storage.state;
    report.storage_status = storage.status;
    report.aborts = storage.abort_count;
    report.timeouts = storage.timeout_count;
    report.rejects = storage.integrity_reject_count;
    report.program_failures = nvm.program_failure_count;
    report.erase_failures = nvm.erase_failure_count;
#endif
    print_line = 1;
    last_line_ms = timer_read32();
}

void era_split_wire_diagnostics_task(void) {
    if (!print_line || timer_elapsed32(last_line_ms) < 20U) {
        return;
    }
    /* Print only after the window closes. The small report is frozen; its
     * independently sampled Core1 counters do not claim one atomic instant. */
    switch (print_line) {
        case 1:
            uprintf("wire scan ms=%lu count=%lu hz=%lu pio_valid=%u pio_hz=%lu retry=%lu rearm=%lu fd=%X\r\n",
                    (unsigned long)report.elapsed_ms, (unsigned long)report.scans,
                    (unsigned long)rate_hz(report.scans, report.elapsed_ms), report.sampler_valid,
                    (unsigned long)rate_hz(report.samples, report.elapsed_ms),
                    (unsigned long)report.torn_retries, (unsigned long)report.rearms, report.fdebug);
            break;
        case 2:
            uprintf("wire core mode=%u run=%u capped=%u dead=%lu expired=%lu\r\n",
                    report.relation, report.core.running, report.core.launch_capped,
                    (unsigned long)report.core.dead_declared_count,
                    (unsigned long)report.core.initiator_queue_expired_count);
            break;
        case 3:
            uprintf("wire link tx=%lu rx=%lu miss=%lu bad=%lu fail=%lu\r\n",
                    (unsigned long)report.link.compact_tx_count, (unsigned long)report.link.compact_rx_count,
                    (unsigned long)report.link.compact_miss_count, (unsigned long)report.link.compact_bad_count,
                    (unsigned long)report.link.compact_fail_count);
            break;
#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
        case 4:
            uprintf("wire storage state=%u status=%u abort=%lu timeout=%lu reject=%lu program_fail=%lu erase_fail=%lu\r\n",
                    report.storage_state, report.storage_status, (unsigned long)report.aborts,
                    (unsigned long)report.timeouts, (unsigned long)report.rejects,
                    (unsigned long)report.program_failures, (unsigned long)report.erase_failures);
            break;
#endif
        default:
            /* The cause header follows the prefix even without storage. */
            if (cause_cursor == UINT8_MAX) {
                unsigned occupied = 0;
                for (unsigned i = 0; i < ERA_CAUSE_COUNT; ++i) {
                    occupied += causes.slots[i].count != 0;
                }
                uprintf("wire cause v=1 slots=%u used=%u unavailable=%lu\r\n", ERA_CAUSE_COUNT, occupied,
                        (unsigned long)causes.slots[ERA_CAUSE_UNAVAILABLE].count);
                cause_cursor = 0;
            } else {
                while (cause_cursor < ERA_CAUSE_COUNT && !causes.slots[cause_cursor].count) {
                    cause_cursor++;
                }
                if (cause_cursor == ERA_CAUSE_COUNT) {
                    print_line = 0;
                    return;
                }
                const era_split_wire_cause_slot_t *slot = &causes.slots[cause_cursor];
                const era_split_wire_cause_record_t *r = &slot->first;
                uprintf("wire detail id=%u n=%lu us=%lu ep=%u/%u rel=%u mode=%u why=%X role=%u baud=%lu v=%X ph=%u end=%u/%u wait=%u send=%u clean=%u pc=%u/%u len=%u/%u flags=%X kind=%u/%u/%u seq=%u/%u\r\n",
                        cause_cursor, (unsigned long)slot->count, (unsigned long)r->us,
                        r->attempt_epoch, r->observed_epoch, r->relation, r->mode, r->reason, r->role,
                        (unsigned long)r->baud, r->valid, r->phase, r->result, r->failure,
                        r->wait, r->send_wait, r->cleanup_wait, r->pc, r->program, r->a, r->b, r->flags,
                        r->expected, r->alternate, r->kind, r->tx, r->ack);
                cause_cursor++;
            }
            break;
    }
    print_line++;
    last_line_ms = timer_read32();
}

bool era_split_wire_diagnostics_process_record(uint16_t keycode, keyrecord_t *record) {
    if (keycode != WIRE_DIAG && keycode != WIRE_QWIN && keycode != WIRE_DIAG_2) {
        return true;
    }
    if (!record || !record->event.pressed || keycode == WIRE_DIAG_2 || print_line) {
        return false;
    }
    if (armed) {
        armed = false;
        capture();
    } else {
        era_rp2040_matrix_pio_get_diagnostics(&start_pio);
        era_rp2040_matrix_pio_clear_fdebug();
        start_ms = timer_read32();
        start_count = era_split_wire_diagnostics_raw_matrix_scan_count;
        armed = true;
    }
    return false;
}
