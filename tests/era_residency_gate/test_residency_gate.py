#!/usr/bin/env python3
"""Exercise the production allocation parser and complete gate on linked ARM ELFs."""
import importlib.util
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "keyboards/era/common/tools"
spec = importlib.util.spec_from_file_location("budget", TOOLS / "era_sram_budget.py")
budget = importlib.util.module_from_spec(spec)
spec.loader.exec_module(budget)


def section(name, size, address, flags=("ALLOC",)):
    return budget.Section(name, size, address, frozenset(flags))


def measure(sections):
    return budget.measure_sections(sections, 0x10000000, 0x101E0000)


class AllocationTests(unittest.TestCase):
    def test_small_growth_margin_is_not_runtime_overflow(self):
        self.assertEqual(measure([section(".sram_image", 245336, 0x20000000)]), (245336, 16808))

    def test_exact_physical_capacity(self):
        self.assertEqual(measure([section(".sram_image", 262144, 0x20000000)]), (262144, 0))

    def test_crossing_each_bank_end_is_rejected(self):
        for lo, hi in budget.REGIONS:
            with self.subTest(bank=hex(lo)), self.assertRaisesRegex(ValueError, "outside"):
                measure([section(".vectors", 192, 0x20000000), section(".spill", 8, hi - 4)])

    def test_outside_and_alias_addresses_are_rejected(self):
        for address in (0, 0x1FFFFFFC, 0x20041F00, 0x20042000, 0x21000000):
            with self.subTest(address=hex(address)), self.assertRaisesRegex(ValueError, "outside"):
                measure([section(".sram_image", 192, address)])

    def test_overlap_is_not_hidden_by_total_free_space(self):
        with self.assertRaisesRegex(ValueError, "overlapping"):
            measure([section(".vectors", 192, 0x20000000), section(".bss", 32, 0x20000080)])

    def test_heap_is_excluded_from_cost_but_not_overlap(self):
        image = section(".sram_image", 1024, 0x20000000)
        self.assertEqual(measure([image, section(".heap", 261120, 0x20000400)]), (1024, 261120))
        with self.assertRaisesRegex(ValueError, "overlapping"):
            measure([image, section(".heap", 1024, 0x20000300)])

    def test_heap_cannot_hide_initialised_or_scratch_storage(self):
        image = section(".sram_image", 1024, 0x20000000)
        for heap in (section(".heap", 16, 0x20040000), section(".heap", 16, 0x20000400, ("ALLOC", "CONTENTS"))):
            with self.subTest(heap=heap), self.assertRaisesRegex(ValueError, "filler"):
                measure([image, heap])

    def test_core0_stacks_stay_in_their_bank(self):
        with self.assertRaisesRegex(ValueError, "Core0 scratch"):
            measure([section(".pstack", 2560, 0x20000000)])

    def test_flash_carve_out_is_closed(self):
        image = section(".sram_image", 1024, 0x20000000)
        self.assertEqual(measure([image, section(".boot2", 256, 0x10000000)]), (1024, 261120))
        with self.assertRaisesRegex(ValueError, "outside"):
            measure([image, section(".text", 256, 0x10000400)])
        with self.assertRaisesRegex(ValueError, "flash startup"):
            measure([section(".boot2", 256, 0x20000000)])

    def test_flash_outside_linked_capacity_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "outside"):
            measure([section(".sram_image", 1024, 0x20000000), section(".flash_startup", 16, 0x1FFFF000)])

    def test_linked_reservations_are_required(self):
        symbols = "10000000 A __flash1_base__\n10200000 A __flash1_end__\n101e0000 A __era_nvm_region_base__\n"
        stacks = [section(".mstack", 1024, 0x20040000), section(".pstack", 2560, 0x20040400)]
        self.assertEqual(budget.linked_bounds(stacks, symbols), (0x10000000, 0x101E0000))
        with self.assertRaisesRegex(ValueError, "missing linked"):
            budget.linked_bounds(stacks, "")
        with self.assertRaisesRegex(ValueError, "Core0 stack"):
            budget.linked_bounds([], symbols)
        with self.assertRaisesRegex(ValueError, "Core1 scratch"):
            budget.linked_bounds(stacks, symbols + "20001000 00000008 T era_split_communication_core_entry\n")

    def test_nonalloc_debug_and_empty_sections_are_not_resident(self):
        self.assertEqual(measure([section(".sram_image", 1024, 0x20000000), section(".debug", 999999, 0, ("CONTENTS",)), section(".ram1", 0, 0)]), (1024, 261120))

    def test_no_measured_image_fails_closed(self):
        for sections in ([], [section(".heap", 262144, 0x20000000)]):
            with self.subTest(sections=sections), self.assertRaisesRegex(ValueError, "no resident"):
                measure(sections)

    def test_parser_reads_hex_and_alloc_flags(self):
        parsed = budget.parse_sections("Sections:\nIdx Name Size VMA LMA File off Algn\n 0 .sram_image 0002a07c 200000c0 10000488 000030c0 2**4\n CONTENTS, ALLOC, LOAD, READONLY, CODE\n")
        self.assertEqual(parsed, [section(".sram_image", 172156, 0x200000C0, ("CONTENTS", "ALLOC", "LOAD", "READONLY", "CODE"))])

    def test_empty_malformed_truncated_and_missing_rows_fail(self):
        for output in ("", "objdump: error", " 0 .sram_image BAD! 200000c0\n", " 1 .sram_image 10 20000000 10000488 30c0 2**4\n ALLOC\n", " 0 .sram_image 10 20000000 10000488 30c0 2**4\n"):
            with self.subTest(output=output), self.assertRaises(ValueError):
                budget.parse_sections(output)


