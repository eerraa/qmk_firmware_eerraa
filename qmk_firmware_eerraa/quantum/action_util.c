/*
Copyright 2013 Jun Wako <wakojun@gmail.com>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "host.h"
#include "report.h"
#include "debug.h"
#include "action_util.h"
#include "action_layer.h"
#include "action_tapping.h"
#include "timer.h"
#include "keycode_config.h"
#ifdef ERA_HID_REPORT_INTERVAL_ENABLE
#    include "usb_device_state.h"
#endif
#include <string.h>
#if defined(ACTION_OWNED_OUTPUTS) && defined(MOUSEKEY_ENABLE)
#    include "mousekey.h"
#endif

extern keymap_config_t keymap_config;

static uint8_t real_mods = 0;
static uint8_t weak_mods = 0;

#ifdef ACTION_OWNED_OUTPUTS
_Static_assert(ACTION_OUTPUT_OWNERS < UINT8_MAX, "Output owners must fit in a byte");
static action_output_owner_t output_owner = ACTION_OUTPUT_OWNER_REGULAR;
static uint8_t output_real_mods[ACTION_OUTPUT_OWNERS];
static uint8_t output_weak_mods[ACTION_OUTPUT_OWNERS];
static uint8_t output_real_union, output_weak_union;
static uint8_t output_real_counts[8], output_weak_counts[8];
/* Ordinary QMK usages remain bit state, not per-key reference counts. Only
 * synthetic contributions have owners; report format and transport stay unchanged. */
static uint8_t regular_keys[32];
static uint8_t output_keys[ACTION_OUTPUT_OWNERS][32];
static uint8_t output_key_counts[256];
static uint8_t output_owner_key_counts[ACTION_OUTPUT_OWNERS];

void add_key(uint8_t key) {
    const uint8_t bit = (uint8_t)(1U << (key & 7));
    if (output_owner < ACTION_OUTPUT_OWNERS) {
        if (!(output_keys[output_owner][key >> 3] & bit)) {
            output_keys[output_owner][key >> 3] |= bit;
            ++output_key_counts[key];
            ++output_owner_key_counts[output_owner];
        }
    } else {
        regular_keys[key >> 3] |= bit;
    }
    add_key_to_report(key);
}

void del_key(uint8_t key) {
    const uint8_t bit = (uint8_t)(1U << (key & 7));
    if (output_owner < ACTION_OUTPUT_OWNERS) {
        if (output_keys[output_owner][key >> 3] & bit) {
            output_keys[output_owner][key >> 3] &= (uint8_t)~bit;
            --output_key_counts[key];
            --output_owner_key_counts[output_owner];
        }
    } else {
        regular_keys[key >> 3] &= (uint8_t)~bit;
    }
    if (!output_key_counts[key] && !(regular_keys[key >> 3] & bit)) del_key_from_report(key);
}

void action_output_clear_owner_keys(action_output_owner_t owner) {
    if (owner >= ACTION_OUTPUT_OWNERS || !output_owner_key_counts[owner]) return;
    const uint8_t previous = output_owner;
    output_owner = owner;
    for (uint16_t key = 0; key < 256; ++key) {
        if (output_keys[owner][key >> 3] & (1U << (key & 7))) del_key((uint8_t)key);
    }
    output_owner = previous;
}

void action_output_reset_keys(void) {
    memset(regular_keys, 0, sizeof(regular_keys));
    memset(output_keys, 0, sizeof(output_keys));
    memset(output_key_counts, 0, sizeof(output_key_counts));
    memset(output_owner_key_counts, 0, sizeof(output_owner_key_counts));
}

action_output_owner_t action_output_set_owner(action_output_owner_t owner) {
    uint8_t previous = output_owner;
    output_owner = owner;
    return previous;
}
action_output_owner_t action_output_get_owner(void) { return output_owner; }
/* Cost is bounded by the eight HID modifier bits, not the matrix size. */
static void output_replace_mods(uint8_t *owned, uint8_t mods, uint8_t *counts, uint8_t *combined) {
    const uint8_t changed = *owned ^ mods;
    for (uint8_t i = 0; i < 8; ++i) {
        const uint8_t bit = (uint8_t)(1U << i);
        if (!(changed & bit)) continue;
        if (mods & bit) {
            ++counts[i];
            *combined |= bit;
        } else if (--counts[i] == 0) {
            *combined &= (uint8_t)~bit;
        }
    }
    *owned = mods;
}
void action_output_clear_owner_mods(action_output_owner_t owner) {
    if (owner < ACTION_OUTPUT_OWNERS) {
        output_replace_mods(&output_real_mods[owner], 0, output_real_counts, &output_real_union);
        output_replace_mods(&output_weak_mods[owner], 0, output_weak_counts, &output_weak_union);
    }
}

