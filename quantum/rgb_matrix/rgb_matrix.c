/* Copyright 2017 Jason Williams
 * Copyright 2017 Jack Humbert
 * Copyright 2018 Yiancar
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "rgb_matrix.h"
#include "progmem.h"
#include "eeconfig.h"
#include "keyboard.h"
#include "sync_timer.h"
#include "debug.h"
#include <stddef.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

#include <lib/lib8tion/lib8tion.h>

#if defined(RGB_MATRIX_IDLE_GATE_ENABLE) && defined(MCU_RP)
/* ERA, the idle arm's millisecond gate. The register struct only, never
   pico-sdk's hardware/timer.h, which collides with the ChibiOS TIMER macro --
   the note keyboards/era/common/system/era_pass_phase_diagnostics.c and
   keyboards/era/common/split/era_split_keyboard.c carry for the same include. */
#    include "hardware/structs/timer.h"

/* Microseconds between two evaluations of the SYNCING arm. The default lives
   here because this file is its only reader.

   Both facts that arm decides are millisecond facts: rgb_task_sync() compares a
   millisecond clock against a millisecond frame limit, and the deferred
   eeconfig flush it runs is a millisecond timer. Asking them once a millisecond
   therefore reads the values asking them forty times reads.

   What one pass of quantisation costs, measured. The gate stamps at the pass
   that opens it, so two openings are 1000 to 1000-plus-one-pass microseconds
   apart and the sampling period is marginally longer than the millisecond it
   samples: the 16 ms frame limit is observed a millisecond late on some frames
   and on time on the rest. That reads as **62.34 frames a second against
   62.50**, -0.26 %, on six device windows across both halves (2026-08-17) --
   about four frames in a hundred, where the arithmetic here first said one.
   Nothing accumulates, because rgb_task_start() re-reads the epoch from the
   clock at every frame start, and nothing visible moves either: effect phase is
   computed from g_rgb_timer, the real millisecond clock, so what changed is how
   often a frame starts and not how fast anything animates. A value below 1000
   makes the sampling period strictly sub-millisecond and would recover most of
   the 0.26 % for about +0.004 us a pass -- left at 1000 deliberately, because
   the recovery is arithmetic and the figure above is a measurement
   (`era_performance_gates.md`, Fixed Baselines). */
#    ifndef RGB_MATRIX_IDLE_GATE_US
#        define RGB_MATRIX_IDLE_GATE_US 1000
#    endif

static uint32_t rgb_idle_gate_last_us;
#endif

#ifndef RGB_MATRIX_CENTER
const led_point_t k_rgb_matrix_center = {112, 32};
#else
const led_point_t k_rgb_matrix_center = RGB_MATRIX_CENTER;
#endif

__attribute__((weak)) rgb_t rgb_matrix_hsv_to_rgb(hsv_t hsv) {
    return hsv_to_rgb(hsv);
}

// Generic effect runners
#include "rgb_matrix_runners.inc"

// ------------------------------------------
// -----Begin rgb effect includes macros-----
#define RGB_MATRIX_EFFECT(name)
#define RGB_MATRIX_CUSTOM_EFFECT_IMPLS

#include "rgb_matrix_effects.inc"
#ifdef COMMUNITY_MODULES_ENABLE
#    include "rgb_matrix_community_modules.inc"
#endif
#ifdef RGB_MATRIX_CUSTOM_KB
#    include "rgb_matrix_kb.inc"
#endif
#ifdef RGB_MATRIX_CUSTOM_USER
#    include "rgb_matrix_user.inc"
#endif

#undef RGB_MATRIX_CUSTOM_EFFECT_IMPLS
#undef RGB_MATRIX_EFFECT
// -----End rgb effect includes macros-------
// ------------------------------------------

// globals
rgb_config_t rgb_matrix_config; // TODO: would like to prefix this with g_ for global consistancy, do this in another pr
uint32_t     g_rgb_timer;
#ifdef RGB_MATRIX_FRAMEBUFFER_EFFECTS
uint8_t g_rgb_frame_buffer[MATRIX_ROWS][MATRIX_COLS] = {{0}};
#endif // RGB_MATRIX_FRAMEBUFFER_EFFECTS
#ifdef RGB_MATRIX_KEYREACTIVE_ENABLED
last_hit_t g_last_hit_tracker;
#endif // RGB_MATRIX_KEYREACTIVE_ENABLED

#ifndef RGB_MATRIX_FLAG_STEPS
#    define RGB_MATRIX_FLAG_STEPS {LED_FLAG_ALL, LED_FLAG_KEYLIGHT | LED_FLAG_MODIFIER, LED_FLAG_UNDERGLOW, LED_FLAG_NONE}
#endif
static const uint8_t rgb_matrix_flag_steps[] = RGB_MATRIX_FLAG_STEPS;
#define RGB_MATRIX_FLAG_STEPS_COUNT ARRAY_SIZE(rgb_matrix_flag_steps)

// internals
static bool            suspend_state      = false;
static uint8_t         rgb_last_enable    = UINT8_MAX;
static uint8_t         rgb_last_effect    = UINT8_MAX;
static uint8_t         rgb_current_effect = 0;
static effect_params_t rgb_effect_params  = {0, LED_FLAG_ALL, false};
static rgb_task_states rgb_task_state     = SYNCING;

// double buffers
static uint32_t rgb_timer_buffer;
#ifdef RGB_MATRIX_KEYREACTIVE_ENABLED
static last_hit_t last_hit_buffer;
#endif // RGB_MATRIX_KEYREACTIVE_ENABLED

// split rgb matrix
#if defined(RGB_MATRIX_SPLIT)
const uint8_t k_rgb_matrix_split[2] = RGB_MATRIX_SPLIT;
#endif

#if defined(RGB_MATRIX_RENDER_POLICY_ENABLE)
static rgb_matrix_render_policy_t rgb_active_render_policy;
static rgb_matrix_render_policy_t rgb_last_render_policy;
static bool                       rgb_render_policy_valid;
static uint8_t                    rgb_render_frame_flags;

static bool rgb_matrix_render_policy_has(uint16_t flag) {
    return (rgb_active_render_policy.flags & flag) != 0;
}

/* The split half's private helper, and inside the guard for that reason: a
   split board cannot use the driver's set_color_all, because half the global
   LED indices belong to the other half and rgb_matrix_led_index() returns < 0
   for them. A non-split board has one call and no per-index mapping to do, so
   outside this guard the helper has no caller at all -- which
   -Werror=unused-function reports correctly, and which nothing noticed while
   every board with the ERA render policy was a split board. */
#    if defined(RGB_MATRIX_SPLIT)
static void rgb_matrix_set_color_raw(int index, uint8_t red, uint8_t green, uint8_t blue) {
    const int led_index = rgb_matrix_led_index(index);
    if (led_index < 0) {
        return;
    }

    rgb_matrix_driver.set_color(led_index, red, green, blue);
}
#    endif

static void rgb_matrix_set_color_all_raw(uint8_t red, uint8_t green, uint8_t blue) {
#    if defined(RGB_MATRIX_SPLIT)
    for (uint8_t i = 0; i < RGB_MATRIX_LED_COUNT; i++) {
        rgb_matrix_set_color_raw(i, red, green, blue);
    }
#    else
    rgb_matrix_driver.set_color_all(red, green, blue);
#    endif
}

/* Guarded to match its only call site, in rgb_matrix_set_color(), which is
   `RGB_MATRIX_RENDER_POLICY_ENABLE && RGB_MATRIX_RENDER_DOMAIN_ENABLE`. The
   definition carried only the outer half of that until 2026-08-13, so turning
   the domain sub-option off left this unreferenced and -Werror=unused-function
   failed the build -- an off state a declared selector promises. */
