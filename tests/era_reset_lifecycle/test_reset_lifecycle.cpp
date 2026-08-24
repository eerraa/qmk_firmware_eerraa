// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "gtest/gtest.h"
#include "test_common.hpp"

extern "C" {
#include "keyboards/era/common/system/era_board_hooks.h"
#include "keyboards/era/common/system/era_common_via.h"
#include "keyboards/era/common/system/era_flash_slice.h"
#include "wear_leveling.h"
}

namespace {

enum class Event : uint8_t {
    None,
    ShutdownUser,
    Persist,
    ClearEeprom,
    Bootloader,
    Reboot,
};

bool     g_shutdown_user_result;
bool     g_shutdown_user_arms_save;
bool     g_process_record_user_result;
bool     g_outer_operation_active;
bool     g_outer_operation_completed;
uint32_t g_shutdown_user_count;
uint32_t g_persist_count;
uint32_t g_board_persist_count;
uint32_t g_clear_count;
uint32_t g_bootloader_count;
uint32_t g_reboot_count;
uint32_t g_process_record_user_count;
uint32_t g_event_count;
Event    g_events[8];

void note(Event event) {
    ASSERT_LT(g_event_count, sizeof(g_events) / sizeof(g_events[0]));
    g_events[g_event_count++] = event;
}

void arm_keyboard_channel_save() {
    uint8_t data[4] = {id_custom_save, id_custom_channel, 0, 0};
    era_board_via_keyboard_channel_command(data, sizeof(data));
}

void begin_outer_gap() {
    g_outer_operation_active    = true;
    g_outer_operation_completed = false;
    backing_store_erase_yield_kb();
    g_outer_operation_active    = false;
    g_outer_operation_completed = true;
}

} // namespace

extern "C" bool shutdown_user(bool jump_to_bootloader) {
    (void)jump_to_bootloader;
    g_shutdown_user_count++;
    note(Event::ShutdownUser);
    if (g_shutdown_user_arms_save) {
        arm_keyboard_channel_save();
    }
    return g_shutdown_user_result;
}

extern "C" bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    (void)keycode;
    (void)record;
    g_process_record_user_count++;
    return g_process_record_user_result;
}

/* QMK's TEST platform owns a strong matrix_init_kb(), so it cannot link an ERA
   class skeleton. Keep these two seams byte-for-byte equivalent to the new
   class calls: user veto first, reset interception last; top-level work first,
   deferred reset drain last. Firmware builds cover both actual skeletons. */
extern "C" bool process_record_kb(uint16_t keycode, keyrecord_t *record) {
    if (!process_record_user(keycode, record)) {
        return false;
    }
    return era_flash_slice_defer_reset_action(keycode, record->event.pressed);
}

extern "C" void housekeeping_task_kb(void) {
    era_board_housekeeping_tick();
    era_flash_slice_deferred_reset_task();
}

extern "C" void era_common_via_keyboard_channel_save(void) {
    g_persist_count++;
    note(Event::Persist);
}

extern "C" void era_board_via_save(void) {
    g_board_persist_count++;
}

extern "C" bool era_common_via_handle_keyboard_channel_command(uint8_t *data, uint8_t length) {
    (void)data;
    (void)length;
    return false;
}

extern "C" bool era_common_via_handle_command(uint8_t *data, uint8_t length) {
    (void)data;
    (void)length;
    return false;
}

extern "C" bool process_record_via(uint16_t keycode, keyrecord_t *record) {
    (void)keycode;
    (void)record;
    return true;
}
extern "C" bool via_eeprom_is_valid(void) {
    return true;
}
extern "C" void via_init(void) {}
extern "C" void eeconfig_init_via(void) {}

extern "C" void __wrap_bootloader_jump(void) {
    EXPECT_FALSE(era_flash_slice_in_yield());
    EXPECT_FALSE(g_outer_operation_active);
    EXPECT_TRUE(g_outer_operation_completed);
    g_bootloader_count++;
    note(Event::Bootloader);
}