/* Mouse codes keep QMK bit state for ordinary inputs; synthetic contributions are
 * counted like keyboard usages. Each extra report holds one usage, so an owned
 * release clears only its own usage and never one another input holds. */
#    define OUTPUT_MOUSE_CODES (QK_MOUSE_ACCELERATION_2 - QK_MOUSE_CURSOR_UP + 1)
_Static_assert(OUTPUT_MOUSE_CODES <= 32, "Mouse ownership uses one 32-bit mask");
static uint32_t regular_mouse;
static uint32_t output_mouse[ACTION_OUTPUT_OWNERS];
static uint8_t  output_mouse_counts[OUTPUT_MOUSE_CODES];
static uint16_t regular_usage[2];
static uint16_t output_usage[ACTION_OUTPUT_OWNERS][2];
static uint8_t  output_usage_owners[2];

bool action_output_mouse_update(uint8_t code, bool pressed) {
    if (!IS_MOUSE_KEYCODE(code)) return true;
    const uint8_t  i   = (uint8_t)(code - QK_MOUSE_CURSOR_UP);
    const uint32_t bit = 1UL << i;
    if (output_owner < ACTION_OUTPUT_OWNERS) {
        uint32_t *owned = &output_mouse[output_owner];
        if (pressed && !(*owned & bit)) {
            *owned |= bit;
            ++output_mouse_counts[i];
        } else if (!pressed && (*owned & bit)) {
            *owned &= ~bit;
            --output_mouse_counts[i];
        }
    } else {
        regular_mouse = pressed ? regular_mouse | bit : regular_mouse & ~bit;
    }
    if (pressed) return true;
    if (output_mouse_counts[i] || (regular_mouse & bit)) return false;
#    ifdef MOUSEKEY_ENABLE
    if (IS_MOUSEKEY_MOVE(code) || IS_MOUSEKEY_WHEEL(code)) {
        /* Direction pairs are adjacent in the QMK keycode range. Restore only
         * an axis with a scoped participant; ordinary QMK keeps its release
         * semantics. Do this before mousekey_off can end the live gesture. */
        const uint8_t  opposite = i ^ 1;
        const uint32_t other    = 1UL << opposite;
        if ((output_owner < ACTION_OUTPUT_OWNERS || output_mouse_counts[opposite]) && (output_mouse_counts[opposite] || (regular_mouse & other))) {
            mousekey_restore_direction((uint8_t)(QK_MOUSE_CURSOR_UP + opposite));
        }
    }
#    endif
    return true;
}

/* Scanned only while some synthetic owner holds a usage on this page. */
static bool output_usage_held(uint8_t page, uint16_t usage) {
    for (uint8_t i = 0; output_usage_owners[page] && i < ACTION_OUTPUT_OWNERS; ++i) {
        if (output_usage[i][page] == usage) return true;
    }
    return false;
}

bool action_output_usage_update(uint8_t page, uint16_t usage, uint16_t current, bool pressed) {
    const bool synthetic = output_owner < ACTION_OUTPUT_OWNERS;
    uint16_t  *held = synthetic ? &output_usage[output_owner][page] : &regular_usage[page];
    if (pressed) {
        if (synthetic && !*held) ++output_usage_owners[page];
        *held = usage;
        return true;
    }
    if (*held == usage) {
        *held = 0;
        if (synthetic) --output_usage_owners[page];
    }
    if (regular_usage[page] == usage || output_usage_held(page, usage)) return false;
    /* Without a synthetic owner on either side QMK's single-slot release is unchanged. */
    return current == usage || !(synthetic || output_usage_held(page, current));
}

#ifdef ERA_MACRO_ENABLE
/* A single-usage report can display only one owner. On release restore the
 * ordinary input first, then the lowest remaining synthetic owner. */
uint16_t action_output_usage_remaining(uint8_t page) {
    if (regular_usage[page]) return regular_usage[page];
    for (uint8_t i = 0; output_usage_owners[page] && i < ACTION_OUTPUT_OWNERS; ++i)
        if (output_usage[i][page]) return output_usage[i][page];
    return 0;
}
#endif