#    if defined(RGB_MATRIX_RENDER_DOMAIN_ENABLE)
static bool rgb_matrix_render_policy_led_allowed(uint8_t index) {
    if (rgb_matrix_render_policy_has(RGB_MATRIX_RENDER_POLICY_RENDER_DOMAIN)) {
        return index >= rgb_active_render_policy.led_min_index && index < rgb_active_render_policy.led_max_index;
    }
    return true;
}
#    endif

static void rgb_matrix_render_policy_defaults(rgb_matrix_render_policy_t *policy, uint8_t effect) {
    memset(policy, 0, sizeof(*policy));
    policy->led_min_index = 0;
    policy->led_max_index = RGB_MATRIX_LED_COUNT;
    if (effect != RGB_MATRIX_NONE) {
        policy->flags |= RGB_MATRIX_RENDER_POLICY_INDICATORS_ENABLE;
    }
}

static void rgb_matrix_render_policy_sanitize(rgb_matrix_render_policy_t *policy) {
#    if !defined(RGB_MATRIX_RENDER_DOMAIN_ENABLE)
    policy->flags &= ~RGB_MATRIX_RENDER_POLICY_RENDER_DOMAIN;
#    endif
#    if !defined(RGB_MATRIX_INDICATORS_INDEPENDENT_ENABLE)
    policy->flags &= ~(RGB_MATRIX_RENDER_POLICY_INDICATORS_DIRTY);
    if (rgb_current_effect == RGB_MATRIX_NONE) {
        policy->flags &= ~RGB_MATRIX_RENDER_POLICY_INDICATORS_ENABLE;
    }
#    endif
#    if !defined(RGB_MATRIX_INDICATORS_WHEN_DISABLED_ENABLE)
    policy->flags &= ~RGB_MATRIX_RENDER_POLICY_ALLOW_DISABLED;
#    endif
    if (!rgb_matrix_config.enable && (policy->flags & RGB_MATRIX_RENDER_POLICY_ALLOW_DISABLED) == 0) {
        policy->flags &= ~(RGB_MATRIX_RENDER_POLICY_STATUS_ACTIVE | RGB_MATRIX_RENDER_POLICY_STATUS_DIRTY | RGB_MATRIX_RENDER_POLICY_INDICATORS_ENABLE | RGB_MATRIX_RENDER_POLICY_INDICATORS_DIRTY);
    }
    if (policy->led_max_index > RGB_MATRIX_LED_COUNT) {
        policy->led_max_index = RGB_MATRIX_LED_COUNT;
    }
    if (policy->led_min_index >= policy->led_max_index) {
        policy->flags &= ~RGB_MATRIX_RENDER_POLICY_RENDER_DOMAIN;
        policy->led_min_index = 0;
        policy->led_max_index = RGB_MATRIX_LED_COUNT;
    }
    if ((policy->flags & RGB_MATRIX_RENDER_POLICY_STATUS_ACTIVE) != 0) {
        policy->flags |= RGB_MATRIX_RENDER_POLICY_DISABLE_EFFECT;
        policy->flags &= ~(RGB_MATRIX_RENDER_POLICY_RENDER_DOMAIN | RGB_MATRIX_RENDER_POLICY_INDICATORS_ENABLE | RGB_MATRIX_RENDER_POLICY_INDICATORS_DIRTY);
    }
}

static bool rgb_matrix_render_policy_domain_changed(const rgb_matrix_render_policy_t *a, const rgb_matrix_render_policy_t *b) {
    const bool a_domain = (a->flags & RGB_MATRIX_RENDER_POLICY_RENDER_DOMAIN) != 0;
    const bool b_domain = (b->flags & RGB_MATRIX_RENDER_POLICY_RENDER_DOMAIN) != 0;
    return a_domain != b_domain || (a_domain && (a->led_min_index != b->led_min_index || a->led_max_index != b->led_max_index));
}

static bool rgb_matrix_render_policy_mode_changed(const rgb_matrix_render_policy_t *policy) {
    if (!rgb_render_policy_valid) {
        return true;
    }
    const bool status_changed = ((policy->flags ^ rgb_last_render_policy.flags) & RGB_MATRIX_RENDER_POLICY_STATUS_ACTIVE) != 0;
    return status_changed || rgb_matrix_render_policy_domain_changed(policy, &rgb_last_render_policy);
}

static void rgb_matrix_render_policy_update(uint8_t effect) {
    rgb_matrix_render_policy_t policy;
    rgb_matrix_render_policy_defaults(&policy, effect);
    rgb_matrix_render_policy_kb(&policy);
    rgb_matrix_render_policy_sanitize(&policy);

    const bool mode_changed             = rgb_matrix_render_policy_mode_changed(&policy);
    const bool indicator_effect_refresh = !mode_changed &&
                                          rgb_effect_params.iter == 0 &&
                                          (policy.flags & RGB_MATRIX_RENDER_POLICY_INDICATORS_DIRTY) != 0 &&
                                          effect != RGB_MATRIX_NONE &&
                                          (policy.flags & RGB_MATRIX_RENDER_POLICY_DISABLE_EFFECT) == 0;
    if (mode_changed || indicator_effect_refresh) {
        rgb_matrix_set_color_all_raw(0, 0, 0);
        rgb_last_effect       = UINT8_MAX;
        rgb_effect_params.iter = 0;
    }
    if (mode_changed) {
        if ((policy.flags & RGB_MATRIX_RENDER_POLICY_STATUS_ACTIVE) != 0) {
            policy.flags |= RGB_MATRIX_RENDER_POLICY_STATUS_DIRTY;
        }
        if ((policy.flags & RGB_MATRIX_RENDER_POLICY_INDICATORS_ENABLE) != 0) {
            policy.flags |= RGB_MATRIX_RENDER_POLICY_INDICATORS_DIRTY;
        }
    }

    rgb_active_render_policy = policy;
    rgb_last_render_policy   = policy;
    rgb_render_policy_valid  = true;
}
#endif

#if defined(ERA_STORAGE_QUIET_DEFER_MS)
EECONFIG_QUIET_DEBOUNCE_HELPER(rgb_matrix, rgb_matrix_config);
#else
EECONFIG_DEBOUNCE_HELPER(rgb_matrix, rgb_matrix_config);
#endif

void eeconfig_force_flush_rgb_matrix(void) {
    eeconfig_flush_rgb_matrix(true);
}

#if defined(ERA_STORAGE_QUIET_DEFER_MS)
void eeconfig_defer_flush_rgb_matrix(void) {
    eeconfig_schedule_deferred_flush_rgb_matrix();
}

void eeconfig_flush_rgb_matrix_deferred_task(void) {
    eeconfig_run_deferred_flush_rgb_matrix();
}

/* Commit an approved-but-unwritten config now, ignoring the quiet timer, and
 * write nothing when none is pending. That last half is what separates this
 * from eeconfig_force_flush_rgb_matrix(), which writes unconditionally -- the
 * callers of this one are the controlled-reset and suspend paths, where writing
 * a config nobody approved would persist whatever a suspend routine had just
 * put in the live object.
 *
 * Clearing `deferred` is how the timer is bypassed rather than a second
 * predicate: with it clear eeconfig_deferred_ready_rgb_matrix() answers true,
 * so the flush below runs on `dirty` alone -- which is exactly "a save was
 * approved and has not landed yet". The statics are this translation unit's,
 * declared by the helper macro instantiated above.
 */
void eeconfig_flush_rgb_matrix_deferred_now(void) {
    deferred_rgb_matrix = false;
    eeconfig_run_deferred_flush_rgb_matrix();
}
#endif

void eeconfig_update_rgb_matrix_default(void) {
    dprintf("eeconfig_update_rgb_matrix_default\n");
    rgb_matrix_config.enable = RGB_MATRIX_DEFAULT_ON;
    rgb_matrix_config.mode   = RGB_MATRIX_DEFAULT_MODE;
    rgb_matrix_config.hsv    = (hsv_t){RGB_MATRIX_DEFAULT_HUE, RGB_MATRIX_DEFAULT_SAT, RGB_MATRIX_DEFAULT_VAL};
    rgb_matrix_config.speed  = RGB_MATRIX_DEFAULT_SPD;
    rgb_matrix_config.flags  = RGB_MATRIX_DEFAULT_FLAGS;
    eeconfig_flush_rgb_matrix(true);
}

