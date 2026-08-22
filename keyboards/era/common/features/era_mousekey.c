// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

/* Mouse key tuning through VIA, over upstream's own engine.
 *
 * **This unit has no runtime state of its own, and that is the design rather
 * than an economy.** QMK already keeps every value this page tunes as a
 * writable runtime variable (`quantum/mousekey.c`), so the whole feature is a
 * persisted image plus an assignment into those variables at init, at a
 * cross-half reload, and at each VIA set. era_tapping.c is the same shape one
 * step heavier -- it holds a state struct because its values reach the engine
 * through `get_*` hook overrides, and there is no hook to arm here.
 *
 * The two the engine did not already offer as variables are the per-event step
 * sizes, `MOUSEKEY_MOVE_DELTA` and `MOUSEKEY_WHEEL_DELTA`; they are variables
 * only under ERA_MOUSEKEY_RUNTIME_DELTA, which the make block that compiles
 * this file emits beside its SRC line (era_qmk_fork_ledger.md).
 *
 * **It adds nothing to a mouse-key pass.** Every value is written outside the
 * pass and read where the engine already read a macro or a variable, so the
 * guard at `quantum/mousekey.c`'s head -- 86.7 % of 2.35 us of a 20.21 us scan
 * pass, device 2026-08-17 -- keeps its whole return, and no control is read
 * ahead of it. */

#include "era_mousekey.h"

#include <string.h>
#include "mousekey.h"
#include "../storage/era_eeprom_storage.h"
#ifdef VIA_ENABLE
#    include "../system/era_state_sync.h"
#endif

/* No apostrophe in the message: the preprocessor lexes the text of a skipped
   group, so one reads as an unterminated character constant and -Werror stops
   the build in every configuration this refusal is not for. */
#if !defined(ERA_MOUSEKEY_RUNTIME_DELTA)
#    error the ERA mousekey page writes the per-event step sizes, which are variables only under ERA_MOUSEKEY_RUNTIME_DELTA -- era_common_qmk_rules.mk emits it beside the SRC line for this file
#endif

/* "MUSK". It must differ from every other signature this block can land on
   after a layout move, and it does: the 2026-08-18 regrouping put this block's
   signature word exactly where the debounce block's used to sit, so sharing a
   value would have made an older debounce block read as a valid mouse one. */
#define ERA_MOUSEKEY_SIGNATURE 0x4B53554DUL
#define ERA_MOUSEKEY_VERSION   1U

enum {
    /* The report descriptor's ceiling, which `move_unit()`/`wheel_unit()` clamp
       to anyway; clamping here as well is what keeps the readback honest about
       a value the engine would have cut. */
    ERA_MOUSEKEY_UNIT_MAX       = MOUSEKEY_MOVE_MAX,
    ERA_MOUSEKEY_WHEEL_UNIT_MAX = MOUSEKEY_WHEEL_MAX,
    /* Zero is the one interval that is not a slow setting: the engine's test is
       `timer_elapsed(...) > interval`, so zero emits a report every pass. */
    ERA_MOUSEKEY_INTERVAL_MIN_MS = 1,
    /* The unit the page states the climb in, and it is **chosen so the
       readback is exact** rather than for resolution. The engine holds a count
       of events, so a duration survives the round trip only when it rounds back
       to the same unit at every update rate the page offers -- and 33 ms
       divides none of the round durations, which cost 20 ms two blank readings
       and 10 ms nine. At 50 ms every offered duration returns exactly what was
       picked at every offered rate, and one byte still reaches 12.75 s. */
    ERA_MOUSEKEY_RAMP_UNIT_MS = 50,
};

