// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "action_layer.h"

/* The peer half's layer contribution in a confirmed DUAL-HOST relation.
 *
 * It is a separate core0-owned variable, OR-composed into the one expression
 * QMK already uses to compose default_layer_state, and never written into
 * layer_state itself. That separation is not stylistic: layer_off() and
 * layer_invert() compose on layer_state, so peer bits merged there would be
 * cleared by ordinary local operations, and a lost release frame would strand
 * the peer's layer with nothing left to clear it.
 *
 * OR is the correct composition for momentary layer actions: if one half
 * holds MO(1) and the other MO(2), both halves compute the same merged value
 * and layer_switch_get_layer() picks the same highest matching layer either
 * way -- which is exactly what the same keymap produces on a
 * single-controller split. Local key releases are unaffected by a peer layer
 * change because source_layers_cache already resolves a release on the layer
 * the key was pressed on.
 *
 * It does not compose for TG()/TT(), and that is a recorded limitation rather
 * than a defect found later: a toggle inverts the executing half's own bit, so
 * a layer toggled on by one half cannot be toggled off by the other. Neither
 * keycode is in the default tomak79h VIA keymap. The recorded fix is a
 * one-byte peer-layer-clear request section, unscheduled.
 */

layer_state_t era_split_peer_layer_state(void);

/* Applied only from the generation-matched result path on core0. Returns
   whether the held value moved, which is what `lay` counts. */
bool era_split_peer_layer_apply(layer_state_t state);

/* Cleared whenever the relation is not a confirmed DUAL-HOST, the same rule
   the peer matrix cache already follows. A cable pull therefore drops the
   peer's layer within the responder-silence window plus one planning pass,
   rather than leaving this half shifted with nothing to unshift it. */
void era_split_peer_layer_clear(void);
