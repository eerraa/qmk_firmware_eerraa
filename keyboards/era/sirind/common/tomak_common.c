// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "tomak_common.h"

#include "quantum.h"
#include "eeprom.h"
#include "nvm_eeconfig.h"
#include "usb_device_state.h"
#ifdef VIA_ENABLE
#    include "via.h"
#endif
#include <string.h>
#include "../../common/storage/era_eeprom_storage.h"
#include "../../common/system/era_common_features.h"
#include "../../common/system/era_common_via.h"
#include "../../common/split/era_split_sync_policy.h"
#include "../../common/split/era_split_board.h"
#include "../../common/split/era_split_keyboard.h"
#include "tomak_era_keyboard_config.h"
#ifdef ERA_SPLIT_EEPROM_SYNC_ENABLE
#    include "../../common/split/era_split_eeprom_sync.h"
#endif
#if defined(RGB_MATRIX_ENABLE)
#    include "../../common/split/communication_core/era_split_communication_core_launch_signal.h"
#    include "../../common/split/era_split_link.h"
#endif

/* The tomak family's whole board content, owned once for three boards that
   keep no `.c` of their own. What that means and what it cost is at the top of
   tomak_common.h; read it before adding anything here, and in particular
   before giving one board back a file for one behaviour.

   A few of the functions below have no in-file caller in every configuration
   -- the VIA surface and the EEPROM-sync reload table are each behind a build
   option, and the indicator invalidation they drive is reached through them.
   Those stay non-static on purpose: an unreferenced external is dropped by
   --gc-sections, where an unreferenced static raises -Werror=unused-function
   and would put the guard question back into a unit whose whole point is not
   having one. */

tomak_config_t   g_tomak_config;
static HSV       tomak_cached_indicator_hsv;
static RGB       tomak_cached_indicator_rgb;
static bool      tomak_indicator_rgb_valid;
static bool      tomak_indicator_dirty = true;
static bool      tomak_indicator_clear_when_off;
static bool      tomak_config_save_pending;
static uint16_t  tomak_config_save_timer;

static led_t     tomak_host_led_state;
static led_t     tomak_usb_led_state;

/* Declared here rather than in the header since the family stopped having a
   board layer to declare it to: it is defined below the persisted record but
   reached from above it. */
void tomak_invalidate_indicator_render(bool allow_without_usb_configured);

typedef struct __attribute__((packed)) {
    uint32_t reset_key32;
    uint32_t reset_key_inverse32;
    uint8_t  reserved_zero[8];
} tomak_era_reset_guard_t;

/* No sizeof(tomak_config_t) assert, and this family has not carried one since
   the type took its present shape: the pin existed to keep the union's `raw`
   uint32_t view a valid whole-struct view, and that view went with the union
   on 2026-08-11. This type is in-RAM only. The size that matters is the
   persisted record's, and tomak_era_keyboard_config.h still asserts that one
   against ERA_EEPROM_KEYBOARD_CONFIG_SIZE. */
_Static_assert(sizeof(tomak_era_reset_guard_t) == ERA_EEPROM_RESET_GUARD_CONFIG_SIZE, "ERA reset guard size changed.");

/* The badge LED range is board geometry -- a 9-LED badge on 79H and 79S, a
   3-LED one on tomak -- so it is a board fact under ERA build-selector rule 3
   and each board's config.h states it. This refusal is here because a board
   that forgets it would otherwise inherit whatever the compiler last saw. */
#if !defined(TOMAK_BADGE_LED_MIN)
#    error a tomak-family board must state TOMAK_BADGE_LED_MIN in its config.h -- the first LED of the badge range the lock indicator paints, which is board geometry rather than a family constant
#endif
#define TOMAK_BADGE_LED_MAX RGB_MATRIX_LED_COUNT

/* --- Indicator configuration --------------------------------------------- */

static uint8_t tomak_normalized_indicator_mode(uint8_t mode) {
    return mode <= TOMAK_INDICATOR_NUM_LOCK ? mode : TOMAK_INDICATOR_OFF;
}

static void tomak_config_set_indicator_mode(tomak_config_t *config, uint8_t mode) {
    config->lock_indicator_mode = tomak_normalized_indicator_mode(mode);
}

static bool tomak_indicator_enabled(void) {
    return g_tomak_config.lock_indicator_mode != TOMAK_INDICATOR_OFF;
}

static bool tomak_indicator_led_active(led_t led_state) {
    switch (g_tomak_config.lock_indicator_mode) {
        case TOMAK_INDICATOR_CAPS_LOCK:
            return led_state.caps_lock;
        case TOMAK_INDICATOR_SCROLL_LOCK:
            return led_state.scroll_lock;
        case TOMAK_INDICATOR_NUM_LOCK:
            return led_state.num_lock;
        default:
            return false;
    }
}

static bool tomak_indicator_led_changed(led_t previous, led_t current) {
    return tomak_indicator_led_active(previous) != tomak_indicator_led_active(current);
}