/* **ERA's defaults, not upstream's, and they come from a device sitting.**
   Upstream ships 8 px a step at 50 events a second climbing to 80 px in 30
   events -- 400 counts a second reaching 4000 in 0.6 s -- which was tuned for
   the displays of its day and is both too abrupt and too fast on a modern one.
   Measured 2026-08-18 on 5120x2160, with each figure the midpoint of a band the
   owner bracketed: a climb of 1.0-1.5 s (0.6 read fast, 2.0 slow), a start of
   400 counts a second, and a top whose whole point is that it does not
   transfer -- see the note at the top speed's setter.

   The top is set for the middle of the target range rather than for the rig it
   was measured on: 1600 counts a second crosses 2560 in 1.6 s and 1920 in
   1.2 s, and a 4K or wider desktop raises it. A default that is a little slow
   is recoverable by aiming; one that is a little fast is not. */
enum {
    ERA_MOUSEKEY_DEFAULT_INTERVAL_MS = 10, /* 100 events a second */
    ERA_MOUSEKEY_DEFAULT_START_PX    = 4,  /* 400 counts a second */
    ERA_MOUSEKEY_DEFAULT_TOP_PX      = 16, /* 1600 counts a second */
    ERA_MOUSEKEY_DEFAULT_RAMP_MS     = 1000,
};

/* The engine stores a ratio and a count of events, so both defaults have to
   divide exactly or the block would boot on a value its own page cannot show. */
_Static_assert(ERA_MOUSEKEY_DEFAULT_TOP_PX % ERA_MOUSEKEY_DEFAULT_START_PX == 0, "The default top speed must be a whole multiple of the default start speed.");
_Static_assert(ERA_MOUSEKEY_DEFAULT_RAMP_MS % ERA_MOUSEKEY_DEFAULT_INTERVAL_MS == 0, "The default climb must be a whole number of default-rate events.");
_Static_assert(ERA_MOUSEKEY_DEFAULT_RAMP_MS / ERA_MOUSEKEY_DEFAULT_INTERVAL_MS <= UINT8_MAX, "The default climb must fit mk_time_to_max.");

enum {
    ERA_MOUSEKEY_WHEEL_ACCEL_OFF = 0,
    ERA_MOUSEKEY_WHEEL_ACCEL_MILD,
    ERA_MOUSEKEY_WHEEL_ACCEL_STRONG,
    ERA_MOUSEKEY_WHEEL_ACCEL_COUNT,
};

/* One dropdown moves both wheel acceleration values, because separately they
   are two ways to say the same thing to a user: a ramp length means nothing
   without the speed it ramps to. Strong is upstream's own pair. */
static const struct {
    uint8_t max_speed;
    uint8_t time_to_max;
} era_mousekey_wheel_accel[ERA_MOUSEKEY_WHEEL_ACCEL_COUNT] = {
    [ERA_MOUSEKEY_WHEEL_ACCEL_OFF]    = {1, 0},
    [ERA_MOUSEKEY_WHEEL_ACCEL_MILD]   = {4, MOUSEKEY_WHEEL_TIME_TO_MAX},
    [ERA_MOUSEKEY_WHEEL_ACCEL_STRONG] = {MOUSEKEY_WHEEL_MAX_SPEED, MOUSEKEY_WHEEL_TIME_TO_MAX},
};

/* All ten raw values, whichever six the page shows. Opening the rest later is
   then a definition change and not a migration. */
typedef struct __attribute__((packed)) {
    uint8_t  delay;
    uint8_t  interval;
    uint8_t  max_speed;
    uint8_t  time_to_max;
    uint8_t  wheel_delay;
    uint8_t  wheel_interval;
    uint8_t  wheel_max_speed;
    uint8_t  wheel_time_to_max;
    uint8_t  move_delta;
    uint8_t  wheel_delta;
    uint8_t  version;
    uint8_t  reserved;
    uint32_t signature;
} era_mousekey_config_t;

_Static_assert(sizeof(era_mousekey_config_t) == ERA_EEPROM_MOUSEKEY_CONFIG_SIZE, "ERA mousekey config size changed.");

static era_mousekey_config_t mousekey_config;

