// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_split_input.h"
#include "era_split_matrix_frame.h"
#include "era_split_wire_payload.h"
#include <limits.h>
#include <string.h>

_Static_assert(ERA_SPLIT_WIRE_HALF_MATRIX_BITS <= 128, "Input tokens have seven position bits.");
_Static_assert(ERA_SPLIT_WIRE_HALF_MATRIX_BYTES <= ERA_INPUT_TOKENS, "Input FULL must fit compact prefix6.");

/* Aligned single-writer words. Acquire/release generates the RP2040 DMB; no
 * cross-core RMW, allocator, spinlock, or ChibiOS operation belongs here. */
static uint32_t acquire(const uint32_t *p) { return __atomic_load_n(p, __ATOMIC_ACQUIRE); }
static void release(uint32_t *p, uint32_t v) { __atomic_store_n(p, v, __ATOMIC_RELEASE); }
static bool expired(uint32_t now, uint32_t at) { return (int32_t)(now - at) >= 0; }
static uint8_t next_slot(uint8_t n) { return n + 1U == ERA_INPUT_PACKET_SLOTS ? 0 : n + 1U; }

bool era_split_input_validate(const uint8_t *p, uint16_t n) {
    if (!p || n < 6 || n > 15 || !era_split_wire_get16(p + 2)) return false;
    uint16_t record = era_split_wire_get16(p + 4);
    if (p[1] >= ERA_INPUT_ACCEPTED && p[1] <= ERA_INPUT_MISMATCH) return n == 6;
    if (p[1] == ERA_INPUT_RESET || p[1] == ERA_INPUT_FULL) {
        return n == 6 + ERA_SPLIT_WIRE_HALF_MATRIX_BYTES &&
               (p[1] == ERA_INPUT_RESET ? record == 0 : record != 0) &&
               era_split_wire_matrix_reserved_bits_clear(p + 6);
    }
    if (p[1] != ERA_INPUT_DELTA || n == 6 || !record) return false;
    int previous = -1;
    uint8_t groups = 0;
    for (uint8_t i = 6; i < n; ++i) {
        uint8_t position = p[i] & 0x7f;
        if (position >= ERA_SPLIT_WIRE_HALF_MATRIX_BITS || position <= previous) return false;
        previous = position;
        if (p[i] & 0x80) { ++groups; previous = -1; }
    }
    return (p[n - 1] & 0x80) && (uint32_t)record + groups - 1U <= UINT16_MAX;
}

bool era_split_input_encode(uint8_t control, const era_split_input_request_t *r, uint8_t *p, uint8_t *n) {
    if (!r || !p || !n || r->length > ERA_INPUT_TOKENS) return false;
    p[0] = control | ERA_SPLIT_WIRE_CONTROL_EXT;
    p[1] = r->op;
    era_split_wire_put16(p + 2, r->epoch);
    era_split_wire_put16(p + 4, r->record);
    memcpy(p + 6, r->body, r->length);
    *n = 6 + r->length;
    return era_split_input_validate(p, *n);
}

enum { SOURCE_CLOSED, SOURCE_FENCE, SOURCE_BIND, SOURCE_BASELINE, SOURCE_LIVE, SOURCE_FRESH };
typedef struct { matrix_row_t rows[MATRIX_ROWS_PER_HAND]; uint32_t born; } input_snapshot_t;
static struct {
    bool enabled, latest_valid, pending_valid, waiting;
    uint16_t owner, relation, epoch, next_record;
    uint8_t phase, head, count;
    uint32_t generation;
    input_snapshot_t history[ERA_INPUT_HISTORY_CAPACITY];
    matrix_row_t latest[MATRIX_ROWS_PER_HAND], shadow[MATRIX_ROWS_PER_HAND];
    uint8_t facts[7];
    era_split_input_request_t pending;
} source;
static uint32_t source_live_generation, source_identity, source_flow_wait, source_barrier, source_terminal;
static uint32_t flow_hint, flow_hint_generation, flow_hint_seq, flow_hint_seen;
static void source_apply_flow(uint8_t status);

