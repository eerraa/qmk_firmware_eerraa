#!/usr/bin/env python3
"""Detect a lost Caps feedback bit with the real native LED task and TD fixture."""
from pathlib import Path
from datetime import datetime, timezone
import hashlib
import json
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[2]
TARGET = ROOT / ".build/test_obj/era_backlight_pulse"
CASE = "EraBacklightTapHoldTiming.IndependentCapsFeedbackDoesNotWaitForTapDanceOrPulse"


def main():
    subprocess.run(["make", "test:era_backlight_pulse", "-j8"], cwd=ROOT, check=True,
                   stdout=subprocess.DEVNULL, timeout=180)
    evidence = ROOT / ".era-artifacts/td-caps-controls" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    evidence.mkdir(parents=True)
    flags = shlex.split((TARGET / "cflags.txt").read_text())
    # QMK links $(call uniq,$(OBJ)); the emitted raw list may contain repeats.
    objects = list(dict.fromkeys(shlex.split((TARGET / "obj.txt").read_text())))
    linkflags = shlex.split((TARGET / "ldflags.txt").read_text())
    selected = [o for o in objects if o.endswith("tests/era_backlight_pulse/test_caps_feedback_led.o")]
    assert len(selected) == 1, selected
    product = ROOT / "quantum/led.c"
    wrapper = ROOT / "tests/era_backlight_pulse/test_caps_feedback_led.c"
    paths = [product, wrapper, *(ROOT / o for o in objects)]
    digest = lambda: {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
    before = digest()
    needle = "gpio_write_pin(LED_CAPS_LOCK_PIN, led_state.caps_lock);"
    original = product.read_text()
    assert original.count(needle) == 1
    results = []
    for name, mutant in (("baseline", False), ("lost_caps_bit", True)):
        led = evidence / (name + "-led.c")
        led.write_text(original.replace(needle, "gpio_write_pin(LED_CAPS_LOCK_PIN, false);") if mutant else original)
        c = evidence / (name + ".c")
        # The private symbol prefix is identical to the ordinary fixture.
        c.write_text(wrapper.read_text().replace('"../../quantum/led.c"', '"' + led.as_posix() + '"'))
        obj, exe = evidence / (name + ".o"), evidence / name
        commands = [["gcc", *flags, "-c", str(c), "-o", str(obj)],
                    ["gcc", *(str(obj) if o == selected[0] else o for o in objects), "-o", str(exe), *linkflags],
                    [str(exe), "--gtest_filter=" + CASE]]
        exits = []
        output = ""
        for index, command in enumerate(commands):
            run = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=90)
            output = run.stdout + run.stderr
            (evidence / f"{name}-{index}.log").write_text(output)
            exits.append(run.returncode)
            if index < 2:
                assert run.returncode == 0, output
        assert (exits[-1] != 0 and "[  FAILED  ] " + CASE in output) if mutant else exits[-1] == 0, output
        results.append(dict(control=name, commands=commands, exit_codes=exits, expected_runtime_failure=mutant))
        print("PASS", name, "runtime failure detected" if mutant else "production LED path", flush=True)
    assert before == digest(), "Control changed original source or native objects"
    (evidence / "results.json").write_text(json.dumps(dict(results=results, unchanged_source_objects=before), indent=2))
    print("Evidence:", evidence)


if __name__ == "__main__":
    main()