void eeconfig_debug_rgb_matrix(void) {
    dprintf("rgb_matrix_config EEPROM\n");
    dprintf("rgb_matrix_config.enable = %d\n", rgb_matrix_config.enable);
#ifdef RGB_MATRIX_MODE_NAME_ENABLE
    dprintf("rgb_matrix_config.mode = %d (%s)\n", rgb_matrix_config.mode, rgb_matrix_get_mode_name(rgb_matrix_config.mode));
#else
    dprintf("rgb_matrix_config.mode = %d\n", rgb_matrix_config.mode);
#endif // RGB_MATRIX_MODE_NAME_ENABLE
    dprintf("rgb_matrix_config.hsv.h = %d\n", rgb_matrix_config.hsv.h);
    dprintf("rgb_matrix_config.hsv.s = %d\n", rgb_matrix_config.hsv.s);
    dprintf("rgb_matrix_config.hsv.v = %d\n", rgb_matrix_config.hsv.v);
    dprintf("rgb_matrix_config.speed = %d\n", rgb_matrix_config.speed);
    dprintf("rgb_matrix_config.flags = %d\n", rgb_matrix_config.flags);
}

void rgb_matrix_reload_from_eeprom(void) {
    rgb_matrix_disable_noeeprom();
    /* Reset back to what we have in eeprom */
    eeconfig_init_rgb_matrix();
    eeconfig_debug_rgb_matrix(); // display current eeprom values
    if (rgb_matrix_config.enable) {
        rgb_matrix_mode_noeeprom(rgb_matrix_config.mode);
    }
}

__attribute__((weak)) uint8_t rgb_matrix_map_row_column_to_led_kb(uint8_t row, uint8_t column, uint8_t *led_i) {
    return 0;
}

uint8_t rgb_matrix_map_row_column_to_led(uint8_t row, uint8_t column, uint8_t *led_i) {
    uint8_t led_count = rgb_matrix_map_row_column_to_led_kb(row, column, led_i);
    uint8_t led_index = g_led_config.matrix_co[row][column];
    if (led_index != NO_LED) {
        led_i[led_count] = led_index;
        led_count++;
    }
    return led_count;
}

void rgb_matrix_update_pwm_buffers(void) {
    rgb_matrix_driver.flush();
}

__attribute__((weak)) int rgb_matrix_led_index(int index) {
#if defined(RGB_MATRIX_SPLIT)
    const bool index_in_left_side = index < k_rgb_matrix_split[0];

    if (is_keyboard_left()) {
        if (index_in_left_side) {
            return index;
        }
        return -1;
    }

    if (index_in_left_side) {
        return -1;
    }

    return index - k_rgb_matrix_split[0];
#endif
    return index;
}

void rgb_matrix_set_color(int index, uint8_t red, uint8_t green, uint8_t blue) {
#if defined(RGB_MATRIX_RENDER_POLICY_ENABLE) && defined(RGB_MATRIX_RENDER_DOMAIN_ENABLE)
    /* The domain is asked with an unsigned index, so a negative one has to be
       refused before the cast rather than at the `led_index < 0` test below
       that upstream already makes. The guard belongs inside this arm and not
       above it: without the domain there is nothing to protect, and an
       unconditional guard here would be an ERA edit every keyboard in the fork
       compiles for a feature only ERA boards have. */
    if (index < 0) {
        return;
    }
    if (!rgb_matrix_render_policy_led_allowed((uint8_t)index)) {
        return;
    }
#endif
    const int led_index = rgb_matrix_led_index(index);
    if (led_index < 0) {
        return;
    }

    rgb_matrix_driver.set_color(led_index, red, green, blue);
}

void rgb_matrix_set_color_all(uint8_t red, uint8_t green, uint8_t blue) {
#if defined(RGB_MATRIX_RENDER_POLICY_ENABLE) && defined(RGB_MATRIX_RENDER_DOMAIN_ENABLE)
    if (rgb_matrix_render_policy_has(RGB_MATRIX_RENDER_POLICY_RENDER_DOMAIN)) {
        for (uint8_t i = rgb_active_render_policy.led_min_index; i < rgb_active_render_policy.led_max_index; i++) {
            rgb_matrix_set_color(i, red, green, blue);
        }
        return;
    }
#endif
#if defined(RGB_MATRIX_SPLIT)
    for (uint8_t i = 0; i < RGB_MATRIX_LED_COUNT; i++)
        rgb_matrix_set_color(i, red, green, blue);
#else
    rgb_matrix_driver.set_color_all(red, green, blue);
#endif
}

void rgb_matrix_handle_key_event(uint8_t row, uint8_t col, bool pressed) {
#ifndef RGB_MATRIX_SPLIT
    if (!is_keyboard_master()) return;
#endif

#ifdef RGB_MATRIX_KEYREACTIVE_ENABLED
    uint8_t led[LED_HITS_TO_REMEMBER];
    uint8_t led_count = 0;

#    if defined(RGB_MATRIX_KEYRELEASES)
    if (!pressed)
#    elif defined(RGB_MATRIX_KEYPRESSES)
    if (pressed)
#    endif // defined(RGB_MATRIX_KEYRELEASES)
    {
        led_count = rgb_matrix_map_row_column_to_led(row, col, led);
    }

    if (last_hit_buffer.count + led_count > LED_HITS_TO_REMEMBER) {
        memcpy(&last_hit_buffer.x[0], &last_hit_buffer.x[led_count], LED_HITS_TO_REMEMBER - led_count);
        memcpy(&last_hit_buffer.y[0], &last_hit_buffer.y[led_count], LED_HITS_TO_REMEMBER - led_count);
        memcpy(&last_hit_buffer.tick[0], &last_hit_buffer.tick[led_count], (LED_HITS_TO_REMEMBER - led_count) * 2); // 16 bit
        memcpy(&last_hit_buffer.index[0], &last_hit_buffer.index[led_count], LED_HITS_TO_REMEMBER - led_count);
        last_hit_buffer.count = LED_HITS_TO_REMEMBER - led_count;
    }

    for (uint8_t i = 0; i < led_count; i++) {
        uint8_t index                = last_hit_buffer.count;
        last_hit_buffer.x[index]     = g_led_config.point[led[i]].x;
        last_hit_buffer.y[index]     = g_led_config.point[led[i]].y;
        last_hit_buffer.index[index] = led[i];
        last_hit_buffer.tick[index]  = 0;
        last_hit_buffer.count++;
    }
#endif // RGB_MATRIX_KEYREACTIVE_ENABLED

#if defined(RGB_MATRIX_FRAMEBUFFER_EFFECTS) && defined(ENABLE_RGB_MATRIX_TYPING_HEATMAP)
#    if defined(RGB_MATRIX_KEYRELEASES)
    if (!pressed)
#    else
    if (pressed)
#    endif // defined(RGB_MATRIX_KEYRELEASES)
    {
        if (rgb_matrix_config.mode == RGB_MATRIX_TYPING_HEATMAP) {
            process_rgb_matrix_typing_heatmap(row, col);
        }
    }
#endif // defined(RGB_MATRIX_FRAMEBUFFER_EFFECTS) && defined(ENABLE_RGB_MATRIX_TYPING_HEATMAP)
}