static input_snapshot_t *history(uint8_t offset) {
    uint8_t index = source.head + offset;
    if (index >= ERA_INPUT_HISTORY_CAPACITY) index -= ERA_INPUT_HISTORY_CAPACITY;
    return &source.history[index];
}

static void source_end_history(void) {
    source.count = source.head = 0;
    source.pending_valid = false;
    source.waiting = true;
    source.phase = SOURCE_FRESH;
    /* Zero permanently closes a saturated local generation until reboot. */
    if (source.generation != UINT32_MAX) ++source.generation;
    else source.enabled = false;
    release(&source_live_generation, source.enabled ? source.generation : 0);
    release(&source_flow_wait, 1);
    release(&source_terminal, 1);
}

bool era_split_input_source_gate(bool enabled, uint16_t owner, uint16_t relation) {
    enabled = enabled && owner && relation && source.generation != UINT32_MAX;
    if (enabled == source.enabled && owner == source.owner && relation == source.relation) return false;
    release(&source_live_generation, 0);
    source.enabled = enabled;
    source.owner = owner; source.relation = relation;
    source.head = source.count = 0;
    source.pending_valid = source.waiting = false;
    source.phase = enabled ? SOURCE_FENCE : SOURCE_CLOSED;
    source.epoch = 1; source.next_record = 1;
    if (source.generation != UINT32_MAX) ++source.generation;
    else source.enabled = false;
    release(&source_identity, ((uint32_t)owner << 16) | relation);
    release(&source_flow_wait, 0);
    release(&source_terminal, 0);
    release(&source_barrier, source.enabled);
    flow_hint_seen = acquire(&flow_hint_seq);
    release(&source_live_generation, source.enabled ? source.generation : 0);
    return true;
}

void era_split_input_source_snapshot(const matrix_row_t *rows, uint32_t now) {
    bool changed = !source.latest_valid || memcmp(source.latest, rows, sizeof(source.latest));
    memcpy(source.latest, rows, sizeof(source.latest)); source.latest_valid = true;
    if (!changed || !source.enabled ||
        (source.phase != SOURCE_LIVE && !(source.phase == SOURCE_BASELINE && source.pending_valid))) return;
    if (source.count == ERA_INPUT_HISTORY_CAPACITY) { source_end_history(); return; }
    input_snapshot_t *slot = history(source.count++);
    memcpy(slot->rows, rows, sizeof(slot->rows)); slot->born = now;
}
void era_split_input_source_facts(const era_split_wire_session_status_t *status) {
    if (source.phase != SOURCE_FENCE || source.pending_valid || !status) return;
    uint8_t payload[9], length;
    if (era_split_wire_encode_session_status(1, status, payload, &length)) memcpy(source.facts, payload + 2, 7);
}

bool era_split_input_source_service_needed(void) {
    return source.enabled && (source.pending_valid || source.count || source.phase != SOURCE_LIVE ||
                              acquire(&flow_hint_seq) != flow_hint_seen);
}
bool era_split_input_source_due(uint32_t now) {
    uint32_t sequence = acquire(&flow_hint_seq);
    if (sequence != flow_hint_seen) {
        uint32_t generation = acquire(&flow_hint_generation), status = acquire(&flow_hint);
        if (sequence == acquire(&flow_hint_seq)) {
            flow_hint_seen = sequence;
            if (generation == source.generation) source_apply_flow(status);
        }
    }
    if (!source.enabled || !source.latest_valid) return false;
    if ((source.pending_valid && source.pending.op >= ERA_INPUT_DELTA &&
         expired(now, source.pending.born_deadline_us)) ||
        (source.count && expired(now, history(0)->born + ERA_INPUT_SENDER_AGE_US))) source_end_history();
    return !source.waiting && (source.pending_valid || source.phase != SOURCE_LIVE || source.count);
}

