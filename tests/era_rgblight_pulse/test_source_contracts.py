#!/usr/bin/env python3
# Copyright 2026 Hyojin Bak (@eerraa)
# SPDX-License-Identifier: GPL-2.0-or-later
"""Pin the Pulse family's seams: one 0..255 width for both lighting families,
the four underglow modes after Twinkle, the timer clause, the 1 ms dispatch,
the switch-edge and suspend wiring, the make refusal, and the boards' selector,
default speed and definition controls."""
from __future__ import annotations

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

RGBLIGHT_BOARDS = {
    "comm/7b75": "7B75-VIA.json",
    "comm/classicd_a1_ug": "CLASSICD_A1_UG-VIA.json",
    "comm/classicd_core": "CLASSICD_CORE-VIA.json",
    "comm/classicd_coreless": "CLASSICD_CORELESS-VIA.json",
    "comm/riley": "RILEY-VIA.json",
    "newone/odessey60h": "ODESSEY60H-VIA.json",
    "newone/odessey60s": "ODESSEY60S-VIA.json",
}
BACKLIGHT_BOARDS = {
    "era65": "ERA65-VIA.json",
    "linx3/n8x": "N8X-VIA.json",
    "newone/a1": "NEWONE_A1-VIA.json",
    "comm/et_tkl": "ET_TKL-VIA.json",
    "comm/classicd_a1": "CLASSICD_A1-VIA.json",
    "comm/classicd_a1_ug": "CLASSICD_A1_UG-VIA.json",
    "comm/classicd_core": "CLASSICD_CORE-VIA.json",
    "comm/classicd_coreless": "CLASSICD_CORELESS-VIA.json",
    "comm/7b75": "7B75-VIA.json",
}
SYMBOLS = ("ERA_PULSE_OFF_PRESS", "ERA_PULSE_ON_PRESS", "ERA_PULSE_OFF_PRESS_HOLD", "ERA_PULSE_ON_PRESS_HOLD")
LABELS = ("Pulse Off Press", "Pulse On Press", "Pulse Off Press (Hold)", "Pulse On Press (Hold)")
UPSTREAM_ANIMATIONS = {"alternating", "breathing", "christmas", "knight", "rainbow_mood", "rainbow_swirl", "rgb_test", "snake", "static_gradient", "twinkle"}
FIRST_PULSE_MODE = 43  # Solid Color 1 + the 41 upstream dynamic/static variants, Twinkle 6 = 42
EFFECT_COMMAND = ["id_qmk_rgblight_effect", 2, 2]
PULSE_SPEED_COMMAND = ["id_custom_blink_speed", 0, 3]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def control(node, command):
    if isinstance(node, dict):
        if node.get("content") == command:
            return node
        for value in node.values():
            found = control(value, command)
            if found:
                return found
    elif isinstance(node, list):
        for value in node:
            found = control(value, command)
            if found:
                return found
    return None


# One width for both families, read from the ordinary 0..255 speed value.
policy = read("keyboards/era/common/features/era_pulse_policy.h")
assert "ERA_PULSE_MIN_MS  = 5" in policy and "ERA_PULSE_STEP_MS = 1" in policy and "era_pulse_duration_ms(uint8_t speed)" in policy
backlight_header = read("keyboards/era/common/features/era_backlight.h")
assert "ERA_BACKLIGHT_SPEED_MIN  = 0" in backlight_header and "ERA_BACKLIGHT_SPEED_MAX  = 255" in backlight_header, "the backlight takes the full slider"
backlight = read("keyboards/era/common/features/era_backlight.c")
assert "era_pulse_duration_ms(backlight_config_era.pulse_speed)" in backlight and "ERA_BACKLIGHT_DEFAULT_SPEED = 15" in backlight
assert "ERA_BACKLIGHT_CONFIG_VALID = 0xB2" in backlight, "a block stored on the 1..10 scale is reset, not misread"
engine = read("keyboards/era/common/features/era_rgblight_pulse.c")
assert '#include "era_pulse_policy.h"' in engine and "era_pulse_duration_ms(rgblight_get_speed())" in engine and "chVTSetI(" in engine
assert "timer_read" not in engine and "sync_timer_read" not in engine, "no clock read; the one-shot and the tick keep time"
backlight_policy = read("keyboards/era/common/features/era_backlight_pulse_policy.h")
assert '#include "era_pulse_policy.h"' in backlight_policy and "typedef era_pulse_state_t era_backlight_pulse_state_t;" in backlight_policy, "the backlight runs the same policy"