void rgb_matrix_test(void) {
    // Mask out bits 4 and 5
    // Increase the factor to make the test animation slower (and reduce to make it faster)
    uint8_t factor = 10;
    switch ((g_rgb_timer & (0b11 << factor)) >> factor) {
        case 0: {
            rgb_matrix_set_color_all(20, 0, 0);
            break;
        }
        case 1: {
            rgb_matrix_set_color_all(0, 20, 0);
            break;
        }
        case 2: {
            rgb_matrix_set_color_all(0, 0, 20);
            break;
        }
        case 3: {
            rgb_matrix_set_color_all(20, 20, 20);
            break;
        }
    }
}

static bool rgb_matrix_none(effect_params_t *params) {
    if (!params->init) {
        return false;
    }

    rgb_matrix_set_color_all(0, 0, 0);
    return false;
}

static void rgb_task_timers(void) {
    /* ERA: one clock reading, not two. `sync_timer_elapsed32(x)` is
       `sync_timer_read32() - x` on both arms of its own predicate, so the pair
       below took two readings of the same instant -- and the two could straddle
       a millisecond boundary, leaving `deltaTime` computed against a different
       instant than the one stored. One reading is both cheaper and the more
       correct of the two, which is why it is not gated: it is a fix that
       happens to be faster rather than an optimisation.
       Priced 2026-08-16: `timer_read32()` is about 0.42 us on the ERA image
       even from its millisecond cache, and this task asks for it on every
       matrix scan pass against a 16 ms frame. */
    uint32_t now = sync_timer_read32();
#if defined(RGB_MATRIX_KEYREACTIVE_ENABLED)
    uint32_t deltaTime = now - rgb_timer_buffer;
#endif // defined(RGB_MATRIX_KEYREACTIVE_ENABLED)
    rgb_timer_buffer = now;

    // Update double buffer last hit timers
#ifdef RGB_MATRIX_KEYREACTIVE_ENABLED
    /* ERA: skipped whole when no millisecond has passed, which is provably the
       same walk and not a shortened one. `deltaTime` is in milliseconds and this
       task runs at the matrix scan rate, so it is zero on about thirty-nine of
       every forty passes -- and with it zero the expiry test is
       `UINT16_MAX < tick[i]`, false for every uint16 there is, and the body is
       `tick[i] += 0`. The loop already did nothing; it just did it at about
       four microseconds per live entry per pass.
       Measured 2026-08-16: 0.87 us a pass with three live entries, and an entry
       lives 65,535 ms after the key that made it -- so a keyboard being typed on
       carries the full eight and pays about 2.3 us a pass for a walk whose
       result cannot change. It is also what the qwin window's `seg=` step was:
       a window opens on a keypress, so its first sixty-five seconds ran this. */
    if (deltaTime != 0) {
        uint8_t count = last_hit_buffer.count;
        for (uint8_t i = 0; i < count; ++i) {
            if (UINT16_MAX - deltaTime < last_hit_buffer.tick[i]) {
                last_hit_buffer.count--;
                continue;
            }
            last_hit_buffer.tick[i] += deltaTime;
        }
    }
#endif // RGB_MATRIX_KEYREACTIVE_ENABLED
}

static void rgb_task_sync(void) {
#if defined(ERA_STORAGE_QUIET_DEFER_MS)
    eeconfig_flush_rgb_matrix_deferred_task();
#else
    eeconfig_flush_rgb_matrix(false);
#endif
    /* Next task, against the reading `rgb_task_timers()` already took this pass
       rather than a second one of its own.
       ERA, and the same substitution the timer head takes above: this is the
       *more* correct comparison, not merely the cheaper one. `rgb_task_start()`
       sets the frame epoch from `rgb_timer_buffer`, so the frame is measured
       from the head's reading -- and asking `sync_timer_elapsed32()` here
       compared it against a different instant, one that can sit on the other
       side of a millisecond boundary. The frame limit is in milliseconds and the
       two readings are microseconds apart, so what this can move is one pass at
       a boundary, on a sixteen-millisecond frame. */
    if ((uint32_t)(rgb_timer_buffer - g_rgb_timer) >= RGB_MATRIX_LED_FLUSH_LIMIT) rgb_task_state = STARTING;
}

static void rgb_task_start(void) {
    // reset iter
    rgb_effect_params.iter = 0;

    // update double buffers
    g_rgb_timer = rgb_timer_buffer;
#ifdef RGB_MATRIX_KEYREACTIVE_ENABLED
    g_last_hit_tracker = last_hit_buffer;
#endif // RGB_MATRIX_KEYREACTIVE_ENABLED

    // Ideally we would also stop sending zeros to the LED driver PWM buffers
    // while suspended and just do a software shutdown. This is a cheap hack for now.
    bool suspend_backlight = suspend_state ||
#if RGB_MATRIX_TIMEOUT > 0
                             (last_input_activity_elapsed() > (uint32_t)RGB_MATRIX_TIMEOUT) ||
#endif // RGB_MATRIX_TIMEOUT > 0
                             false;

    // Set effect to be renedered
    rgb_current_effect = suspend_backlight || !rgb_matrix_config.enable ? 0 : rgb_matrix_config.mode;

    // next task
    rgb_task_state = RENDERING;
}

static void rgb_task_render(uint8_t effect) {
    bool rendering         = false;
    rgb_effect_params.init = (effect != rgb_last_effect) || (rgb_matrix_config.enable != rgb_last_enable);
    if (rgb_effect_params.flags != rgb_matrix_config.flags) {
        rgb_effect_params.flags = rgb_matrix_config.flags;
        rgb_matrix_set_color_all(0, 0, 0);
    }

    // each effect can opt to do calculations
    // and/or request PWM buffer updates.
    switch (effect) {
        case RGB_MATRIX_NONE:
            rendering = rgb_matrix_none(&rgb_effect_params);
            break;

// ---------------------------------------------
// -----Begin rgb effect switch case macros-----
#define RGB_MATRIX_EFFECT(name, ...)          \
    case RGB_MATRIX_##name:                   \
        rendering = name(&rgb_effect_params); \
        break;
#include "rgb_matrix_effects.inc"
#undef RGB_MATRIX_EFFECT

#ifdef COMMUNITY_MODULES_ENABLE
#    define RGB_MATRIX_EFFECT(name, ...)          \
        case RGB_MATRIX_COMMUNITY_MODULE_##name:  \
            rendering = name(&rgb_effect_params); \
            break;
#    include "rgb_matrix_community_modules.inc"
#    undef RGB_MATRIX_EFFECT
#endif

#if defined(RGB_MATRIX_CUSTOM_KB) || defined(RGB_MATRIX_CUSTOM_USER)
#    define RGB_MATRIX_EFFECT(name, ...)          \
        case RGB_MATRIX_CUSTOM_##name:            \
            rendering = name(&rgb_effect_params); \
            break;
#    ifdef RGB_MATRIX_CUSTOM_KB
#        include "rgb_matrix_kb.inc"
#    endif
#    ifdef RGB_MATRIX_CUSTOM_USER
#        include "rgb_matrix_user.inc"
#    endif
#    undef RGB_MATRIX_EFFECT
#endif
            // -----End rgb effect switch case macros-------
            // ---------------------------------------------

        // Factory default magic value
        case UINT8_MAX: {
            rgb_matrix_test();
            rgb_task_state = FLUSHING;
        }
            return;
    }

    rgb_effect_params.iter++;

    // next task
    if (!rendering) {
        rgb_task_state = FLUSHING;
        if (!rgb_effect_params.init && effect == RGB_MATRIX_NONE) {
            // We only need to flush once if we are RGB_MATRIX_NONE
            rgb_task_state = SYNCING;
        }
    }
}