bool era_split_input_source_prepare(era_split_input_request_t *r, uint32_t now) {
    if (!r || !era_split_input_source_due(now)) return false;
    if (source.pending_valid) { *r = source.pending; return true; }
    *r = (era_split_input_request_t){.epoch = source.epoch, .generation = source.generation};
    if (source.phase == SOURCE_FENCE || source.phase == SOURCE_BIND) {
        r->op = source.phase == SOURCE_FENCE ? ERA_INPUT_FENCE : ERA_INPUT_BIND;
        r->length = 7; memcpy(r->body, source.facts, 7);
    } else if (source.phase == SOURCE_BASELINE) {
        r->op = ERA_INPUT_RESET; r->length = ERA_SPLIT_WIRE_HALF_MATRIX_BYTES;
        r->born_deadline_us = now + ERA_INPUT_SENDER_AGE_US;
        if (!era_split_wire_pack_matrix(source.latest, r->body)) return false;
        memcpy(source.shadow, source.latest, sizeof(source.shadow));
    } else {
        if (!source.next_record) { source_end_history(); return false; }
        r->op = ERA_INPUT_DELTA; r->record = source.next_record;
        r->born_deadline_us = history(0)->born + ERA_INPUT_SENDER_AGE_US;
        matrix_row_t base[MATRIX_ROWS_PER_HAND]; memcpy(base, source.shadow, sizeof(base));
        for (uint8_t i = 0; i < source.count && (uint32_t)r->record + r->groups <= UINT16_MAX; ++i) {
            input_snapshot_t *snapshot = history(i);
            uint8_t tokens[ERA_INPUT_TOKENS], count = 0;
            bool too_large = false;
            for (uint8_t row = 0; row < MATRIX_ROWS_PER_HAND; ++row) {
                matrix_row_t diff = snapshot->rows[row] ^ base[row];
                for (uint8_t col = 0; col < MATRIX_COLS; ++col) {
                    if (!(diff & ((matrix_row_t)1 << col))) continue;
                    if (count == ERA_INPUT_TOKENS) { too_large = true; break; }
                    tokens[count++] = row * MATRIX_COLS + col;
                }
                if (too_large) break;
            }
            if (too_large || !count || count > ERA_INPUT_TOKENS - r->length) {
                if (!r->groups) {
                    r->op = ERA_INPUT_FULL; r->length = ERA_SPLIT_WIRE_HALF_MATRIX_BYTES; r->groups = 1;
                    if (!era_split_wire_pack_matrix(snapshot->rows, r->body)) return false;
                }
                break;
            }
            tokens[count - 1] |= 0x80;
            memcpy(r->body + r->length, tokens, count); r->length += count; ++r->groups;
            memcpy(base, snapshot->rows, sizeof(base));
        }
        if (!r->groups) return false;
    }
    source.pending = *r; source.pending_valid = true;
    return true;
}

static void source_rebind(void) {
    source_end_history();
    if (!source.enabled) return;
    source.phase = SOURCE_FENCE; source.epoch = source.next_record = 1;
    source.waiting = false;
    release(&source_barrier, 1);
    release(&source_flow_wait, 0); release(&source_terminal, 0);
}

