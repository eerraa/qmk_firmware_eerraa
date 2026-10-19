// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "era_split_wire_protocol.h"

/* The two frame checksums are declared here as of 2026-08-11, and the reason
   they were not is worth keeping because it stopped being true rather than
   being wrong: both were used only by the encoders and decoders below them in
   era_split_wire_frame.c, and this header is the wire layer's outward surface.
   The reflected CRC32 was written out a second and third time in the two
   storage units instead, so the premise "only this unit uses it" was already
   false in effect; deduplicating onto this one made it false in form.

   They keep external linkage, which is the half of the old note that still
   binds: making either `static` invites the inlining that moves this image on
   a path every received frame runs. */
uint8_t  era_split_wire_crc8(const uint8_t *data, uint8_t length);
/* Incremental reflected-CRC accumulator. Seed with 0xFFFFFFFF and XOR the
   final result with 0xFFFFFFFF; era_split_wire_crc32() is that wrapper. */
uint32_t era_split_wire_crc32_update(uint32_t crc, const uint8_t *data, uint16_t length);
uint32_t era_split_wire_crc32(const uint8_t *data, uint16_t length);

uint8_t era_split_wire_next_seq(uint8_t seq);
uint8_t era_split_wire_control_byte(uint8_t tx_seq, uint8_t ack_seq, bool ext);

bool era_split_wire_encode_compact_frame(era_split_wire_direction_t direction, const uint8_t *payload, uint8_t payload_len, uint8_t *frame, uint8_t *frame_len);
bool era_split_wire_decode_compact_frame(const uint8_t *frame, uint8_t frame_len, era_split_wire_frame_t *decoded);
bool era_split_wire_encode_bulk_page_frame(era_split_wire_direction_t direction, const uint8_t *payload, uint16_t payload_len, uint8_t *frame, uint16_t *frame_len);
bool era_split_wire_decode_bulk_page_frame(const uint8_t *frame, uint16_t frame_len, era_split_wire_bulk_page_frame_t *decoded);