static uint8_t era_mousekey_clamp(uint8_t value, uint8_t min, uint8_t max) {
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}

static void era_mousekey_apply_defaults(era_mousekey_config_t *config) {
    memset(config, 0, sizeof(*config));
    config->delay             = MOUSEKEY_DELAY / 10;
    config->interval          = ERA_MOUSEKEY_DEFAULT_INTERVAL_MS;
    config->max_speed         = ERA_MOUSEKEY_DEFAULT_TOP_PX / ERA_MOUSEKEY_DEFAULT_START_PX;
    config->time_to_max       = ERA_MOUSEKEY_DEFAULT_RAMP_MS / ERA_MOUSEKEY_DEFAULT_INTERVAL_MS;
    config->wheel_delay       = MOUSEKEY_WHEEL_DELAY / 10;
    config->wheel_interval    = MOUSEKEY_WHEEL_INTERVAL;
    config->wheel_max_speed   = MOUSEKEY_WHEEL_MAX_SPEED;
    config->wheel_time_to_max = MOUSEKEY_WHEEL_TIME_TO_MAX;
    config->move_delta        = ERA_MOUSEKEY_DEFAULT_START_PX;
    config->wheel_delta       = MOUSEKEY_WHEEL_DELTA;
    config->version           = ERA_MOUSEKEY_VERSION;
    config->signature         = ERA_MOUSEKEY_SIGNATURE;
}

/* Identity only. Range is normalize's, below, so one out-of-range byte costs
   its own field and not the other nine -- the block is ten independent knobs
   with no combination that is invalid as a whole, which is what makes clamping
   the right repair and the signature the whole of the rejection. */
static bool era_mousekey_config_is_valid(const era_mousekey_config_t *config) {
    return config && config->signature == ERA_MOUSEKEY_SIGNATURE && config->version == ERA_MOUSEKEY_VERSION;
}

static bool era_mousekey_normalize_config(era_mousekey_config_t *config) {
    era_mousekey_config_t previous = *config;

    config->interval        = era_mousekey_clamp(config->interval, ERA_MOUSEKEY_INTERVAL_MIN_MS, UINT8_MAX);
    config->wheel_interval  = era_mousekey_clamp(config->wheel_interval, ERA_MOUSEKEY_INTERVAL_MIN_MS, UINT8_MAX);
    config->max_speed       = era_mousekey_clamp(config->max_speed, 1, UINT8_MAX);
    config->wheel_max_speed = era_mousekey_clamp(config->wheel_max_speed, 1, UINT8_MAX);
    config->move_delta      = era_mousekey_clamp(config->move_delta, 1, ERA_MOUSEKEY_UNIT_MAX);
    config->wheel_delta     = era_mousekey_clamp(config->wheel_delta, 1, ERA_MOUSEKEY_WHEEL_UNIT_MAX);
    config->version         = ERA_MOUSEKEY_VERSION;
    config->reserved        = 0;
    config->signature       = ERA_MOUSEKEY_SIGNATURE;

    return memcmp(&previous, config, sizeof(*config)) != 0;
}

/* **Acceleration off is a constant-speed mode, and the constant is the first
   step and not the top one.** With `mk_time_to_max` zero the engine's
   `mousekey_repeat >= mk_time_to_max` test is true from the first repeat, so
   whatever `mk_max_speed` holds becomes the step size of every event after the
   key-down one -- which shipped as the *top* speed and was unusable at all six
   of its values: 32 px at the default 50 events a second is 1600 px/s, and the
   127 px pick crossed half a screen at once once the host's own pointer
   acceleration had multiplied it (device 2026-08-18). The step size is what a
   user feels, not the average speed, and the usable band for a constant step is
   the start speed's 1..16 px rather than the top speed's 32..127.

   So the runtime multiplier is derived rather than stored: off applies 1, which
   collapses every branch of `move_unit()` (`quantum/mousekey.c`) to
   `mk_move_delta` alone. The stored `max_speed` is untouched, so the ratio the
   user chose comes back when they pick a ramp again -- no shadow field, and the
   config stays the one truth.

   This is also what the wheel already did: its off level is `{1, 0}`, a
   constant at the base step. The cursor was the odd one out, and one page whose
   two controls of the same name meant opposite things is what made the setting
   unreadable on device. */
