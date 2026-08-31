// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#include "era_split_wire_diagnostics.h"

#include <string.h>

#include <ch.h>

#include "timer.h"
#include "bootloader.h"
#include "usb_device_state.h" /* the host's last SET_IDLE, printed on the auth line */
#include "usb_endpoints.h"     /* the IN endpoint table, for the per-report idle rates behind usb_idle_task() */
#include "usb_driver.h"
#include "usb_report_handling.h"
#include "report.h"

#include "../../system/era_rp2040_matrix.h"
#include "../era_split_authority_reducer.h"
#include "../era_host_peer_matrix_link.h" /* the PEER key-path span, printed on the wire keypath line */
#ifdef SPLIT_KEYBOARD
#    include "../era_host_peer_transaction.h"
#    include "sync_timer.h"
#endif
#include "../communication_core/era_split_communication_core_diagnostics.h"
#include "../communication_core/era_split_communication_core_lifecycle.h"
#include "../era_split_keyboard.h"
#include "../era_split_mode_planner.h"
#include "../era_split_tap_activity.h"
#include "../era_split_transport_scheduler.h"
#ifdef ERA_SPLIT_EEPROM_SYNC_ENABLE
#    include "../era_split_eeprom_sync.h"
#    include "../era_split_sync_policy.h"
#endif
#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
#    include "../communication_core/era_split_communication_core_storage.h"
#    include "../era_host_peer_storage.h"
#    include "../../storage/era_eeprom_driver.h"
#    ifdef ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE
#        include "era_via_macro_diagnostics.h"
#    endif
#endif

#ifndef ERA_SPLIT_WIRE_DIAGNOSTICS_LINE_INTERVAL_MS
#    define ERA_SPLIT_WIRE_DIAGNOSTICS_LINE_INTERVAL_MS 150
#endif

enum {
    /* The DUAL-HOST era line is appended last on purpose: every other
     * scheduler line keeps its index, so a capture's line order and the
     * communication-core sampling offsets move by exactly one line rather
     * than by a renumbering nobody can diff. `wire maint`, `wire sect` and
     * then `wire act` (FA-2) were appended under the same rule and for the
     * same reason, so the only thing any of the four moved is this offset.
     *
     * **This count is the only thing that makes a new case reachable.** The
     * dispatcher routes by range, so a `case` added to the switch below without
     * bumping this number compiles, links, and prints nothing — which is what
     * `wire keypath` did on its first device sitting, costing a leg. There is
     * no compiler check for it; the case list and this number are one fact with
     * two homes and the only guard is reading this comment. */
    ERA_SPLIT_WIRE_DIAGNOSTICS_SCHEDULER_LINES = 22,
    /* The four smaller counts that stood beside this one selected on
       pre-CORE1_FULL stages the build system refuses, so none of them was
       reachable in any buildable configuration. */
    ERA_SPLIT_WIRE_DIAGNOSTICS_COMMUNICATION_CORE_BASE_LINES = 5,
    ERA_SPLIT_WIRE_DIAGNOSTICS_COMMUNICATION_CORE_LINES = ERA_SPLIT_WIRE_DIAGNOSTICS_COMMUNICATION_CORE_BASE_LINES + 1,
    ERA_SPLIT_WIRE_DIAGNOSTICS_AUTHORITY_LINES = 1,
#ifdef ERA_SPLIT_EEPROM_SYNC_ENABLE
    ERA_SPLIT_WIRE_DIAGNOSTICS_EEPROM_LINES = 3,
#endif
#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
#    ifdef ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE
    ERA_SPLIT_WIRE_DIAGNOSTICS_STORAGE_LINES = 9,
#    else
    ERA_SPLIT_WIRE_DIAGNOSTICS_STORAGE_LINES = 5,
#    endif
#endif
};

typedef struct {
    uint32_t                              print_count;
    uint32_t                              last_print_ms;
    bool                                  print_pending;
    uint8_t                               print_line;
    uint32_t                              last_line_ms;
    bool                                  raw_matrix_scan_rate_valid;
    uint32_t                              raw_matrix_scan_rate_ms;
    uint32_t                              raw_matrix_scan_rate_count;
    uint32_t                              raw_matrix_read_us_total;
    uint32_t                              raw_matrix_read_us_max;
    /* The PIO sampler's frame rate between two captures, from the sample
       DMA's word count -- a hardware counter, so the scan path pays nothing.
       Same shape as the scan-rate pair above: first capture primes, later
       ones read a delta. */
    bool                                  pio_sample_rate_valid;
    uint32_t                              pio_sample_rate_ms;
    uint32_t                              pio_sample_rate_words;
    uint32_t                              pio_sample_hz;
    era_rp2040_matrix_pio_diagnostics_t   pio_snapshot;
    bool                                  host_peer_rate_valid;
    uint32_t                              host_peer_rate_ms;
    uint32_t                              host_peer_rate_source_push_tx_count;
    uint32_t                              host_peer_rate_ack_status_tx_count;
    era_split_wire_diagnostics_snapshot_t wire_snapshot;
    era_split_communication_core_diagnostics_t communication_core_snapshot;
    era_authority_snapshot_t                   auth_snapshot;
#ifdef ERA_SPLIT_EEPROM_SYNC_ENABLE
    era_split_eeprom_sync_diagnostics_t eeprom_snapshot;
    era_split_sync_policy_snapshot_t    sync_policy_snapshot;
#endif
#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
    era_host_peer_storage_diagnostics_t                              storage_snapshot;
    era_split_communication_core_storage_probe_diagnostics_t         storage_probe_snapshot;
#    ifdef ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE
    era_host_peer_storage_cause_timeline_t                            storage_cause_timeline_snapshot;
    era_host_peer_storage_cause_edge_t                                storage_cause_edge_snapshot;
    era_via_macro_diagnostics_t                                       via_macro_snapshot;
#    endif
#endif
} era_split_wire_diagnostics_state_t;

static era_split_wire_diagnostics_state_t era_split_wire_diagnostics_state;

#ifdef MATRIX_SCAN_RAW_DIAGNOSTICS_ENABLE
void matrix_scan_raw_diagnostics_kb(uint32_t raw_read_us) {
    era_split_wire_diagnostics_raw_matrix_scan_count++;
    era_split_wire_diagnostics_state.raw_matrix_read_us_total += raw_read_us;
    if (raw_read_us > era_split_wire_diagnostics_state.raw_matrix_read_us_max) {
        era_split_wire_diagnostics_state.raw_matrix_read_us_max = raw_read_us;
    }
}
#endif