void action_output_clear_owner_hid(action_output_owner_t owner) {
    if (owner >= ACTION_OUTPUT_OWNERS) return;
    const uint8_t previous = action_output_set_owner(owner);
    for (uint8_t i = 0; output_mouse[owner] && i < OUTPUT_MOUSE_CODES; ++i) {
        if (output_mouse[owner] & (1UL << i)) register_mouse((uint8_t)(QK_MOUSE_CURSOR_UP + i), false);
    }
#    ifdef EXTRAKEY_ENABLE
    if (output_usage[owner][ACTION_OUTPUT_USAGE_SYSTEM] && action_output_usage_update(ACTION_OUTPUT_USAGE_SYSTEM, output_usage[owner][ACTION_OUTPUT_USAGE_SYSTEM], host_last_system_usage(), false)) host_system_send(
#    ifdef ERA_MACRO_ENABLE
        action_output_usage_remaining(ACTION_OUTPUT_USAGE_SYSTEM)
#    else
        0
#    endif
    );
    if (output_usage[owner][ACTION_OUTPUT_USAGE_CONSUMER] && action_output_usage_update(ACTION_OUTPUT_USAGE_CONSUMER, output_usage[owner][ACTION_OUTPUT_USAGE_CONSUMER], host_last_consumer_usage(), false)) host_consumer_send(
#    ifdef ERA_MACRO_ENABLE
        action_output_usage_remaining(ACTION_OUTPUT_USAGE_CONSUMER)
#    else
        0
#    endif
    );
#    endif
    action_output_set_owner(previous);
}

void action_output_reset_hid(void) {
    regular_mouse = 0;
    memset(output_mouse, 0, sizeof(output_mouse));
    memset(output_mouse_counts, 0, sizeof(output_mouse_counts));
    memset(regular_usage, 0, sizeof(regular_usage));
    memset(output_usage, 0, sizeof(output_usage));
    memset(output_usage_owners, 0, sizeof(output_usage_owners));
}

void action_output_clear_regular(void) {
    /* An explicit ownership transition, never a per-scan reconstruction. */
    real_mods = weak_mods = 0;
    memset(regular_keys, 0, sizeof(regular_keys));
    clear_keys_from_report();
    for (uint16_t key = 1; key < 256; ++key) {
        if (output_key_counts[key]) add_key_to_report((uint8_t)key);
    }
    regular_mouse = 0;
    memset(regular_usage, 0, sizeof(regular_usage));
#    ifdef MOUSEKEY_ENABLE
    mousekey_clear();
    for (uint8_t i = 0; i < OUTPUT_MOUSE_CODES; ++i) {
        if (output_mouse_counts[i]) mousekey_on((uint8_t)(QK_MOUSE_CURSOR_UP + i));
    }
    report_mouse_t report = {.buttons = mousekey_get_report().buttons};
    host_mouse_send(&report);
#    endif
#    ifdef EXTRAKEY_ENABLE
    uint16_t usages[2] = {host_last_system_usage(), host_last_consumer_usage()};
    for (uint8_t page = 0; page < 2; ++page) {
        if (!output_usage_held(page, usages[page])) {
            usages[page] = 0;
#        ifdef ERA_MACRO_ENABLE
            usages[page] = action_output_usage_remaining(page);
#        endif
        }
    }
    host_system_send(usages[ACTION_OUTPUT_USAGE_SYSTEM]);
    host_consumer_send(usages[ACTION_OUTPUT_USAGE_CONSUMER]);
#    endif
}
#endif
#ifdef KEY_OVERRIDE_ENABLE
static uint8_t weak_override_mods = 0;
static uint8_t suppressed_mods    = 0;
#endif

// TODO: pointer variable is not needed
// report_keyboard_t keyboard_report = {};
report_keyboard_t *keyboard_report = &(report_keyboard_t){};
#ifdef NKRO_ENABLE
report_nkro_t *nkro_report = &(report_nkro_t){};
#endif

#ifndef ACTION_OWNED_OUTPUTS
extern inline void add_key(uint8_t key);
extern inline void del_key(uint8_t key);
#endif
extern inline void clear_keys(void);

#ifndef NO_ACTION_ONESHOT
static uint8_t oneshot_mods        = 0;
static uint8_t oneshot_locked_mods = 0;
/**
 * @brief Retrieve current state of locked oneshot modifiers.
 *
 * @return Current state of the locked oneshot modifier keys as a bitmask.
 */