void era_split_input_source_complete(const era_split_input_request_t *r, bool ok, uint8_t status) {
    if (!source.pending_valid || !r || memcmp(r, &source.pending, sizeof(*r))) return;
    if (!ok) return; /* immutable retry, original born deadline */
    if (r->op == ERA_INPUT_FENCE) { source.phase = SOURCE_BIND; source.pending_valid = false; return; }
    if (r->op == ERA_INPUT_BIND) { source.phase = SOURCE_BASELINE; source.pending_valid = false; release(&source_barrier, 0); return; }
    if (status == ERA_INPUT_WAIT) { source.waiting = true; release(&source_flow_wait, 1); return; }
    if (status == ERA_INPUT_RESET_REQUIRED || status == ERA_INPUT_MISMATCH) {
        if (status == ERA_INPUT_MISMATCH) source_rebind();
        else source_end_history();
        return;
    }
    if (status != ERA_INPUT_ACCEPTED && status != ERA_INPUT_ACCEPTED_STOP) return;
    if (r->op == ERA_INPUT_RESET) {
        source.phase = SOURCE_LIVE; source.next_record = 1;
    } else {
        memcpy(source.shadow, history(r->groups - 1)->rows, sizeof(source.shadow));
        source.head += r->groups;
        if (source.head >= ERA_INPUT_HISTORY_CAPACITY) source.head -= ERA_INPUT_HISTORY_CAPACITY;
        source.count -= r->groups;
        source.next_record = (uint16_t)((uint32_t)r->record + r->groups);
    }
    source.pending_valid = false;
    source.waiting = status == ERA_INPUT_ACCEPTED_STOP;
    release(&source_flow_wait, source.waiting);
    if (!source.next_record) source_end_history();
}

static void source_apply_flow(uint8_t status) {
    /* A standing exchange may have sampled the new generation just before
     * the control barrier was published. Its hint cannot cancel A/B. */
    if (!source.enabled || source.phase == SOURCE_FENCE || source.phase == SOURCE_BIND) return;
    if (status == ERA_INPUT_REBIND) { source_rebind(); return; }
    if (status == ERA_INPUT_FLOW_RESET) { if (source.phase != SOURCE_FRESH) source_end_history(); return; }
    if (status == ERA_INPUT_BASELINE_READY) {
        if (source.phase == SOURCE_BASELINE && source.pending_valid) {
            source.waiting = false; release(&source_flow_wait, 0); return;
        }
        if (source.phase != SOURCE_FRESH && source.phase != SOURCE_LIVE && source.phase != SOURCE_BASELINE) return;
        if (source.phase != SOURCE_FRESH) source_end_history();
        if (source.epoch == UINT16_MAX) { source.epoch = 1; source.phase = SOURCE_FENCE; release(&source_barrier, 1); }
        else { ++source.epoch; source.phase = SOURCE_BASELINE; }
        source.waiting = false; release(&source_flow_wait, 0);
        release(&source_terminal, 0);
    } else if (status == ERA_INPUT_READY && source.phase != SOURCE_FRESH) {
        source.waiting = false; release(&source_flow_wait, 0);
    }
}
uint32_t era_split_input_source_generation(void) { return acquire(&source_live_generation); }
void era_split_input_source_flow(uint8_t status, uint32_t generation) {
    release(&flow_hint, status);
    release(&flow_hint_generation, generation);
    release(&flow_hint_seq, acquire(&flow_hint_seq) + 1U);
}
bool era_split_input_source_pressure(void) { return acquire(&source_flow_wait) != 0; }
bool era_split_input_source_control_active(void) { return acquire(&source_barrier) != 0; }
bool era_split_input_source_terminal(void) { return acquire(&source_terminal) != 0; }
bool era_split_input_source_tx_allowed(const era_split_input_request_t *r, uint16_t owner, uint16_t relation, uint32_t now) {
    return r && r->generation && acquire(&source_live_generation) == r->generation &&
           acquire(&source_identity) == (((uint32_t)owner << 16) | relation) &&
           (r->op == ERA_INPUT_FENCE || r->op == ERA_INPUT_BIND || !expired(now, r->born_deadline_us));
}

