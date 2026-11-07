// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_split_eeprom_sync.h"

#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
#    include "era_host_peer_storage.h"
#endif

bool era_split_eeprom_sync_indicator_visible_advance(void) {
#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
    return era_host_peer_storage_indicator_pending();
#else
    return false;
#endif
}

__attribute__((weak)) void era_split_eeprom_sync_reload_domain_user(era_split_eeprom_sync_domain_t domain) {
    (void)domain;
}

__attribute__((weak)) void era_split_eeprom_sync_reload_domain_kb(era_split_eeprom_sync_domain_t domain) {
    era_split_eeprom_sync_reload_domain_user(domain);
}