extern "C" void __wrap_mcu_reset(void) {
    EXPECT_FALSE(era_flash_slice_in_yield());
    EXPECT_FALSE(g_outer_operation_active);
    EXPECT_TRUE(g_outer_operation_completed);
    g_reboot_count++;
    note(Event::Reboot);
}

extern "C" void __wrap_eeconfig_disable(void) {
    EXPECT_FALSE(era_flash_slice_in_yield());
    EXPECT_FALSE(g_outer_operation_active);
    EXPECT_TRUE(g_outer_operation_completed);
    g_clear_count++;
    note(Event::ClearEeprom);
}

class EraResetLifecycle : public TestFixture {
   protected:
    void SetUp() override {
        g_shutdown_user_result       = true;
        g_shutdown_user_arms_save    = false;
        g_process_record_user_result = true;
        g_outer_operation_active     = false;
        g_outer_operation_completed  = true;
        g_shutdown_user_count        = 0;
        g_persist_count              = 0;
        g_board_persist_count        = 0;
        g_clear_count                = 0;
        g_bootloader_count           = 0;
        g_reboot_count               = 0;
        g_process_record_user_count  = 0;
        g_event_count                = 0;
        memset(g_events, 0, sizeof(g_events));
        era_flash_slice_arm();
    }

    void TearDown() override {
        /* Leave both production statics empty for whichever test the runner
           schedules next. Reset primitives return on the TEST platform. */
        g_shutdown_user_result    = true;
        g_shutdown_user_arms_save = false;
        (void)shutdown_kb(false);
        era_flash_slice_deferred_reset_task();
    }
};

TEST_F(EraResetLifecycle, ShutdownUserFalseVetoesPersistence) {
    arm_keyboard_channel_save();
    g_shutdown_user_result = false;

    EXPECT_FALSE(shutdown_kb(false));
    EXPECT_EQ(g_shutdown_user_count, 1U);
    EXPECT_EQ(g_persist_count, 0U);
    EXPECT_EQ(g_board_persist_count, 0U);
    ASSERT_EQ(g_event_count, 1U);
    EXPECT_EQ(g_events[0], Event::ShutdownUser);
}

TEST_F(EraResetLifecycle, ShutdownUserCanArmSaveBeforePersistenceFlush) {
    g_shutdown_user_arms_save = true;

    EXPECT_TRUE(shutdown_kb(false));
    EXPECT_EQ(g_shutdown_user_count, 1U);
    EXPECT_EQ(g_persist_count, 1U);
    EXPECT_EQ(g_board_persist_count, 1U);
    ASSERT_EQ(g_event_count, 2U);
    EXPECT_EQ(g_events[0], Event::ShutdownUser);
    EXPECT_EQ(g_events[1], Event::Persist);
}

TEST_F(EraResetLifecycle, NormalBootloaderKeyFlushesThenResetsWithoutDeferral) {
    TestDriver driver;
    auto       key = KeymapKey(0, 0, 0, QK_BOOT);
    set_keymap({key});
    arm_keyboard_channel_save();

    key.press();
    keyboard_task();

    EXPECT_EQ(g_persist_count, 1U);
    EXPECT_EQ(g_bootloader_count, 1U);
    ASSERT_GE(g_event_count, 3U);
    EXPECT_EQ(g_events[0], Event::ShutdownUser);
    EXPECT_EQ(g_events[1], Event::Persist);
    EXPECT_EQ(g_events[2], Event::Bootloader);

    key.release();
    keyboard_task();
}

