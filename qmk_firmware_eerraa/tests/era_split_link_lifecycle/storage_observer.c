// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later
#include <stdint.h>

/* A separate translation unit overrides the adapter's weak fallback.
 * This fixture observes completion; region classification has its own tests. */
uint8_t era_host_peer_storage_eeprom_domain_mask(uint32_t address, uint32_t length) {
    (void)address;
    return length != 0U;
}
