// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>

#include "era_host_peer_transaction.h"

bool era_host_peer_source_snapshot_publish_visual(void);
/* `include_sleep` is the zero-at-capture half of the Slice 12 sleep rule: a
   DUAL-HOST half passes false and captures the sleep bit as zero into every
   snapshot it publishes, so a suspend flip on this half cannot re-arm the RGB
   section or reach the peer's render gate. HOST-PEER passes true. */
bool era_host_peer_source_snapshot_publish_rgb_state(bool include_sleep);
/* The same capture for a caller that stages the state itself rather than
   publishing the responder snapshot -- the DUAL-HOST standing plan's RGB
   field (Slice 12). Same sleep rule, same body. */
bool era_host_peer_source_snapshot_capture_rgb_state(era_host_peer_rgb_state_t *state, bool include_sleep);
