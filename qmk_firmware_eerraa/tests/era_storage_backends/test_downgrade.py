"""Cross-format proof using production ERA NVM and the previous QMK WL parser."""
from pathlib import Path
import json
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
BOARDS = (
    "sirind/divine shared/n8x shared/n87 shared/n86 linworks/fave65s comm/era65 "
    "classicd/classicd_coreless classicd/classicd_core classicd/classicd_a1_ug "
    "classicd/classicd_a1 sirind/klein_hs sirind/klein_sd newone/odessey60s "
    "sirind/brick65s sirind/chickpad newone/odessey60h newone/h1 newone/a1 "
    "comm/riley comm/et_tkl comm/7b75"
).split()


def resolve(board, via):
    path = Path("keyboards/era") / board
    info = json.loads((ROOT / path / ("info.json" if board.startswith("shared/") else "keyboard.json")).read_text())
    features = "\n".join(f"{name.upper()}_ENABLE := {'yes' if enabled else 'no'}"
                         for name, enabled in info["features"].items())
    fixture = f"""
.DEFAULT_GOAL := print
KEYMAP_PATH := {path}/keymaps/{'via' if via else 'default'}
CONFIG_H := {path}/config.h
MCU := RP2040
VIA_ENABLE := {'yes' if via else 'no'}
{features}
include {path}/post_rules.mk
print:
\t@echo DRIVER=$(EEPROM_DRIVER)
\t@echo CONFIG=$(CONFIG_H)
\t@echo DEFINES=$(OPT_DEFS)
\t@echo LINK=$(LDFLAGS)
"""
    result = subprocess.run(["make", "--no-print-directory", "-f", "-"], input=fixture,
                            cwd=ROOT, capture_output=True, text=True, check=True)
    return dict(line.split("=", 1) for line in result.stdout.splitlines() if "=" in line)


def link_flash_fixture(directory, image_size=4):
    source = (ROOT / "keyboards/era/ld/ERA_RP2040_SRAM_RESIDENT.ld").read_text()
    script = directory / "placement.ld"
    # Use the production placement and collision assertions; unrelated startup
    # and SRAM assertions are exercised by every complete firmware link.
    script.write_text(source[:source.index("/* Aliases consumed")] +
                      f"\nSECTIONS {{ .text : {{ *(.text) . = {image_size}; }} > flash1 }}\n"
                      "__flash_binary_end = ORIGIN(flash1) + SIZEOF(.text);\n" +
                      source[source.index("ASSERT(LENGTH(flash1)"):])
    obj, elf = directory / "empty.o", directory / "placement.elf"
    subprocess.run(["arm-none-eabi-as", "-o", str(obj), "-"], input=".text\nnop\n", text=True, check=True)
    result = subprocess.run(["arm-none-eabi-ld", "-T", str(script), str(obj), "-o", str(elf)],
                            capture_output=True, text=True)
    return result, elf


class Downgrade(unittest.TestCase):
    def test_board_geometry_and_macro_surfaces(self):
        for board in BOARDS:
            for via in (False, True):
                with self.subTest(board=board, via=via):
                    values = resolve(board, via)
                    self.assertEqual(values["DRIVER"], "custom")
                    for feature in ("ERA_MACRO_REPLACE_ENABLE", "ERA_MACRO_OPTIONS_ENABLE"):
                        self.assertEqual("-D" + feature in values["DEFINES"].split(), via)
                    includes = [arg for header in values["CONFIG"].split() for arg in ("-include", header)]
                    source = """
_Static_assert(EEPROM_SIZE == 24576, "adoption must expose the full logical store");
_Static_assert(DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE == 16384, "macro upload capacity must be 16 KiB");
_Static_assert(VIA_EEPROM_MAGIC_ADDR == ERA_EEPROM_CONFIG_END, "VIA must follow ERA config");
#ifndef VIA_ENABLE
_Static_assert(DYNAMIC_KEYMAP_EEPROM_ADDR == ERA_EEPROM_CONFIG_END, "non-VIA must preserve the ERA config seat");
#endif
"""
                    subprocess.run(["gcc", "-x", "c", "-fsyntax-only", *includes,
                                    *(["-DVIA_ENABLE"] if via else []), "-"],
                                   input=source, cwd=ROOT, text=True, check=True)

    def test_flash_reservation_rejects_image_overlap(self):
        with tempfile.TemporaryDirectory() as directory:
            result, _ = link_flash_fixture(Path(directory), 2 * 1024 * 1024 - 264 * 1024 + 4)
            self.assertNotEqual(result.returncode, 0, "firmware may overwrite the new NVM region")
            self.assertIn("overlaps", result.stderr)

    def test_legacy_stores_and_reupgrade_survive_macro_replace(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            executable = directory / "downgrade"
            linked, elf = link_flash_fixture(directory)
            self.assertEqual(linked.returncode, 0, linked.stderr)
            symbols = subprocess.check_output(["arm-none-eabi-nm", str(elf)], text=True)
            base = int(re.search(r"^([0-9a-f]+) \w __era_nvm_region_base__$", symbols, re.M).group(1), 16)
            sources = [
                "tests/era_storage_backends/downgrade.c",
                "keyboards/era/common/storage/era_nvm.c",
                "quantum/wear_leveling/wear_leveling.c",
                "lib/fnv/hash_64a.c",
            ]
            command = [
                "gcc", "-std=gnu11", "-O2", "-Wall", "-Wextra", "-Werror",
                "-DEECONFIG_MAGIC_NUMBER=" + re.search(
                    r"#\s*define EECONFIG_MAGIC_NUMBER ([^\n/]+)",
                    (ROOT / "quantum/nvm/nvm_eeconfig.h").read_text()
                ).group(1).strip(),
                "-DBACKING_STORE_WRITE_SIZE=2",
                "-DWEAR_LEVELING_BACKING_SIZE=8192",
                "-DWEAR_LEVELING_LOGICAL_SIZE=4096",
                f"-DNVM_BASE_OFFSET={base - 0x10000000}",
                *["-I" + str(ROOT / p) for p in
                  (".", "quantum", "quantum/nvm", "quantum/wear_leveling", "lib/fnv")],
                *[str(ROOT / p) for p in sources], "-o", str(executable),
            ]
            subprocess.run(command, check=True)
            for mode in ("wear-leveling", "tomak"):
                with self.subTest(previous_firmware=mode):
                    result = subprocess.run([str(executable), mode], capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    print(result.stdout, end="")


if __name__ == "__main__":
    unittest.main()
