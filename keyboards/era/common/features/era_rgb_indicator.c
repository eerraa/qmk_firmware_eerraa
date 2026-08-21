// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

/* The RGB Matrix lock-indicator slots. What the feature is and why it is
 * common rather than per board is at the top of era_rgb_indicator.h.
 *
 * Three things here are not free choices:
 *
 *  - **The paint runs from the indicator hooks, never from a task.** The lock
 *    state changes on a host report and the colour changes on a VIA write, so
 *    both are edges; what they raise is one dirty byte, and the render pass
 *    reads it. Nothing here reads a clock and nothing here runs on the matrix
 *    scan path.
 *  - **The HSV to RGB conversion is cached per slot**, the way
 *    `newone/common/odessey_common.c` and `sirind/common/tomak_common.c` both
 *    cache theirs: `hsv_to_rgb()` is called once per colour change rather than
 *    once per frame.
 *  - **The clearing pass is owed only while the panel is dark.** With an effect
 *    running, a slot that stops being an indicator is repainted by the next
 *    effect frame and needs nothing. With the effect off or `RGB_MATRIX_NONE`
 *    nothing repaints it at all, so the last indicator colour would stay lit
 *    forever — which is the shape tomak_common.c's `clear_when_off` already
 *    exists for, and this is the same mechanism on one LED per slot.
 */

#include "era_rgb_indicator.h"

#if !defined(RGB_MATRIX_ENABLE)
#    error "ERA RGB indicators are a layer over RGB Matrix; era_common_qmk_rules.mk refuses the selector without it."
#endif

#if !defined(ERA_RGB_INDICATOR_1_LED)
#    error "a board taking ERA_RGB_INDICATOR_ENABLE must state ERA_RGB_INDICATOR_1_LED in its config.h -- which LED a slot paints is board geometry, not a family constant"
#endif

#include <string.h>
#include "rgb_matrix.h"
#include "../storage/era_eeprom_storage.h"

_Static_assert(ERA_RGB_INDICATOR_1_LED < RGB_MATRIX_LED_COUNT, "ERA_RGB_INDICATOR_1_LED is outside this board's RGB Matrix.");
#if ERA_RGB_INDICATOR_SLOT_COUNT > 1
_Static_assert(ERA_RGB_INDICATOR_2_LED < RGB_MATRIX_LED_COUNT, "ERA_RGB_INDICATOR_2_LED is outside this board's RGB Matrix.");
_Static_assert(ERA_RGB_INDICATOR_2_LED != ERA_RGB_INDICATOR_1_LED, "The two ERA indicator slots must paint different LEDs.");
#endif

enum {
    /* Any byte but zero would do; a fresh EEPROM reads zero and has to land on
       the defaults rather than on a disabled indicator with a black colour. */
    ERA_RGB_INDICATOR_CONFIG_VALID = 0xB2,
};

/* Eight bytes, which is the whole of what the common-feature reserved hole can
   spare beside the backlight claim (storage/era_eeprom_layout.h). The two
   sources and the master bit share one byte for that reason: three bytes of
   HSV per slot is what actually has to be stored, and 2 + 6 does not fit. */
typedef struct __attribute__((packed)) {
    uint8_t valid;
    uint8_t flags;
    uint8_t hsv[2][3];
} era_rgb_indicator_config_t;

_Static_assert(sizeof(era_rgb_indicator_config_t) == ERA_EEPROM_RGB_INDICATOR_CONFIG_SIZE, "ERA RGB indicator config size changed.");

#define ERA_RGB_INDICATOR_FLAG_ENABLED 0x01u
#define ERA_RGB_INDICATOR_SOURCE_SHIFT(slot) (1u + ((slot) * 2u))
#define ERA_RGB_INDICATOR_SOURCE_MASK 0x03u

static era_rgb_indicator_config_t rgb_indicator_config;

static const uint8_t rgb_indicator_led[ERA_RGB_INDICATOR_SLOT_COUNT] = {
    ERA_RGB_INDICATOR_1_LED,
#if ERA_RGB_INDICATOR_SLOT_COUNT > 1
    ERA_RGB_INDICATOR_2_LED,
#endif
};