enum { RX_CLOSED, RX_BOUND, RX_LIVE, RX_TERMINAL };
typedef struct {
    uint32_t version, gate, accepted_at;
    uint16_t epoch;
    uint8_t op, length, groups;
    uint8_t body[ERA_INPUT_TOKENS];
    matrix_row_t base[MATRIX_ROWS_PER_HAND];
} input_packet_t;
static struct {
    /* Core0 publishes gate identity and pressure separately from the plan. */
    uint32_t gate_seq, gate_identity, gate_enabled, pressure, baseline_room;
    /* Core1 writes these scalar publications. */
    uint32_t version, write, baseline_published, terminal;
    input_packet_t packets[ERA_INPUT_PACKET_SLOTS], baselines[2];
    /* Core0 writes these. A pinned packet is still the current read slot. */
    uint32_t read, baseline_retired, abort_version;
    bool claimed, claim_baseline;
    uint8_t cursor, groups_done;
    uint32_t claim_sequence, claim_version;
    matrix_row_t consumed[MATRIX_ROWS_PER_HAND];
    /* Core1-private transport state and persistent receipt after consumption. */
    uint32_t observed_gate;
    uint8_t state;
    uint16_t epoch, expected;
    matrix_row_t shadow[MATRIX_ROWS_PER_HAND];
    bool receipt_valid, fence_prepared, bind_valid;
    uint8_t receipt[14], receipt_length, fence[8], bind[10];
} receiver;

void era_split_input_receiver_gate(bool enabled, uint16_t owner, uint16_t relation) {
    enabled = enabled && owner && relation;
    uint32_t identity = ((uint32_t)owner << 16) | relation;
    if (receiver.gate_seq >= UINT32_MAX - 2U) enabled = false;
    if (receiver.gate_identity == identity && receiver.gate_enabled == (uint32_t)enabled) return;
    uint32_t seq = receiver.gate_seq;
    /* Holders can survive at most a synchronous pass; refusing at exhaustion
     * is safer than reusing a version underneath that holder. */
    if (seq >= UINT32_MAX - 2U) enabled = false;
    release(&receiver.gate_seq, seq | 1U);
    receiver.gate_identity = identity; receiver.gate_enabled = enabled;
    release(&receiver.gate_seq, seq < UINT32_MAX - 2U ? seq + 2U : UINT32_MAX - 1U);
}

static uint32_t receiver_gate(uint16_t owner, uint16_t relation) {
    uint32_t first = acquire(&receiver.gate_seq);
    if ((first & 1U) || !receiver.gate_enabled || receiver.gate_identity != (((uint32_t)owner << 16) | relation)) return 0;
    return first == acquire(&receiver.gate_seq) ? first : 0;
}

static void close_execution(void) {
    uint32_t version = acquire(&receiver.version);
    release(&receiver.version, version < UINT32_MAX ? version + 1U : UINT32_MAX);
    receiver.state = RX_TERMINAL; release(&receiver.terminal, 1);
}

static bool receiver_context(uint16_t owner, uint16_t relation) {
    uint32_t gate = receiver_gate(owner, relation);
    if (gate != receiver.observed_gate) {
        close_execution(); receiver.state = RX_CLOSED;
        receiver.receipt_valid = receiver.fence_prepared = receiver.bind_valid = false;
        receiver.observed_gate = gate;
    }
    return gate != 0 && acquire(&receiver.version) != UINT32_MAX;
}

static void expire_packets(uint32_t now) {
    if (receiver.state != RX_LIVE) return;
    uint32_t read = acquire(&receiver.read), write = acquire(&receiver.write);
    if (read != write) {
        input_packet_t *p = &receiver.packets[read];
        if (p->version == acquire(&receiver.version) && expired(now, p->accepted_at + ERA_INPUT_RECEIVER_AGE_US)) close_execution();
    }
    uint32_t sequence = acquire(&receiver.baseline_published);
    if (sequence != acquire(&receiver.baseline_retired)) {
        input_packet_t *p = &receiver.baselines[sequence & 1U];
        if (p->version == acquire(&receiver.version) && expired(now, p->accepted_at + ERA_INPUT_RECEIVER_AGE_US)) close_execution();
    }
    if (acquire(&receiver.pressure) || acquire(&receiver.abort_version) == acquire(&receiver.version)) close_execution();
}