static void era_mousekey_apply_runtime(void) {
    mk_delay             = mousekey_config.delay;
    mk_interval          = mousekey_config.interval;
    mk_max_speed         = (mousekey_config.time_to_max == 0) ? 1 : mousekey_config.max_speed;
    mk_time_to_max       = mousekey_config.time_to_max;
    mk_wheel_delay       = mousekey_config.wheel_delay;
    mk_wheel_interval    = mousekey_config.wheel_interval;
    mk_wheel_max_speed   = mousekey_config.wheel_max_speed;
    mk_wheel_time_to_max = mousekey_config.wheel_time_to_max;
    mk_move_delta        = mousekey_config.move_delta;
    mk_wheel_delta       = mousekey_config.wheel_delta;
}

static void era_mousekey_publish_if_changed(const era_mousekey_config_t *previous) {
#ifdef VIA_ENABLE
    if (memcmp(previous, &mousekey_config, sizeof(*previous)) != 0) {
        era_state_sync_note_config_semantic_commit(ERA_EEPROM_MOUSEKEY_CONFIG_OFFSET, sizeof(mousekey_config));
    }
#else
    (void)previous;
#endif
}

void era_mousekey_save_config(void) {
    era_eeprom_update_config(&mousekey_config, ERA_EEPROM_MOUSEKEY_CONFIG_OFFSET, sizeof(mousekey_config));
}

static bool era_mousekey_load_from_eeprom(bool write_defaults) {
    bool dirty = false;

    if (era_eeprom_read_config(&mousekey_config, ERA_EEPROM_MOUSEKEY_CONFIG_OFFSET, sizeof(mousekey_config)) != sizeof(mousekey_config) || !era_mousekey_config_is_valid(&mousekey_config)) {
        era_mousekey_apply_defaults(&mousekey_config);
        dirty = true;
    } else {
        dirty = era_mousekey_normalize_config(&mousekey_config);
    }

    era_mousekey_apply_runtime();

    if (write_defaults && dirty) {
        era_mousekey_save_config();
    }
    return true;
}

void era_mousekey_init(void) {
    era_mousekey_load_from_eeprom(true);
}

void era_mousekey_reload_from_eeprom(void) {
    era_mousekey_load_from_eeprom(false);
}

/* --- The two cursor speeds ------------------------------------------------ */

/* The page offers the two speeds a user can see -- the first step and the top
   step, both in pixels -- rather than the step size and the multiplier the
   engine keeps. Several (step, multiplier) pairs produce one top speed, so the
   raw pair is a control whose two halves argue with each other.
   `era_tapping_normalize_term()` quantises in the same place for the same
   reason: the adapter is where a page's units become the engine's. */
static uint8_t era_mousekey_effective_max_speed(void) {
    uint16_t unit = (uint16_t)mousekey_config.move_delta * (uint16_t)mousekey_config.max_speed;
    return unit > ERA_MOUSEKEY_UNIT_MAX ? (uint8_t)ERA_MOUSEKEY_UNIT_MAX : (uint8_t)unit;
}

/* Rounded, not truncated, and the difference is visible: at a step of 8 the
   ceiling pick of 127 truncates to a multiplier of 15 and reads back as 120,
   while rounding gives 16 and the engine's own clamp lands it back on 127
   exactly. Only a step of 12 leaves a pick unreachable, which the readback then
   reports honestly. */