#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
static bool era_split_wire_diagnostics_print_storage_line(const era_host_peer_storage_diagnostics_t *snapshot,
                                                          const era_split_communication_core_storage_probe_diagnostics_t *probe_snapshot,
#ifdef ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE
                                                          const era_host_peer_storage_cause_timeline_t *cause_timeline_snapshot,
                                                          const era_host_peer_storage_cause_edge_t *cause_edge_snapshot,
                                                          const era_via_macro_diagnostics_t *via_macro_snapshot,
#endif
                                                          uint8_t line) {
    const era_host_peer_storage_diagnostics_t storage = *snapshot;
    const era_split_communication_core_storage_probe_diagnostics_t probe = *probe_snapshot;

    switch (line) {
        case 0: {
            era_host_peer_storage_foundation_snapshot_t foundation;
            era_host_peer_storage_get_foundation_snapshot(&foundation);
            uprintf("wire storage st=%u role=%u dom=%u status=%u active=%u excl=%u news=%02X probe=%02X open=%lu close=%lu abort=%lu restart=%lu\r\n", (unsigned)storage.state, (unsigned)storage.role, (unsigned)storage.domain, (unsigned)storage.status, (unsigned)storage.active, (unsigned)storage.exclusive, (unsigned)foundation.settled_news_value, (unsigned)foundation.probe_pending_mask, (unsigned long)storage.open_count, (unsigned long)storage.close_count, (unsigned long)storage.abort_count, (unsigned long)storage.restart_count);
            return true;
        }
        case 1:
            uprintf("wire storage proof=%lu match=%lu xfer=%lu chunk=%lu dup=%lu retry=%lu timeout=%lu apply=%lu complete=%lu\r\n", (unsigned long)storage.proof_count, (unsigned long)storage.match_count, (unsigned long)storage.transfer_count, (unsigned long)storage.chunk_count, (unsigned long)storage.duplicate_count, (unsigned long)storage.retry_count, (unsigned long)storage.timeout_count, (unsigned long)storage.apply_count, (unsigned long)storage.complete_count);
            return true;
        case 2:
            uprintf("wire storage stale=%lu source=%u full=%lu/%lu integrity=%lu version=%lu domain=%lu qfail=%lu\r\n", (unsigned long)storage.stale_count, (unsigned)storage.source_changed_count, (unsigned long)storage.initiator_full_count, (unsigned long)storage.responder_full_count, (unsigned long)storage.integrity_reject_count, (unsigned long)storage.version_reject_count, (unsigned long)storage.domain_reject_count, (unsigned long)storage.quiesce_fail_count);
            return true;
        case 3: {
            const era_split_communication_core_storage_probe_failure_context_t *failure_context = &probe.failure_context;
            uprintf("wire storage core cl=%lu tx=%lu rx=%lu pub=%lu fail=%lu gen=%u/%u/%u last=%u/%u/%u/%02X/%u flast=%u/%u/%u/%02X/%u/%u/%u ddu=%ld qctx=%lu/%ld psvc=%u/%u/%u/%u span=%ld/%ld/%ld fid=%u/%u/%u/%u/%u/%u\r\n",
                    (unsigned long)probe.claim_count,
                    (unsigned long)probe.tx_count,
                    (unsigned long)probe.rx_count,
                    (unsigned long)probe.publish_count,
                    (unsigned long)probe.failure_count,
                    (unsigned)probe.request_claim_generation,
                    (unsigned)probe.result_claim_generation,
                    (unsigned)probe.result_ready_generation,
                    (unsigned)probe.last_stage,
                    (unsigned)probe.last_result,
                    (unsigned)probe.last_failure,
                    (unsigned)probe.last_operation,
                    (unsigned)probe.last_status,
                    (unsigned)probe.failure_stage,
                    (unsigned)probe.failure_result,
                    (unsigned)probe.failure_failure,
                    (unsigned)probe.failure_operation,
                    (unsigned)probe.failure_status,
                    (unsigned)probe.failure_classification,
                    (unsigned)probe.failure_access,
                    (long)probe.failure_deadline_delta_us,
                    (unsigned long)failure_context->queue_delay_us,
                    (long)failure_context->queue_window_us,
                    (unsigned)failure_context->prior_route_timing_valid,
                    (unsigned)failure_context->prior_route_kind,
                    (unsigned)failure_context->prior_route_reason,
                    (unsigned)failure_context->prior_route_result,
                    (long)failure_context->prior_route_start_delta_us,
                    (long)failure_context->prior_route_end_delta_us,
                    (long)failure_context->prior_route_to_failure_delta_us,
                    (unsigned)failure_context->owner_epoch,
                    (unsigned)failure_context->relation_generation,
                    (unsigned)failure_context->transaction_generation,
                    (unsigned)failure_context->request_generation,
                    (unsigned)failure_context->domain,
                    (unsigned)failure_context->detail);
            return true;
        }
        case 4: {
            /* Recency layer, read cold at print time like the news/probe
             * fields above -- `news` printed as `mask` until D2 renamed the
             * value it carries; the fixed diagnostic record does not grow. */
            era_host_peer_storage_recency_snapshot_t recency;
            era_host_peer_storage_get_recency_snapshot(&recency);
            era_host_peer_storage_foundation_snapshot_t arbitration;
            era_host_peer_storage_get_foundation_snapshot(&arbitration);
            uprintf("wire storage recency ok=%u chg=%02X cnt=%u/%u/%u/%u/%u/%u/%u arb=%u/%02X psh=%02X cfl=%02X prv=%02X/%02X cfm=%u\r\n", (unsigned)recency.baseline_record_valid, (unsigned)recency.changed_mask, (unsigned)recency.divergence_counter[0], (unsigned)recency.divergence_counter[1], (unsigned)recency.divergence_counter[2], (unsigned)recency.divergence_counter[3], (unsigned)recency.divergence_counter[4], (unsigned)recency.divergence_counter[5], (unsigned)recency.divergence_counter[6], (unsigned)arbitration.arbitration_flags, (unsigned)arbitration.peer_changed_mask, (unsigned)arbitration.push_pending_mask, (unsigned)arbitration.conflict_pending_mask, (unsigned)arbitration.provisional_changed_mask, (unsigned)arbitration.provisional_cell_mask, (unsigned)arbitration.indicator_round_confirmed);
            return true;
        }
#ifdef ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE
        case 5:
            if (cause_timeline_snapshot == NULL) {
                return false;
            }
            uprintf("wire storage cause role=%u dom=%u gen=%u n=%u ov=%u stale=%u/%u ev=",
                    (unsigned)cause_timeline_snapshot->role,
                    (unsigned)cause_timeline_snapshot->domain,
                    (unsigned)cause_timeline_snapshot->transaction_generation,
                    (unsigned)cause_timeline_snapshot->event_count,
                    (unsigned)cause_timeline_snapshot->overflow,
                    (unsigned)cause_timeline_snapshot->stale_watch_age_ms,
                    (unsigned)cause_timeline_snapshot->stale_limit_ms);
            for (uint8_t index = 0; index < ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_EVENT_CAPACITY; index++) {
                uprintf("%s%02X@%u",
                        index == 0 ? "" : ",",
                        (unsigned)cause_timeline_snapshot->event[index],
                        (unsigned)cause_timeline_snapshot->elapsed_ms[index]);
            }
            uprintf("\r\n");
            return true;
        case 6:
            if (cause_edge_snapshot == NULL) {
                return false;
            }
            uprintf("wire storage edge wr=%lu at=%u/%u gap=%u/%u oq=%u n=%u ov=%u ev=",
                    (unsigned long)cause_edge_snapshot->macro_dirty_count,
                    (unsigned)cause_edge_snapshot->macro_first_elapsed_ms,
                    (unsigned)cause_edge_snapshot->macro_last_elapsed_ms,
                    (unsigned)cause_edge_snapshot->macro_gap_last_ms,
                    (unsigned)cause_edge_snapshot->macro_gap_max_ms,
                    (unsigned)cause_edge_snapshot->macro_gap_over_quiet_count,
                    (unsigned)cause_edge_snapshot->event_count,
                    (unsigned)cause_edge_snapshot->overflow);
            if (cause_edge_snapshot->event_count == 0) {
                uprintf("-");
            }
            for (uint8_t index = 0; index < cause_edge_snapshot->event_count; index++) {
                uprintf("%s%u/%02X/%02X/%u/%02X/%02X/%u@%u",
                        index == 0 ? "" : ",",
                        (unsigned)cause_edge_snapshot->event[index],
                        (unsigned)cause_edge_snapshot->arms[index],
                        (unsigned)cause_edge_snapshot->state[index],
                        (unsigned)cause_edge_snapshot->domain[index],
                        (unsigned)cause_edge_snapshot->dirty_mask[index],
                        (unsigned)cause_edge_snapshot->changed_mask[index],
                        (unsigned)cause_edge_snapshot->transaction_generation[index],
                        (unsigned)cause_edge_snapshot->elapsed_ms[index]);
            }
            uprintf("\r\n");
            return true;
        case 7:
            if (via_macro_snapshot == NULL) {
                return false;
            }
            uprintf("wire via macro rx=%lu rsp=%lu dr=%lu at=%u/%u gap=%u/%u oq=%u h=%lu/%u send=%lu/%u drain=%lu/%u app=%lu/%u ovl=%u int=%u p=%u\r\n",
                    (unsigned long)via_macro_snapshot->receive_count,
                    (unsigned long)via_macro_snapshot->response_count,
                    (unsigned long)via_macro_snapshot->drain_count,
                    (unsigned)via_macro_snapshot->first_receive_elapsed_ms,
                    (unsigned)via_macro_snapshot->last_receive_elapsed_ms,
                    (unsigned)via_macro_snapshot->receive_gap_last_ms,
                    (unsigned)via_macro_snapshot->receive_gap_max_ms,
                    (unsigned)via_macro_snapshot->receive_gap_over_quiet_count,
                    (unsigned long)via_macro_snapshot->handler_total_ms,
                    (unsigned)via_macro_snapshot->handler_max_ms,
                    (unsigned long)via_macro_snapshot->send_total_ms,
                    (unsigned)via_macro_snapshot->send_max_ms,
                    (unsigned long)via_macro_snapshot->drain_total_ms,
                    (unsigned)via_macro_snapshot->drain_max_ms,
                    (unsigned long)via_macro_snapshot->application_total_ms,
                    (unsigned)via_macro_snapshot->application_max_ms,
                    (unsigned)via_macro_snapshot->response_overlap_count,
                    (unsigned)via_macro_snapshot->intervening_raw_count,
                    (unsigned)via_macro_snapshot->response_pending);
            return true;
        case 8:
            if (cause_edge_snapshot == NULL) {
                return false;
            }
            uprintf("wire storage ppath pub=%u/%u/%02X tx=%u/%u rx=%u/%u app=%u/%u\r\n",
                    (unsigned)cause_edge_snapshot->pending_path_rise_ms[ERA_HOST_PEER_STORAGE_CAUSE_PENDING_PLAN_PUBLISH],
                    (unsigned)cause_edge_snapshot->pending_path_fall_ms[ERA_HOST_PEER_STORAGE_CAUSE_PENDING_PLAN_PUBLISH],
                    (unsigned)cause_edge_snapshot->pending_publish_fall_context,
                    (unsigned)cause_edge_snapshot->pending_path_rise_ms[ERA_HOST_PEER_STORAGE_CAUSE_PENDING_INITIATOR_TX],
                    (unsigned)cause_edge_snapshot->pending_path_fall_ms[ERA_HOST_PEER_STORAGE_CAUSE_PENDING_INITIATOR_TX],
                    (unsigned)cause_edge_snapshot->pending_path_rise_ms[ERA_HOST_PEER_STORAGE_CAUSE_PENDING_RESPONDER_RX],
                    (unsigned)cause_edge_snapshot->pending_path_fall_ms[ERA_HOST_PEER_STORAGE_CAUSE_PENDING_RESPONDER_RX],
                    (unsigned)cause_edge_snapshot->pending_path_rise_ms[ERA_HOST_PEER_STORAGE_CAUSE_PENDING_MIRROR_APPLY],
                    (unsigned)cause_edge_snapshot->pending_path_fall_ms[ERA_HOST_PEER_STORAGE_CAUSE_PENDING_MIRROR_APPLY]);
            return true;
#endif
        default:
            return false;
    }
}
#endif

