// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_split_peer_layer.h"

#include "era_split_wire_protocol.h"

/* The loud gate for the one-byte INPUT layer section, placed here because this
   is the unit that would truncate. The wire header states the body size and
   deliberately does not include action_layer.h to assert it: that header
   defaults DYNAMIC_KEYMAP_LAYER_COUNT itself rather than taking it from
   config, so reaching this type from there would drag the whole QMK action
   layer into every core1 translation unit. A board whose layer count selects a
   wider layer_state_t fails here, at the definition of the value that crosses,
   naming the wire constant it no longer fits. */
_Static_assert(sizeof(layer_state_t) == ERA_SPLIT_WIRE_INPUT_LAYER_BYTES,
               "The INPUT layer wire section is one byte; this board's layer count selects a wider layer_state_t and would truncate on the wire.");

/* The assert above is necessary and, on its own, no longer sufficient.
   era_split_qmk_rules.mk defines LAYER_STATE_8BIT for every ERA split build,
   because the wire section admits eight layers and a build with no dynamic
   keymap would otherwise take QMK's 16-bit default and fail the assert for a
   reason that is not the board's. But `#if defined(LAYER_STATE_8BIT)` is
   tested first in action_layer.h, so that define also *wins* over the 16-bit
   choice action_layer.h makes for itself at a layer count above eight -- which
   would satisfy the assert by truncating, the exact failure it exists to
   catch. This is the half the assert cannot see, and it is checked here rather
   than in make because the layer count is a C-level fact the keyboard rules
   cannot read (QMK includes the keymap's rules.mk after the keyboard's). */
#if defined(DYNAMIC_KEYMAP_LAYER_COUNT) && (DYNAMIC_KEYMAP_LAYER_COUNT > 8)
#    error "ERA split carries the peer layer in one wire byte, so it admits at most eight layers; this build's DYNAMIC_KEYMAP_LAYER_COUNT is larger."
#endif

/* Core0-owned. One byte, so a read is atomic on this core and the merge below
   needs no critical section on the hot path it sits in. */
static layer_state_t g_era_split_peer_layer_state;

layer_state_t era_split_peer_layer_state(void) {
    return g_era_split_peer_layer_state;
}

bool era_split_peer_layer_apply(layer_state_t state) {
    /* Reports whether the applied value moved, because the unit that holds the
       value is the only one that can answer it. Its callers count `lay` from
       the answer: the standing record is latest-state, so an apply site reached
       on an unrelated section's edge re-applies an unchanged layer, and a
       counter incremented at the call site counts standing edges rather than
       layer changes -- device-shown 2026-08-13, three `lay` steps inside a
       window in which nothing was touched. */
    if (g_era_split_peer_layer_state == state) {
        return false;
    }
    g_era_split_peer_layer_state = state;
    return true;
}

void era_split_peer_layer_clear(void) {
    g_era_split_peer_layer_state = 0;
}