static void tomak_mark_indicator_dirty(void) {
    tomak_indicator_dirty = true;
}

void tomak_note_local_config_changed(void) {
    tomak_invalidate_indicator_render(false);
}

static bool tomak_badge_only_enabled(void) {
    return !g_tomak_config.full_rgb_matrix_enabled;
}

static void tomak_config_apply_defaults(tomak_config_t *config) {
    memset(config, 0, sizeof(*config));
    tomak_config_set_indicator_mode(config, TOMAK_INDICATOR_CAPS_LOCK);
    config->lock_indicator_overrides_rgb = false;
    config->lock_indicator_hsv.h         = 0;
    config->lock_indicator_hsv.s         = 0;
    config->lock_indicator_hsv.v         = 255;
    config->full_rgb_matrix_enabled      = true;
}

/* --- The persisted record ------------------------------------------------ */

static void tomak_build_era_keyboard_storage(const tomak_config_t *config, tomak_era_keyboard_config_t *storage) {
    memset(storage, 0, sizeof(*storage));
    storage->lock_indicator_mode          = tomak_normalized_indicator_mode(config->lock_indicator_mode);
    storage->lock_indicator_overrides_rgb = config->lock_indicator_overrides_rgb ? 1 : 0;
    storage->lock_indicator_hue           = config->lock_indicator_hsv.h;
    storage->lock_indicator_sat           = config->lock_indicator_hsv.s;
    storage->lock_indicator_val           = config->lock_indicator_hsv.v;
    storage->full_rgb_matrix_enabled      = config->full_rgb_matrix_enabled ? 1 : 0;
}

static void tomak_apply_era_keyboard_storage(const tomak_era_keyboard_config_t *storage, tomak_config_t *config) {
    tomak_config_set_indicator_mode(config, storage->lock_indicator_mode);
    config->lock_indicator_overrides_rgb = storage->lock_indicator_overrides_rgb != 0;
    config->lock_indicator_hsv.h         = storage->lock_indicator_hue;
    config->lock_indicator_hsv.s         = storage->lock_indicator_sat;
    config->lock_indicator_hsv.v         = storage->lock_indicator_val;
    config->full_rgb_matrix_enabled      = storage->full_rgb_matrix_enabled != 0;
}

static void tomak_write_config_to_era_eeprom(const tomak_config_t *config) {
    tomak_era_keyboard_config_t storage;
    tomak_build_era_keyboard_storage(config, &storage);
    era_eeprom_update_config(&storage, ERA_EEPROM_KEYBOARD_CONFIG_OFFSET, sizeof(storage));
}

void tomak_schedule_config_save_to_era_eeprom(void) {
    tomak_config_save_pending = true;
    tomak_config_save_timer = timer_read();
}

static void tomak_config_save_deferred_task(void) {
    if (!tomak_config_save_pending || timer_elapsed(tomak_config_save_timer) <= ERA_STORAGE_QUIET_DEFER_MS) {
        return;
    }

    tomak_config_save_pending = false;
    tomak_write_config_to_era_eeprom(&g_tomak_config);
    /* No presenter note here since the 2026-08-14 redesign: the write above
       reaches the storage engine's own dirty intake through the NVM changed
       hook, and the engine's pending fact is what lights the lamp — at this
       same instant, for every domain, not just this one. */
}

static void tomak_read_config_from_era_eeprom(tomak_config_t *config) {
    tomak_era_keyboard_config_t storage;
    if (era_eeprom_read_config(&storage, ERA_EEPROM_KEYBOARD_CONFIG_OFFSET, sizeof(storage)) != sizeof(storage)) {
        tomak_config_apply_defaults(config);
        tomak_write_config_to_era_eeprom(config);
        return;
    }

    tomak_apply_era_keyboard_storage(&storage, config);

    tomak_era_keyboard_config_t normalized;
    tomak_build_era_keyboard_storage(config, &normalized);
    if (memcmp(&storage, &normalized, sizeof(storage)) != 0) {
        era_eeprom_update_config(&normalized, ERA_EEPROM_KEYBOARD_CONFIG_OFFSET, sizeof(normalized));
    }
}

#ifdef ERA_SPLIT_EEPROM_SYNC_ENABLE
static void tomak_reload_config_from_era_eeprom(void) {
    tomak_era_keyboard_config_t storage;
    if (era_eeprom_read_config(&storage, ERA_EEPROM_KEYBOARD_CONFIG_OFFSET, sizeof(storage)) == sizeof(storage)) {
        tomak_apply_era_keyboard_storage(&storage, &g_tomak_config);
    } else {
        tomak_config_apply_defaults(&g_tomak_config);
    }
    tomak_indicator_rgb_valid = false;
    tomak_invalidate_indicator_render(true);
}
#endif

/* --- The ERA reset guard ------------------------------------------------- */