static void era_split_wire_diagnostics_flush(void) {
#ifdef CONSOLE_ENABLE
    extern void console_task(void);
    console_task();
#endif
}

static uint32_t era_split_wire_diagnostics_raw_scan_hz(uint32_t now_ms, uint32_t scan_count) {
    if (!era_split_wire_diagnostics_state.raw_matrix_scan_rate_valid) {
        era_split_wire_diagnostics_state.raw_matrix_scan_rate_valid = true;
        era_split_wire_diagnostics_state.raw_matrix_scan_rate_ms    = now_ms;
        era_split_wire_diagnostics_state.raw_matrix_scan_rate_count = scan_count;
        return 0;
    }

    uint32_t elapsed_ms = now_ms - era_split_wire_diagnostics_state.raw_matrix_scan_rate_ms;
    uint32_t delta      = scan_count - era_split_wire_diagnostics_state.raw_matrix_scan_rate_count;

    era_split_wire_diagnostics_state.raw_matrix_scan_rate_ms    = now_ms;
    era_split_wire_diagnostics_state.raw_matrix_scan_rate_count = scan_count;

    if (elapsed_ms == 0) {
        return 0;
    }
    return (uint32_t)(((uint64_t)delta * 1000ULL + elapsed_ms / 2U) / elapsed_ms);
}

static uint32_t era_split_wire_diagnostics_rate_hz_from_delta(uint32_t elapsed_ms, uint32_t current, uint32_t previous) {
    if (elapsed_ms == 0) {
        return 0;
    }
    return (uint32_t)(((uint64_t)(current - previous) * 1000ULL + elapsed_ms / 2U) / elapsed_ms);
}

static void era_split_wire_diagnostics_get_snapshot(era_split_wire_diagnostics_snapshot_t *snapshot) {
    if (snapshot == NULL) {
        return;
    }

    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->print_count              = era_split_wire_diagnostics_state.print_count;
    snapshot->last_print_ms            = era_split_wire_diagnostics_state.last_print_ms;
    snapshot->raw_matrix_scan_count    = era_split_wire_diagnostics_raw_matrix_scan_count;
    snapshot->raw_matrix_read_us_total = era_split_wire_diagnostics_state.raw_matrix_read_us_total;
    snapshot->raw_matrix_read_us_max   = era_split_wire_diagnostics_state.raw_matrix_read_us_max;

    era_split_transport_scheduler_get_diagnostics_snapshot(&snapshot->scheduler);
}

static void era_split_wire_diagnostics_update_host_peer_rates(uint32_t now_ms, era_split_wire_diagnostics_snapshot_t *snapshot) {
    if (snapshot == NULL) {
        return;
    }
    if (!era_split_wire_diagnostics_state.host_peer_rate_valid) {
        era_split_wire_diagnostics_state.host_peer_rate_valid                = true;
        era_split_wire_diagnostics_state.host_peer_rate_ms                   = now_ms;
        era_split_wire_diagnostics_state.host_peer_rate_source_push_tx_count = snapshot->scheduler.host_peer_source_push_tx_count;
        era_split_wire_diagnostics_state.host_peer_rate_ack_status_tx_count  = snapshot->scheduler.host_peer_ack_status_tx_count;
        return;
    }

    uint32_t elapsed_ms                = now_ms - era_split_wire_diagnostics_state.host_peer_rate_ms;
    snapshot->host_peer_source_push_hz = era_split_wire_diagnostics_rate_hz_from_delta(elapsed_ms, snapshot->scheduler.host_peer_source_push_tx_count, era_split_wire_diagnostics_state.host_peer_rate_source_push_tx_count);
    snapshot->host_peer_ack_status_hz  = era_split_wire_diagnostics_rate_hz_from_delta(elapsed_ms, snapshot->scheduler.host_peer_ack_status_tx_count, era_split_wire_diagnostics_state.host_peer_rate_ack_status_tx_count);

    era_split_wire_diagnostics_state.host_peer_rate_ms                   = now_ms;
    era_split_wire_diagnostics_state.host_peer_rate_source_push_tx_count = snapshot->scheduler.host_peer_source_push_tx_count;
    era_split_wire_diagnostics_state.host_peer_rate_ack_status_tx_count  = snapshot->scheduler.host_peer_ack_status_tx_count;
}

#ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
static uint32_t era_split_wire_diagnostics_avg_us(uint32_t total_us, uint32_t count) {
    if (count == 0) {
        return 0;
    }
    return (uint32_t)(((uint64_t)total_us + count / 2U) / count);
}

static bool era_split_wire_diagnostics_print_transaction_timing_bucket(const char *label, const era_split_transaction_timing_bucket_diagnostics_t *bucket) {
    if (label == NULL || bucket == NULL) {
        return false;
    }

    uprintf("wire hp peer_tavg %s n=%lu to=%lu tx=%lu wait=%lu rx=%lu total=%lu last=%lu\r\n",
            label,
            (unsigned long)bucket->sample_count,
            (unsigned long)bucket->timeout_count,
            (unsigned long)era_split_wire_diagnostics_avg_us(bucket->tx_us_total, bucket->sample_count),
            (unsigned long)era_split_wire_diagnostics_avg_us(bucket->wait_rx_us_total, bucket->sample_count),
            (unsigned long)era_split_wire_diagnostics_avg_us(bucket->rx_decode_us_total, bucket->sample_count),
            (unsigned long)era_split_wire_diagnostics_avg_us(bucket->total_us_total, bucket->sample_count),
            (unsigned long)bucket->total_us_last);
    return true;
}

#endif

static bool era_split_wire_diagnostics_communication_core_probe_allowed(void) {
    era_split_transport_scheduler_get_diagnostics_snapshot(&era_split_wire_diagnostics_state.wire_snapshot.scheduler);
    return era_split_wire_diagnostics_state.wire_snapshot.scheduler.relation != ERA_SPLIT_MODE_HOST_PEER_HOST;
}

static void era_split_wire_diagnostics_communication_core_on_diag(void) {
    if (!era_split_wire_diagnostics_communication_core_probe_allowed()) {
        return;
    }
    era_split_communication_core_get_diagnostics_snapshot(&era_split_wire_diagnostics_state.communication_core_snapshot);

    if (!era_split_wire_diagnostics_state.communication_core_snapshot.running || era_split_wire_diagnostics_state.communication_core_snapshot.stop_requested) {
        (void)era_split_communication_core_start();
        return;
    }

    /* No diagnostic-only wake here: CORE1_FULL publishes before it idles, so a
       wake would only race the capture. The arm that called
       era_split_communication_core_wake() selected on a pre-FULL stage, and
       era_split_qmk_rules.mk admits no such stage -- it was unreachable in
       every buildable configuration. */
}