uint8_t get_oneshot_locked_mods(void) {
    return oneshot_locked_mods;
}
/**
 * Same as \ref get_oneshot_locked_mods but returns \ref mod_t for convenience.
 */
mod_t get_oneshot_locked_mod_state(void) {
    return (mod_t)get_oneshot_locked_mods();
}
void add_oneshot_locked_mods(uint8_t mods) {
    if ((oneshot_locked_mods & mods) != mods) {
        oneshot_locked_mods |= mods;
        oneshot_locked_mods_changed_kb(oneshot_locked_mods);
    }
}
void set_oneshot_locked_mods(uint8_t mods) {
    if (mods != oneshot_locked_mods) {
        oneshot_locked_mods = mods;
        oneshot_locked_mods_changed_kb(oneshot_locked_mods);
    }
}
void clear_oneshot_locked_mods(void) {
    if (oneshot_locked_mods) {
        oneshot_locked_mods = 0;
        oneshot_locked_mods_changed_kb(oneshot_locked_mods);
    }
}
void del_oneshot_locked_mods(uint8_t mods) {
    if (oneshot_locked_mods & mods) {
        oneshot_locked_mods &= ~mods;
        oneshot_locked_mods_changed_kb(oneshot_locked_mods);
    }
}
#    if (defined(ONESHOT_TIMEOUT) && (ONESHOT_TIMEOUT > 0))
static uint16_t oneshot_time = 0;
bool            has_oneshot_mods_timed_out(void) {
    return TIMER_DIFF_16(timer_read(), oneshot_time) >= ONESHOT_TIMEOUT;
}
#    else
bool has_oneshot_mods_timed_out(void) {
    return false;
}
#    endif
#endif

/* oneshot layer */
#ifndef NO_ACTION_ONESHOT
/** \brief oneshot_layer_data bits
 * LLLL LSSS
 * where:
 *   L => are layer bits
 *   S => oneshot state bits
 */
static uint8_t oneshot_layer_data = 0;

inline uint8_t get_oneshot_layer(void) {
    return oneshot_layer_data >> 3;
}
inline uint8_t get_oneshot_layer_state(void) {
    return oneshot_layer_data & 0b111;
}

#    ifdef SWAP_HANDS_ENABLE
enum {
    SHO_OFF,
    SHO_ACTIVE,  // Swap hands button was pressed, and we didn't send any swapped keys yet
    SHO_PRESSED, // Swap hands button is currently pressed
    SHO_USED,    // Swap hands button is still pressed, and we already sent swapped keys
} swap_hands_oneshot = SHO_OFF;
#    endif

#    if (defined(ONESHOT_TIMEOUT) && (ONESHOT_TIMEOUT > 0))
static uint16_t oneshot_layer_time = 0;
inline bool     has_oneshot_layer_timed_out(void) {
    return TIMER_DIFF_16(timer_read(), oneshot_layer_time) >= ONESHOT_TIMEOUT && !(get_oneshot_layer_state() & ONESHOT_TOGGLED);
}
#        ifdef SWAP_HANDS_ENABLE
static uint16_t oneshot_swaphands_time = 0;
inline bool     has_oneshot_swaphands_timed_out(void) {
    return TIMER_DIFF_16(timer_read(), oneshot_swaphands_time) >= ONESHOT_TIMEOUT && (swap_hands_oneshot == SHO_ACTIVE);
}
#        endif
#    endif

#    ifdef SWAP_HANDS_ENABLE

void set_oneshot_swaphands(void) {
    swap_hands_oneshot = SHO_PRESSED;
    swap_hands         = true;
#        if (defined(ONESHOT_TIMEOUT) && (ONESHOT_TIMEOUT > 0))
    oneshot_swaphands_time = timer_read();
    if (oneshot_layer_time != 0) {
        oneshot_layer_time = oneshot_swaphands_time;
    }
#        endif
}

void release_oneshot_swaphands(void) {
    if (swap_hands_oneshot == SHO_PRESSED) {
        swap_hands_oneshot = SHO_ACTIVE;
    }
    if (swap_hands_oneshot == SHO_USED) {
        clear_oneshot_swaphands();
    }
}

void use_oneshot_swaphands(void) {
    if (swap_hands_oneshot == SHO_PRESSED) {
        swap_hands_oneshot = SHO_USED;
    }
    if (swap_hands_oneshot == SHO_ACTIVE) {
        clear_oneshot_swaphands();
    }
}

