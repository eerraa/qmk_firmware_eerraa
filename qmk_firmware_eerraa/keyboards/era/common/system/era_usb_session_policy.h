// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Pure policy for the one SOF-observation gap that is deliberate rather than
   scheduler starvation. While ChibiOS owns SOFRD for a remote-wake SOF, a
   pending DEV_SOF is positive arrival evidence. If no SOF is pending, both the
   uninterrupted ownership interval and the age of the last observed frame must
   span the loss threshold before the gap can be classified as frame loss. */
static inline bool era_usb_session_policy_isr_owned_frames_lost(uint32_t owner_age_us,
                                                                uint32_t frame_age_us,
                                                                bool sof_pending,
                                                                uint32_t stale_us) {
    return !sof_pending && owner_age_us >= stale_us && frame_age_us >= stale_us;
}
