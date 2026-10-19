#include <array>
#include <cstring>
#include "gtest/gtest.h"
#include "test_driver.hpp"
#include "test_common.hpp"
extern "C" {
#include "rgb_matrix.h"
#include "keycode_config.h"
#include "wait.h"
#include "keyboards/era/common/features/era_rgb_indicator.h"
#include "keyboards/era/common/features/era_lighting_sleep.h"
}

namespace {
std::array<uint8_t, 280> config_bytes;
std::array<bool, 2> buffer_pixels;
std::array<bool, 2> pushed_pixels;
uint32_t flushes;
void driver_init() {}
void driver_color(int i, uint8_t r, uint8_t g, uint8_t b) { buffer_pixels.at(i) = r || g || b; }
void driver_all(uint8_t r, uint8_t g, uint8_t b) { buffer_pixels.fill(r || g || b); }
void driver_flush() { pushed_pixels = buffer_pixels; ++flushes; }
void render_frames() {
    for (unsigned i = 0; i < 80; ++i) { rgb_matrix_task(); wait_ms(1); }
}
class EraRgbIndicatorSleep : public testing::Test {
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
        buffer_pixels.fill(false);
        pushed_pixels.fill(false);
        host.set_leds(0);
        keymap_config.era_rgb_sleep_disabled = false;
        g_led_config.flags[1] = LED_FLAG_KEYLIGHT;
        era_lighting_sleep_init();
        rgb_matrix_set_suspend_state(false);
        rgb_matrix_init();
        era_rgb_indicator_init();
        rgb_matrix_enable_noeeprom();
        rgb_matrix_mode_noeeprom(RGB_MATRIX_SOLID_COLOR);
        rgb_matrix_sethsv_noeeprom(0, 0, 50);
        render_frames();
        caps(false);
        render_frames();
        flushes = 0;
    }
};
}
extern "C" {
led_config_t g_led_config = {};
const rgb_matrix_driver_t rgb_matrix_driver = {driver_init, driver_color, driver_all, driver_flush};
uint32_t era_eeprom_read_config(void *buf, uint32_t offset, uint32_t len) {
    if (offset + len > config_bytes.size()) return 0;
    std::memcpy(buf, config_bytes.data() + offset, len); return len;
}
uint32_t era_eeprom_update_config(const void *buf, uint32_t offset, uint32_t len) {
    if (offset + len > config_bytes.size()) return 0;
    std::memcpy(config_bytes.data() + offset, buf, len); return len;
}
}
TEST_F(EraRgbIndicatorSleep, AwakeCapsFollowsHostReport) {
    caps(true); render_frames(); EXPECT_TRUE(pushed_pixels[0]);
    caps(false); render_frames(); EXPECT_FALSE(pushed_pixels[0]);
}
TEST_F(EraRgbIndicatorSleep, SleepMasterOffKeepsCapsVisibleDuringSuspendReason) {
    caps(true); render_frames();
    reason(true, false); render_frames();
    EXPECT_FALSE(rgb_matrix_get_suspend_state());
    EXPECT_TRUE(pushed_pixels[0]);
}
TEST_F(EraRgbIndicatorSleep, SleepFromEffectStaysDarkDespiteHostLedChange) {
    caps(false); render_frames();
    reason(true); render_frames();
    ASSERT_TRUE(rgb_matrix_get_suspend_state());
    EXPECT_FALSE(pushed_pixels[0]);
    caps(true); render_frames();
    EXPECT_FALSE(pushed_pixels[0]) << "sleep must outrank an independent lock indicator";
}
TEST_F(EraRgbIndicatorSleep, DisabledPanelIndicatorMustBlankAtSleepEntry) {
    rgb_matrix_disable_noeeprom();
    caps(true); render_frames();
    ASSERT_TRUE(pushed_pixels[0]);
    reason(true);
    EXPECT_TRUE(rgb_matrix_get_suspend_state());
    EXPECT_FALSE(pushed_pixels[0]) << "NONE must clear an already-lit indicator even when effect init is false";
    render_frames();
    EXPECT_FALSE(pushed_pixels[0]);
}
TEST_F(EraRgbIndicatorSleep, WakeWithoutNewHostLedEventRestoresCaps) {
    caps(true); render_frames();
    reason(true); render_frames(); EXPECT_FALSE(pushed_pixels[0]);
    reason(false); render_frames();
    EXPECT_FALSE(rgb_matrix_get_suspend_state());
    EXPECT_TRUE(pushed_pixels[0]);
}
TEST_F(EraRgbIndicatorSleep, DisabledPanelWakeRestoresIndicatorAndPreservesSettings) {
    rgb_matrix_disable_noeeprom();
    caps(true); render_frames();
    const auto before = rgb_matrix_config;
    const auto stored = config_bytes;
    reason(true); render_frames();
    EXPECT_FALSE(pushed_pixels[0]);
    reason(false); render_frames();
    EXPECT_TRUE(pushed_pixels[0]);
    EXPECT_EQ(0, std::memcmp(&before, &rgb_matrix_config, sizeof(before)));
    EXPECT_EQ(stored, config_bytes);
}
TEST_F(EraRgbIndicatorSleep, AsleepConfigAndLedChangesStayDarkAndWakeUsesLatestState) {
    caps(true); render_frames();
    ASSERT_TRUE(pushed_pixels[1]);
    reason(true);
    rgb_matrix_disable_noeeprom();
    caps(false); render_frames();
    EXPECT_FALSE(pushed_pixels[0]);
    rgb_matrix_enable_noeeprom();
    rgb_matrix_sethsv_noeeprom(80, 100, 60);
    render_frames();
    EXPECT_FALSE(pushed_pixels[0]); EXPECT_FALSE(pushed_pixels[1]);
    reason(false); render_frames();
    EXPECT_FALSE(pushed_pixels[0]); EXPECT_TRUE(pushed_pixels[1]);
}