void clear_oneshot_swaphands(void) {
    swap_hands_oneshot = SHO_OFF;
    swap_hands         = false;
#        if (defined(ONESHOT_TIMEOUT) && (ONESHOT_TIMEOUT > 0))
    oneshot_swaphands_time = 0;
#        endif
}

#    endif

/** \brief Set oneshot layer
 *
 * FIXME: needs doc
 */
/* One-shot layer state is global, so its layer bit is never a TD contribution,
 * even when a TD callback starts or consumes it. */
static void oneshot_layer_switch(uint8_t layer, bool on) {
#    ifdef ACTION_OWNED_OUTPUTS
    const uint8_t owner = action_output_set_owner(ACTION_OUTPUT_OWNER_REGULAR);
#    endif
    if (on) {
        layer_on(layer);
    } else {
        layer_off(layer);
    }
#    ifdef ACTION_OWNED_OUTPUTS
    action_output_set_owner(owner);
#    endif
}

void set_oneshot_layer(uint8_t layer, uint8_t state) {
    if (keymap_config.oneshot_enable) {
        oneshot_layer_data = layer << 3 | state;
        oneshot_layer_switch(layer, true);
#    if (defined(ONESHOT_TIMEOUT) && (ONESHOT_TIMEOUT > 0))
        oneshot_layer_time = timer_read();
#    endif
        oneshot_layer_changed_kb(get_oneshot_layer());
    } else {
        oneshot_layer_switch(layer, true);
    }
}
/** \brief Reset oneshot layer
 *
 * FIXME: needs doc
 */
void reset_oneshot_layer(void) {
    oneshot_layer_data = 0;
#    if (defined(ONESHOT_TIMEOUT) && (ONESHOT_TIMEOUT > 0))
    oneshot_layer_time = 0;
#    endif
    oneshot_layer_changed_kb(get_oneshot_layer());
}
/** \brief Clear oneshot layer
 *
 * FIXME: needs doc
 */
void clear_oneshot_layer_state(oneshot_fullfillment_t state) {
    uint8_t start_state = oneshot_layer_data;
    oneshot_layer_data &= ~state;
    if ((!get_oneshot_layer_state() && start_state != oneshot_layer_data) && keymap_config.oneshot_enable) {
        oneshot_layer_switch(get_oneshot_layer(), false);
        reset_oneshot_layer();
    }
}
/** \brief Is oneshot layer active
 *
 * FIXME: needs doc
 */
bool is_oneshot_layer_active(void) {
    return get_oneshot_layer_state();
}

/** \brief set oneshot
 *
 * FIXME: needs doc
 */
void oneshot_set(bool active) {
    if (keymap_config.oneshot_enable != active) {
        keymap_config.oneshot_enable = active;
        eeconfig_update_keymap(&keymap_config);
        clear_oneshot_layer_state(ONESHOT_OTHER_KEY_PRESSED);
        dprintf("Oneshot: active: %d\n", active);
    }
}

/** \brief toggle oneshot
 *
 * FIXME: needs doc
 */
void oneshot_toggle(void) {
    oneshot_set(!keymap_config.oneshot_enable);
}

/** \brief enable oneshot
 *
 * FIXME: needs doc
 */
void oneshot_enable(void) {
    oneshot_set(true);
}

/** \brief disable oneshot
 *
 * FIXME: needs doc
 */
void oneshot_disable(void) {
    oneshot_set(false);
}

bool is_oneshot_enabled(void) {
    return keymap_config.oneshot_enable;
}

#endif

static uint8_t get_mods_for_report(void) {
    uint8_t mods = get_mods() | get_weak_mods();

#ifndef NO_ACTION_ONESHOT
    if (oneshot_mods) {
#    if (defined(ONESHOT_TIMEOUT) && (ONESHOT_TIMEOUT > 0))
        if (has_oneshot_mods_timed_out()) {
            dprintf("Oneshot: timeout\n");
            clear_oneshot_mods();
        }
#    endif
        mods |= oneshot_mods;
        if (has_anykey()) {
            clear_oneshot_mods();
        }
    }
#endif

#ifdef SPECULATIVE_HOLD
    mods |= get_speculative_mods();
#endif

#ifdef KEY_OVERRIDE_ENABLE
    // These need to be last to be able to properly control key overrides
    mods &= ~suppressed_mods;
    mods |= weak_override_mods;
#endif

    return mods;
}

__attribute__((weak)) void keyboard_report_filter(report_keyboard_t *report) {
    (void)report;
}