static bool tomak_reset_guard_reserved_is_zero(const tomak_era_reset_guard_t *guard) {
    for (uint8_t i = 0; i < sizeof(guard->reserved_zero); i++) {
        if (guard->reserved_zero[i] != 0) {
            return false;
        }
    }
    return true;
}

static bool tomak_era_reset_guard_is_valid(void) {
    tomak_era_reset_guard_t guard;
    if (era_eeprom_read_config(&guard, ERA_EEPROM_RESET_GUARD_CONFIG_OFFSET, sizeof(guard)) != sizeof(guard)) {
        return false;
    }

    return guard.reset_key32 == (uint32_t)ERA_EEPROM_RESET_KEY &&
           guard.reset_key_inverse32 == (uint32_t)~(uint32_t)ERA_EEPROM_RESET_KEY &&
           tomak_reset_guard_reserved_is_zero(&guard);
}

static void tomak_write_era_reset_guard(void) {
    tomak_era_reset_guard_t guard = {
        .reset_key32         = (uint32_t)ERA_EEPROM_RESET_KEY,
        .reset_key_inverse32 = (uint32_t)~(uint32_t)ERA_EEPROM_RESET_KEY,
    };
    era_eeprom_update_config(&guard, ERA_EEPROM_RESET_GUARD_CONFIG_OFFSET, sizeof(guard));
}

static void tomak_era_eeprom_strict_reset(void) {
    uint8_t zeros[ERA_EEPROM_CONFIG_SIZE] = {0};
    era_eeprom_update_config(zeros, 0, sizeof(zeros));

    tomak_config_apply_defaults(&g_tomak_config);
    tomak_write_config_to_era_eeprom(&g_tomak_config);
    era_split_sync_policy_reset_to_defaults();
    tomak_write_era_reset_guard();
}

static bool tomak_era_eeprom_ensure_ready(void) {
    if (tomak_era_reset_guard_is_valid()) {
        return false;
    }

    /* A pending eeconfig_init_quantum() erases the whole logical EEPROM and
     * then runs this same strict reset through eeconfig_init_kb(), so a reset
     * written here is destroyed a few init steps later. Defer to it.
     *
     * The predicate is nvm_eeconfig_is_enabled() and not eeconfig_is_enabled():
     * the latter also requires via_eeprom_is_valid(), which via_init() sets
     * true before quantum_init() reads it, so it can be false here and true
     * there - and then no strict reset would run at all. */
    if (!nvm_eeconfig_is_enabled()) {
        return false;
    }

    tomak_era_eeprom_strict_reset();
    return true;
}

static void tomak_init_era_owned_modules(void) {
    era_split_sync_policy_init();
    era_common_features_init();
}

/* --- The EEPROM-sync reload table ---------------------------------------- */

#ifdef ERA_SPLIT_EEPROM_SYNC_ENABLE
static void tomak_reload_rgb_matrix_from_eeprom_silent(void) {
#    ifdef RGB_MATRIX_ENABLE
    /* Read first, decide once. The previous shape opened with an
       unconditional rgb_matrix_disable_noeeprom() and re-lit only behind
       `if (enable)` on the value it had just read — a one-way trap: the
       panel was committed to darkness before the target state was known,
       and a read yielding enable == 0 left it dark with nothing scheduled
       to recover (device-observed 2026-08-14). It also clamped the mode only inside that same arm, so
       a stored-disabled config with an out-of-range mode entered RAM
       unclamped and waited for a manual RGB_TOG to index past the effect
       table. Now the stored image is staged, clamped unconditionally, and
       applied through the ordinary transition helpers exactly once — the
       enable=0-before-enable trick survives only where it forces the
       STARTING re-init for a mode change on an already-lit panel, with no
       task pass between the two writes. */
    rgb_config_t stored;
    eeconfig_read_rgb_matrix(&stored);
    if (stored.mode < 1) {
        stored.mode = 1;
    } else if (stored.mode >= RGB_MATRIX_EFFECT_MAX) {
        stored.mode = RGB_MATRIX_EFFECT_MAX - 1;
    }
    bool target_enable = stored.enable != 0;
    stored.enable      = rgb_matrix_config.enable;
    rgb_matrix_config  = stored;
    if (target_enable) {
        rgb_matrix_config.enable = 0;
        rgb_matrix_enable_noeeprom();
    } else {
        rgb_matrix_disable_noeeprom();
    }
#    endif
}

/* The storage contract's reload table, and every domain in it. The
   ERA_CONFIG-only body that stood in two of the three copies was live -- a
   converged write into any other domain landed in EEPROM and was never
   reloaded into RAM until a reboot -- which is the divergence that made this
   whole extraction worth doing rather than the line count. */