static HSV   rgb_indicator_cached_hsv[ERA_RGB_INDICATOR_SLOT_COUNT];
static RGB   rgb_indicator_cached_rgb[ERA_RGB_INDICATOR_SLOT_COUNT];
static bool  rgb_indicator_cached_valid[ERA_RGB_INDICATOR_SLOT_COUNT];
static led_t rgb_indicator_host_led_state;
static bool  rgb_indicator_dirty = true;
#if defined(RGB_MATRIX_RENDER_POLICY_ENABLE)
static bool rgb_indicator_clear_when_off;
#endif

bool era_rgb_indicator_slot_exists(uint8_t slot) {
    return slot < ERA_RGB_INDICATOR_SLOT_COUNT;
}

static uint8_t era_rgb_indicator_normalized_source(uint8_t source) {
    return source < ERA_RGB_INDICATOR_SOURCE_COUNT ? source : ERA_RGB_INDICATOR_SOURCE_OFF;
}

static uint8_t era_rgb_indicator_source_of(uint8_t slot) {
    return (rgb_indicator_config.flags >> ERA_RGB_INDICATOR_SOURCE_SHIFT(slot)) & ERA_RGB_INDICATOR_SOURCE_MASK;
}

static void era_rgb_indicator_store_source(uint8_t slot, uint8_t source) {
    const uint8_t shift = ERA_RGB_INDICATOR_SOURCE_SHIFT(slot);
    rgb_indicator_config.flags &= (uint8_t)~(ERA_RGB_INDICATOR_SOURCE_MASK << shift);
    rgb_indicator_config.flags |= (uint8_t)(era_rgb_indicator_normalized_source(source) << shift);
}

static bool era_rgb_indicator_source_active(uint8_t source, led_t led_state) {
    switch (source) {
        case ERA_RGB_INDICATOR_SOURCE_CAPS_LOCK:
            return led_state.caps_lock;
        case ERA_RGB_INDICATOR_SOURCE_SCROLL_LOCK:
            return led_state.scroll_lock;
        case ERA_RGB_INDICATOR_SOURCE_NUM_LOCK:
            return led_state.num_lock;
        default:
            return false;
    }
}

/* A slot is an indicator only while the master bit is on and it has a source.
   Both halves of that are the same statement to the render pass: the LED
   belongs to the effect. */
static bool era_rgb_indicator_slot_is_indicator(uint8_t slot) {
    return (rgb_indicator_config.flags & ERA_RGB_INDICATOR_FLAG_ENABLED) != 0 && era_rgb_indicator_source_of(slot) != ERA_RGB_INDICATOR_SOURCE_OFF;
}

static bool era_rgb_indicator_any_slot_is_indicator(void) {
    for (uint8_t slot = 0; slot < ERA_RGB_INDICATOR_SLOT_COUNT; slot++) {
        if (era_rgb_indicator_slot_is_indicator(slot)) {
            return true;
        }
    }
    return false;
}

static void era_rgb_indicator_mark_dirty(void) {
    rgb_indicator_dirty = true;
}

static RGB era_rgb_indicator_rgb(uint8_t slot) {
    HSV hsv = {.h = rgb_indicator_config.hsv[slot][0], .s = rgb_indicator_config.hsv[slot][1], .v = rgb_indicator_config.hsv[slot][2]};
    if (!rgb_indicator_cached_valid[slot] || rgb_indicator_cached_hsv[slot].h != hsv.h || rgb_indicator_cached_hsv[slot].s != hsv.s || rgb_indicator_cached_hsv[slot].v != hsv.v) {
        rgb_indicator_cached_hsv[slot]   = hsv;
        rgb_indicator_cached_rgb[slot]   = hsv_to_rgb(hsv);
        rgb_indicator_cached_valid[slot] = true;
    }
    return rgb_indicator_cached_rgb[slot];
}