TEST_F(EraResetLifecycle, FlashGapBootloaderWaitsForOuterOperationAndRunsOnce) {
    TestDriver driver;
    auto       key = KeymapKey(0, 0, 0, QK_BOOT);
    set_keymap({key});
    arm_keyboard_channel_save();

    key.press();
    begin_outer_gap();
    EXPECT_EQ(g_bootloader_count, 0U);
    EXPECT_EQ(g_persist_count, 0U);

    housekeeping_task_kb();
    EXPECT_EQ(g_persist_count, 1U);
    EXPECT_EQ(g_bootloader_count, 1U);
    housekeeping_task_kb();
    EXPECT_EQ(g_bootloader_count, 1U);

    key.release();
    keyboard_task();
}

TEST_F(EraResetLifecycle, FlashGapRebootWaitsForOuterOperationAndRunsOnce) {
    TestDriver driver;
    auto       key = KeymapKey(0, 1, 0, QK_REBOOT);
    set_keymap({key});
    arm_keyboard_channel_save();

    key.press();
    begin_outer_gap();
    EXPECT_EQ(g_reboot_count, 0U);
    EXPECT_EQ(g_persist_count, 0U);

    housekeeping_task_kb();
    EXPECT_EQ(g_persist_count, 1U);
    EXPECT_EQ(g_reboot_count, 1U);
    housekeeping_task_kb();
    EXPECT_EQ(g_reboot_count, 1U);

    key.release();
    keyboard_task();
}

TEST_F(EraResetLifecycle, FlashGapClearEepromDefersBothClearAndReset) {
    TestDriver driver;
    auto       key = KeymapKey(0, 2, 0, QK_CLEAR_EEPROM);
    set_keymap({key});

    key.press();
    begin_outer_gap();
    EXPECT_EQ(g_clear_count, 0U);
    EXPECT_EQ(g_reboot_count, 0U);

    housekeeping_task_kb();
    EXPECT_EQ(g_clear_count, 1U);
    EXPECT_EQ(g_reboot_count, 1U);
    ASSERT_GE(g_event_count, 3U);
    EXPECT_EQ(g_events[g_event_count - 3], Event::ClearEeprom);
    EXPECT_EQ(g_events[g_event_count - 2], Event::ShutdownUser);
    EXPECT_EQ(g_events[g_event_count - 1], Event::Reboot);

    housekeeping_task_kb();
    EXPECT_EQ(g_clear_count, 1U);
    EXPECT_EQ(g_reboot_count, 1U);

    key.release();
    keyboard_task();
}

TEST_F(EraResetLifecycle, FlashGapWithoutResetHasNoDeferredSideEffect) {
    TestDriver driver;
    begin_outer_gap();
    housekeeping_task_kb();

    EXPECT_EQ(g_shutdown_user_count, 0U);
    EXPECT_EQ(g_persist_count, 0U);
    EXPECT_EQ(g_clear_count, 0U);
    EXPECT_EQ(g_bootloader_count, 0U);
    EXPECT_EQ(g_reboot_count, 0U);
}

TEST_F(EraResetLifecycle, HeldPressAndReleaseProduceOneDeferredAction) {
    TestDriver driver;
    auto       key = KeymapKey(0, 0, 0, QK_BOOT);
    set_keymap({key});

    key.press();
    begin_outer_gap();
    begin_outer_gap();
    EXPECT_EQ(g_bootloader_count, 0U);

    key.release();
    begin_outer_gap();
    EXPECT_EQ(g_bootloader_count, 0U);

    housekeeping_task_kb();
    housekeeping_task_kb();
    EXPECT_EQ(g_bootloader_count, 1U);
}

TEST_F(EraResetLifecycle, UserVetoPreventsGapResetFromBeingLatched) {
    TestDriver driver;
    auto       key = KeymapKey(0, 0, 0, QK_BOOT);
    set_keymap({key});
    g_process_record_user_result = false;

    key.press();
    begin_outer_gap();
    housekeeping_task_kb();

    EXPECT_EQ(g_process_record_user_count, 1U);
    EXPECT_EQ(g_bootloader_count, 0U);

    key.release();
    keyboard_task();
}
