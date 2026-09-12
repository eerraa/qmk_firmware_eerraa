// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

/* Included only by the scheduler and its production-boundary host fixture.
 * Configuration selection is not lease readiness. The reset helper's checked
 * Core1 READY and the new epoch's publications must precede LINK persistence. */
static bool era_split_transport_scheduler_apply_link_step(uint8_t level, bool rotate_relation) {
    if (level > ERA_SPLIT_LINK_LEVEL_LOW) {
        return false;
    }
    if (era_split_link_active_level() != level || rotate_relation) {
        if (!era_split_transport_scheduler_stop_communication_core_for_flash_write()) {
            goto failed;
        }
        (void)era_split_transaction_backend_set_speed(era_split_link_speed(level));
        era_split_link_note_level_selected(level);
    }
    if (!era_split_transport_scheduler_reset_serial_for_transport_role(
            g_era_split_transport_scheduler.local_wire_available,
            g_era_split_transport_scheduler.local_wire_initiator, rotate_relation)) {
        goto failed;
    }
    /* An unchanged level can still have a lost lease. Conversely a repaired
     * lease without its new-epoch snapshots would be silent throughout NVM. */
    (void)era_split_transport_scheduler_publish_communication_core_responder_snapshot();
    (void)era_split_transport_scheduler_publish_standing_plan();
    if (g_era_split_transport_scheduler.responder_snapshot_publish_due ||
        era_split_transport_scheduler_standing_plan_stale()) {
        goto failed;
    }
    return true;
failed:
    era_split_transport_scheduler_mark_dirty(ERA_SPLIT_SCHEDULER_DIRTY_WIRE_ROLE);
    return false;
}

bool era_split_transport_scheduler_apply_link_level(uint8_t level) {
    if (!g_era_split_transport_scheduler.communication_core_started ||
        !g_era_split_transport_scheduler.local_wire_available) {
        return false;
    }
    return era_split_transport_scheduler_apply_link_step(level, false);
}
