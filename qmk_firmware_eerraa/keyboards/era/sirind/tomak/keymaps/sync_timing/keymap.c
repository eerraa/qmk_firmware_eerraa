// SPDX-License-Identifier: GPL-2.0-or-later
#include "../via/keymap.c"
#include "../../../../common/split/era_sync_timing.h"
#include "../../../../common/split/era_host_peer_storage.h"
#include "../../../../common/storage/era_eeprom_driver.h"
#include "timer.h"
#include "print.h"
#include <string.h>

/* First 40 completed spans; no printing during the measured workload.
 * Logical bytes are distinct from wire bytes and include no transport headers.
 * F24 freezes sampling, emits a snapshot header, then four paced rows/span. */
typedef struct {
    uint32_t start, rise, end, last_write;
    uint32_t requested, local, remote, calls, write_ms, apply_ms;
    uint32_t generation, program, erase, retry, complete, abort;
    uint32_t fault;
    bool pending;
} timing_t;
static timing_t current, rows[40], baseline;
static bool active, frozen, overflow, armed;
static uint8_t count, index_out, part, end_out;
static uint32_t printed_at;

static void snapshot(timing_t *s) {
    era_nvm_diagnostics_t nvm;
    era_host_peer_storage_diagnostics_t storage;
    era_eeprom_driver_get_nvm_diagnostics(&nvm);
    era_host_peer_storage_get_diagnostics_snapshot(&storage);
    s->generation = era_eeprom_driver_timing_generation();
    s->program = nvm.program_count; s->erase = nvm.erase_count;
    s->retry = storage.retry_count; s->complete = storage.complete_count;
    s->abort = storage.abort_count;
}
static void begin(void) {
    if (active || frozen) return;
    memset(&current, 0, sizeof(current));
    current.start = timer_read32();
    snapshot(&baseline);
    active = true;
}
void era_sync_timing_abort(uint8_t state, uint8_t operation, uint8_t status, uint8_t path) {
    if (!active || frozen || current.fault) return;
    current.fault = (uint32_t)state << 24 | (uint32_t)operation << 16 |
                    (uint32_t)status << 8 | path;
}
void era_sync_timing_write_begin(uint32_t bytes) {
    if (frozen || !armed) return;
    begin(); current.requested += bytes; current.calls++;
}
void era_sync_timing_write_end(uint32_t elapsed_ms) {
    if (!active || frozen) return;
    current.last_write = timer_read32(); current.write_ms += elapsed_ms;
}
void era_sync_timing_commit(uint32_t bytes, bool remote) {
    if (frozen || !armed) return;
    begin();
    if (remote) current.remote += bytes;
    else current.local += bytes;
}
void era_sync_timing_apply_end(uint32_t elapsed_ms) {
    if (active && !frozen) current.apply_ms += elapsed_ms;
}
void era_sync_timing_pending(bool pending) {
    if (frozen || !armed) return;
    if (pending) {
        begin();
        if (!current.pending) { current.rise = timer_read32(); current.pending = true; }
        return;
    }
    if (!active) return;
    current.end = timer_read32();
    snapshot(&current);
    current.generation -= baseline.generation;
    current.program -= baseline.program; current.erase -= baseline.erase;
    current.retry -= baseline.retry; current.complete -= baseline.complete;
    current.abort -= baseline.abort;
    if (count < ARRAY_SIZE(rows)) rows[count++] = current;
    else overflow = true;
    active = false;
}
void housekeeping_task_user(void) {
    /* Initialization writes precede this first task pass, not the workload. */
    armed = true;
    if (index_out >= end_out || timer_elapsed32(printed_at) < 150) return;
    printed_at = timer_read32();
    const timing_t *s = &rows[index_out];
    unsigned i = index_out + 1;
    if (part == 0) {
        uprintf("tm9 i=%u start=%lu end=%lu\n", i,
            (unsigned long)s->start, (unsigned long)s->end);
    } else if (part == 1) {
        uprintf("tm9 i=%u total_ms=%lu sync_ms=%lu\n", i,
            (unsigned long)(s->end-s->start), (unsigned long)(s->pending ? s->end-s->rise : 0));
    } else if (part == 2) {
        uprintf("tm9 i=%u req_B=%lu calls=%lu\n", i,
            (unsigned long)s->requested, (unsigned long)s->calls);
    } else if (part == 3) {
        uprintf("tm9 i=%u local_B=%lu remote_B=%lu\n", i,
            (unsigned long)s->local, (unsigned long)s->remote);
    } else if (part == 4) {
        uprintf("tm9 i=%u upload_ms=%lu write_ms=%lu\n", i,
            (unsigned long)(s->last_write ? s->last_write-s->start : 0),
            (unsigned long)s->write_ms);
    } else if (part == 5) {
        uprintf("tm9 i=%u apply_ms=%lu rotate=%lu\n", i,
            (unsigned long)s->apply_ms, (unsigned long)s->generation);
    } else if (part == 6) {
        uprintf("tm9 i=%u prog=%lu erase=%lu\n", i,
            (unsigned long)s->program, (unsigned long)s->erase);
    } else {
        uprintf("tm9 i=%u retry=%lu done=%lu abort=%lu fault=%08lX\n", i,
            (unsigned long)s->retry, (unsigned long)s->complete, (unsigned long)s->abort,
            (unsigned long)s->fault);
    }
    if (++part == 8) { part = 0; index_out++; }
}
bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    if (keycode != KC_F24) return true;
    if (record->event.pressed) {
        frozen = true;
        uprintf("tm9 capture=%lu rows=%u overflow=%u active=%u\n",
            (unsigned long)timer_read32(), count, overflow, active);
        index_out = part = 0; end_out = count; printed_at = timer_read32();
    }
    return false;
}