static void rgb_task_flush(uint8_t effect) {
    // update last trackers after the first full render so we can init over several frames
    rgb_last_effect = effect;
    rgb_last_enable = rgb_matrix_config.enable;

    // update pwm buffers
    rgb_matrix_update_pwm_buffers();

#if defined(RGB_MATRIX_RENDER_POLICY_ENABLE)
    /* Every PWM push reports, a zero-flag push included (2026-08-14). The
     * flush hook is where a board keeps its held-STATUS-frame proof and where
     * the EEPROM SYNC indicator's LED truth is stamped, and a push this hook
     * does not see is a repaint that proof silently survives: a transition to
     * RGB_MATRIX_NONE black-fills the buffer and reaches this flush with no
     * frame flag set, so the board kept believing the held red field was on
     * the LEDs while the panel had gone dark — and nothing repainted it until
     * the field's era ended. Reporting flags==0 is one call per such frame;
     * ordinary effect frames carried a nonzero flag and called this already. */
    rgb_matrix_render_policy_flush_kb(rgb_render_frame_flags);
    rgb_render_frame_flags = 0;
#endif

    // next task
    rgb_task_state = SYNCING;
}

#if defined(RGB_MATRIX_RENDER_POLICY_ENABLE)
bool rgb_matrix_indicators_advanced_modules(uint8_t led_min, uint8_t led_max);

/* Guarded to match its only call site, the independent-indicator arm below.
   Same defect and same date as rgb_matrix_render_policy_led_allowed(): the
   definition sat one guard wider than its caller, so the sub-option's off
   state failed the build instead of building without it. */
#    if defined(RGB_MATRIX_INDICATORS_INDEPENDENT_ENABLE)
static void rgb_matrix_indicators_advanced_range(uint8_t led_min, uint8_t led_max) {
    rgb_matrix_indicators_advanced_modules(led_min, led_max);
    rgb_matrix_indicators_advanced_kb(led_min, led_max);
}
#    endif

static void rgb_matrix_task_render_with_policy(uint8_t effect) {
    rgb_matrix_render_policy_update(effect);

    if (rgb_matrix_render_policy_has(RGB_MATRIX_RENDER_POLICY_STATUS_ACTIVE)) {
        if (rgb_matrix_render_policy_has(RGB_MATRIX_RENDER_POLICY_STATUS_DIRTY) && rgb_matrix_render_status_kb(&rgb_active_render_policy)) {
            rgb_render_frame_flags |= RGB_MATRIX_RENDER_FRAME_STATUS;
            rgb_task_state = FLUSHING;
        } else {
            rgb_task_state = SYNCING;
        }
        return;
    }

    const bool effect_enabled = effect != RGB_MATRIX_NONE && !rgb_matrix_render_policy_has(RGB_MATRIX_RENDER_POLICY_DISABLE_EFFECT);
    if (effect_enabled || effect == RGB_MATRIX_NONE) {
        rgb_task_render(effect);
        if (effect_enabled) {
            rgb_render_frame_flags |= RGB_MATRIX_RENDER_FRAME_EFFECT;
        }
    } else {
        rgb_task_state = SYNCING;
    }

    if (!rgb_matrix_render_policy_has(RGB_MATRIX_RENDER_POLICY_INDICATORS_ENABLE)) {
        return;
    }

    if (effect_enabled) {
        if (rgb_task_state == FLUSHING) {
            rgb_matrix_indicators();
        }
        rgb_matrix_indicators_advanced(&rgb_effect_params);
        rgb_render_frame_flags |= RGB_MATRIX_RENDER_FRAME_INDICATORS;
        return;
    }

#    if defined(RGB_MATRIX_INDICATORS_INDEPENDENT_ENABLE)
    if (rgb_matrix_render_policy_has(RGB_MATRIX_RENDER_POLICY_INDICATORS_DIRTY) || rgb_task_state == FLUSHING) {
        struct rgb_matrix_limits_t limits = rgb_matrix_get_limits(0);
        rgb_matrix_indicators();
        rgb_matrix_indicators_advanced_range(limits.led_min_index, limits.led_max_index);
        rgb_render_frame_flags |= RGB_MATRIX_RENDER_FRAME_INDICATORS;
        rgb_task_state = FLUSHING;
    }
#    endif
}
#endif

#ifdef ERA_PASS_PHASE_DIAGNOSTICS_ENABLE
/* ERA pass-phase instrument (keyboards/era/common/system/
   era_pass_phase_diagnostics.h). Declared here rather than included, the
   treatment quantum/keyboard.c's marks and quantum/action_layer.c's accessor
   take: keyboards/era is not on core's include path.

   The ids are the header's; the five below tile this task. Exactly one arm runs
   per pass, so an arm's maximum is that arm's own cost -- and the render arm's
   cost is what bounds how long moving this task to core1 may delay a wire
   exchange, which in HOST-PEER carries half the keyboard's keys. */
#    define ERA_PASS_PHASE_RGB_TIMERS 0U
#    define ERA_PASS_PHASE_RGB_START 1U
#    define ERA_PASS_PHASE_RGB_RENDER 2U
#    define ERA_PASS_PHASE_RGB_FLUSH 3U
#    define ERA_PASS_PHASE_RGB_SYNC 4U
void era_pass_phase_rgb_mark(uint8_t part);
#    define ERA_RGB_PHASE_MARK(part) era_pass_phase_rgb_mark(part)
#else
#    define ERA_RGB_PHASE_MARK(part) ((void)0)
#endif

void rgb_matrix_task(void) {
#if defined(RGB_MATRIX_IDLE_GATE_ENABLE) && defined(MCU_RP)
    /* ERA: the idle arm, and only the idle arm. SYNCING is about ninety-nine of
       every hundred passes on this image, and the whole of what it decides is
       two millisecond comparisons -- yet it costs 1.42 us a pass between the
       timer head's clock reading and its own body, against a 16 ms frame.

       The three working arms are untouched and still run on every pass, so the
       longest pass this task can produce does not move. That is the difference
       between this and a gate over the whole task, which was refused: this
       removes the bookkeeping of the passes that do nothing and leaves the work
       of the ones that do exactly where it was.

       Nothing outside this task leaves the state machine in SYNCING for
       something it needs answered sooner. Every external change -- mode,
       toggle, enable, disable -- writes STARTING, which this gate does not
       test, so the next pass serves it exactly as it did before. */
    if (rgb_task_state == SYNCING) {
        const uint32_t now_us = timer_hw->timerawl;
        if ((uint32_t)(now_us - rgb_idle_gate_last_us) < RGB_MATRIX_IDLE_GATE_US) {
            return;
        }
        rgb_idle_gate_last_us = now_us;
    }
#endif
    rgb_task_timers();
    ERA_RGB_PHASE_MARK(ERA_PASS_PHASE_RGB_TIMERS);

    uint8_t effect = rgb_current_effect;

    switch (rgb_task_state) {
        case STARTING:
            rgb_task_start();
            ERA_RGB_PHASE_MARK(ERA_PASS_PHASE_RGB_START);
            break;
        case RENDERING:
#if defined(RGB_MATRIX_RENDER_POLICY_ENABLE)
            rgb_matrix_task_render_with_policy(effect);
#else
            rgb_task_render(effect);
            if (effect) {
                if (rgb_task_state == FLUSHING) { // ensure we only draw basic indicators once rendering is finished
                    rgb_matrix_indicators();
                }
                rgb_matrix_indicators_advanced(&rgb_effect_params);
            }
#endif
            ERA_RGB_PHASE_MARK(ERA_PASS_PHASE_RGB_RENDER);
            break;
        case FLUSHING:
            rgb_task_flush(effect);
            ERA_RGB_PHASE_MARK(ERA_PASS_PHASE_RGB_FLUSH);
            break;
        case SYNCING:
            rgb_task_sync();
            ERA_RGB_PHASE_MARK(ERA_PASS_PHASE_RGB_SYNC);
            break;
    }
}

__attribute__((weak)) bool rgb_matrix_indicators_modules(void) {
    return true;
}

void rgb_matrix_indicators(void) {
    rgb_matrix_indicators_modules();
    rgb_matrix_indicators_kb();
}

__attribute__((weak)) bool rgb_matrix_indicators_kb(void) {
    return rgb_matrix_indicators_user();
}

