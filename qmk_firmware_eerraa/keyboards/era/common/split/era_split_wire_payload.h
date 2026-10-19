// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stddef.h>

#include "era_split_wire_protocol.h"

#define ERA_SPLIT_WIRE_SECTION_SLOTS 8

/* Where each present section's body starts, produced by one walk of byte2's
   mask. Indexed by section bit position, so `offset[3]` describes the marker
   `0x08` section. Every consumer -- both validators and every extractor --
   reads this instead of re-deriving offsets from the mask, which is where a
   body-size change would otherwise have to be repeated once per section.

   There is no `length[]` beside it, and the absence is deliberate: a section's
   length is a compile-time constant of its marker, held in the per-direction
   body-size tables the walk already reads, so storing it per decode was eight
   bytes written on every frame for a reader that never existed. A consumer
   that needs a length reads the table. */
typedef struct {
    uint8_t sections;
    uint8_t offset[ERA_SPLIT_WIRE_SECTION_SLOTS];
} era_split_wire_section_layout_t;

bool era_split_wire_layout_source_push(const uint8_t *payload, uint16_t payload_len, era_split_wire_section_layout_t *layout);
bool era_split_wire_layout_host_source_rsp(const uint8_t *payload, uint16_t payload_len, era_split_wire_section_layout_t *layout);

/* The send side of the walk above: what a source-push carrying exactly these
   sections will occupy. A sender asks whether one more section still fits by
   passing the mask it would then hold, rather than by summing the sections it
   knows ran before it. The reasoning is at the definition. */
uint16_t era_split_wire_source_push_projected_len(uint8_t sections);

/* Marker-bit position. Callers pass a compile-time marker, so the loop folds
   away; it exists so the marker values stay the single encoding of a section
   identity and no parallel slot-index table can drift against them. */
static inline uint8_t era_split_wire_section_slot(uint8_t marker) {
    uint8_t slot = 0;
    while ((marker >>= 1) != 0) {
        slot++;
    }
    return slot;
}

static inline bool era_split_wire_section_present(const era_split_wire_section_layout_t *layout, uint8_t marker) {
    return layout != NULL && (layout->sections & marker) != 0;
}

static inline uint8_t era_split_wire_section_offset(const era_split_wire_section_layout_t *layout, uint8_t marker) {
    return layout->offset[era_split_wire_section_slot(marker)];
}


/* The linked eligibility table's accessor. `mode` is an era_split_mode_t
   value; an out-of-range one yields zero, which closes every section rather
   than opening one. Relations are keyed by relation, not by role, so the
   sender's send-clip and the receiver's accept-clip read the same entry. */
#define ERA_SPLIT_WIRE_SECTION_DIRECTION_PUSH 0
#define ERA_SPLIT_WIRE_SECTION_DIRECTION_RSP 1
extern const uint8_t g_era_split_wire_section_eligibility[ERA_SPLIT_WIRE_SECTION_ELIGIBILITY_MODES][2];
uint8_t era_split_wire_eligible_sections(uint8_t mode, uint8_t direction);

/* The classifier reports a kind and nothing else. It used to hand back the op
   byte's low nibble as a `subtype` too, which every caller stored and none
   read: the storage lane, the one consumer that would plausibly want an
   operation subtype, re-reads `frame->payload[1]` itself. */
bool era_split_wire_classify_payload(const uint8_t *payload, uint16_t payload_len, era_split_wire_frame_lane_t lane, era_split_wire_payload_kind_t *kind);
bool era_split_wire_validate_bulk_page_payload(const uint8_t *payload, uint16_t payload_len);

bool era_split_wire_encode_session_status(uint8_t control, const era_split_wire_session_status_t *status, uint8_t *payload, uint8_t *payload_len);
bool era_split_wire_decode_session_status(const era_split_wire_frame_t *frame, era_split_wire_session_status_t *status);

/* The AUTHORITY body codec. One pair for both directions, because the section
   is one fact set and giving each direction its own encoder is how the two
   drift. The layout walk has already proved the body well-formed by the time
   the decoder runs, exactly as it has for every other section since Slice 11.

   `equal` is the edge test both shadows use -- the sender's send-clip and the
   receiver's report-clip -- so "edge-consumed" is one comparison rather than
   two hand-written field lists that can disagree. */
void era_split_wire_encode_authority_body(const era_split_wire_authority_section_t *authority, uint8_t *body);
void era_split_wire_decode_authority_body(const uint8_t *body, era_split_wire_authority_section_t *authority);
bool era_split_wire_authority_equal(const era_split_wire_authority_section_t *lhs, const era_split_wire_authority_section_t *rhs);

/* The ACTIVITY body codec (FA-2 S2), on the same one-pair-both-directions
   rule; `equal` is the one edge test every activity shadow uses. */
void era_split_wire_encode_activity_body(const era_split_wire_activity_section_t *activity, uint8_t *body);
void era_split_wire_decode_activity_body(const uint8_t *body, era_split_wire_activity_section_t *activity);
bool era_split_wire_activity_equal(const era_split_wire_activity_section_t *lhs, const era_split_wire_activity_section_t *rhs);