static bool era_split_wire_diagnostics_print_communication_core_line(const era_split_communication_core_diagnostics_t *snapshot, uint8_t line) {
    if (snapshot == NULL) {
        return false;
    }

    const era_split_communication_core_diagnostics_t *diag = snapshot;

    switch (line) {
        case 0:
            uprintf("wire ccore av=%u init=%u att=%u launch=%u run=%u stop=%u wake=%u err=%u/%u/%u eto=%u sto=%u cnt=%lu/%lu/%lu/%lu loop=%lu idle=%lu ec=%lu/%lu/%lu cap=%u dead=%lu park=%lu/%lu idleus=%lu\r\n",
                    (unsigned)diag->available,
                    (unsigned)diag->initialized,
                    (unsigned)diag->launch_attempted,
                    (unsigned)diag->launched,
                    (unsigned)diag->running,
                    (unsigned)diag->stop_requested,
                    (unsigned)diag->wake_pending,
                    (unsigned)diag->launch_error,
                    (unsigned)diag->launch_error_stage,
                    (unsigned)diag->launch_error_phase,
                    (unsigned)diag->entry_timeout,
                    (unsigned)diag->stop_timeout,
                    (unsigned long)diag->start_count,
                    (unsigned long)diag->stop_count,
                    (unsigned long)diag->wake_count,
                    (unsigned long)diag->wake_observed_count,
                    (unsigned long)diag->loop_count,
                    (unsigned long)diag->idle_count,
                    (unsigned long)diag->launch_error_count,
                    (unsigned long)diag->entry_timeout_count,
                    (unsigned long)diag->stop_timeout_count,
                    (unsigned)diag->launch_capped,
                    (unsigned long)diag->dead_declared_count,
                    (unsigned long)diag->park_count,
                    (unsigned long)diag->park_us,
                    (unsigned long)diag->idle_us);
            return true;
        case 1: {
            uprintf("wire cqueue av=%u gen=%u cap=%u q=%u/%u r=%u/%u flush=%lu\r\n",
                    (unsigned)diag->queue_available,
                    (unsigned)diag->queue_generation,
                    (unsigned)diag->queue_capacity,
                    (unsigned)diag->queue_level,
                    (unsigned)diag->queue_high_water,
                    (unsigned)diag->queue_result_level,
                    (unsigned)diag->queue_result_high_water,
                    (unsigned long)diag->queue_flush_count);
            return true;
        }
        case 2:
            uprintf("wire cown av=%u own=%u role=%u epoch=%u/%u/%u rev=%u/%u/%u cnt=%lu/%lu/%lu/%lu err=%lu/%lu/%lu/%lu io=%lu/%lu/%lu/%lu/%lu/%lu/%lu/%lu/%lu/%lu/%lu/%lu\r\n",
                    (unsigned)diag->backend_owner_available,
                    (unsigned)diag->backend_owner,
                    (unsigned)diag->backend_owner_role,
                    (unsigned)diag->backend_owner_epoch,
                    (unsigned)diag->backend_owner_released_epoch,
                    (unsigned)diag->backend_owner_ready_epoch,
                    (unsigned)diag->backend_owner_revoke_pending,
                    (unsigned)diag->backend_owner_cancel_pending,
                    (unsigned)diag->backend_owner_reset_pending,
                    (unsigned long)diag->backend_owner_transfer_count,
                    (unsigned long)diag->backend_owner_revoke_count,
                    (unsigned long)diag->backend_owner_release_count,
                    (unsigned long)diag->backend_owner_ready_count,
                    (unsigned long)diag->backend_owner_transfer_timeout_count,
                    (unsigned long)diag->backend_owner_ready_timeout_count,
                    (unsigned long)diag->backend_owner_init_fail_count,
                    (unsigned long)diag->backend_owner_reclaim_count,
                    (unsigned long)diag->initiator_queue_expired_count,
                    (unsigned long)diag->initiator_owner_count,
                    (unsigned long)diag->initiator_epoch_count,
                    (unsigned long)diag->initiator_cancel_count,
                    (unsigned long)diag->initiator_reset_count,
                    (unsigned long)diag->initiator_pio_error_count,
                    (unsigned long)diag->initiator_send_timeout_count,
                    (unsigned long)diag->initiator_response_timeout_count,
                    (unsigned long)diag->initiator_partial_frame_count,
                    (unsigned long)diag->initiator_io_count,
                    (unsigned long)diag->initiator_decode_count,
                    (unsigned long)diag->initiator_response_contract_count);
            return true;
        case 3:
            uprintf("wire csess av=%u pend=%u ready=%u gen=%u/%u sub=%lu/%lu full=%lu sf=%lu tx=%lu res=%lu stale=%lu rfull=%lu ok=%lu/%lu/%lu/%lu last=%u/%u/%u/%u/%u rsp=%u peer=%u\r\n",
                    (unsigned)diag->lane[ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SESSION_STATUS].available,
                    (unsigned)diag->lane[ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SESSION_STATUS].pending,
                    (unsigned)diag->lane[ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SESSION_STATUS].result_ready,
                    (unsigned)diag->lane[ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SESSION_STATUS].generation,
                    (unsigned)diag->lane[ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SESSION_STATUS].last_result_generation,
                    (unsigned long)diag->lane[ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SESSION_STATUS].submit_count,
                    (unsigned long)diag->lane[ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SESSION_STATUS].accept_count,
                    (unsigned long)diag->lane[ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SESSION_STATUS].full_count,
                    (unsigned long)diag->lane[ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SESSION_STATUS].start_fail_count,
                    (unsigned long)diag->lane[ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SESSION_STATUS].transaction_count,
                    (unsigned long)diag->lane[ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SESSION_STATUS].result_count,
                    (unsigned long)diag->lane[ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SESSION_STATUS].result_stale_count,
                    (unsigned long)diag->lane[ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SESSION_STATUS].result_full_count,
                    (unsigned long)diag->lane[ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SESSION_STATUS].ok_count,
                    (unsigned long)diag->lane[ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SESSION_STATUS].miss_count,
                    (unsigned long)diag->lane[ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SESSION_STATUS].bad_count,
                    (unsigned long)diag->lane[ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SESSION_STATUS].fail_count,
                    (unsigned)diag->lane[ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SESSION_STATUS].last_result,
                    (unsigned)diag->session_last_failure,
                    (unsigned)diag->lane[ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SESSION_STATUS].last_request_sent,
                    (unsigned)diag->lane[ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SESSION_STATUS].last_route_kind,
                    (unsigned)diag->lane[ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SESSION_STATUS].last_route_reason,
                    (unsigned)diag->lane[ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SESSION_STATUS].last_response_kind,
                    (unsigned)diag->session_last_peer_status_valid);
            return true;
        case 4:
            uprintf("wire csp av=%u pend=%u ready=%u gen=%u/%u sub=%lu/%lu full=%lu sf=%lu tx=%lu res=%lu stale=%lu rfull=%lu ok=%lu/%lu/%lu/%lu last=%u/%u/%u/%u seq=%u rsp=%u/%u/%02X lock=%u/%u vis=%u rgb=%u news=%u/%02X hsrsp=%lu visn=%lu rgbn=%lu newsn=%lu secor=%02X rxm=%u\r\n",
                    (unsigned)diag->lane[ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SOURCE_PUSH].available,
                    (unsigned)diag->lane[ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SOURCE_PUSH].pending,
                    (unsigned)diag->lane[ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SOURCE_PUSH].result_ready,
                    (unsigned)diag->lane[ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SOURCE_PUSH].generation,
                    (unsigned)diag->lane[ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SOURCE_PUSH].last_result_generation,
                    (unsigned long)diag->lane[ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SOURCE_PUSH].submit_count,
                    (unsigned long)diag->lane[ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SOURCE_PUSH].accept_count,
                    (unsigned long)diag->lane[ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SOURCE_PUSH].full_count,
                    (unsigned long)diag->lane[ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SOURCE_PUSH].start_fail_count,
                    (unsigned long)diag->lane[ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SOURCE_PUSH].transaction_count,
                    (unsigned long)diag->lane[ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SOURCE_PUSH].result_count,
                    (unsigned long)diag->lane[ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SOURCE_PUSH].result_stale_count,
                    (unsigned long)diag->lane[ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SOURCE_PUSH].result_full_count,
                    (unsigned long)diag->lane[ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SOURCE_PUSH].ok_count,
                    (unsigned long)diag->lane[ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SOURCE_PUSH].miss_count,
                    (unsigned long)diag->lane[ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SOURCE_PUSH].bad_count,
                    (unsigned long)diag->lane[ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SOURCE_PUSH].fail_count,
                    (unsigned)diag->lane[ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SOURCE_PUSH].last_result,
                    (unsigned)diag->lane[ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SOURCE_PUSH].last_request_sent,
                    (unsigned)diag->lane[ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SOURCE_PUSH].last_route_kind,
                    (unsigned)diag->lane[ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SOURCE_PUSH].last_route_reason,
                    (unsigned)diag->source_push_last_matrix_seq,
                    (unsigned)diag->lane[ERA_SPLIT_COMMUNICATION_CORE_INITIATOR_LANE_SOURCE_PUSH].last_response_kind,
                    (unsigned)diag->source_push_last_response_payload_len,
                    (unsigned)diag->source_push_last_response_section_byte,
                    (unsigned)diag->source_push_last_lock_state_valid,
                    (unsigned)diag->source_push_last_lock_state,
                    (unsigned)diag->source_push_last_visual_snapshot_valid,
                    (unsigned)diag->source_push_last_rgb_state_valid,
                    (unsigned)diag->source_push_last_storage_news_valid,
                    (unsigned)diag->source_push_last_storage_news,
                    (unsigned long)diag->source_push_hsrsp_count,
                    (unsigned long)diag->source_push_visual_snapshot_count,
                    (unsigned long)diag->source_push_rgb_state_count,
                    (unsigned long)diag->source_push_storage_news_count,
                    (unsigned)diag->source_push_response_section_or,
                    (unsigned)diag->source_push_rx_wait_mode);
            return true;
        case ERA_SPLIT_WIRE_DIAGNOSTICS_COMMUNICATION_CORE_BASE_LINES:
            uprintf("wire crsp av=%u src=%u snap=%u slot=%u ready=%u own=%u/%lu/%lu gen=%u/%u/%u/%u pub=%lu resv=%lu full=%lu acc=%lu stale=%lu drain=%lu noack=%lu arx=%lu undec=%lu quiet=%lu coal=%lu/%lu prep=%lu/%lu last=%u/%02X/%02X plen=%u vr=%u ahold=%lu/%lu\r\n",
                    (unsigned)diag->responder_available,
                    (unsigned)diag->responder_snapshot_source,
                    (unsigned)diag->responder_snapshot_valid,
                    (unsigned)diag->responder_source_push_slot_reserved,
                    (unsigned)diag->responder_source_push_result_ready,
                    (unsigned)diag->responder_owner_gate_ready,
                    (unsigned long)diag->responder_owner_gate_ready_count,
                    (unsigned long)diag->responder_owner_gate_block_count,
                    (unsigned)diag->responder_owner_epoch,
                    (unsigned)diag->responder_relation_generation,
                    (unsigned)diag->responder_snapshot_generation,
                    (unsigned)diag->responder_last_result_generation,
                    (unsigned long)diag->responder_snapshot_publish_count,
                    (unsigned long)diag->responder_slot_reserve_count,
                    (unsigned long)diag->responder_slot_full_count,
                    (unsigned long)diag->responder_accept_count,
                    (unsigned long)diag->responder_accept_stale_count,
                    (unsigned long)diag->responder_drain_count,
                    (unsigned long)diag->responder_noack_count,
                    (unsigned long)diag->responder_accepted_rx_count,
                    (unsigned long)diag->responder_undecodable_rx_count,
                    (unsigned long)diag->responder_quiet_count,
                    (unsigned long)diag->responder_coalesced_heartbeat_count,
                    (unsigned long)diag->responder_coalesced_runtime_section_count,
                    (unsigned long)diag->responder_response_prepare_count,
                    (unsigned long)diag->responder_response_prepare_fail_count,
                    (unsigned)diag->responder_last_matrix_seq,
                    (unsigned)diag->responder_last_request_kind_flags,
                    (unsigned)diag->responder_last_response_section_mask,
                    (unsigned)diag->responder_last_response_payload_len,
                    (unsigned)diag->responder_last_visual_reason,
                    (unsigned long)diag->responder_anchor_hold_last_us,
                    (unsigned long)diag->responder_anchor_hold_max_us);
            return true;
        default:
            return false;
    }
}