void era_split_eeprom_sync_reload_domain_kb(era_split_eeprom_sync_domain_t domain) {
    switch (domain) {
        case ERA_SPLIT_EEPROM_SYNC_DOMAIN_ERA_CONFIG:
            tomak_reload_config_from_era_eeprom();
            era_split_keyboard_reload_features_from_eeprom();
            break;
        case ERA_SPLIT_EEPROM_SYNC_DOMAIN_QMK_RGB_MATRIX:
            tomak_reload_rgb_matrix_from_eeprom_silent();
            break;
        case ERA_SPLIT_EEPROM_SYNC_DOMAIN_QMK_KEYMAP_CONFIG:
            eeconfig_read_keymap(&keymap_config);
            break;
        case ERA_SPLIT_EEPROM_SYNC_DOMAIN_QMK_DEFAULT_LAYER:
            default_layer_set(eeconfig_read_default_layer());
            break;
        case ERA_SPLIT_EEPROM_SYNC_DOMAIN_VIA_LAYOUT_OPTIONS:
#    ifdef VIA_ENABLE
            via_set_layout_options_kb(via_get_layout_options());
#    endif
            break;
        case ERA_SPLIT_EEPROM_SYNC_DOMAIN_DYNAMIC_KEYMAP:
        case ERA_SPLIT_EEPROM_SYNC_DOMAIN_DYNAMIC_MACRO:
        default:
            break;
    }
    era_split_eeprom_sync_reload_domain_user(domain);
}
#endif

/* --- Painting the indicator ---------------------------------------------- */

static RGB tomak_indicator_rgb(void) {
    if (!tomak_indicator_rgb_valid || tomak_cached_indicator_hsv.h != g_tomak_config.lock_indicator_hsv.h || tomak_cached_indicator_hsv.s != g_tomak_config.lock_indicator_hsv.s || tomak_cached_indicator_hsv.v != g_tomak_config.lock_indicator_hsv.v) {
        tomak_cached_indicator_hsv = g_tomak_config.lock_indicator_hsv;
        tomak_cached_indicator_rgb = hsv_to_rgb(tomak_cached_indicator_hsv);
        tomak_indicator_rgb_valid  = true;
    }
    return tomak_cached_indicator_rgb;
}

static void tomak_resume_rgb_matrix_for_caps_indicator(bool allow_without_usb_configured) {
#if defined(RGB_MATRIX_ENABLE) && defined(RGB_MATRIX_SLEEP)
    if (!tomak_indicator_enabled() || !rgb_matrix_get_suspend_state()) {
        return;
    }
    if (!allow_without_usb_configured && usb_device_state_get_configure_state() != USB_DEVICE_STATE_CONFIGURED) {
        return;
    }

    rgb_matrix_set_suspend_state(false);
#endif
}

void tomak_invalidate_indicator_render(bool allow_without_usb_configured) {
    tomak_mark_indicator_dirty();
    tomak_resume_rgb_matrix_for_caps_indicator(allow_without_usb_configured);
}

static bool tomak_set_caps_indicator_color(uint8_t led, uint8_t r, uint8_t g, uint8_t b, uint8_t led_min, uint8_t led_max) {
    if (led >= led_min && led < led_max) {
        rgb_matrix_set_color(led, r, g, b);
        return true;
    }
    return false;
}

/* The lock state led_update_kb() last saw, not a live host_keyboard_led_state().
   This was the seam each board filled and is now one answer for the family: on
   a split responder, and between matrix_init_kb() and the first
   led_update_kb(), the two can disagree, and 79H's cached read is the one the
   family adopted. */
static void tomak_apply_caps_indicator(uint8_t led_min, uint8_t led_max) {
    led_t led_state         = tomak_host_led_state;
    bool  indicator_enabled = tomak_indicator_enabled();
    bool  indicator_on      = indicator_enabled && tomak_indicator_led_active(led_state);
    if (!indicator_enabled && !tomak_indicator_clear_when_off) {
        return;
    }

    if (indicator_on) {
        RGB rgb_caps = tomak_indicator_rgb();
        for (uint8_t i = TOMAK_BADGE_LED_MIN; i < TOMAK_BADGE_LED_MAX; ++i) {
            tomak_set_caps_indicator_color(i, rgb_caps.r, rgb_caps.g, rgb_caps.b, led_min, led_max);
        }
    } else if (g_tomak_config.lock_indicator_overrides_rgb || tomak_indicator_clear_when_off) {
        for (uint8_t i = TOMAK_BADGE_LED_MIN; i < TOMAK_BADGE_LED_MAX; ++i) {
            tomak_set_caps_indicator_color(i, 0, 0, 0, led_min, led_max);
        }
    }
}

/* --- The STATUS field's three producers ----------------------------------- */

/* The full-field red frame has three producers on every board of this family:
   the core1 launch-failure report, the link-fallback report, and the
   storage-sync indicator. All are advanced from the housekeeping cadence and
   cached, and the policy pass reads only the caches -- 79H's shape, adopted,
   because a predicate advanced from inside the render pass runs at the
   panel's rate rather than the cadence's and cannot be arbitrated against a
   second producer that does not. */

