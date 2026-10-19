// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstring>

#include "test_common.hpp"

extern "C" {
#include "eeconfig.h"
#include "nvm_eeconfig.h"
#include "keyboards/era/common/storage/era_reset_guard.h"
#define _Static_assert static_assert
#include "keyboards/era/common/storage/era_eeprom_layout.h"
#undef _Static_assert
}

namespace {

std::array<uint8_t, ERA_EEPROM_CONFIG_SIZE> g_era_config{};
unsigned                                    g_eeconfig_init_kb_calls;

void write_guard_words(uint32_t key, uint32_t inverse) {
    std::memcpy(g_era_config.data() + ERA_EEPROM_RESET_GUARD_CONFIG_OFFSET, &key, sizeof(key));
    std::memcpy(g_era_config.data() + ERA_EEPROM_RESET_GUARD_CONFIG_OFFSET + 4, &inverse, sizeof(inverse));
}

/* What quantum_init() does with the eeconfig magic on every boot. */
void boot_quantum_init(void) {
    if (!eeconfig_is_enabled()) {
        eeconfig_init();
    }
}

keymap_config_t read_keymap(void) {
    keymap_config_t keymap;
    eeconfig_read_keymap(&keymap);
    return keymap;
}

} // namespace

extern "C" {

uint32_t era_eeprom_read_config(void *buf, uint32_t offset, uint32_t length) {
    if (offset > g_era_config.size() || length > g_era_config.size() - offset) {
        return 0;
    }
    std::memcpy(buf, g_era_config.data() + offset, length);
    return length;
}

uint32_t era_eeprom_update_config(const void *buf, uint32_t offset, uint32_t length) {
    if (offset > g_era_config.size() || length > g_era_config.size() - offset) {
        return 0;
    }
    std::memcpy(g_era_config.data() + offset, buf, length);
    return length;
}

/* The class skeletons' order: defaults first, the guard last. */
void eeconfig_init_kb(void) {
    ++g_eeconfig_init_kb_calls;
    eeconfig_init_user();
    era_reset_guard_write();
}

} // extern "C"

class ResetGuard : public TestFixture {
   protected:
    void SetUp() override {
        g_era_config.fill(0);
        eeconfig_init();
        g_eeconfig_init_kb_calls = 0;
    }
};

TEST_F(ResetGuard, AMatchingGuardKeepsEverything) {
    keymap_config_t keymap         = read_keymap();
    keymap.swap_control_capslock   = true;
    eeconfig_update_keymap(&keymap);

    EXPECT_FALSE(era_reset_guard_check());
    boot_quantum_init();

    EXPECT_TRUE(nvm_eeconfig_is_enabled());
    EXPECT_EQ(g_eeconfig_init_kb_calls, 0U);
    EXPECT_TRUE(read_keymap().swap_control_capslock);
}

TEST_F(ResetGuard, AnOtherKeyResetsEverythingOnceAndRewritesTheGuard) {
    keymap_config_t keymap         = read_keymap();
    keymap.swap_control_capslock   = true;
    eeconfig_update_keymap(&keymap);
    write_guard_words(ERA_EEPROM_RESET_KEY - 1U, ~(uint32_t)(ERA_EEPROM_RESET_KEY - 1U));

    EXPECT_TRUE(era_reset_guard_check());
    EXPECT_FALSE(nvm_eeconfig_is_enabled());
    boot_quantum_init();

    EXPECT_EQ(g_eeconfig_init_kb_calls, 1U);
    EXPECT_FALSE(read_keymap().swap_control_capslock);
    EXPECT_FALSE(era_reset_guard_check()); // the next boot finds the new guard and keeps everything
    boot_quantum_init();
    EXPECT_EQ(g_eeconfig_init_kb_calls, 1U);
}

TEST_F(ResetGuard, APendingEraseIsNotScheduledTwice) {
    write_guard_words(0, 0);
    eeconfig_disable(); // EEPROM CLEAN, or a store QMK never initialised

    EXPECT_FALSE(era_reset_guard_check());
    boot_quantum_init();
    EXPECT_EQ(g_eeconfig_init_kb_calls, 1U);
    EXPECT_FALSE(era_reset_guard_check());
}

TEST_F(ResetGuard, AGuardWithNonZeroReservedBytesIsStale) {
    g_era_config[ERA_EEPROM_RESET_GUARD_CONFIG_OFFSET + 8] = 0x5A;

    EXPECT_TRUE(era_reset_guard_check());
    boot_quantum_init();
    EXPECT_EQ(g_eeconfig_init_kb_calls, 1U);
    EXPECT_FALSE(era_reset_guard_check());
}
