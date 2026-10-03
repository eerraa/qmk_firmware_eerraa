#!/usr/bin/env python3
# Copyright 2026 Hyojin Bak (@eerraa)
# SPDX-License-Identifier: GPL-2.0-or-later
"""Measure linked allocations, including scratch-bank bounds and overlap.

Main SRAM has no implicit runtime reserve: allocators are forbidden by the
calling gate and both cores' stacks have explicit scratch-bank reservations.
The unused main-SRAM count is growth capacity, not stack headroom. Stack-depth
and pre-copy closure checks remain separate obligations.
"""
import re
import subprocess
import sys
from typing import NamedTuple


class Section(NamedTuple):
    name: str
    size: int
    address: int
    flags: frozenset


# SRAM5's final 256 bytes belong to the RP2040 ROM, including during Core1 launch.
REGIONS = ((0x20000000, 0x20040000), (0x20040000, 0x20041000), (0x20041000, 0x20041F00))
FLASH_SECTIONS = frozenset((".boot2", ".flash_startup"))
SECTION_ROW = re.compile(r"\s*(\d+)\s+(\S+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+[0-9a-fA-F]+\s+[0-9a-fA-F]+\s+2\*\*\d+\s*")


def parse_sections(output):
    lines = output.splitlines()
    sections = []
    for index, line in enumerate(lines):
        if not re.match(r"\s*\d+\s", line):
            continue
        row = SECTION_ROW.fullmatch(line)
        if row is None or int(row[1]) != len(sections):
            raise ValueError("invalid or incomplete objdump section table")
        if index + 1 == len(lines) or not re.fullmatch(r"\s*[A-Z_]+(?:, [A-Z_]+)*\s*", lines[index + 1]):
            raise ValueError("missing section flags")
        flags = frozenset(lines[index + 1].strip().split(", "))
        sections.append(Section(row[2], int(row[3], 16), int(row[4], 16), flags))
    if not sections:
        raise ValueError("no section table measured")
    return sections


def measure_sections(sections, flash_base, flash_limit):
    allocations = []
    resident = 0
    ram0_count = 0
    for section in sections:
        if not section.size or "ALLOC" not in section.flags:
            continue
        start, end = section.address, section.address + section.size
        region = next((i for i, (lo, hi) in enumerate(REGIONS) if lo <= start < end <= hi), None)
        if region is None:
            if section.name not in FLASH_SECTIONS or not (flash_base <= start < end <= flash_limit):
                raise ValueError(f"{section.name} allocation outside its SRAM bank: {start:#x}..{end:#x}")
        elif section.name in FLASH_SECTIONS:
            raise ValueError(f"{section.name} must remain in the flash startup carve-out")
        if section.name == ".heap" and (region != 0 or "CONTENTS" in section.flags):
            raise ValueError(".heap must be uninitialised main-SRAM filler")
        if section.name in (".mstack", ".pstack") and region != 1:
            raise ValueError(f"{section.name} must use the Core0 scratch bank")
        allocations.append((start, end, section.name))
        if region == 0 and section.name != ".heap":
            resident += section.size
            ram0_count += 1
    if not ram0_count:
        raise ValueError("no resident ram0 section measured")
    # Include .heap: excluding it from the cost does not permit a collision.
    allocations.sort()
    for previous, current in zip(allocations, allocations[1:]):
        if previous[1] > current[0]:
            raise ValueError(f"overlapping allocations: {previous[2]} and {current[2]}")
    return resident, REGIONS[0][1] - REGIONS[0][0] - resident


def linked_bounds(sections, nm_output):
    symbols = {}
    for line in nm_output.splitlines():
        fields = line.split()
        if len(fields) not in (3, 4):
            raise ValueError("invalid defined-symbol table")
        symbols[fields[-1]] = (int(fields[0], 16), int(fields[1], 16) if len(fields) == 4 else 0)
    required = ("__flash1_base__", "__flash1_end__", "__era_nvm_region_base__")
    if any(name not in symbols for name in required):
        raise ValueError("missing linked flash/NVM bounds")
    base, end, limit = (symbols[name][0] for name in required)
    if base != 0x10000000 or not base < limit < end <= 0x20000000:
        raise ValueError("invalid linked flash/NVM bounds")
    by_name = {section.name: section for section in sections}
    for name in (".mstack", ".pstack"):
        if name not in by_name or not by_name[name].size or "ALLOC" not in by_name[name].flags:
            raise ValueError(f"missing Core0 stack reservation: {name}")
    if "era_split_communication_core_entry" in symbols:
        for name in ("g_era_split_communication_core_stack", "g_era_split_communication_core_vector_table"):
            address, size = symbols.get(name, (0, 0))
            region = by_name.get(".ram5")
            if not size or region is None or not region.address <= address < address + size <= region.address + region.size:
                raise ValueError(f"missing Core1 scratch reservation: {name}")
    return base, limit


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: era_sram_budget.py <elf>")
    try:
        output = subprocess.run(["arm-none-eabi-objdump", "-h", sys.argv[1]], check=True, capture_output=True, text=True).stdout
        sections = parse_sections(output)
        symbols = subprocess.run(["arm-none-eabi-nm", "--defined-only", "-S", sys.argv[1]], check=True, capture_output=True, text=True).stdout
        base, limit = linked_bounds(sections, symbols)
        resident, free = measure_sections(sections, base, limit)
    except (OSError, subprocess.CalledProcessError, ValueError) as error:
        sys.exit(f"era_sram_budget.py: SRAM allocation gate failed: {error}")
    print(f"ram0_resident_bytes={resident}")
    print(f"ram0_free_bytes={free}")


if __name__ == "__main__":
    main()