bool era_split_input_control(const uint8_t *p, uint8_t n, uint16_t owner, uint16_t relation) {
    if (!p || !receiver_context(owner, relation) ||
        !(p[2] & ERA_SPLIT_WIRE_SESSION_STATUS_FLAG_NO_HOST) ||
        !(p[2] & ERA_SPLIT_WIRE_SESSION_STATUS_FLAG_MATRIX_READY)) return false;
    if (p[1] == ERA_INPUT_FENCE && n == 9) {
        close_execution(); receiver.state = RX_CLOSED;
        memcpy(receiver.fence, p + 1, 8);
        receiver.fence_prepared = true; receiver.bind_valid = receiver.receipt_valid = false;
        return true;
    }
    if (p[1] != ERA_INPUT_BIND || n != 11 || !era_split_wire_get16(p + 9)) return false;
    if (!receiver.fence_prepared) return receiver.bind_valid && !memcmp(receiver.bind, p + 1, 10);
    /* B uses the same authority facts as A. Only op and the epoch differ. */
    if (memcmp(receiver.fence + 1, p + 2, 7)) return false;
    memcpy(receiver.bind, p + 1, 10);
    receiver.bind_valid = true; receiver.fence_prepared = false;
    receiver.epoch = era_split_wire_get16(p + 9); receiver.state = RX_BOUND;
    return true;
}

uint8_t era_split_input_receive(const uint8_t *p, uint8_t n, uint16_t owner, uint16_t relation, uint32_t now) {
    if (!receiver_context(owner, relation) || !era_split_input_validate(p, n)) return ERA_INPUT_RESET_REQUIRED;
    expire_packets(now);
    uint16_t epoch = era_split_wire_get16(p + 2), record = era_split_wire_get16(p + 4);
    if (receiver.state == RX_CLOSED) return ERA_INPUT_MISMATCH;
    if (receiver.state == RX_TERMINAL &&
        (p[1] != ERA_INPUT_RESET || epoch == receiver.epoch)) return ERA_INPUT_RESET_REQUIRED;
    /* Receipt survives read-index release, ACK TX failure, and baseline
     * retired-not-applied. A terminal context cannot be revived by it. */
    if (receiver.receipt_valid && receiver.receipt_length >= 5 &&
        era_split_wire_get16(receiver.receipt + 1) == epoch && era_split_wire_get16(receiver.receipt + 3) == record) {
        if (receiver.receipt_length != n - 1 || memcmp(receiver.receipt, p + 1, n - 1)) {
            close_execution(); return ERA_INPUT_MISMATCH;
        }
        return next_slot(acquire(&receiver.write)) == acquire(&receiver.read) ? ERA_INPUT_ACCEPTED_STOP : ERA_INPUT_ACCEPTED;
    }
    bool reset = p[1] == ERA_INPUT_RESET;
    if (reset) {
        if ((receiver.state == RX_BOUND && epoch != receiver.epoch) ||
            ((receiver.state == RX_LIVE || receiver.state == RX_TERMINAL) &&
             (receiver.epoch == UINT16_MAX || epoch <= receiver.epoch))) return ERA_INPUT_MISMATCH;
        if (acquire(&receiver.baseline_published) != acquire(&receiver.baseline_retired) || !acquire(&receiver.baseline_room)) return ERA_INPUT_WAIT;
    } else if (receiver.state != RX_LIVE || epoch != receiver.epoch || record != receiver.expected || !record) {
        return ERA_INPUT_RESET_REQUIRED;
    }
    uint32_t write = acquire(&receiver.write), sequence = acquire(&receiver.baseline_published);
    if (!reset && next_slot(write) == acquire(&receiver.read)) return ERA_INPUT_WAIT;
    input_packet_t packet = {.gate = receiver.observed_gate, .accepted_at = now, .epoch = epoch, .op = p[1], .length = n - 6};
    memcpy(packet.body, p + 6, packet.length);
    memcpy(packet.base, receiver.shadow, sizeof(packet.base));
    matrix_row_t shadow[MATRIX_ROWS_PER_HAND]; memcpy(shadow, receiver.shadow, sizeof(shadow));
    if (p[1] == ERA_INPUT_DELTA) {
        for (uint8_t i = 0; i < packet.length; ++i) {
            uint8_t pos = packet.body[i] & 0x7f;
            shadow[pos / MATRIX_COLS] ^= (matrix_row_t)1 << (pos % MATRIX_COLS);
            if (packet.body[i] & 0x80) ++packet.groups;
        }
    } else {
        if (!era_split_wire_unpack_matrix(packet.body, shadow)) return ERA_INPUT_MISMATCH;
        packet.groups = 1;
    }
    /* No Core0 plan pointer or plan generation is retained in the packet. */
    if (!receiver_gate(owner, relation) || receiver.observed_gate != acquire(&receiver.gate_seq)) return ERA_INPUT_RESET_REQUIRED;
    if (reset) {
        if (sequence == UINT32_MAX) return ERA_INPUT_RESET_REQUIRED;
        if (acquire(&receiver.version) >= UINT32_MAX - 1U) return ERA_INPUT_RESET_REQUIRED;
        close_execution(); packet.version = acquire(&receiver.version);
        receiver.baselines[(sequence + 1U) & 1U] = packet;
    } else {
        packet.version = acquire(&receiver.version); receiver.packets[write] = packet;
    }
    memcpy(receiver.receipt, p + 1, n - 1); receiver.receipt_length = n - 1; receiver.receipt_valid = true;
    memcpy(receiver.shadow, shadow, sizeof(shadow)); receiver.epoch = epoch;
    receiver.expected = reset ? 1 : (uint16_t)((uint32_t)record + packet.groups);
    receiver.state = RX_LIVE; release(&receiver.terminal, 0);
    if (reset) release(&receiver.baseline_published, sequence + 1U);
    else release(&receiver.write, next_slot(write));
    return next_slot(acquire(&receiver.write)) == acquire(&receiver.read) ? ERA_INPUT_ACCEPTED_STOP : ERA_INPUT_ACCEPTED;
}