static void era_rgb_indicator_apply(uint8_t led_min, uint8_t led_max) {
    for (uint8_t slot = 0; slot < ERA_RGB_INDICATOR_SLOT_COUNT; slot++) {
        const uint8_t led = rgb_indicator_led[slot];
        if (led < led_min || led >= led_max) {
            continue;
        }

        if (!era_rgb_indicator_slot_is_indicator(slot)) {
#if defined(RGB_MATRIX_RENDER_POLICY_ENABLE)
            /* The one case the effect cannot clean up after: nothing is
               repainting this LED, so the colour it was last given as an
               indicator would stay on it. */
            if (rgb_indicator_clear_when_off) {
                rgb_matrix_set_color(led, 0, 0, 0);
            }
#endif
            continue;
        }

        if (era_rgb_indicator_source_active(era_rgb_indicator_source_of(slot), rgb_indicator_host_led_state)) {
            const RGB rgb = era_rgb_indicator_rgb(slot);
            rgb_matrix_set_color(led, rgb.r, rgb.g, rgb.b);
        } else {
            rgb_matrix_set_color(led, 0, 0, 0);
        }
    }
}

/* --- The persisted record ------------------------------------------------ */

static void era_rgb_indicator_apply_defaults(era_rgb_indicator_config_t *config) {
    memset(config, 0, sizeof(*config));
    config->valid = ERA_RGB_INDICATOR_CONFIG_VALID;
    config->flags = ERA_RGB_INDICATOR_FLAG_ENABLED;
    /* Caps on slot 1 and Scroll on slot 2, white at full brightness. Not a new
       choice: it is what odessey and the tomak family already default to, and
       on the two boards that had a hard-coded indicator it is the same lock on
       the same LED. */
    config->flags |= (uint8_t)(ERA_RGB_INDICATOR_SOURCE_CAPS_LOCK << ERA_RGB_INDICATOR_SOURCE_SHIFT(0));
#if ERA_RGB_INDICATOR_SLOT_COUNT > 1
    config->flags |= (uint8_t)(ERA_RGB_INDICATOR_SOURCE_SCROLL_LOCK << ERA_RGB_INDICATOR_SOURCE_SHIFT(1));
#endif
    for (uint8_t slot = 0; slot < 2; slot++) {
        config->hsv[slot][0] = 0;
        config->hsv[slot][1] = 0;
        config->hsv[slot][2] = 255;
    }
}

void era_rgb_indicator_save_config(void) {
    era_eeprom_update_config(&rgb_indicator_config, ERA_EEPROM_RGB_INDICATOR_CONFIG_OFFSET, sizeof(rgb_indicator_config));
}

/* No `write_defaults` argument, unlike era_backlight.c's twin: this record has
   nothing to normalise, so the only reason to write is that the stored block
   was missing or stale, and that arm writes for itself. */
void era_rgb_indicator_reload_from_eeprom(void) {
    if (era_eeprom_read_config(&rgb_indicator_config, ERA_EEPROM_RGB_INDICATOR_CONFIG_OFFSET, sizeof(rgb_indicator_config)) != sizeof(rgb_indicator_config) || rgb_indicator_config.valid != ERA_RGB_INDICATOR_CONFIG_VALID) {
        era_rgb_indicator_apply_defaults(&rgb_indicator_config);
        era_rgb_indicator_save_config();
    }

    for (uint8_t slot = 0; slot < ERA_RGB_INDICATOR_SLOT_COUNT; slot++) {
        rgb_indicator_cached_valid[slot] = false;
    }
    era_rgb_indicator_mark_dirty();
}

void era_rgb_indicator_init(void) {
    rgb_indicator_host_led_state = host_keyboard_led_state();
    era_rgb_indicator_reload_from_eeprom();
}

/* --- The VIA surface's state access -------------------------------------- */

bool era_rgb_indicator_get_enabled(void) {
    return (rgb_indicator_config.flags & ERA_RGB_INDICATOR_FLAG_ENABLED) != 0;
}

void era_rgb_indicator_set_enabled(bool enabled) {
    if (enabled) {
        rgb_indicator_config.flags |= ERA_RGB_INDICATOR_FLAG_ENABLED;
    } else {
        rgb_indicator_config.flags &= (uint8_t)~ERA_RGB_INDICATOR_FLAG_ENABLED;
    }
    era_rgb_indicator_mark_dirty();
}