#ifdef ERA_HID_REPORT_INTERVAL_ENABLE
static bool era_report_force;
bool era_hid_report_interval_select_format(uint8_t format);
#    ifdef NKRO_ENABLE
void rebuild_6kro_report(void);
#    endif
void resend_keyboard_report(void) {
    bool previous = era_report_force;
    era_report_force = true;
    send_keyboard_report();
    era_report_force = previous;
}
void force_send_keyboard_report(void) {
#    ifdef NKRO_ENABLE
    if (!(host_can_send_nkro() && keymap_config.nkro)) rebuild_6kro_report();
#    endif
    resend_keyboard_report();
}
#endif

void send_6kro_report(void) {
    keyboard_report->mods = get_mods_for_report();

    /* Static like keyboard_report itself, so the host driver sees the same lifetime. */
    static report_keyboard_t report;
    memcpy(&report, keyboard_report, sizeof(report_keyboard_t));
    keyboard_report_filter(&report);

#ifdef PROTOCOL_VUSB
    host_keyboard_send(&report);
#else
    static report_keyboard_t last_report;

    /* Only send the report if there are changes to propagate to the host. */
    if (
#ifdef ERA_HID_REPORT_INTERVAL_ENABLE
        era_report_force ||
#endif
        memcmp(&report, &last_report, sizeof(report_keyboard_t)) != 0) {
        memcpy(&last_report, &report, sizeof(report_keyboard_t));
        host_keyboard_send(&report);
    }
#endif
}

#ifdef NKRO_ENABLE
__attribute__((weak)) void nkro_report_filter(report_nkro_t *report) {
    (void)report;
}

void send_nkro_report(void) {
    nkro_report->mods = get_mods_for_report();

    static report_nkro_t report;
    memcpy(&report, nkro_report, sizeof(report_nkro_t));
    nkro_report_filter(&report);

    static report_nkro_t last_report;

    /* Only send the report if there are changes to propagate to the host. */
    if (
#ifdef ERA_HID_REPORT_INTERVAL_ENABLE
        era_report_force ||
#endif
        memcmp(&report, &last_report, sizeof(report_nkro_t)) != 0) {
        memcpy(&last_report, &report, sizeof(report_nkro_t));
        host_nkro_send(&report);
    }
}
#endif

/** \brief Send keyboard report
 *
 * FIXME: needs doc
 */
void send_keyboard_report(void) {
#ifdef ERA_HID_REPORT_INTERVAL_ENABLE
    uint8_t format = usb_device_state_get_protocol() == USB_PROTOCOL_BOOT ? 0 : 1;
#    ifdef NKRO_ENABLE
    if (host_can_send_nkro() && keymap_config.nkro) format = 2;
#    endif
    bool previous_force = era_report_force;
    if (era_hid_report_interval_select_format(format)) {
#    ifdef NKRO_ENABLE
        if (format != 2) rebuild_6kro_report();
#    endif
        era_report_force = true;
    }
#endif
#ifdef NKRO_ENABLE
    if (host_can_send_nkro() && keymap_config.nkro) {
        send_nkro_report();
#ifdef ERA_HID_REPORT_INTERVAL_ENABLE
        era_report_force = previous_force;
#endif
        return;
    }
#endif
    send_6kro_report();
#ifdef ERA_HID_REPORT_INTERVAL_ENABLE
    era_report_force = previous_force;
#endif
}

/**
 * @brief Retrieve current state of modifiers.
 *
 * @return Current state of the modifier keys as a bitmask.
 */
uint8_t get_mods(void) {
#ifdef ACTION_OWNED_OUTPUTS
    return real_mods | output_real_union;
#else
    return real_mods;
#endif
}
/**
 * Same as \ref get_mods but returns \ref mod_t for convenience.
 */
mod_t get_mod_state(void) {
    return (mod_t)get_mods();
}
/** \brief add mods
 *
 * FIXME: needs doc
 */
void add_mods(uint8_t mods) {
#ifdef ACTION_OWNED_OUTPUTS
    if (output_owner < ACTION_OUTPUT_OWNERS) {
        output_replace_mods(&output_real_mods[output_owner], output_real_mods[output_owner] | mods, output_real_counts, &output_real_union);
        return;
    }
#endif
    real_mods |= mods;
}
/** \brief del mods
 *
 * FIXME: needs doc
 */