#if defined(RGB_MATRIX_ENABLE)
static bool tomak_launch_signal_active_cached;
static bool tomak_launch_signal_on_cached;

static void tomak_launch_signal_visibility_task(void) {
    bool on     = false;
    bool active = era_split_communication_core_launch_signal_advance(&on);

#    if defined(RGB_MATRIX_SLEEP)
    /* Re-asserted on every pass the pattern runs, not once when it starts.
       Five live callers drive the suspend flag and three of them can raise it
       again inside the 3.68 s this takes, which would eat the rest of a report
       nobody gets to see a second time. The dedicated resume is needed because
       the family's indicator resume declines when the lock indicator is
       switched off, and a core1 failure has to show either way. */
    if (active && rgb_matrix_get_suspend_state()) {
        rgb_matrix_set_suspend_state(false);
    }
#    endif

    if (tomak_launch_signal_active_cached == active && tomak_launch_signal_on_cached == on) {
        return;
    }
    tomak_launch_signal_active_cached = active;
    tomak_launch_signal_on_cached     = on;
    tomak_invalidate_indicator_render(true);
}

static bool tomak_link_fallback_active_cached;
static bool tomak_link_fallback_on_cached;

static void tomak_link_fallback_visibility_task(void) {
    /* Withheld while the launch report still owns the field, so the
       fallback's first advance — which is what starts it — cannot arm
       against a live core1-failure pattern. */
    if (tomak_launch_signal_active_cached) {
        return;
    }

    bool on     = false;
    bool active = era_split_link_fallback_report_advance(&on);

#    if defined(RGB_MATRIX_SLEEP)
    if (active && rgb_matrix_get_suspend_state()) {
        rgb_matrix_set_suspend_state(false);
    }
#    endif

    if (tomak_link_fallback_active_cached == active && tomak_link_fallback_on_cached == on) {
        return;
    }
    tomak_link_fallback_active_cached = active;
    tomak_link_fallback_on_cached     = on;
    tomak_invalidate_indicator_render(true);
}
#endif

#if defined(ERA_SPLIT_EEPROM_SYNC_ENABLE) && defined(RGB_MATRIX_ENABLE)
static bool tomak_eeprom_sync_status_visible_cached;

static void tomak_eeprom_sync_status_visibility_task(void) {
    /* The family-wide indicator predicate. The held-frame re-flush helper that
       once rode this task retired with the presenter's flush handshake: the
       lamp is one engine-owned pending fact now, and no note waits on a frame
       reaching the panel. */
    bool visible = era_split_eeprom_sync_indicator_visible_advance();
    if (tomak_eeprom_sync_status_visible_cached == visible) {
        return;
    }

    tomak_eeprom_sync_status_visible_cached = visible;
    tomak_invalidate_indicator_render(true);
}
#endif

#if defined(RGB_MATRIX_RENDER_POLICY_ENABLE) && defined(RGB_MATRIX_ENABLE)
/* The STATUS frame's own bookkeeping rather than any one producer's: what the
   last policy pass published, and what the last flush actually put on the
   LEDs, which is what decides STATUS_DIRTY. */
static bool tomak_status_was_active;
static bool tomak_status_frame_valid;
static bool tomak_status_frame_on;
static bool tomak_status_policy_on;

enum {
    TOMAK_STATUS_SOURCE_NONE = 0,
    TOMAK_STATUS_SOURCE_LAUNCH_SIGNAL,
    TOMAK_STATUS_SOURCE_LINK_FALLBACK,
    TOMAK_STATUS_SOURCE_EEPROM_SYNC,
};
#endif

/* --- The render path ------------------------------------------------------ */

#if defined(RGB_MATRIX_RENDER_POLICY_ENABLE)
/* The indicator halves of the policy and flush passes, which this unit owns
   because it owns tomak_indicator_dirty and tomak_indicator_clear_when_off.
   They were a seam a board reached through until the STATUS producer above
   them stopped differing per board; now the whole pass is here and the dirty
   flags never leave the file that decides them. */
static void tomak_apply_indicator_render_policy(rgb_matrix_render_policy_t *policy) {
    if (tomak_badge_only_enabled()) {
        policy->flags |= RGB_MATRIX_RENDER_POLICY_RENDER_DOMAIN;
        policy->led_min_index = TOMAK_BADGE_LED_MIN;
        policy->led_max_index = TOMAK_BADGE_LED_MAX;
    }

    tomak_indicator_clear_when_off = tomak_indicator_dirty && (!rgb_matrix_config.enable || rgb_matrix_config.mode == RGB_MATRIX_NONE);
    if (tomak_indicator_dirty) {
        policy->flags |= RGB_MATRIX_RENDER_POLICY_INDICATORS_DIRTY;
    }
    if (tomak_indicator_enabled() || tomak_indicator_clear_when_off) {
        policy->flags |= RGB_MATRIX_RENDER_POLICY_INDICATORS_ENABLE | RGB_MATRIX_RENDER_POLICY_ALLOW_DISABLED;
    }
}