void era_split_input_receiver_pressure(bool pressured) {
    release(&receiver.pressure, pressured);
}
void era_split_input_receiver_pressure_event(void) {
    uint32_t version = acquire(&receiver.version);
    if (version) release(&receiver.abort_version, version);
}
void era_split_input_receiver_capacity(bool room) { release(&receiver.baseline_room, room); }
bool era_split_input_receiver_enabled(void) { return receiver.gate_enabled != 0; }
void era_split_input_receiver_end(void) {
    if (receiver.state == RX_LIVE || receiver.state == RX_BOUND) close_execution();
}

uint8_t era_split_input_receiver_flow(uint32_t now) {
    if (!receiver.gate_enabled) return ERA_INPUT_FLOW_RESET;
    expire_packets(now);
    if (receiver.state == RX_CLOSED) return ERA_INPUT_REBIND;
    if (receiver.state == RX_TERMINAL || receiver.state == RX_BOUND) {
        return acquire(&receiver.baseline_room) && !acquire(&receiver.pressure) &&
               acquire(&receiver.baseline_published) == acquire(&receiver.baseline_retired)
                   ? ERA_INPUT_BASELINE_READY : ERA_INPUT_FLOW_RESET;
    }
    return next_slot(acquire(&receiver.write)) == acquire(&receiver.read) ? ERA_INPUT_FLOW_WAIT : ERA_INPUT_READY;
}

bool era_split_input_receiver_rebind(uint16_t owner, uint16_t relation) {
    /* Core1's existing standing slot observes the gate even with no input TX.
     * No timer, scan-path work or normal wire byte is needed for this edge. */
    return receiver_context(owner, relation) && receiver.state == RX_CLOSED;
}