static bool era_split_wire_diagnostics_print_scheduler_line(const era_split_wire_diagnostics_snapshot_t *snapshot, uint8_t line) {
    const era_split_wire_diagnostics_snapshot_t *wire = snapshot;
    const era_split_transport_scheduler_diagnostics_snapshot_t *diag = &snapshot->scheduler;

    switch (line) {
        case 0:
            uprintf("wire pc=%lu last=%lu sch=%u bem=%u/%lu be=%u/%u/%u bri=%u rel=%u prev=%u\r\n", (unsigned long)wire->print_count, (unsigned long)wire->last_print_ms, (unsigned)diag->scheduler_enabled, (unsigned)diag->transaction_engine_diagnostics_fresh, (unsigned long)diag->transaction_engine_diagnostics_fallback_count, (unsigned)diag->io.transaction_backend_initialized, (unsigned)diag->io.transaction_backend_init_role, (unsigned)diag->io.transaction_backend_role, (unsigned)diag->io.transaction_backend_reinit_on_role_change, (unsigned)diag->relation, (unsigned)diag->previous_relation);
            return true;
        case 1:
            uprintf("wire qmk scan_hz=%lu raw=%lu raw_us=%lu raw_max=%lu smp_hz=%lu ovr=%lu rearm=%lu fd1=%X fw=%u\r\n", (unsigned long)wire->raw_matrix_scan_hz, (unsigned long)wire->raw_matrix_scan_count, (unsigned long)wire->raw_matrix_read_us_total, (unsigned long)wire->raw_matrix_read_us_max, (unsigned long)era_split_wire_diagnostics_state.pio_sample_hz, (unsigned long)era_split_wire_diagnostics_state.pio_snapshot.torn_retries, (unsigned long)era_split_wire_diagnostics_state.pio_snapshot.rearms, (unsigned)era_split_wire_diagnostics_state.pio_snapshot.fdebug, (unsigned)era_split_wire_diagnostics_state.pio_snapshot.frame_words);
            return true;
        case 2:
            uprintf("wire sched initwork=%lu hkwork=%lu plan=%lu dirty=%02X due=%02X open_ms=%u/%u\r\n", (unsigned long)diag->scheduler_init_call_count, (unsigned long)diag->scheduler_housekeeping_task_count, (unsigned long)diag->scheduler_plan_count, (unsigned)diag->scheduler_dirty_flags, (unsigned)diag->scheduler_route_due_flags, (unsigned)diag->communication_core_start_entry_ms, (unsigned)diag->communication_core_start_exit_ms);
            return true;
        case 3:
            uprintf("wire route step=%lu owner=%lu/%u/%u\r\n", (unsigned long)diag->transport_step_call_count, (unsigned long)diag->owner_step_count, (unsigned)diag->owner_route_kind, (unsigned)diag->owner_route_reason);
            return true;
        case 4:
            uprintf("wire resp th=%u svc=%u blk=%u io=%u trn=%u rst=%u qsc=%u rx=%lu srq=%lu stx=%lu ign=%lu\r\n", (unsigned)diag->responder_thread_started, (unsigned)diag->responder_service_enabled, (unsigned)diag->responder_admission_blocked, (unsigned)diag->responder_in_serial_io, (unsigned)diag->responder_in_transaction, (unsigned)diag->responder_reset_requested, (unsigned)diag->responder_quiesced, (unsigned long)diag->responder_frame_rx_count, (unsigned long)diag->responder_session_request_rx_count, (unsigned long)diag->responder_session_response_tx_count, (unsigned long)diag->responder_ignored_frame_count);
            return true;
        case 5:
            uprintf("wire sess pk=%u pho=%u pno=%u pmr=%u pb=%u pnews=%02X pe=%u pog=%u pcg=%u stx=%lu srx=%lu sfg=%lu pst=%u io=%lu/%lu/%lu/%lu/%lu\r\n", (unsigned)diag->peer_session_known, (unsigned)diag->peer_accepted_host_open, (unsigned)diag->peer_accepted_no_host, (unsigned)diag->peer_matrix_ready, (unsigned)diag->peer_bulk_page_supported, (unsigned)diag->peer_storage_news_observed, (unsigned)diag->peer_usb_epoch, (unsigned)diag->peer_host_open_generation, (unsigned)diag->peer_host_close_generation, (unsigned long)diag->local_session_tx_count, (unsigned long)diag->peer_session_rx_count, (unsigned long)diag->peer_session_forget_count, (unsigned)diag->peer_session_stale_pending, (unsigned long)diag->io.compact_tx_count, (unsigned long)diag->io.compact_rx_count, (unsigned long)diag->io.compact_miss_count, (unsigned long)diag->io.compact_bad_count, (unsigned long)diag->io.compact_fail_count);
            return true;
        case 6:
            if (diag->relation == ERA_SPLIT_MODE_HOST_PEER_HOST) {
                uprintf("wire hp role=host pc=%u pseq=%u ack=%lu/%lu vis=%lu rgb=%lu cache=%lu/%lu/%lu rrx=%lu hrx=%lu srx=%lu\r\n", (unsigned)diag->host_peer_peer_cache_valid, (unsigned)diag->host_peer_peer_matrix_seq8, (unsigned long)wire->host_peer_ack_status_hz, (unsigned long)diag->host_peer_ack_status_tx_count, (unsigned long)diag->responder_host_peer_visual_snapshot_tx_count, (unsigned long)diag->responder_host_peer_rgb_state_tx_count, (unsigned long)diag->host_peer_peer_cache_update_count, (unsigned long)diag->host_peer_peer_cache_project_count, (unsigned long)diag->host_peer_peer_cache_flush_count, (unsigned long)diag->responder_relation_request_rx_count, (unsigned long)diag->responder_host_peer_heartbeat_rx_count, (unsigned long)diag->responder_host_peer_source_push_rx_count);
                return true;
            }
            if (diag->relation == ERA_SPLIT_MODE_HOST_PEER_PEER) {
                uprintf("wire hp role=peer lmr=%u f=%u seq=%u/%u sp=%lu/%lu/%lu vis=%lu rgb=%lu\r\n", (unsigned)diag->host_peer_local_matrix_ready, (unsigned)diag->host_peer_source_push_forced, (unsigned)diag->host_peer_local_current_seq8, (unsigned)diag->host_peer_local_host_known_seq8, (unsigned long)wire->host_peer_source_push_hz, (unsigned long)diag->host_peer_source_push_tx_count, (unsigned long)diag->host_peer_source_push_ack_count, (unsigned long)diag->host_peer_visual_snapshot_rx_count, (unsigned long)diag->host_peer_rgb_state_rx_count);
                return true;
            }
            uprintf("wire hp role=off rel=%u lmr=%u f=%u pc=%u seq=%u/%u/%u cache=%lu/%lu/%lu\r\n", (unsigned)diag->relation, (unsigned)diag->host_peer_local_matrix_ready, (unsigned)diag->host_peer_source_push_forced, (unsigned)diag->host_peer_peer_cache_valid, (unsigned)diag->host_peer_local_current_seq8, (unsigned)diag->host_peer_local_host_known_seq8, (unsigned)diag->host_peer_peer_matrix_seq8, (unsigned long)diag->host_peer_peer_cache_update_count, (unsigned long)diag->host_peer_peer_cache_project_count, (unsigned long)diag->host_peer_peer_cache_flush_count);
            return true;
        case 7:
            uprintf("wire hp peer_era valid=%u mode=%u sp=%lu/%lu seq=%u/%u io=%lu/%lu/%lu/%lu/%lu meas=%u\r\n", (unsigned)diag->host_peer_peer_era.valid, (unsigned)diag->host_peer_peer_era.last_mode, (unsigned long)diag->host_peer_peer_era_source_push_tx_count, (unsigned long)diag->host_peer_peer_era_source_push_ack_count, (unsigned)diag->host_peer_peer_era_local_current_seq8, (unsigned)diag->host_peer_peer_era_local_host_known_seq8, (unsigned long)diag->host_peer_peer_era.compact_tx_count, (unsigned long)diag->host_peer_peer_era.compact_rx_count, (unsigned long)diag->host_peer_peer_era.compact_miss_count, (unsigned long)diag->host_peer_peer_era.compact_bad_count, (unsigned long)diag->host_peer_peer_era.compact_fail_count, (unsigned)diag->host_peer_peer_era.measured);
            return true;
        case 8:
            uprintf("wire hp peer_txrx n=%lu to=%lu valid=%u rk=%u rr=%u req=%u/%u rsp=%u/%u sec=%02X proxy=%u res=%u tx_us=%lu wait_rx_us=%lu rx_decode_us=%lu total_us=%lu\r\n", (unsigned long)diag->host_peer_peer_era_timing_sample_count, (unsigned long)diag->host_peer_peer_era_timing_timeout_count, (unsigned)diag->host_peer_peer_era_timing.valid, (unsigned)diag->host_peer_peer_era_timing.route_kind, (unsigned)diag->host_peer_peer_era_timing.route_reason, (unsigned)diag->host_peer_peer_era_timing.request_kind, (unsigned)diag->host_peer_peer_era_timing.request_payload_len, (unsigned)diag->host_peer_peer_era_timing.response_kind, (unsigned)diag->host_peer_peer_era_timing.response_payload_len, (unsigned)diag->host_peer_peer_era_timing.response_section_byte, (unsigned)diag->host_peer_peer_era_timing.payload_proxy, (unsigned)diag->host_peer_peer_era_timing.result, (unsigned long)diag->host_peer_peer_era_timing.tx_us, (unsigned long)diag->host_peer_peer_era_timing.wait_rx_us, (unsigned long)diag->host_peer_peer_era_timing.rx_decode_us, (unsigned long)diag->host_peer_peer_era_timing.total_us);
            return true;
        case 9:
            return era_split_wire_diagnostics_print_transaction_timing_bucket("hb", &diag->host_peer_peer_era_timing_buckets[ERA_SPLIT_TRANSACTION_TIMING_BUCKET_HEARTBEAT_ACK]);
        case 10:
            return era_split_wire_diagnostics_print_transaction_timing_bucket("sp", &diag->host_peer_peer_era_timing_buckets[ERA_SPLIT_TRANSACTION_TIMING_BUCKET_SOURCE_PUSH_ACK]);
        case 11:
            return era_split_wire_diagnostics_print_transaction_timing_bucket("hs", &diag->host_peer_peer_era_timing_buckets[ERA_SPLIT_TRANSACTION_TIMING_BUCKET_HOST_SOURCE_RSP]);
        case 12:
            return era_split_wire_diagnostics_print_transaction_timing_bucket("px", &diag->host_peer_peer_era_timing_buckets[ERA_SPLIT_TRANSACTION_TIMING_BUCKET_PROXY]);
        case 13:
            uprintf("wire hp host_era valid=%u mode=%u pc=%u pseq=%u ack=%lu cache=%lu/%lu/%lu rrx=%lu hrx=%lu srx=%lu io=%lu/%lu/%lu/%lu/%lu meas=%u\r\n", (unsigned)diag->host_peer_host_era.valid, (unsigned)diag->host_peer_host_era.last_mode, (unsigned)diag->host_peer_host_era_peer_cache_valid, (unsigned)diag->host_peer_host_era_peer_matrix_seq8, (unsigned long)diag->host_peer_host_era_ack_status_tx_count, (unsigned long)diag->host_peer_host_era_peer_cache_update_count, (unsigned long)diag->host_peer_host_era_peer_cache_project_count, (unsigned long)diag->host_peer_host_era_peer_cache_flush_count, (unsigned long)diag->host_peer_host_era_responder_relation_request_rx_count, (unsigned long)diag->host_peer_host_era_responder_host_peer_heartbeat_rx_count, (unsigned long)diag->host_peer_host_era_responder_host_peer_source_push_rx_count, (unsigned long)diag->host_peer_host_era.compact_tx_count, (unsigned long)diag->host_peer_host_era.compact_rx_count, (unsigned long)diag->host_peer_host_era.compact_miss_count,
                    (unsigned long)diag->host_peer_host_era.compact_bad_count, (unsigned long)diag->host_peer_host_era.compact_fail_count, (unsigned)diag->host_peer_host_era.measured);
            return true;
        case 14:
            uprintf("wire txrx n=%lu to=%lu valid=%u rk=%u rr=%u req=%u/%u rsp=%u/%u sec=%02X proxy=%u res=%u tx_us=%lu wait_rx_us=%lu rx_decode_us=%lu total_us=%lu\r\n", (unsigned long)diag->io.timing_sample_count, (unsigned long)diag->io.timing_timeout_count, (unsigned)diag->io.timing.valid, (unsigned)diag->io.timing.route_kind, (unsigned)diag->io.timing.route_reason, (unsigned)diag->io.timing.request_kind, (unsigned)diag->io.timing.request_payload_len, (unsigned)diag->io.timing.response_kind, (unsigned)diag->io.timing.response_payload_len, (unsigned)diag->io.timing.response_section_byte, (unsigned)diag->io.timing.payload_proxy, (unsigned)diag->io.timing.result, (unsigned long)diag->io.timing.tx_us, (unsigned long)diag->io.timing.wait_rx_us, (unsigned long)diag->io.timing.rx_decode_us, (unsigned long)diag->io.timing.total_us);
            return true;
        case 15:
            uprintf("wire edge reset dtap_ms=%u\r\n", (unsigned)rp2040_bootloader_double_tap_reset_window_ms());
            return true;
        case 16:
            {
                era_nvm_diagnostics_t nvm;
                era_eeprom_driver_get_nvm_diagnostics(&nvm);
                uprintf("wire nvm pg=%lu pgfail=%lu er=%lu erfail=%lu\r\n",
                        (unsigned long)nvm.program_count,
                        (unsigned long)nvm.program_failure_count,
                        (unsigned long)nvm.erase_count,
                        (unsigned long)nvm.erase_failure_count);
            }
            return true;
        case 17:
            uprintf("wire dh era valid=%u mode=%u io=%lu/%lu/%lu/%lu/%lu stor=%lu/%lu/%lu rt=%lu/%lu meas=%u\r\n", (unsigned)diag->dual_host_era.valid, (unsigned)diag->dual_host_era.last_mode, (unsigned long)diag->dual_host_era.compact_tx_count, (unsigned long)diag->dual_host_era.compact_rx_count, (unsigned long)diag->dual_host_era.compact_miss_count, (unsigned long)diag->dual_host_era.compact_bad_count, (unsigned long)diag->dual_host_era.compact_fail_count, (unsigned long)diag->dual_host_era_storage_open_count, (unsigned long)diag->dual_host_era_storage_close_count, (unsigned long)diag->dual_host_era_storage_transfer_count, (unsigned long)diag->dual_host_era_runtime_tx_count, (unsigned long)diag->dual_host_era_runtime_rx_count, (unsigned)diag->dual_host_era.measured);
            return true;
        case 18: {
            /* Appended rather than slotted beside `sched`, because one paced
               slot is one line: two uprintf in a slot double that slot's burst
               against CONSOLE_IN_CAPACITY. Read cold at print time, so the
               fixed diagnostic record does not grow for a question about one
               design decision.

               `entry` counts task bodies that passed the due gate, so
               `entry - hkwork` is the passes that woke core0 and found nothing
               to do, and the seven columns say which contributor asked for the
               rest. The question is whether core0's periodic wake belongs to
               the SESSION_STATUS lane or to the 5 ms authority-poll deadline
               that gates the body -- `hkwork` alone cannot tell them apart. */
            uint32_t maint_entry = 0;
            uint32_t maint[ERA_SPLIT_SCHEDULER_MAINT_SOURCE_COUNT];
            memset(maint, 0, sizeof(maint));
            era_split_transport_scheduler_get_maintenance_source_counts(&maint_entry, maint);
            uprintf("wire maint entry=%lu stor=%lu rsp=%lu stand=%lu init=%lu time=%lu mode=%lu route=%lu rsnp=%lu\r\n", (unsigned long)maint_entry, (unsigned long)maint[0], (unsigned long)maint[1], (unsigned long)maint[2], (unsigned long)maint[3], (unsigned long)maint[4], (unsigned long)maint[5], (unsigned long)maint[6], (unsigned long)era_split_transport_scheduler_get_responder_snapshot_retry_count());
            return true;
        }
        case 19: {
            /* Appended last under the same rule as `wire dh era` and
               `wire maint`, and read cold at print time for the same reason:
               the fixed diagnostic record does not grow for two questions R2's
               owed legs ask once each.

               **Both fields exist because the half that produces them cannot
               print them.** A HOST-PEER PEER has no open USB host, so it has no
               console, so `wire hp role=peer` -- the only line carrying that
               half's apply counters -- never reaches anybody. Everything below
               is cumulative and free-running so that a capture taken after
               promotion still carries the whole PEER era.

               `sect` is the runtime section pair, tx then rx, counted in
               sections and not in polls exactly as `wire dh era rt=` is. It is
               the same underlying pair; the difference is the unit of time.
               `rt=` is era-scoped and already differenced by the capture, and
               its block accumulates only in DUAL-HOST, so before this line the
               gates' "read it from `io` and `rt`" named nothing a HOST-PEER
               capture prints. This one is cumulative from boot in every
               relation, so a rate is a delta over the `last` delta like every
               other cumulative counter here.

               `sync` is the shared clock this half is running on, printed
               beside `wire pc last=`, which is its local one. On a half that
               never applies an anchor the two track; the difference is the
               applied offset. `anc` is the watch that a sample cannot be:
               applies, backwards steps, and the worst backwards step in ms. */
            uint32_t section_tx = 0;
            uint32_t section_rx = 0;
            era_split_transport_scheduler_get_dual_runtime_counts(&section_tx, &section_rx);
            uint32_t anchor_applies  = 0;
            uint32_t anchor_back     = 0;
            uint32_t anchor_back_max = 0;
            int32_t  anchor_corr_ms  = 0;
            uint32_t anchor_ival_ms  = 0;
            era_host_peer_transaction_get_time_anchor_diagnostics(&anchor_applies, &anchor_back, &anchor_back_max);
            era_host_peer_transaction_get_time_anchor_refresh_diagnostics(&anchor_corr_ms, &anchor_ival_ms);
            uprintf("wire sect tx=%lu rx=%lu sync=%lu anc=%lu/%lu/%lu corr=%ld/%lu app=%lu/%lu/%lu lay=%lu\r\n", (unsigned long)section_tx, (unsigned long)section_rx, (unsigned long)sync_timer_read32(), (unsigned long)anchor_applies, (unsigned long)anchor_back, (unsigned long)anchor_back_max, (long)anchor_corr_ms, (unsigned long)anchor_ival_ms, (unsigned long)diag->host_peer_visual_snapshot_rx_count, (unsigned long)diag->host_peer_rgb_state_rx_count, (unsigned long)diag->host_peer_authority_rx_count, (unsigned long)diag->host_peer_input_layer_apply_count);
            return true;
        }
        case 20: {
            /* FA-2's line, appended last under the same rule as the three
               before it and read cold at print time like `wire sect`. Every
               counter is cumulative from boot. The three-way console
               distinction is deliberately cross-half: this half's `adv` (and
               `atx` on a responder) moving while the *other* half's `rt` rx
               side sits still reads "didn't send"; the peer's `aap` still
               against a moving `rt` rx reads "sent but not applied"; `aap`
               moving is "applied". DUAL-HOST has a console on both halves, so
               both readings exist wherever this section is eligible. */
            era_split_tap_activity_diagnostics_t act;
            era_split_tap_activity_get_diagnostics(&act);
            uprintf("wire act win=%lu spec=%lu/%lu/%lu jh=%lu/%lu rcl=%lu adv=%lu atx=%lu aap=%lu\r\n", (unsigned long)act.window_open_count, (unsigned long)act.speculative_activate_count, (unsigned long)act.speculative_revert_count, (unsigned long)act.speculative_abort_count, (unsigned long)act.judged_hold_hokp_count, (unsigned long)act.judged_hold_ph_count, (unsigned long)act.retro_cancel_count, (unsigned long)act.advertise_change_count, (unsigned long)act.sent_count, (unsigned long)act.peer_update_count);
            return true;
        }
        case 21: {
            /* The PEER's key-path span: core0 publishing a source-push to core0
               applying its ACK, so it contains core1's pickup, the wire
               exchange, and the HOST responder's turnaround. It exists because
               every performance figure this tree records is a core0 scan rate,
               and in HOST-PEER half the keyboard's keys do not travel on core0
               at all.

               Cumulative and free-running, for the reason `wire sect` above
               states: the half that produces this line is the half with no
               console, so it is read after that half is given a USB host. The
               histogram is what a decision uses -- the mean cannot separate a
               clean round trip from one that waited behind another exchange,
               and that difference is the whole question when anything new is
               put on core1. Bucket upper edges in microseconds: 64, 128, 192,
               256, 384, 512, 1024, then everything above. */
            era_host_peer_matrix_link_diagnostics_t link;
            era_host_peer_matrix_link_get_diagnostics_snapshot(&link);
            uprintf("wire keypath n=%lu avg=%lu max=%lu h=", (unsigned long)link.push_span_count, (unsigned long)era_split_wire_diagnostics_avg_us(link.push_span_sum_us, link.push_span_count), (unsigned long)link.push_span_max_us);
            for (uint8_t bucket = 0; bucket < ERA_HOST_PEER_MATRIX_LINK_SPAN_BUCKETS; bucket++) {
                uprintf(bucket == 0 ? "%lu" : ",%lu", (unsigned long)link.push_span_bucket[bucket]);
            }
            uprintf("\r\n");
            return true;
        }
        default:
            return false;
    }
}