uint8_t era_rgb_indicator_get_source(uint8_t slot) {
    return era_rgb_indicator_source_of(slot);
}

void era_rgb_indicator_set_source(uint8_t slot, uint8_t source) {
    era_rgb_indicator_store_source(slot, source);
    era_rgb_indicator_mark_dirty();
}

uint8_t era_rgb_indicator_get_brightness(uint8_t slot) {
    return rgb_indicator_config.hsv[slot][2];
}

void era_rgb_indicator_set_brightness(uint8_t slot, uint8_t brightness) {
    rgb_indicator_config.hsv[slot][2] = brightness;
    era_rgb_indicator_mark_dirty();
}

void era_rgb_indicator_get_color(uint8_t slot, uint8_t *hue_sat) {
    hue_sat[0] = rgb_indicator_config.hsv[slot][0];
    hue_sat[1] = rgb_indicator_config.hsv[slot][1];
}

void era_rgb_indicator_set_color(uint8_t slot, const uint8_t *hue_sat) {
    rgb_indicator_config.hsv[slot][0] = hue_sat[0];
    rgb_indicator_config.hsv[slot][1] = hue_sat[1];
    era_rgb_indicator_mark_dirty();
}

/* --- The QMK hooks this feature owns ------------------------------------- */

/* Strong definitions of QMK's own weak hooks, which `era_board_hooks.h` keeps
   out of the class-skeleton contract and leaves to whoever paints. A board that
   turns this selector on therefore may not define them itself, and the two that
   used to — `linx3/fave65s` and `sirind/brick65s` — have no `.c` at all now.
   The mutual exclusion is the same one `ERA_BACKLIGHT_EFFECT_ENABLE` already
   carries against a board's keyboard-channel handler, and it is what the
   selector's declaration says. */

bool led_update_kb(led_t led_state) {
    if (rgb_indicator_host_led_state.raw != led_state.raw) {
        rgb_indicator_host_led_state = led_state;
        era_rgb_indicator_mark_dirty();
    }
    bool res = led_update_user(led_state);
    if (res) {
        led_update_ports(led_state);
    }
    return res;
}

bool rgb_matrix_indicators_kb(void) {
    if (!rgb_matrix_indicators_user()) {
        return false;
    }
    era_rgb_indicator_apply(0, RGB_MATRIX_LED_COUNT);
    return true;
}

bool rgb_matrix_indicators_advanced_kb(uint8_t led_min, uint8_t led_max) {
    if (!rgb_matrix_indicators_advanced_user(led_min, led_max)) {
        return false;
    }
    era_rgb_indicator_apply(led_min, led_max);
    return true;
}

#if defined(RGB_MATRIX_RENDER_POLICY_ENABLE)
void rgb_matrix_render_policy_kb(rgb_matrix_render_policy_t *policy) {
    rgb_matrix_render_policy_user(policy);

    const bool panel_dark = !rgb_matrix_config.enable || rgb_matrix_config.mode == RGB_MATRIX_NONE;
    rgb_indicator_clear_when_off = rgb_indicator_dirty && panel_dark;

    if (rgb_indicator_dirty) {
        policy->flags |= RGB_MATRIX_RENDER_POLICY_INDICATORS_DIRTY;
    }
    if (era_rgb_indicator_any_slot_is_indicator() || rgb_indicator_clear_when_off) {
        policy->flags |= RGB_MATRIX_RENDER_POLICY_INDICATORS_ENABLE | RGB_MATRIX_RENDER_POLICY_ALLOW_DISABLED;
    }
}

void rgb_matrix_render_policy_flush_kb(uint8_t frame_flags) {
    if ((frame_flags & RGB_MATRIX_RENDER_FRAME_INDICATORS) != 0) {
        rgb_indicator_dirty          = false;
        rgb_indicator_clear_when_off = false;
    }
    rgb_matrix_render_policy_flush_user(frame_flags);
}
#endif