__attribute__((weak)) bool rgb_matrix_indicators_user(void) {
    return true;
}

#if defined(RGB_MATRIX_RENDER_POLICY_ENABLE)
__attribute__((weak)) void rgb_matrix_render_policy_user(rgb_matrix_render_policy_t *policy) {
    (void)policy;
}

__attribute__((weak)) void rgb_matrix_render_policy_kb(rgb_matrix_render_policy_t *policy) {
    rgb_matrix_render_policy_user(policy);
}

__attribute__((weak)) bool rgb_matrix_render_status_user(const rgb_matrix_render_policy_t *policy) {
    (void)policy;
    return false;
}

__attribute__((weak)) bool rgb_matrix_render_status_kb(const rgb_matrix_render_policy_t *policy) {
    return rgb_matrix_render_status_user(policy);
}

__attribute__((weak)) void rgb_matrix_render_policy_flush_user(uint8_t frame_flags) {
    (void)frame_flags;
}

__attribute__((weak)) void rgb_matrix_render_policy_flush_kb(uint8_t frame_flags) {
    rgb_matrix_render_policy_flush_user(frame_flags);
}
#endif

struct rgb_matrix_limits_t rgb_matrix_get_limits(uint8_t iter) {
    struct rgb_matrix_limits_t limits = {0};
#if defined(RGB_MATRIX_RENDER_POLICY_ENABLE) && defined(RGB_MATRIX_RENDER_DOMAIN_ENABLE)
    if (rgb_matrix_render_policy_has(RGB_MATRIX_RENDER_POLICY_RENDER_DOMAIN)) {
        uint8_t domain_min = rgb_active_render_policy.led_min_index;
        uint8_t domain_max = rgb_active_render_policy.led_max_index;
#    if defined(RGB_MATRIX_SPLIT)
        if (is_keyboard_left()) {
            if (domain_max > k_rgb_matrix_split[0]) domain_max = k_rgb_matrix_split[0];
        } else {
            if (domain_min < k_rgb_matrix_split[0]) domain_min = k_rgb_matrix_split[0];
        }
#    endif
        if (domain_min >= domain_max) {
            limits.led_min_index = domain_max;
            limits.led_max_index = domain_max;
            return limits;
        }
#    if defined(RGB_MATRIX_LED_PROCESS_LIMIT) && RGB_MATRIX_LED_PROCESS_LIMIT > 0 && RGB_MATRIX_LED_PROCESS_LIMIT < RGB_MATRIX_LED_COUNT
        limits.led_min_index = domain_min + RGB_MATRIX_LED_PROCESS_LIMIT * iter;
        if (limits.led_min_index > domain_max) limits.led_min_index = domain_max;
        limits.led_max_index = limits.led_min_index + RGB_MATRIX_LED_PROCESS_LIMIT;
        if (limits.led_max_index > domain_max) limits.led_max_index = domain_max;
#    else
        limits.led_min_index = domain_min;
        limits.led_max_index = domain_max;
#    endif
        return limits;
    }
#endif
#if defined(RGB_MATRIX_LED_PROCESS_LIMIT) && RGB_MATRIX_LED_PROCESS_LIMIT > 0 && RGB_MATRIX_LED_PROCESS_LIMIT < RGB_MATRIX_LED_COUNT
#    if defined(RGB_MATRIX_SPLIT)
    limits.led_min_index = RGB_MATRIX_LED_PROCESS_LIMIT * (iter);
    limits.led_max_index = limits.led_min_index + RGB_MATRIX_LED_PROCESS_LIMIT;
    if (limits.led_max_index > RGB_MATRIX_LED_COUNT) limits.led_max_index = RGB_MATRIX_LED_COUNT;
    if (is_keyboard_left() && (limits.led_max_index > k_rgb_matrix_split[0])) limits.led_max_index = k_rgb_matrix_split[0];
    if (!(is_keyboard_left()) && (limits.led_min_index < k_rgb_matrix_split[0])) limits.led_min_index = k_rgb_matrix_split[0];
#    else
    limits.led_min_index = RGB_MATRIX_LED_PROCESS_LIMIT * (iter);
    limits.led_max_index = limits.led_min_index + RGB_MATRIX_LED_PROCESS_LIMIT;
    if (limits.led_max_index > RGB_MATRIX_LED_COUNT) limits.led_max_index = RGB_MATRIX_LED_COUNT;
#    endif
#else
#    if defined(RGB_MATRIX_SPLIT)
    limits.led_min_index = 0;
    limits.led_max_index = RGB_MATRIX_LED_COUNT;
    if (is_keyboard_left() && (limits.led_max_index > k_rgb_matrix_split[0])) limits.led_max_index = k_rgb_matrix_split[0];
    if (!(is_keyboard_left()) && (limits.led_min_index < k_rgb_matrix_split[0])) limits.led_min_index = k_rgb_matrix_split[0];
#    else
    limits.led_min_index = 0;
    limits.led_max_index = RGB_MATRIX_LED_COUNT;
#    endif
#endif
    return limits;
}

#if defined(RGB_MATRIX_RENDER_DOMAIN_ENABLE)
bool rgb_matrix_check_finished_leds(uint8_t led_idx) {
    uint8_t led_max = RGB_MATRIX_LED_COUNT;
#    if defined(RGB_MATRIX_SPLIT)
    if (is_keyboard_left()) {
        led_max = k_rgb_matrix_split[0];
    }
#    endif
#    if defined(RGB_MATRIX_RENDER_POLICY_ENABLE)
    if (rgb_matrix_render_policy_has(RGB_MATRIX_RENDER_POLICY_RENDER_DOMAIN)) {
        uint8_t domain_max = rgb_active_render_policy.led_max_index;
#        if defined(RGB_MATRIX_SPLIT)
        if (is_keyboard_left() && domain_max > k_rgb_matrix_split[0]) {
            domain_max = k_rgb_matrix_split[0];
        }
#        endif
        if (domain_max < led_max) {
            led_max = domain_max;
        }
    }
#    endif
    return led_idx < led_max;
}
#endif

__attribute__((weak)) bool rgb_matrix_indicators_advanced_modules(uint8_t led_min, uint8_t led_max) {
    return true;
}

void rgb_matrix_indicators_advanced(effect_params_t *params) {
    /* special handling is needed for "params->iter", since it's already been incremented.
     * Could move the invocations to rgb_task_render, but then it's missing a few checks
     * and not sure which would be better. Otherwise, this should be called from
     * rgb_task_render, right before the iter++ line.
     */
    RGB_MATRIX_USE_LIMITS_ITER(min, max, params->iter - 1);
    rgb_matrix_indicators_advanced_modules(min, max);
    rgb_matrix_indicators_advanced_kb(min, max);
}

__attribute__((weak)) bool rgb_matrix_indicators_advanced_kb(uint8_t led_min, uint8_t led_max) {
    return rgb_matrix_indicators_advanced_user(led_min, led_max);
}

__attribute__((weak)) bool rgb_matrix_indicators_advanced_user(uint8_t led_min, uint8_t led_max) {
    return true;
}

void rgb_matrix_init(void) {
    rgb_matrix_driver.init();

#ifdef RGB_MATRIX_KEYREACTIVE_ENABLED
    g_last_hit_tracker.count = 0;
    for (uint8_t i = 0; i < LED_HITS_TO_REMEMBER; ++i) {
        g_last_hit_tracker.tick[i] = UINT16_MAX;
    }

    last_hit_buffer.count = 0;
    for (uint8_t i = 0; i < LED_HITS_TO_REMEMBER; ++i) {
        last_hit_buffer.tick[i] = UINT16_MAX;
    }
#endif // RGB_MATRIX_KEYREACTIVE_ENABLED

    eeconfig_init_rgb_matrix();
    if (!rgb_matrix_config.mode) {
        dprintf("rgb_matrix_init_drivers rgb_matrix_config.mode = 0. Write default values to EEPROM.\n");
        eeconfig_update_rgb_matrix_default();
    }
    eeconfig_debug_rgb_matrix(); // display current eeprom values
}