/* Declared locally the way usb_main.c and usb_report_handling.c declare it: QMK exports the table by name and
   not through a header. */
extern usb_endpoint_in_t usb_endpoints_in[USB_ENDPOINT_IN_COUNT];

/* Exactly the predicate usb_idle_task() (tmk_core/protocol/chibios/usb_report_handling.c) recomputes on every
   pass -- true while any IN report carries a nonzero idle rate, which is the condition under which that task
   walks the endpoint table, takes its locks and converts time (once per millisecond on the ERA image, which
   paces it; on every pass upstream). Read the same way it reads it. A torn byte read here is harmless: this is
   a diagnostics print, not a decision. */
static uint8_t era_split_wire_diagnostics_usb_idle_task_active(void) {
    uint8_t any = 0;
    for (int ep = 0; ep < USB_ENDPOINT_IN_COUNT; ep++) {
        usb_report_storage_t *storage = usb_endpoints_in[ep].report_storage;
        if (storage == NULL) {
            continue;
        }
#if defined(SHARED_EP_ENABLE)
        if (ep == USB_ENDPOINT_IN_SHARED) {
            for (int report_id = 1; report_id <= REPORT_ID_COUNT; report_id++) {
                any |= storage->get_idle(storage->reports, report_id) != 0;
            }
            continue;
        }
#endif
        any |= storage->get_idle(storage->reports, 0) != 0;
    }
    return any;
}