void del_mods(uint8_t mods) {
#ifdef ACTION_OWNED_OUTPUTS
    if (output_owner < ACTION_OUTPUT_OWNERS) {
        output_replace_mods(&output_real_mods[output_owner], output_real_mods[output_owner] & (uint8_t)~mods, output_real_counts, &output_real_union);
        return;
    }
#endif
    real_mods &= ~mods;
}
/** \brief set mods
 *
 * FIXME: needs doc
 */
void set_mods(uint8_t mods) {
#ifdef ACTION_OWNED_OUTPUTS
    if (output_owner < ACTION_OUTPUT_OWNERS) {
        output_replace_mods(&output_real_mods[output_owner], mods, output_real_counts, &output_real_union);
        return;
    }
#endif
    real_mods = mods;
}
/** \brief clear mods
 *
 * FIXME: needs doc
 */
void clear_mods(void) {
    real_mods = 0;
}

/**
 * @brief Retrieve current state of weak modifiers.
 *
 * @return Current state of the weak modifier keys as a bitmask.
 */
uint8_t get_weak_mods(void) {
#ifdef ACTION_OWNED_OUTPUTS
    return weak_mods | output_weak_union;
#else
    return weak_mods;
#endif
}
/**
 * Same as \ref get_weak_mods but returns \ref mod_t for convenience.
 */
mod_t get_weak_mod_state(void) {
    return (mod_t)get_weak_mods();
}
/** \brief add weak mods
 *
 * FIXME: needs doc
 */
void add_weak_mods(uint8_t mods) {
#ifdef ACTION_OWNED_OUTPUTS
    if (output_owner < ACTION_OUTPUT_OWNERS) {
        output_replace_mods(&output_weak_mods[output_owner], output_weak_mods[output_owner] | mods, output_weak_counts, &output_weak_union);
        return;
    }
#endif
    weak_mods |= mods;
}
/** \brief del weak mods
 *
 * FIXME: needs doc
 */
void del_weak_mods(uint8_t mods) {
#ifdef ACTION_OWNED_OUTPUTS
    if (output_owner < ACTION_OUTPUT_OWNERS) {
        output_replace_mods(&output_weak_mods[output_owner], output_weak_mods[output_owner] & (uint8_t)~mods, output_weak_counts, &output_weak_union);
        return;
    }
#endif
    weak_mods &= ~mods;
}
/** \brief set weak mods
 *
 * FIXME: needs doc
 */
void set_weak_mods(uint8_t mods) {
#ifdef ACTION_OWNED_OUTPUTS
    if (output_owner < ACTION_OUTPUT_OWNERS) {
        output_replace_mods(&output_weak_mods[output_owner], mods, output_weak_counts, &output_weak_union);
        return;
    }
#endif
    weak_mods = mods;
}
/** \brief clear weak mods
 *
 * FIXME: needs doc
 */
void clear_weak_mods(void) {
#ifdef ACTION_OWNED_OUTPUTS
    /* Preserve QMK's clear-on-next-press rule for weak (not held) modifiers. */
    memset(output_weak_mods, 0, sizeof(output_weak_mods));
    memset(output_weak_counts, 0, sizeof(output_weak_counts));
    output_weak_union = 0;
#endif
    weak_mods = 0;
}

#ifdef KEY_OVERRIDE_ENABLE
/** \brief set weak mods used by key overrides. DO not call this manually
 */
void set_weak_override_mods(uint8_t mods) {
    weak_override_mods = mods;
}
/** \brief clear weak mods used by key overrides. DO not call this manually
 */
void clear_weak_override_mods(void) {
    weak_override_mods = 0;
}

/** \brief set suppressed mods used by key overrides. DO not call this manually
 */
void set_suppressed_override_mods(uint8_t mods) {
    suppressed_mods = mods;
}
/** \brief clear suppressed mods used by key overrides. DO not call this manually
 */
void clear_suppressed_override_mods(void) {
    suppressed_mods = 0;
}
#endif

#ifndef NO_ACTION_ONESHOT
/**
 * @brief Retrieve current state of oneshot modifiers.
 *
 * @return Current state of the oneshot modifier keys as a bitmask.
 */
uint8_t get_oneshot_mods(void) {
    return oneshot_mods;
}

/**
 * Same as \ref get_oneshot_mods but returns \ref mod_t for convenience.
 */
mod_t get_oneshot_mod_state(void) {
    return (mod_t)get_oneshot_mods();
}