static void tomak_note_indicator_frame_flushed(uint8_t frame_flags) {
    if ((frame_flags & RGB_MATRIX_RENDER_FRAME_INDICATORS) != 0) {
        tomak_indicator_dirty          = false;
        tomak_indicator_clear_when_off = false;
    }
}

void rgb_matrix_render_policy_kb(rgb_matrix_render_policy_t *policy) {
    rgb_matrix_render_policy_user(policy);

#    if defined(RGB_MATRIX_SLEEP)
    if (rgb_matrix_get_suspend_state()) {
        return;
    }
#    endif

#    if defined(RGB_MATRIX_ENABLE)
    /* Which producer owns the STATUS frame this pass, and whether it wants the
       field lit. The launch-failure report outranks both others because a
       one-shot that is swallowed once is invisible for good. The fallback
       report outranks the storage indicator for the same reason: the
       indicator's steady ON would swallow the long-pulse dark phases. The
       indicator is the recoverable one - it comes back on the next policy
       pass after the report ends. */
    uint8_t status_source = TOMAK_STATUS_SOURCE_NONE;
    bool    status_on     = false;

    if (tomak_launch_signal_active_cached) {
        status_source = TOMAK_STATUS_SOURCE_LAUNCH_SIGNAL;
        status_on     = tomak_launch_signal_on_cached;
    }
    if (status_source == TOMAK_STATUS_SOURCE_NONE && tomak_link_fallback_active_cached) {
        status_source = TOMAK_STATUS_SOURCE_LINK_FALLBACK;
        status_on     = tomak_link_fallback_on_cached;
    }
#        if defined(ERA_SPLIT_EEPROM_SYNC_ENABLE)
    if (status_source == TOMAK_STATUS_SOURCE_NONE && tomak_eeprom_sync_status_visible_cached) {
        status_source = TOMAK_STATUS_SOURCE_EEPROM_SYNC;
        status_on     = true;
    }
#        endif

    if (status_source != TOMAK_STATUS_SOURCE_NONE) {
        tomak_status_policy_on = status_on;
        policy->flags &= ~(RGB_MATRIX_RENDER_POLICY_RENDER_DOMAIN | RGB_MATRIX_RENDER_POLICY_INDICATORS_ENABLE | RGB_MATRIX_RENDER_POLICY_INDICATORS_DIRTY);
        policy->flags |= RGB_MATRIX_RENDER_POLICY_DISABLE_EFFECT | RGB_MATRIX_RENDER_POLICY_STATUS_ACTIVE | RGB_MATRIX_RENDER_POLICY_ALLOW_DISABLED;
        /* The producers paint identical pixels for a given `status_on`, so
           a handover between them with the field unchanged needs no re-render
           and the source is deliberately not part of this test. That is also
           why the source is not kept past this branch. */
        if (!tomak_status_was_active || !tomak_status_frame_valid || tomak_status_policy_on != tomak_status_frame_on) {
            policy->flags |= RGB_MATRIX_RENDER_POLICY_STATUS_DIRTY;
        }
        policy->led_min_index = 0;
        policy->led_max_index = RGB_MATRIX_LED_COUNT;
        tomak_status_was_active = true;
        return;
    }
    if (tomak_status_was_active) {
        tomak_status_was_active  = false;
        tomak_status_frame_valid = false;
        tomak_invalidate_indicator_render(true);
    }
#    endif

    tomak_apply_indicator_render_policy(policy);
}

bool rgb_matrix_render_status_kb(const rgb_matrix_render_policy_t *policy) {
#    if defined(RGB_MATRIX_ENABLE)
    if ((policy->flags & RGB_MATRIX_RENDER_POLICY_STATUS_ACTIVE) != 0) {
        uint8_t red = tomak_status_policy_on ? 255 : 0;
        for (uint8_t i = 0; i < RGB_MATRIX_LED_COUNT; i++) {
            rgb_matrix_set_color(i, red, 0, 0);
        }
        return true;
    }
#    endif
    return rgb_matrix_render_status_user(policy);
}