static bool era_split_wire_diagnostics_print_authority_line(const era_authority_snapshot_t *snapshot, uint8_t line) {
    const era_authority_snapshot_t auth = *snapshot;

    switch (line) {
        case 0:
            /* idle=a/b: a = the host's last SET_IDLE duration in 4 ms units (0 = report on change only), b = 1
               while any report's idle rate is nonzero, i.e. while usb_idle_task() is doing periodic resend
               work at all (paced to once per millisecond on this image). Two fields because the first is only
               the last request the host made and the second is the state the task actually runs on; no other
               field records either. */
            uprintf("wire auth valid=%u left=%u usb=%u epoch=%u idle=%u/%u\r\n", (unsigned)auth.valid, (unsigned)auth.is_left, (unsigned)auth.usb_state, (unsigned)auth.usb_epoch, (unsigned)usb_device_state_get_idle_rate(), (unsigned)era_split_wire_diagnostics_usb_idle_task_active());
            return true;
        default:
            return false;
    }
}

#ifdef ERA_SPLIT_EEPROM_SYNC_ENABLE
static bool era_split_wire_diagnostics_print_eeprom_line(const era_split_eeprom_sync_diagnostics_t *snapshot, const era_split_sync_policy_snapshot_t *policy_snapshot, uint8_t line) {
    const era_split_eeprom_sync_diagnostics_t eeprom_sync = *snapshot;
    const era_split_sync_policy_snapshot_t    sync_policy = policy_snapshot != NULL ? *policy_snapshot : (era_split_sync_policy_snapshot_t){0};

    switch (line) {
        case 0:
            /* The redesigned indicator's whole read (era_capture_reading.md):
               `vis` is the lamp now, `pnd`/`mir`/`gate` are the fact's arms;
               `hold` is the derived case where the local semantic arm is zero
               while this role's last successfully sent peer-facing pending
               level is still one. `spans`/`rise`/
               `fall` are the pending fact's edge stamps — fall is the last
               pending pass, so the two halves' falls compare within the
               known clock offset with no bridge arithmetic left to check. */
            uprintf("eeprom shim ind vis=%u pnd=%u mir=%u gate=%u hold=%u spans=%lu rise=%lu fall=%lu\r\n", (unsigned)eeprom_sync.visible, (unsigned)(eeprom_sync.pending_bits & 0x01), (unsigned)((eeprom_sync.pending_bits >> 1) & 0x01), (unsigned)((eeprom_sync.pending_bits >> 2) & 0x01), (unsigned)((eeprom_sync.pending_bits >> 3) & 0x01), (unsigned long)eeprom_sync.span_count, (unsigned long)eeprom_sync.span_rise_ms, (unsigned long)eeprom_sync.span_fall_ms);
            return true;
        case 1:
            {
                uint32_t sleep_true_ms    = 0;
                unsigned sleep_true_count = era_split_keyboard_lighting_sleep_true_diag(&sleep_true_ms);
                uprintf("eeprom shim led rn=%lu ron=%lu roff=%lu brk=%u/%02X/%02X brkms=%lu slp=%u@%lu\r\n", (unsigned long)eeprom_sync.red_era_count, (unsigned long)eeprom_sync.red_on_ms, (unsigned long)eeprom_sync.red_off_ms, (unsigned)eeprom_sync.break_count, (unsigned)eeprom_sync.break_flags, (unsigned)eeprom_sync.break_state, (unsigned long)eeprom_sync.break_ms, sleep_true_count, (unsigned long)sleep_true_ms);
            }
            return true;
        case 2:
            uprintf("eeprom pol req=%u gen=%u df=%02X hb=%u\r\n", (unsigned)sync_policy.requested[ERA_SPLIT_SYNC_POLICY_FIELD_EEPROM], (unsigned)sync_policy.eeprom_policy_generation, (unsigned)sync_policy.dirty_flags, (unsigned)sync_policy.heartbeat);
            return true;
        default:
            return false;
    }
}
#endif