static uint8_t era_mousekey_speed_ratio(uint8_t target_unit, uint8_t step) {
    uint16_t ratio = ((uint16_t)target_unit + (uint16_t)(step / 2U)) / (uint16_t)step;
    if (ratio < 1U) {
        return 1U;
    }
    return ratio > UINT8_MAX ? UINT8_MAX : (uint8_t)ratio;
}

void era_mousekey_set_cursor_min_speed(uint8_t pixels) {
    era_mousekey_config_t previous = mousekey_config;
    uint8_t target = era_mousekey_effective_max_speed();

    mousekey_config.move_delta = era_mousekey_clamp(pixels, 1, ERA_MOUSEKEY_UNIT_MAX);
    /* Hold the top speed where the user left it: the multiplier is a ratio
       between the two speeds, so changing the first step alone would otherwise
       drag the top one with it. */
    mousekey_config.max_speed = era_mousekey_speed_ratio(target, mousekey_config.move_delta);
    era_mousekey_apply_runtime();
    era_mousekey_publish_if_changed(&previous);
}

/* **The top speed is the one control that does not transfer between users, and
   the page's list is sized for that rather than for one desktop.** It is
   travel-dominated: what it has to buy is crossing the screen, and a screen's
   width in pixels is the thing that differs. The start speed and the climb are
   precision-dominated and a button is the same number of pixels on every
   display at the same scaling, so those two do transfer.

   Measured 2026-08-18 on 5120x2160: crossing in 1.6-2.1 s read good, 1.07 s
   slightly fast and 3.2 s slow. Held at that band the top speed runs from about
   1000 counts a second on a 1920-wide desktop to about 2800 on a 5120-wide one,
   which is the range the list spans -- and the update rate fills between its
   entries, because the two multiply. */
void era_mousekey_set_cursor_max_speed(uint8_t pixels) {
    era_mousekey_config_t previous = mousekey_config;
    uint8_t target = era_mousekey_clamp(pixels, mousekey_config.move_delta, ERA_MOUSEKEY_UNIT_MAX);

    mousekey_config.max_speed = era_mousekey_speed_ratio(target, mousekey_config.move_delta);
    era_mousekey_apply_runtime();
    era_mousekey_publish_if_changed(&previous);
}

uint8_t era_mousekey_get_cursor_min_speed(void) {
    return mousekey_config.move_delta;
}

uint8_t era_mousekey_get_cursor_max_speed(void) {
    return era_mousekey_effective_max_speed();
}

/* --- The remaining four --------------------------------------------------- */

/* **The climb is a time on the page and a count of events in the engine, and
   the adapter is where the two meet.** `mk_time_to_max` counts movement events,
   so a page that passed it straight through made the climb change length
   whenever the update rate moved -- raise the rate for smoothness and the ramp
   you did not touch halves. The owner met it at 100 /s, where Normal's thirty
   events are 0.3 s rather than the 0.6 s the same setting gives at the default
   rate (device 2026-08-18).

   Held as a duration instead, one knob stops moving three things. The duration
   is derived from the two stored values rather than stored beside them, exactly
   as the top speed is: `time_to_max x interval` is the climb, and rounding both
   ways to the nearest unit makes the readback exact for every duration the
   engine can actually reach.

   **The reachable set is what `mk_time_to_max` being one byte decides**, and it
   shrinks as the rate rises: 255 events is 5.1 s at 20 ms and 1.27 s at 5 ms.
   A longer pick than that clamps, and the readback then reports the shorter
   climb the engine holds rather than the one that was asked for. */
static uint16_t era_mousekey_ramp_ms(void) {
    return (uint16_t)mousekey_config.time_to_max * (uint16_t)mousekey_config.interval;
}

static void era_mousekey_hold_ramp_ms(uint16_t ramp_ms) {
    if (ramp_ms == 0) {
        mousekey_config.time_to_max = 0; /* off: the constant-speed mode */
        return;
    }
    uint16_t interval = mousekey_config.interval; /* at least 1, by its clamp */
    uint32_t events   = ((uint32_t)ramp_ms + (interval / 2U)) / interval;
    if (events < 1U) {
        events = 1U;
    }
    mousekey_config.time_to_max = events > UINT8_MAX ? UINT8_MAX : (uint8_t)events;
}