void rgb_matrix_set_suspend_state(bool state) {
#ifdef RGB_MATRIX_SLEEP
    if (state && !suspend_state) { // only run if turning off, and only once
        rgb_task_render(0);        // turn off all LEDs when suspending
        rgb_task_flush(0);         // and actually flash led state to LEDs
    }
    suspend_state = state;
#endif
}

bool rgb_matrix_get_suspend_state(void) {
    return suspend_state;
}

void rgb_matrix_toggle_eeprom_helper(bool write_to_eeprom) {
    rgb_matrix_config.enable ^= 1;
    rgb_task_state = STARTING;
    eeconfig_flag_rgb_matrix(write_to_eeprom);
    dprintf("rgb matrix toggle [%s]: rgb_matrix_config.enable = %u\n", (write_to_eeprom) ? "EEPROM" : "NOEEPROM", rgb_matrix_config.enable);
}
void rgb_matrix_toggle_noeeprom(void) {
    rgb_matrix_toggle_eeprom_helper(false);
}
void rgb_matrix_toggle(void) {
    rgb_matrix_toggle_eeprom_helper(true);
}

void rgb_matrix_enable(void) {
    rgb_matrix_enable_noeeprom();
    eeconfig_flag_rgb_matrix(true);
}

void rgb_matrix_enable_noeeprom(void) {
    if (!rgb_matrix_config.enable) rgb_task_state = STARTING;
    rgb_matrix_config.enable = 1;
}

void rgb_matrix_disable(void) {
    rgb_matrix_disable_noeeprom();
    eeconfig_flag_rgb_matrix(true);
}

void rgb_matrix_disable_noeeprom(void) {
    if (rgb_matrix_config.enable) rgb_task_state = STARTING;
    rgb_matrix_config.enable = 0;
}

uint8_t rgb_matrix_is_enabled(void) {
    return rgb_matrix_config.enable;
}

void rgb_matrix_mode_eeprom_helper(uint8_t mode, bool write_to_eeprom) {
    if (!rgb_matrix_config.enable) {
        return;
    }
    if (mode < 1) {
        rgb_matrix_config.mode = 1;
    } else if (mode >= RGB_MATRIX_EFFECT_MAX) {
        rgb_matrix_config.mode = RGB_MATRIX_EFFECT_MAX - 1;
    } else {
        rgb_matrix_config.mode = mode;
    }
    rgb_task_state = STARTING;
    eeconfig_flag_rgb_matrix(write_to_eeprom);
#ifdef RGB_MATRIX_MODE_NAME_ENABLE
    dprintf("rgb matrix mode [%s]: %u (%s)\n", (write_to_eeprom) ? "EEPROM" : "NOEEPROM", (unsigned)rgb_matrix_config.mode, rgb_matrix_get_mode_name(rgb_matrix_config.mode));
#else
    dprintf("rgb matrix mode [%s]: %u\n", (write_to_eeprom) ? "EEPROM" : "NOEEPROM", (unsigned)rgb_matrix_config.mode);
#endif // RGB_MATRIX_MODE_NAME_ENABLE
}
void rgb_matrix_mode_noeeprom(uint8_t mode) {
    rgb_matrix_mode_eeprom_helper(mode, false);
}
void rgb_matrix_mode(uint8_t mode) {
    rgb_matrix_mode_eeprom_helper(mode, true);
}

uint8_t rgb_matrix_get_mode(void) {
    return rgb_matrix_config.mode;
}

void rgb_matrix_step_helper(bool write_to_eeprom) {
    uint8_t mode = rgb_matrix_config.mode + 1;
    rgb_matrix_mode_eeprom_helper((mode < RGB_MATRIX_EFFECT_MAX) ? mode : 1, write_to_eeprom);
}
void rgb_matrix_step_noeeprom(void) {
    rgb_matrix_step_helper(false);
}
void rgb_matrix_step(void) {
    rgb_matrix_step_helper(true);
}

void rgb_matrix_step_reverse_helper(bool write_to_eeprom) {
    uint8_t mode = rgb_matrix_config.mode - 1;
    rgb_matrix_mode_eeprom_helper((mode < 1) ? RGB_MATRIX_EFFECT_MAX - 1 : mode, write_to_eeprom);
}
void rgb_matrix_step_reverse_noeeprom(void) {
    rgb_matrix_step_reverse_helper(false);
}
void rgb_matrix_step_reverse(void) {
    rgb_matrix_step_reverse_helper(true);
}

void rgb_matrix_sethsv_eeprom_helper(uint16_t hue, uint8_t sat, uint8_t val, bool write_to_eeprom) {
    if (!rgb_matrix_config.enable) {
        return;
    }
    rgb_matrix_config.hsv.h = hue;
    rgb_matrix_config.hsv.s = sat;
    rgb_matrix_config.hsv.v = (val > RGB_MATRIX_MAXIMUM_BRIGHTNESS) ? RGB_MATRIX_MAXIMUM_BRIGHTNESS : val;
    eeconfig_flag_rgb_matrix(write_to_eeprom);
    dprintf("rgb matrix set hsv [%s]: %u,%u,%u\n", (write_to_eeprom) ? "EEPROM" : "NOEEPROM", rgb_matrix_config.hsv.h, rgb_matrix_config.hsv.s, rgb_matrix_config.hsv.v);
}
void rgb_matrix_sethsv_noeeprom(uint16_t hue, uint8_t sat, uint8_t val) {
    rgb_matrix_sethsv_eeprom_helper(hue, sat, val, false);
}
void rgb_matrix_sethsv(uint16_t hue, uint8_t sat, uint8_t val) {
    rgb_matrix_sethsv_eeprom_helper(hue, sat, val, true);
}

hsv_t rgb_matrix_get_hsv(void) {
    return rgb_matrix_config.hsv;
}
uint8_t rgb_matrix_get_hue(void) {
    return rgb_matrix_config.hsv.h;
}
uint8_t rgb_matrix_get_sat(void) {
    return rgb_matrix_config.hsv.s;
}
uint8_t rgb_matrix_get_val(void) {
    return rgb_matrix_config.hsv.v;
}

void rgb_matrix_increase_hue_helper(bool write_to_eeprom) {
    rgb_matrix_sethsv_eeprom_helper(rgb_matrix_config.hsv.h + RGB_MATRIX_HUE_STEP, rgb_matrix_config.hsv.s, rgb_matrix_config.hsv.v, write_to_eeprom);
}
void rgb_matrix_increase_hue_noeeprom(void) {
    rgb_matrix_increase_hue_helper(false);
}
void rgb_matrix_increase_hue(void) {
    rgb_matrix_increase_hue_helper(true);
}

void rgb_matrix_decrease_hue_helper(bool write_to_eeprom) {
    rgb_matrix_sethsv_eeprom_helper(rgb_matrix_config.hsv.h - RGB_MATRIX_HUE_STEP, rgb_matrix_config.hsv.s, rgb_matrix_config.hsv.v, write_to_eeprom);
}
void rgb_matrix_decrease_hue_noeeprom(void) {
    rgb_matrix_decrease_hue_helper(false);
}
void rgb_matrix_decrease_hue(void) {
    rgb_matrix_decrease_hue_helper(true);
}

void rgb_matrix_increase_sat_helper(bool write_to_eeprom) {
    rgb_matrix_sethsv_eeprom_helper(rgb_matrix_config.hsv.h, qadd8(rgb_matrix_config.hsv.s, RGB_MATRIX_SAT_STEP), rgb_matrix_config.hsv.v, write_to_eeprom);
}
void rgb_matrix_increase_sat_noeeprom(void) {
    rgb_matrix_increase_sat_helper(false);
}
void rgb_matrix_increase_sat(void) {
    rgb_matrix_increase_sat_helper(true);
}