static void era_split_wire_diagnostics_capture_snapshot(void) {
    era_split_wire_diagnostics_state.print_count++;
    era_split_wire_diagnostics_state.last_print_ms = timer_read32();

    era_split_wire_diagnostics_get_snapshot(&era_split_wire_diagnostics_state.wire_snapshot);
    era_split_wire_diagnostics_state.wire_snapshot.raw_matrix_scan_hz = era_split_wire_diagnostics_raw_scan_hz(era_split_wire_diagnostics_state.last_print_ms, era_split_wire_diagnostics_state.wire_snapshot.raw_matrix_scan_count);
    era_rp2040_matrix_pio_get_diagnostics(&era_split_wire_diagnostics_state.pio_snapshot);
    {
        uint32_t words = era_split_wire_diagnostics_state.pio_snapshot.sample_words;
        uint32_t frame = era_split_wire_diagnostics_state.pio_snapshot.frame_words != 0 ? era_split_wire_diagnostics_state.pio_snapshot.frame_words : 1U;
        if (!era_split_wire_diagnostics_state.pio_sample_rate_valid) {
            era_split_wire_diagnostics_state.pio_sample_rate_valid = true;
            era_split_wire_diagnostics_state.pio_sample_hz         = 0;
        } else {
            uint32_t elapsed_ms = era_split_wire_diagnostics_state.last_print_ms - era_split_wire_diagnostics_state.pio_sample_rate_ms;
            era_split_wire_diagnostics_state.pio_sample_hz = era_split_wire_diagnostics_rate_hz_from_delta(elapsed_ms, (words - era_split_wire_diagnostics_state.pio_sample_rate_words) / frame, 0);
        }
        era_split_wire_diagnostics_state.pio_sample_rate_ms    = era_split_wire_diagnostics_state.last_print_ms;
        era_split_wire_diagnostics_state.pio_sample_rate_words = words;
        /* The stall flags print as they stood since the previous capture. */
        era_rp2040_matrix_pio_clear_fdebug();
    }
    era_split_wire_diagnostics_update_host_peer_rates(era_split_wire_diagnostics_state.last_print_ms, &era_split_wire_diagnostics_state.wire_snapshot);
    era_split_communication_core_get_diagnostics_snapshot(&era_split_wire_diagnostics_state.communication_core_snapshot);
    era_split_authority_reducer_get_snapshot(&era_split_wire_diagnostics_state.auth_snapshot);
#ifdef ERA_SPLIT_EEPROM_SYNC_ENABLE
    era_split_eeprom_sync_get_diagnostics_snapshot(&era_split_wire_diagnostics_state.eeprom_snapshot);
    era_split_sync_policy_get_snapshot(&era_split_wire_diagnostics_state.sync_policy_snapshot);
#endif
#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
    era_host_peer_storage_get_diagnostics_snapshot(&era_split_wire_diagnostics_state.storage_snapshot);
    era_split_communication_core_storage_get_probe_diagnostics(&era_split_wire_diagnostics_state.storage_probe_snapshot);
#    ifdef ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE
    era_host_peer_storage_get_cause_timeline_snapshot(&era_split_wire_diagnostics_state.storage_cause_timeline_snapshot);
    era_host_peer_storage_get_cause_edge_snapshot(&era_split_wire_diagnostics_state.storage_cause_edge_snapshot);
    era_via_macro_diagnostics_get_snapshot(&era_split_wire_diagnostics_state.via_macro_snapshot);
    era_host_peer_storage_reset_cause_edge();
    era_via_macro_diagnostics_reset();
#    endif
#endif
    era_split_transport_scheduler_reset_diagnostics_era_baselines();

    era_split_wire_diagnostics_state.print_pending = true;
    era_split_wire_diagnostics_state.print_line    = 0;
    era_split_wire_diagnostics_state.last_line_ms  = 0;
}

extern uint8_t __heap_base__[];
extern uint8_t __heap_end__[];

static bool era_split_wire_diagnostics_print_pending_line(void) {
    uint8_t line = era_split_wire_diagnostics_state.print_line;

    if (line < ERA_SPLIT_WIRE_DIAGNOSTICS_SCHEDULER_LINES) {
        return era_split_wire_diagnostics_print_scheduler_line(&era_split_wire_diagnostics_state.wire_snapshot, line);
    }
    line -= ERA_SPLIT_WIRE_DIAGNOSTICS_SCHEDULER_LINES;

    if (line < ERA_SPLIT_WIRE_DIAGNOSTICS_COMMUNICATION_CORE_LINES) {
        era_split_communication_core_get_diagnostics_snapshot(&era_split_wire_diagnostics_state.communication_core_snapshot);
        return era_split_wire_diagnostics_print_communication_core_line(&era_split_wire_diagnostics_state.communication_core_snapshot, line);
    }
    line -= ERA_SPLIT_WIRE_DIAGNOSTICS_COMMUNICATION_CORE_LINES;

    if (line < ERA_SPLIT_WIRE_DIAGNOSTICS_AUTHORITY_LINES) {
        return era_split_wire_diagnostics_print_authority_line(&era_split_wire_diagnostics_state.auth_snapshot, line);
    }
    line -= ERA_SPLIT_WIRE_DIAGNOSTICS_AUTHORITY_LINES;

#ifdef ERA_SPLIT_EEPROM_SYNC_ENABLE
    if (line < ERA_SPLIT_WIRE_DIAGNOSTICS_EEPROM_LINES) {
        return era_split_wire_diagnostics_print_eeprom_line(&era_split_wire_diagnostics_state.eeprom_snapshot, &era_split_wire_diagnostics_state.sync_policy_snapshot, line);
    }
    line -= ERA_SPLIT_WIRE_DIAGNOSTICS_EEPROM_LINES;
#endif

#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
    if (line < ERA_SPLIT_WIRE_DIAGNOSTICS_STORAGE_LINES) {
        return era_split_wire_diagnostics_print_storage_line(&era_split_wire_diagnostics_state.storage_snapshot,
                                                             &era_split_wire_diagnostics_state.storage_probe_snapshot,
#    ifdef ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE
                                                             &era_split_wire_diagnostics_state.storage_cause_timeline_snapshot,
                                                             &era_split_wire_diagnostics_state.storage_cause_edge_snapshot,
                                                             &era_split_wire_diagnostics_state.via_macro_snapshot,
#    endif
                                                             line);
    }
    line -= ERA_SPLIT_WIRE_DIAGNOSTICS_STORAGE_LINES;
#endif

    if (line == 0) {
        /* Heap watermark: core free memory must equal the linker heap span
         * while allocation stays unlinked (era_sram_residency_contract.md). */
        uprintf("wire mem core_free=%lu heap_span=%lu\r\n",
                (unsigned long)chCoreGetStatusX(),
                (unsigned long)(uintptr_t)(__heap_end__ - __heap_base__));
        return true;
    }

    return false;
}

static void era_split_wire_diagnostics_print_snapshot(void) {
    era_split_wire_diagnostics_capture_snapshot();
}

void era_split_wire_diagnostics_task(void) {
    if (!era_split_wire_diagnostics_state.print_pending) {
        return;
    }

    if (era_split_wire_diagnostics_state.print_line > 0 && timer_elapsed32(era_split_wire_diagnostics_state.last_line_ms) < ERA_SPLIT_WIRE_DIAGNOSTICS_LINE_INTERVAL_MS) {
        return;
    }

    if (!era_split_wire_diagnostics_print_pending_line()) {
        era_split_wire_diagnostics_state.print_pending = false;
        return;
    }

    era_split_wire_diagnostics_state.print_line++;
    era_split_wire_diagnostics_state.last_line_ms = timer_read32();
    era_split_wire_diagnostics_flush();
}

bool era_split_wire_diagnostics_process_record(uint16_t keycode, keyrecord_t *record) {
    if (record == NULL || !record->event.pressed) {
        return true;
    }

    switch (keycode) {
        case WIRE_DIAG:
            era_split_wire_diagnostics_communication_core_on_diag();
            era_split_wire_diagnostics_print_snapshot();
            return false;
        case WIRE_DIAG_2:
            /* The two arms that stood beside this one -- a bare queue flush and
               a bare quiesce -- selected on pre-CORE1_FULL stages the build
               system refuses, so neither was reachable in any buildable
               configuration. Both functions they named stay: they are live
               through the scheduler's own callers. */
            (void)era_split_transport_scheduler_flush_communication_core_for_diagnostics();
            era_split_wire_diagnostics_print_snapshot();
            return false;
    }
    return true;
}
