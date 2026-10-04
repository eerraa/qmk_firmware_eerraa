#include <array>
#include <cstring>
#include "gtest/gtest.h"
#include "test_driver.hpp"
#include "test_common.hpp"
extern "C" {
#include "rgblight.h"
#include "keycode_config.h"
#include "wait.h"
#include "keyboards/era/newone/common/odessey_common.h"
#include "keyboards/era/common/system/era_nonsplit_board.h"
#include "keyboards/era/common/features/era_lighting_sleep.h"
extern rgblight_config_t rgblight_config;
}
namespace {
std::array<uint8_t, 280> config_bytes;
std::array<bool, 31> buffer_pixels;
std::array<bool, 31> pushed_pixels;
uint32_t flushes;
void driver_init() {}
void driver_color(int i, uint8_t r, uint8_t g, uint8_t b) { buffer_pixels.at(i) = r || g || b; }
void driver_all(uint8_t r, uint8_t g, uint8_t b) { buffer_pixels.fill(r || g || b); }
void driver_flush() { pushed_pixels = buffer_pixels; ++flushes; }
class EraOdesseyLightingSleep : public testing::Test {
 protected:
    TestDriver host;
    void caps(bool on) {
        led_t state{};
        state.caps_lock = on;
        host.set_leds(state.raw);
        ASSERT_TRUE(led_update_kb(state));
    }
    void reason(bool sleeping, bool master_enabled = true) {
        keymap_config.era_rgb_sleep_disabled = !master_enabled;
        era_lighting_sleep_update(sleeping, false, 0);
    }
    void SetUp() override {
        timer_clear();
        config_bytes.fill(0);
        host.set_leds(0);
        keymap_config.era_rgb_sleep_disabled = false;
        era_lighting_sleep_init();
        if (!is_rgblight_initialized) rgblight_init();
        rgblight_set_output_suspend_state(false);
        rgblight_enable_noeeprom();
        rgblight_mode_noeeprom(RGBLIGHT_MODE_STATIC_LIGHT);
        era_board_config_load();
        keyboard_post_init_kb();
        rgblight_sethsv_noeeprom(10, 200, 120);
        rgblight_task();
        caps(false);
        flushes = 0;
    }
};
}
extern "C" {
const rgblight_driver_t rgblight_driver = {driver_init, driver_color, driver_all, driver_flush};
bool led_update_user(led_t) { return true; }
void led_update_ports(led_t) {}
void keyboard_post_init_user(void) {}
uint32_t era_eeprom_read_config(void *buf, uint32_t offset, uint32_t len) {
    if (offset + len > config_bytes.size()) return 0;
    std::memcpy(buf, config_bytes.data() + offset, len); return len;
}
uint32_t era_eeprom_update_config(const void *buf, uint32_t offset, uint32_t len) {
    if (offset + len > config_bytes.size()) return 0;
    std::memcpy(config_bytes.data() + offset, buf, len); return len;
}
}
TEST_F(EraOdesseyLightingSleep, AwakeCapsFollowsHostReport) {
    caps(true); EXPECT_TRUE(pushed_pixels[0]);
    caps(false); EXPECT_FALSE(pushed_pixels[0]);
}
TEST_F(EraOdesseyLightingSleep, SleepMasterOffKeepsCapsVisibleDuringSuspendReason) {
    caps(true);
    reason(true, false);
    EXPECT_FALSE(rgblight_get_output_suspend_state());
    EXPECT_TRUE(pushed_pixels[0]);
    EXPECT_TRUE(pushed_pixels[1]);
}
TEST_F(EraOdesseyLightingSleep, SleepHostLedChangeCannotRelightTheIndicator) {
    caps(false);
    reason(true); ASSERT_TRUE(rgblight_get_output_suspend_state());
    ASSERT_FALSE(pushed_pixels[0]);
    caps(true);
    EXPECT_FALSE(pushed_pixels[0]) << "board direct flush must not bypass the output gate";
    reason(true); rgblight_task();
    EXPECT_FALSE(pushed_pixels[0]) << "repeating an unchanged sleep decision must keep every LED dark";
}
TEST_F(EraOdesseyLightingSleep, WakeWithoutNewHostLedEventRestoresCaps) {
    caps(true); ASSERT_TRUE(pushed_pixels[0]);
    reason(true); ASSERT_FALSE(pushed_pixels[0]);
    reason(false); rgblight_task();
    EXPECT_FALSE(rgblight_get_output_suspend_state());
    EXPECT_TRUE(pushed_pixels[1]);
    EXPECT_TRUE(pushed_pixels[0]) << "wake must restore the cached lock state outside the effect range";
}
TEST_F(EraOdesseyLightingSleep, WakeUsesTheLatestLedStateSeenWhileAsleep) {
    caps(true); reason(true);
    caps(false); reason(false); rgblight_task();
    EXPECT_FALSE(pushed_pixels[0]);
}
TEST_F(EraOdesseyLightingSleep, DisabledUnderglowWakeRestoresOnlyIndicator) {
    rgblight_disable_noeeprom(); caps(true);
    ASSERT_TRUE(pushed_pixels[0]); ASSERT_FALSE(pushed_pixels[1]);
    const auto before = rgblight_config;
    const auto stored = config_bytes;
    reason(true); EXPECT_FALSE(pushed_pixels[0]);
    reason(false);
    EXPECT_TRUE(pushed_pixels[0]); EXPECT_FALSE(pushed_pixels[1]);
    EXPECT_EQ(0, std::memcmp(&before, &rgblight_config, sizeof(before)));
    EXPECT_EQ(stored, config_bytes);
}
TEST_F(EraOdesseyLightingSleep, AsleepEffectChangesStayDarkAndWakeUsesNewSettings) {
    caps(true); reason(true);
    rgblight_sethsv_noeeprom(80, 100, 60);
    for (bool pixel : pushed_pixels) EXPECT_FALSE(pixel);
    reason(false);
    EXPECT_TRUE(pushed_pixels[0]); EXPECT_TRUE(pushed_pixels[1]);
    EXPECT_EQ(80, rgblight_config.hue);
    EXPECT_EQ(60, rgblight_config.val);
}