void rgb_matrix_render_policy_flush_kb(uint8_t frame_flags) {
    bool status_frame = (frame_flags & RGB_MATRIX_RENDER_FRAME_STATUS) != 0;
    (void)status_frame;
#    if defined(ERA_SPLIT_EEPROM_SYNC_ENABLE) && defined(RGB_MATRIX_ENABLE)
    /* LED truth for the indicator stamps: every flushed frame reports whether
       it was the STATUS field — the core calls this on every PWM push,
       zero-flag frames included, so this is panel truth rather than producer
       truth. The packed state rides along so a mid-era breaker frame is
       latched with the three facts that name its path. */
    uint8_t panel_state = 0;
    if (rgb_matrix_config.enable) {
        panel_state |= ERA_SPLIT_EEPROM_SYNC_BREAK_STATE_RGB_ENABLED;
    }
    if (rgb_matrix_get_suspend_state()) {
        panel_state |= ERA_SPLIT_EEPROM_SYNC_BREAK_STATE_RGB_SUSPENDED;
    }
    if (tomak_status_policy_on) {
        panel_state |= ERA_SPLIT_EEPROM_SYNC_BREAK_STATE_STATUS_POLICY_ON;
    }
    era_split_eeprom_sync_note_status_frame_presence(status_frame, frame_flags, panel_state);
#    endif
#    if defined(RGB_MATRIX_ENABLE)
    /* Reached whenever RGB_MATRIX is built and not only when the storage
       engine is, because the launch report is the other producer and does not
       depend on it. That is the reach 79H had and 79S and `tomak` did not. */
    if (status_frame) {
        tomak_status_frame_on    = tomak_status_policy_on;
        tomak_status_frame_valid = true;
    } else if (tomak_status_frame_valid) {
        /* Any non-STATUS push while the held-frame proof stands means the
           panel no longer shows the held field — the device-caught case is a
           zero-flag black fill from an RGB_MATRIX_NONE transition mid-era,
           which stranded the receiving half dark until the era ended because
           this proof survived it. Dropping the proof makes the next policy
           pass re-render the field within one frame, and the presence note
           above turns the event into a counted re-light (`rn`) instead of an
           invisible one. */
        tomak_status_frame_valid = false;
    }
#    endif
    tomak_note_indicator_frame_flushed(frame_flags);
    rgb_matrix_render_policy_flush_user(frame_flags);
}
#endif

bool rgb_matrix_indicators_kb(void) {
    if (!rgb_matrix_indicators_user()) {
        return false;
    }

    tomak_apply_caps_indicator(0, RGB_MATRIX_LED_COUNT);
    /* No badge-only clear loop here: every board of this family sets
       RGB_MATRIX_RENDER_DOMAIN_ENABLE unconditionally, so narrowing the render
       domain is the implementation of the badge-only mask, and an
       indicator-time clear would only blank LEDs the domain already left
       dark. */
    return true;
}

bool rgb_matrix_indicators_advanced_kb(uint8_t led_min, uint8_t led_max) {
    if (!rgb_matrix_indicators_advanced_user(led_min, led_max)) {
        return false;
    }

    tomak_apply_caps_indicator(led_min, led_max);
    return true;
}

/* --- QMK hooks the family owns ------------------------------------------- */

bool led_update_kb(led_t led_state) {
    bool indicator_changed = tomak_indicator_led_changed(tomak_host_led_state, led_state);
    if (indicator_changed) {
        tomak_mark_indicator_dirty();
        tomak_resume_rgb_matrix_for_caps_indicator(false);
    }
    tomak_host_led_state = led_state;
    bool res = led_update_user(led_state);
    if (res) {
        led_update_ports(led_state);
    }
    return res;
}

void notify_usb_device_state_change_kb(struct usb_device_state usb_device_state) {
    led_t led_state = (led_t)usb_device_state.leds;
    bool  indicator_changed = tomak_indicator_led_changed(tomak_usb_led_state, led_state);

    era_split_keyboard_notify_usb_device_state_change((uint8_t)usb_device_state.configure_state);
    if (indicator_changed || usb_device_state.configure_state == USB_DEVICE_STATE_CONFIGURED) {
        if (indicator_changed) {
            tomak_mark_indicator_dirty();
        }
        tomak_resume_rgb_matrix_for_caps_indicator(false);
    }
    tomak_usb_led_state = led_state;
    notify_usb_device_state_change_user(usb_device_state);
}

/* The pin the half-duplex wire does not use is parked as an analog input.
   Guarded on the board's own TOMAK_SPLIT_SAFE_SINGLE_WIRE, so a family board
   that does not state one takes neither the macro nor the call. Run twice, at
   pre-init and again at post-init, because the split layer's own init can
   reconfigure the pair in between. */
#if defined(TOMAK_SPLIT_SAFE_SINGLE_WIRE)
static void tomak_disable_unused_split_wire_pins(void) {
    if (TOMAK_SPLIT_SAFE_SINGLE_WIRE != GP0) {
        palSetLineMode(GP0, PAL_MODE_INPUT_ANALOG);
    }
    if (TOMAK_SPLIT_SAFE_SINGLE_WIRE != GP1) {
        palSetLineMode(GP1, PAL_MODE_INPUT_ANALOG);
    }
}
#endif

/* The two split-class extension points (split/era_split_board.h). The class
   skeleton calls the first before era_split_keyboard_pre_init() and the second
   after keyboard_post_init_user() and before era_split_keyboard_post_init() --
   the same two positions the three board files put this in, which is why the
   pin park could move without moving. */
void era_split_board_pre_init(void) {
#if defined(TOMAK_SPLIT_SAFE_SINGLE_WIRE)
    tomak_disable_unused_split_wire_pins();
#endif
}