@unittest.skipUnless(shutil.which("arm-none-eabi-as") and shutil.which("bash"), "run in the configured WSL ARM toolchain")
class LinkedGateTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name)

    def build(self, allocator=False, vector=0x200000C1, resident=245336):
        source = f'''.syntax unified
.cpu cortex-m0plus
.thumb
.section .vectors,"a"
.word 0x20040400
.word 0x10000101
.rept 46
.word {vector}
.endr
.section .sram_image,"ax"
.space {resident - 192}
'''
        if allocator:
            source += ".global malloc\nmalloc:\n bx lr\n"
        (self.path / "test.s").write_text(source)
        (self.path / "test.ld").write_text("__flash1_base__ = 0x10000000; __flash1_end__ = 0x10200000; __era_nvm_region_base__ = 0x101e0000; SECTIONS { .vectors 0x20000000 : { *(.vectors) } .sram_image : { *(.sram_image) } .mstack 0x20040010 (NOLOAD) : { . += 0x400; } .pstack (NOLOAD) : { . += 0xa00; } }")
        subprocess.run(["arm-none-eabi-as", "-o", str(self.path / "test.o"), str(self.path / "test.s")], check=True, capture_output=True)
        subprocess.run(["arm-none-eabi-ld", "-T", str(self.path / "test.ld"), "-o", str(self.path / "test.elf"), str(self.path / "test.o")], check=True, capture_output=True)
        return subprocess.run(["bash", str(TOOLS / "era_residency_gate.sh"), str(self.path / "test.elf")], capture_output=True, text=True)

    def test_complete_gate_accepts_low_margin_image(self):
        run = self.build()
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertIn("ram0_free_bytes=16808", run.stdout)
        self.assertIn("vectors_gate=48 entries", run.stdout)

    def test_complete_gate_rejects_real_overflow(self):
        run = self.build(resident=262148)
        self.assertNotEqual(run.returncode, 0)
        self.assertIn("outside", run.stderr)
        self.assertNotIn("ram0_free_bytes=", run.stdout)

    def test_allocator_still_rejected(self):
        run = self.build(allocator=True)
        self.assertNotEqual(run.returncode, 0)
        self.assertIn("allocator entry points", run.stderr)
        self.assertNotIn("ram0_free_bytes=", run.stdout)

    def test_flash_interrupt_vector_still_rejected(self):
        run = self.build(vector=0x10000101)
        self.assertNotEqual(run.returncode, 0)
        self.assertIn("outside SRAM", run.stderr)

    def test_invalid_elf_tool_failure_cannot_report_capacity(self):
        elf = self.path / "invalid.elf"
        elf.write_bytes(b"not an ELF")
        run = subprocess.run(["bash", str(TOOLS / "era_residency_gate.sh"), str(elf)], capture_output=True, text=True)
        self.assertNotEqual(run.returncode, 0)
        self.assertNotIn("ram0_free_bytes=", run.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)