bool era_split_input_receiver_pending(void) {
    return acquire(&receiver.read) != acquire(&receiver.write) ||
           acquire(&receiver.baseline_published) != acquire(&receiver.baseline_retired);
}

bool era_split_input_claim(matrix_row_t *candidate, const matrix_row_t *previous, uint8_t room, bool hid_room, uint32_t now) {
    release(&receiver.baseline_room, room == ERA_SPLIT_WIRE_HALF_MATRIX_BITS && hid_room);
    release(&receiver.pressure, !hid_room);
    if (receiver.claimed) return false;
    uint32_t sequence = acquire(&receiver.baseline_published), version = acquire(&receiver.version);
    bool baseline = sequence != acquire(&receiver.baseline_retired);
    uint32_t read = acquire(&receiver.read);
    if (!baseline && read == acquire(&receiver.write)) return false;
    input_packet_t *p = baseline ? &receiver.baselines[sequence & 1U] : &receiver.packets[read];
    if (p->version != version || p->gate != acquire(&receiver.gate_seq) || !receiver.gate_enabled ||
        acquire(&receiver.terminal) || expired(now, p->accepted_at + ERA_INPUT_RECEIVER_AGE_US)) {
        if (p->version == version && receiver.gate_enabled) release(&receiver.abort_version, version);
        /* Retired-not-applied leaves the duplicate receipt intact. Core1
         * observes pressure before any later fresh admission/flow reply. */
        if (baseline) release(&receiver.baseline_retired, sequence);
        else { receiver.cursor = receiver.groups_done = 0; release(&receiver.read, next_slot(read)); }
        return false;
    }
    memcpy(candidate, receiver.cursor ? receiver.consumed : p->base, sizeof(receiver.consumed));
    uint8_t cursor = receiver.cursor;
    if (p->op == ERA_INPUT_DELTA) {
        do {
            uint8_t token = p->body[cursor++], pos = token & 0x7f;
            candidate[pos / MATRIX_COLS] ^= (matrix_row_t)1 << (pos % MATRIX_COLS);
            if (token & 0x80) break;
        } while (cursor < p->length);
    } else if (!era_split_wire_unpack_matrix(p->body, candidate)) return false;
    uint8_t changes = 0;
    for (uint8_t row = 0; row < MATRIX_ROWS_PER_HAND; ++row) {
        matrix_row_t bits = candidate[row] ^ previous[row];
        while (bits) { bits &= bits - 1U; ++changes; }
    }
    if (!hid_room || changes > room || (baseline && room != ERA_SPLIT_WIRE_HALF_MATRIX_BITS)) return false;
    /* The same version brackets this age sample. After successful claim,
     * reset may invalidate future work, but this synchronous group finishes. */
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    now = era_split_input_clock();
    if (expired(now, p->accepted_at + ERA_INPUT_RECEIVER_AGE_US) || version != acquire(&receiver.version) ||
        p->gate != acquire(&receiver.gate_seq) || !receiver.gate_enabled) return false;
    receiver.claimed = true; receiver.claim_baseline = baseline;
    receiver.claim_sequence = sequence; receiver.claim_version = version;
    receiver.cursor = cursor; memcpy(receiver.consumed, candidate, sizeof(receiver.consumed));
    return true;
}

void era_split_input_finish(void) {
    if (!receiver.claimed) return;
    receiver.claimed = false;
    if (receiver.claim_baseline) {
        receiver.cursor = receiver.groups_done = 0;
        release(&receiver.baseline_retired, receiver.claim_sequence);
    } else {
        uint32_t read = acquire(&receiver.read);
        input_packet_t *p = &receiver.packets[read];
        if (++receiver.groups_done == p->groups || receiver.claim_version != acquire(&receiver.version)) {
            receiver.cursor = receiver.groups_done = 0; release(&receiver.read, next_slot(read));
        }
    }
}
