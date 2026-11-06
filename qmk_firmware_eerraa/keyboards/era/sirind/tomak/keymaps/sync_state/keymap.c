// SPDX-License-Identifier: GPL-2.0-or-later
/* This single-save fixture leaves SRAM for the pre-cleanup state capture. */
#define ERA_SYNC_TIMING_SPAN_COUNT 4
#define housekeeping_task_user era_sync_timing_housekeeping_task_user
#define process_record_user era_sync_timing_process_record_user
#include "../sync_timing/keymap.c"
#undef housekeeping_task_user
#undef process_record_user

static uint8_t state_line;
static bool state_output;
static uint32_t state_printed_at;

void housekeeping_task_user(void) {
    era_sync_timing_housekeeping_task_user();
    if (!state_output || index_out < end_out || timer_elapsed32(state_printed_at) < 150) return;
    state_printed_at = timer_read32();
    const era_sync_timing_storage_snapshot_t *s = era_host_peer_storage_timing_snapshot();
    if (!s) {
        uprintf("ts1 timeout=0\nts1 end\n");
        state_output = false;
        return;
    }
    unsigned row = state_line++;
    if (row >= 22 && row < 26) {
        unsigned i = (row - 22) / 2;
        const era_sync_timing_operation_t *o = &s->operations[i];
        if (!(row & 1U)) {
            uprintf("ts1 op=%u gen=%lu dl=%lu ph=%u out=%u\n", i,
                (unsigned long)o->generation, (unsigned long)o->deadline_ms, o->phase, o->outcome);
        } else {
            uprintf("ts1 op=%u rem=%02X prep=%02X settle=%02X local=%02X news=%02X\n",
                i, o->remaining_mask, o->preparing_mask, o->settle_mask, o->local_mask, o->news_pending_mask);
        }
        return;
    }
    if (row >= 26 && row < 34) {
        unsigned i = (row - 26) / 2;
        const era_sync_timing_contribution_t *c = &s->contributions[i];
        if (!(row & 1U)) {
            uprintf("ts1 wr=%u token=%lu gen=%lu live=%u mask=%02X\n", i,
                (unsigned long)c->token, (unsigned long)c->operation_generation, c->live, c->mask);
        } else {
            uprintf("ts1 wr=%u dl=%lu idle=%lu\n", i,
                (unsigned long)c->deadline_ms, (unsigned long)c->idle_deadline_ms);
        }
        return;
    }
    switch (row) {
        case 0:
            uprintf("ts1 reason=%u at=%lu\n", s->reason, (unsigned long)s->at_ms);
            break;
        case 1:
            uprintf("ts1 ctx at=%lu valid=%u mode=%u\n",
                (unsigned long)s->context.at_ms, s->context.valid, s->context.mode);
            break;
        case 2:
            uprintf("ts1 ctx owner=%u rel=%u policy=%u\n",
                s->context.owner_epoch, s->context.relation_generation, s->context.policy_generation);
            break;
        case 3:
            uprintf("ts1 ctx input=%u status=%u ready=%u init=%u\n",
                s->context.input_recovery_pending, s->context.status_revalidation_due,
                s->context.owner_ready, s->context.local_initiator);
            break;
        case 4:
            uprintf("ts1 rt state=%u role=%u domain=%u\n", s->state, s->role, s->domain);
            break;
        case 5:
            uprintf("ts1 rt op=%u status=%u flags=%02X\n", s->pending_operation, s->status, s->flags);
            break;
        case 6:
            uprintf("ts1 rt tx=%u req=%u snap=%u\n",
                s->transaction_generation, s->request_generation, s->responder_snapshot_generation);
            break;
        case 7:
            uprintf("ts1 rt owner=%u rel=%u policy=%u\n",
                s->owner_epoch, s->relation_generation, s->policy_generation);
            break;
        case 8:
            uprintf("ts1 dirty=%02X intent=%02X failed=%02X eligible=%02X\n",
                s->dirty_mask, s->intent_mask, s->failed_mask, s->eligible_mask);
            break;
        case 9:
            uprintf("ts1 active=%u retiring=%u result=%u macro_open=%u\n",
                s->active, s->retiring, s->result_due, s->macro_open);
            break;
        case 10:
            uprintf("ts1 episode gen=%lu mask=%02X\n",
                (unsigned long)s->episode_generation, s->episode_mask);
            break;
        case 11:
            uprintf("ts1 snapshot gen=%lu mask=%02X\n",
                (unsigned long)s->snapshot_generation, s->snapshot_mask);
            break;
        case 12:
            uprintf("ts1 macro_token=%lu news=%u peer_news=%u\n",
                (unsigned long)s->macro_token, s->news, s->peer_news);
            break;
        case 13:
            uprintf("ts1 arb=%02X probe=%02X push=%02X conflict=%02X\n",
                s->arbitration_flags, s->probe_mask, s->push_mask, s->conflict_mask);
            break;
        case 14:
            uprintf("ts1 match=%lu transfer=%lu complete=%lu\n",
                (unsigned long)s->match_count, (unsigned long)s->transfer_count, (unsigned long)s->complete_count);
            break;
        case 15:
            uprintf("ts1 abort=%lu stale=%lu retry=%lu\n",
                (unsigned long)s->abort_count, (unsigned long)s->stale_count, (unsigned long)s->retry_count);
            break;
        case 16:
            uprintf("ts1 meta at=%lu domain=%u ok=%u\n",
                (unsigned long)s->metadata.at_ms, s->metadata.domain, s->metadata.ok);
            break;
        case 17:
            uprintf("ts1 meta gen=%lu crc=%08lX\n",
                (unsigned long)s->metadata.operation_generation, (unsigned long)s->metadata.crc32);
            break;
        case 18:
            uprintf("ts1 proof at=%lu gen=%lu\n",
                (unsigned long)s->proof.at_ms, (unsigned long)s->proof.generation);
            break;
        case 19:
            uprintf("ts1 proof cur=%lu ph=%u in=%02X elig=%02X ok=%02X\n",
                (unsigned long)s->proof.current_generation, s->proof.phase,
                s->proof.mask, s->proof.eligible_mask, s->proof.retired_mask);
            break;
        case 20:
            uprintf("ts1 proof episode=%lu mask=%02X\n",
                (unsigned long)s->proof.episode_generation, s->proof.episode_mask);
            break;
        case 21:
            uprintf("ts1 proof snapshot=%lu mask=%02X\n",
                (unsigned long)s->proof.snapshot_generation, s->proof.snapshot_mask);
            break;
        default:
            uprintf("ts1 end\n");
            state_output = false;
            break;
    }
}

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    bool result = era_sync_timing_process_record_user(keycode, record);
    if (keycode == KC_F24 && record->event.pressed) {
        state_line = 0;
        state_output = true;
        state_printed_at = timer_read32();
    }
    return result;
}