void era_mousekey_set_cursor_acceleration(uint8_t ramp_units) {
    era_mousekey_config_t previous = mousekey_config;
    era_mousekey_hold_ramp_ms((uint16_t)ramp_units * (uint16_t)ERA_MOUSEKEY_RAMP_UNIT_MS);
    era_mousekey_apply_runtime();
    era_mousekey_publish_if_changed(&previous);
}

uint8_t era_mousekey_get_cursor_acceleration(void) {
    if (mousekey_config.time_to_max == 0) {
        return 0;
    }
    uint32_t units = ((uint32_t)era_mousekey_ramp_ms() + (ERA_MOUSEKEY_RAMP_UNIT_MS / 2U)) / ERA_MOUSEKEY_RAMP_UNIT_MS;
    return units > UINT8_MAX ? UINT8_MAX : (uint8_t)units;
}

void era_mousekey_set_cursor_interval_ms(uint8_t interval_ms) {
    era_mousekey_config_t previous = mousekey_config;
    /* Read the climb before the rate moves, and restate it after: the rate is
       then the one control that changes only how finely the motion is cut. */
    uint16_t ramp_ms = era_mousekey_ramp_ms();

    mousekey_config.interval = era_mousekey_clamp(interval_ms, ERA_MOUSEKEY_INTERVAL_MIN_MS, UINT8_MAX);
    era_mousekey_hold_ramp_ms(ramp_ms);
    era_mousekey_apply_runtime();
    era_mousekey_publish_if_changed(&previous);
}

uint8_t era_mousekey_get_cursor_interval_ms(void) {
    return mousekey_config.interval;
}

void era_mousekey_set_wheel_interval_ms(uint8_t interval_ms) {
    era_mousekey_config_t previous = mousekey_config;
    mousekey_config.wheel_interval = era_mousekey_clamp(interval_ms, ERA_MOUSEKEY_INTERVAL_MIN_MS, UINT8_MAX);
    era_mousekey_apply_runtime();
    era_mousekey_publish_if_changed(&previous);
}

uint8_t era_mousekey_get_wheel_interval_ms(void) {
    return mousekey_config.wheel_interval;
}

void era_mousekey_set_wheel_acceleration(uint8_t level) {
    era_mousekey_config_t previous = mousekey_config;
    if (level >= ERA_MOUSEKEY_WHEEL_ACCEL_COUNT) {
        level = ERA_MOUSEKEY_WHEEL_ACCEL_STRONG;
    }
    mousekey_config.wheel_max_speed   = era_mousekey_wheel_accel[level].max_speed;
    mousekey_config.wheel_time_to_max = era_mousekey_wheel_accel[level].time_to_max;
    era_mousekey_apply_runtime();
    era_mousekey_publish_if_changed(&previous);
}

/* Derived from the stored speed rather than kept as a second field, so a value
   the full ten-control set may one day write still reads back as the nearest
   level instead of as a level nobody set. */
uint8_t era_mousekey_get_wheel_acceleration(void) {
    if (mousekey_config.wheel_max_speed <= era_mousekey_wheel_accel[ERA_MOUSEKEY_WHEEL_ACCEL_OFF].max_speed) {
        return ERA_MOUSEKEY_WHEEL_ACCEL_OFF;
    }
    if (mousekey_config.wheel_max_speed <= era_mousekey_wheel_accel[ERA_MOUSEKEY_WHEEL_ACCEL_MILD].max_speed) {
        return ERA_MOUSEKEY_WHEEL_ACCEL_MILD;
    }
    return ERA_MOUSEKEY_WHEEL_ACCEL_STRONG;
}