# The four underglow modes and their core seams.
modes = read("quantum/rgblight/rgblight_modes.h")
assert "#    ifdef ERA_RGBLIGHT_PULSE_ENABLE" in modes, "the four modes ride the ERA selector"
positions = [modes.index(f"_RGBM_SINGLE_DYNAMIC({symbol})") for symbol in SYMBOLS]
assert positions == sorted(positions), "Off Press, On Press, Off Press (Hold), On Press (Hold)"
assert modes.index("TWINKLE_end") < positions[0] < modes.index("////  Add a new mode here."), "appended after Twinkle"
header = read("quantum/rgblight/rgblight.h")
assert "|| defined(ERA_RGBLIGHT_PULSE_ENABLE)" in header[: header.index("#    define RGBLIGHT_USE_TIMER")], "dynamic modes need the animation timer"
core = read("quantum/rgblight/rgblight.c")
assert "bool era_rgblight_pulse_mode(uint8_t mode);" in core and "void era_rgblight_pulse_effect(animation_status_t *anim);" in core
dispatch = core[core.index("else if (era_rgblight_pulse_mode(rgblight_status.base_mode))"):]
dispatch = dispatch[: dispatch.index("}")]
assert "interval_time = 1;" in dispatch and "effect_func   = era_rgblight_pulse_effect;" in dispatch, "1 ms tick to the ERA engine"

# Wiring: the switch edge, init, and the non-split suspend bridge.
nonsplit = read("keyboards/era/common/system/era_nonsplit_board.c")
assert "#if defined(ERA_BACKLIGHT_EFFECT_ENABLE) || defined(ERA_RGBLIGHT_PULSE_ENABLE)\nvoid switch_event_kb(" in nonsplit, "switch edge reaches the underglow Pulse"
features = read("keyboards/era/common/system/era_common_features.c")
assert "era_rgblight_pulse_init();" in features and "era_rgblight_pulse_note_key_event(row, col, pressed);" in features
session = read("keyboards/era/common/system/era_usb_session.c")
assert "era_rgblight_pulse_suspend();" not in session and "era_rgblight_pulse_resume();" not in session, "USB hooks must not bypass RGB Sleep master"
assert "era_rgblight_pulse_suspend();" in core and "era_rgblight_pulse_resume();" in core
mode_helper = core[core.index("void rgblight_mode_eeprom_helper("):core.index("void rgblight_mode(uint8_t")]
assert "era_rgblight_pulse_reset();" in mode_helper, "reset before another physical switch edge"

# Make: refused by name without QMK RGBLight, declared once, default no.
rules = read("keyboards/era/common/system/era_common_qmk_rules.mk")
fragment = rules[rules.index("ifeq ($(strip $(ERA_RGBLIGHT_PULSE_ENABLE)), yes)"):]
fragment = fragment[: fragment.index("\nendif")]
assert "$(error" in fragment and "RGBLIGHT_ENABLE" in fragment
assert "OPT_DEFS += -DERA_RGBLIGHT_PULSE_ENABLE" in fragment and "SRC += keyboards/era/common/features/era_rgblight_pulse.c" in fragment
assert "ERA_RGBLIGHT_PULSE_ENABLE ?= no" in read("keyboards/era/era_build_options.mk")

# The seven RGBLIGHT boards: selector above the include, default speed 15, the four labels at 43..46.
for board, definition in RGBLIGHT_BOARDS.items():
    post = read(f"keyboards/era/{board}/post_rules.mk")
    assert "ERA_RGBLIGHT_PULSE_ENABLE = yes" in post, board
    assert post.index("ERA_RGBLIGHT_PULSE_ENABLE = yes") < post.index("include keyboards/era/common/system/era_common_qmk_rules.mk"), f"{board}: above the include"
    rgblight = json.loads(read(f"keyboards/era/{board}/keyboard.json"))["rgblight"]
    assert {name for name, on in rgblight["animations"].items() if on} == UPSTREAM_ANIMATIONS, f"{board}: the 42 upstream modes precede Pulse"
    assert rgblight["default"]["speed"] == 15, f"{board}: 5 + 15 = 20 ms default pulse"
    effect = control(json.loads(read(f"keyboards/era/{board}/keymaps/via/{definition}")), EFFECT_COMMAND)
    assert effect, board
    pairs = [(option, index) if isinstance(option, str) else (option[0], option[1]) for index, option in enumerate(effect["options"])]
    assert pairs[-4:] == list(zip(LABELS, range(FIRST_PULSE_MODE, FIRST_PULSE_MODE + 4))), f"{board}: {pairs[-4:]}"

# The nine backlight-effect boards: Pulse Speed is the full slider.
for board, definition in BACKLIGHT_BOARDS.items():
    speed = control(json.loads(read(f"keyboards/era/{board}/keymaps/via/{definition}")), PULSE_SPEED_COMMAND)
    assert speed and speed["options"] == [0, 255], f"{board}: {speed and speed.get('options')}"

print("PASS: one 5 + speed ms width on a 0..255 slider for both families; underglow seams, wiring and make refusal; seven RGBLIGHT boards at 43..46 with default 15; nine backlight boards on the full slider")