void rgb_matrix_decrease_sat_helper(bool write_to_eeprom) {
    rgb_matrix_sethsv_eeprom_helper(rgb_matrix_config.hsv.h, qsub8(rgb_matrix_config.hsv.s, RGB_MATRIX_SAT_STEP), rgb_matrix_config.hsv.v, write_to_eeprom);
}
void rgb_matrix_decrease_sat_noeeprom(void) {
    rgb_matrix_decrease_sat_helper(false);
}
void rgb_matrix_decrease_sat(void) {
    rgb_matrix_decrease_sat_helper(true);
}

void rgb_matrix_increase_val_helper(bool write_to_eeprom) {
    rgb_matrix_sethsv_eeprom_helper(rgb_matrix_config.hsv.h, rgb_matrix_config.hsv.s, qadd8(rgb_matrix_config.hsv.v, RGB_MATRIX_VAL_STEP), write_to_eeprom);
}
void rgb_matrix_increase_val_noeeprom(void) {
    rgb_matrix_increase_val_helper(false);
}
void rgb_matrix_increase_val(void) {
    rgb_matrix_increase_val_helper(true);
}

void rgb_matrix_decrease_val_helper(bool write_to_eeprom) {
    rgb_matrix_sethsv_eeprom_helper(rgb_matrix_config.hsv.h, rgb_matrix_config.hsv.s, qsub8(rgb_matrix_config.hsv.v, RGB_MATRIX_VAL_STEP), write_to_eeprom);
}
void rgb_matrix_decrease_val_noeeprom(void) {
    rgb_matrix_decrease_val_helper(false);
}
void rgb_matrix_decrease_val(void) {
    rgb_matrix_decrease_val_helper(true);
}

void rgb_matrix_set_speed_eeprom_helper(uint8_t speed, bool write_to_eeprom) {
    rgb_matrix_config.speed = speed;
    eeconfig_flag_rgb_matrix(write_to_eeprom);
    dprintf("rgb matrix set speed [%s]: %u\n", (write_to_eeprom) ? "EEPROM" : "NOEEPROM", rgb_matrix_config.speed);
}
void rgb_matrix_set_speed_noeeprom(uint8_t speed) {
    rgb_matrix_set_speed_eeprom_helper(speed, false);
}
void rgb_matrix_set_speed(uint8_t speed) {
    rgb_matrix_set_speed_eeprom_helper(speed, true);
}

uint8_t rgb_matrix_get_speed(void) {
    return rgb_matrix_config.speed;
}

void rgb_matrix_increase_speed_helper(bool write_to_eeprom) {
    rgb_matrix_set_speed_eeprom_helper(qadd8(rgb_matrix_config.speed, RGB_MATRIX_SPD_STEP), write_to_eeprom);
}
void rgb_matrix_increase_speed_noeeprom(void) {
    rgb_matrix_increase_speed_helper(false);
}
void rgb_matrix_increase_speed(void) {
    rgb_matrix_increase_speed_helper(true);
}

void rgb_matrix_decrease_speed_helper(bool write_to_eeprom) {
    rgb_matrix_set_speed_eeprom_helper(qsub8(rgb_matrix_config.speed, RGB_MATRIX_SPD_STEP), write_to_eeprom);
}
void rgb_matrix_decrease_speed_noeeprom(void) {
    rgb_matrix_decrease_speed_helper(false);
}
void rgb_matrix_decrease_speed(void) {
    rgb_matrix_decrease_speed_helper(true);
}

void rgb_matrix_set_flags_eeprom_helper(led_flags_t flags, bool write_to_eeprom) {
    rgb_matrix_config.flags = flags;
    eeconfig_flag_rgb_matrix(write_to_eeprom);
    dprintf("rgb matrix set flags [%s]: %u\n", (write_to_eeprom) ? "EEPROM" : "NOEEPROM", rgb_matrix_config.flags);
}

led_flags_t rgb_matrix_get_flags(void) {
    return rgb_matrix_config.flags;
}

void rgb_matrix_set_flags(led_flags_t flags) {
    rgb_matrix_set_flags_eeprom_helper(flags, true);
}

void rgb_matrix_set_flags_noeeprom(led_flags_t flags) {
    rgb_matrix_set_flags_eeprom_helper(flags, false);
}

void rgb_matrix_flags_step_helper(bool write_to_eeprom) {
    led_flags_t flags = rgb_matrix_get_flags();

    uint8_t next = 0;
    for (uint8_t i = 0; i < RGB_MATRIX_FLAG_STEPS_COUNT; i++) {
        if (rgb_matrix_flag_steps[i] == flags) {
            next = i == RGB_MATRIX_FLAG_STEPS_COUNT - 1 ? 0 : i + 1;
            break;
        }
    }

    rgb_matrix_set_flags_eeprom_helper(rgb_matrix_flag_steps[next], write_to_eeprom);
}

void rgb_matrix_flags_step_noeeprom(void) {
    rgb_matrix_flags_step_helper(false);
}

void rgb_matrix_flags_step(void) {
    rgb_matrix_flags_step_helper(true);
}

void rgb_matrix_flags_step_reverse_helper(bool write_to_eeprom) {
    led_flags_t flags = rgb_matrix_get_flags();

    uint8_t next = 0;
    for (uint8_t i = 0; i < RGB_MATRIX_FLAG_STEPS_COUNT; i++) {
        if (rgb_matrix_flag_steps[i] == flags) {
            next = i == 0 ? RGB_MATRIX_FLAG_STEPS_COUNT - 1 : i - 1;
            break;
        }
    }

    rgb_matrix_set_flags_eeprom_helper(rgb_matrix_flag_steps[next], write_to_eeprom);
}

void rgb_matrix_flags_step_reverse_noeeprom(void) {
    rgb_matrix_flags_step_reverse_helper(false);
}

void rgb_matrix_flags_step_reverse(void) {
    rgb_matrix_flags_step_reverse_helper(true);
}

//----------------------------------------------------------
// RGB Matrix naming
#undef RGB_MATRIX_EFFECT
#ifdef RGB_MATRIX_MODE_NAME_ENABLE
const char *rgb_matrix_get_mode_name(uint8_t mode) {
    switch (mode) {
        case RGB_MATRIX_NONE:
            return "NONE";

#    define RGB_MATRIX_EFFECT(name, ...) \
        case RGB_MATRIX_##name:          \
            return #name;
#    include "rgb_matrix_effects.inc"
#    undef RGB_MATRIX_EFFECT

#    ifdef COMMUNITY_MODULES_ENABLE
#        define RGB_MATRIX_EFFECT(name, ...)         \
            case RGB_MATRIX_COMMUNITY_MODULE_##name: \
                return #name;
#        include "rgb_matrix_community_modules.inc"
#        undef RGB_MATRIX_EFFECT
#    endif // COMMUNITY_MODULES_ENABLE

#    if defined(RGB_MATRIX_CUSTOM_KB) || defined(RGB_MATRIX_CUSTOM_USER)
#        define RGB_MATRIX_EFFECT(name, ...) \
            case RGB_MATRIX_CUSTOM_##name:   \
                return #name;

#        ifdef RGB_MATRIX_CUSTOM_KB
#            include "rgb_matrix_kb.inc"
#        endif // RGB_MATRIX_CUSTOM_KB

#        ifdef RGB_MATRIX_CUSTOM_USER
#            include "rgb_matrix_user.inc"
#        endif // RGB_MATRIX_CUSTOM_USER

#        undef RGB_MATRIX_EFFECT
#    endif // RGB_MATRIX_CUSTOM_KB || RGB_MATRIX_CUSTOM_USER

        default:
            return "UNKNOWN";
    }
}
#    undef RGB_MATRIX_EFFECT
#endif // RGB_MATRIX_MODE_NAME_ENABLE