void add_oneshot_mods(uint8_t mods) {
    if ((oneshot_mods & mods) != mods) {
#    if (defined(ONESHOT_TIMEOUT) && (ONESHOT_TIMEOUT > 0))
        oneshot_time = timer_read();
#    endif
        oneshot_mods |= mods;
        oneshot_mods_changed_kb(mods);
    }
}

void del_oneshot_mods(uint8_t mods) {
    if (oneshot_mods & mods) {
        oneshot_mods &= ~mods;
#    if (defined(ONESHOT_TIMEOUT) && (ONESHOT_TIMEOUT > 0))
        oneshot_time = oneshot_mods ? timer_read() : 0;
#    endif
        oneshot_mods_changed_kb(oneshot_mods);
    }
}

/** \brief set oneshot mods
 *
 * FIXME: needs doc
 */
void set_oneshot_mods(uint8_t mods) {
    if (keymap_config.oneshot_enable) {
        if (oneshot_mods != mods) {
#    if (defined(ONESHOT_TIMEOUT) && (ONESHOT_TIMEOUT > 0))
            oneshot_time = timer_read();
#    endif
            oneshot_mods = mods;
            oneshot_mods_changed_kb(mods);
        }
    }
}

/** \brief clear oneshot mods
 *
 * FIXME: needs doc
 */
void clear_oneshot_mods(void) {
    if (oneshot_mods) {
        oneshot_mods = 0;
#    if (defined(ONESHOT_TIMEOUT) && (ONESHOT_TIMEOUT > 0))
        oneshot_time = 0;
#    endif
        oneshot_mods_changed_kb(oneshot_mods);
    }
}
#endif

/** \brief Called when the one shot modifiers have been changed.
 *
 * \param mods Contains the active modifiers active after the change.
 */
__attribute__((weak)) void oneshot_locked_mods_changed_user(uint8_t mods) {}

/** \brief Called when the locked one shot modifiers have been changed.
 *
 * \param mods Contains the active modifiers active after the change.
 */
__attribute__((weak)) void oneshot_locked_mods_changed_kb(uint8_t mods) {
    oneshot_locked_mods_changed_user(mods);
}

/** \brief Called when the one shot modifiers have been changed.
 *
 * \param mods Contains the active modifiers active after the change.
 */
__attribute__((weak)) void oneshot_mods_changed_user(uint8_t mods) {}

/** \brief Called when the one shot modifiers have been changed.
 *
 * \param mods Contains the active modifiers active after the change.
 */
__attribute__((weak)) void oneshot_mods_changed_kb(uint8_t mods) {
    oneshot_mods_changed_user(mods);
}

/** \brief Called when the one shot layers have been changed.
 *
 * \param layer Contains the layer that is toggled on, or zero when toggled off.
 */
__attribute__((weak)) void oneshot_layer_changed_user(uint8_t layer) {}

/** \brief Called when the one shot layers have been changed.
 *
 * \param layer Contains the layer that is toggled on, or zero when toggled off.
 */
__attribute__((weak)) void oneshot_layer_changed_kb(uint8_t layer) {
    oneshot_layer_changed_user(layer);
}

/** \brief inspect keyboard state
 *
 * FIXME: needs doc
 */
uint8_t has_anymod(void) {
    return bitpop(real_mods);
}

#ifdef DUMMY_MOD_NEUTRALIZER_KEYCODE
/** \brief Send a dummy keycode in between the register and unregister event of a modifier key, to neutralize the "flashing modifiers" phenomenon.
 *
 * \param active_mods 8-bit packed bit-array describing the currently active modifiers (in the format GASCGASC).
 *
 * Certain QMK features like  key overrides or retro tap must unregister a previously
 * registered modifier before sending another keycode but this can trigger undesired
 * keyboard shortcuts if the clean tap of a single modifier key is bound to an action
 * on the host OS, as is for example the case for the left GUI key on Windows, which
 * opens the Start Menu when tapped.
 */
void neutralize_flashing_modifiers(uint8_t active_mods) {
    // In most scenarios, the flashing modifiers phenomenon is a problem
    // only for a subset of modifier masks.
    const static uint8_t mods_to_neutralize[] = MODS_TO_NEUTRALIZE;
    const static uint8_t n_mods               = ARRAY_SIZE(mods_to_neutralize);
    for (uint8_t i = 0; i < n_mods; ++i) {
        if (active_mods == mods_to_neutralize[i]) {
            tap_code(DUMMY_MOD_NEUTRALIZER_KEYCODE);
            break;
        }
    }
}
#endif