void era_split_board_post_init(void) {
#if defined(TOMAK_SPLIT_SAFE_SINGLE_WIRE)
    tomak_disable_unused_split_wire_pins();
#endif
}

/* No matrix_scan_kb overrides: the strong bodies two of the three board files
   carried were byte-identical to the weak default in era_rp2040_matrix_core.c.
   The slave-side hook pair is gone from the matrix core with the scan path's
   role branch - one hook per pass, on every half. */

/* The family's housekeeping, called by the split class skeleton after
   era_split_keyboard_task(). Two tiers where there were three: the third was a
   weak per-board hook that only TOMAK79H ever overrode, and with both STATUS
   pumps owned by the family there is nothing left for a board tier to carry.

   Pumps first, and that order is load-bearing: the launch retry runs inside
   era_split_keyboard_task(), and the signal's arm reads the post-retry state,
   so a launch that fails and immediately succeeds never arms it. The
   fallback pump then sees the launch cache from this pass and withholds
   its first advance while the launch report is live. */
void era_board_housekeeping_task(void) {
#if defined(RGB_MATRIX_ENABLE)
    tomak_launch_signal_visibility_task();
    tomak_link_fallback_visibility_task();
#endif
#if defined(ERA_SPLIT_EEPROM_SYNC_ENABLE) && defined(RGB_MATRIX_ENABLE)
    tomak_eeprom_sync_status_visibility_task();
#endif
    tomak_config_save_deferred_task();
}

void eeconfig_init_kb(void) {
    tomak_era_eeprom_strict_reset();
    tomak_init_era_owned_modules();
    eeconfig_init_user();
}

void matrix_init_kb(void) {
    if (tomak_era_eeprom_ensure_ready()) {
        tomak_init_era_owned_modules();
    }
    tomak_read_config_from_era_eeprom(&g_tomak_config);
    tomak_host_led_state = host_keyboard_led_state();
    tomak_usb_led_state  = (led_t)usb_device_state_get_leds();
    matrix_init_user();
}

#ifdef VIA_ENABLE
void via_init_kb(void) {
    tomak_era_eeprom_ensure_ready();
    tomak_read_config_from_era_eeprom(&g_tomak_config);
    tomak_init_era_owned_modules();
}

// Some helpers for setting/getting HSV
static void _set_color(HSV *color, uint8_t *data) {
    color->h = data[0];
    color->s = data[1];
}

static void _get_color(HSV *color, uint8_t *data) {
    data[0] = color->h;
    data[1] = color->s;
}

/* The board table the class skeleton's one dispatcher calls
   (common/system/era_board_hooks.h). These returned void until 2026-08-13 and
   an unknown value id fell out of the switch in silence; returning false is
   what makes the dispatcher answer id_unhandled, which is the VIA protocol
   answer the twelve non-split boards already gave. That is the single accepted
   behaviour change of the board-layer work, and it is on this surface only. */
bool era_board_via_get_value(uint8_t *data) {
    // data = [ value_id, value_data ]
    uint8_t *value_id   = &(data[0]);
    uint8_t *value_data = &(data[1]);

    switch (*value_id) {
        case id_custom_indicator_toggle:
            *value_data = g_tomak_config.lock_indicator_mode;
            return true;
        case id_custom_indicator_override:
            *value_data = g_tomak_config.lock_indicator_overrides_rgb;
            return true;
        case id_custom_indicator_brightness:
            *value_data = g_tomak_config.lock_indicator_hsv.v;
            return true;
        case id_custom_indicator_color:
            _get_color(&(g_tomak_config.lock_indicator_hsv), value_data);
            return true;
        case id_custom_badge_only:
            *value_data = tomak_badge_only_enabled();
            return true;
        default:
            return false;
    }
}

bool era_board_via_set_value(uint8_t *data) {
    // data = [ value_id, value_data ]
    uint8_t *value_id   = &(data[0]);
    uint8_t *value_data = &(data[1]);

    switch (*value_id) {
        case id_custom_indicator_toggle:
            tomak_config_set_indicator_mode(&g_tomak_config, *value_data);
            break;
        case id_custom_indicator_override:
            /* `!= 0` and `(bool)` were the two spellings the three copies
               carried; C defines the conversion to _Bool as `!= 0`, so they
               were the same instruction and neither is a choice. */
            g_tomak_config.lock_indicator_overrides_rgb = *value_data != 0;
            break;
        case id_custom_indicator_brightness:
            g_tomak_config.lock_indicator_hsv.v = *value_data;
            break;
        case id_custom_indicator_color:
            _set_color(&(g_tomak_config.lock_indicator_hsv), value_data);
            break;
        case id_custom_badge_only:
            g_tomak_config.full_rgb_matrix_enabled = *value_data == 0;
            break;
        default:
            return false;
    }

    tomak_note_local_config_changed();
    return true;
}

void era_board_via_save(void) {
    tomak_schedule_config_save_to_era_eeprom();
}

#endif
