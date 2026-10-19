#!/usr/bin/env bash
# The three copy-to-RAM gates, read off one linked ELF, for any ERA board.
#
#   era_residency_gate.sh <elf>          -> ram0_resident_bytes=<n>
#                                           ram0_free_bytes=<n>
#                                           vectors_gate=<summary>
#                                          exit 0, or a named failure on stderr
#
# The three lines are the manifest fields era_qmk_build.sh records for every
# UF2 target, so every copy-to-RAM board gets the same evidence.
#
# These are canonical in era_sram_residency_contract.md and
# era_performance_gates.md. They lived inside the TOMAK79H-only launcher until
# 2026-08-11, which made them one board's gates rather than the image's:
# newone/odessey60s went copy-to-RAM on 2026-08-01 and linked malloc through
# RGBLIGHT's twinkle effect for the ten days until something ran an allocator
# check against it by hand. The residency contract already recorded the
# consequence -- "the floor gate currently runs only through the tomak79h
# launcher; other boards rely on the linker's hard ram0 overflow error until
# the migration slice generalizes the launcher" -- and this file is that.
#
# It takes an ELF and nothing else on purpose: no profile, no board name, no
# build. Whatever produced the ELF can call it, and every ERA board's adoption
# is checked by the same code rather than by the same intention.
set -euo pipefail

if (($# != 1)); then
    echo "usage: era_residency_gate.sh <elf>" >&2
    exit 2
fi
elf="$1"
if [[ ! -f "$elf" ]]; then
    echo "era_residency_gate.sh: no such ELF: $elf" >&2
    exit 2
fi

# Capacity is proved against each physical SRAM bank, including section ends
# and overlap. The unused main-SRAM count is growth space, not an implicit
# runtime reserve: allocators are forbidden and stacks are reserved separately.
# Capture the helper status before emitting any successful manifest fields.
budget_fields="$(python3 "$(dirname "$0")/era_sram_budget.py" "$elf")"

# No ERA image links an allocator entry point (era_invariants.md).
allocator_symbols="$(arm-none-eabi-nm "$elf" |
    awk '$NF ~ /^(malloc|calloc|realloc|_sbrk|chCoreAlloc|chCoreAllocI|chCoreAllocFromBaseI|chCoreAllocFromTopI|chHeapAlloc)$/ {print $NF}')"
if [[ -n "$allocator_symbols" ]]; then
    echo "era_residency_gate.sh: allocator entry points linked: $allocator_symbols" >&2
    exit 1
fi

# Vector-table residency gate: on the SRAM-resident image every .vectors entry
# except the reset slot holds an SRAM address, so no fault and no stray IRQ can
# vector into flash while a program/erase has the SSI out of XIP mode. The
# reset slot is the one legitimate exception - boot2 loads MSP/PC from the
# vectors LMA at 0x10000100 and Reset_Handler must be fetchable from flash - so
# it is checked to still BE in flash rather than skipped, which also keeps this
# gate from passing on a table that is somehow entirely SRAM.
#
# This reads the linked table's own bytes rather than a list of handler names,
# and that is the whole point of doing it here instead of as another linker
# ASSERT. The names ERA overrides in era_vector_defaults.c are in SRAM by
# construction, so asserting them proves nothing; the failure worth catching is
# a slot nobody claims - a driver stops installing its handler, or the override
# enumeration was incomplete - which falls back to the ChibiOS weak default in
# the flash carve-out and is invisible to any check built from the same
# enumeration.
vectors_bin="$(mktemp)"
trap 'rm -f "$vectors_bin"' EXIT
arm-none-eabi-objcopy -O binary --only-section=.vectors "$elf" "$vectors_bin"
vector_words=()
while read -r vector_word; do
    vector_words+=("$vector_word")
done < <(od -An -tx4 -v -w4 "$vectors_bin")

# 16 system slots plus CORTEX_NUM_VECTORS (32 on RP2040). A different count
# means the table is not the one this gate and era_vector_defaults.c were
# derived against, and the enumeration in that file is then wrong by
# construction.
if ((${#vector_words[@]} != 48)); then
    echo "era_residency_gate.sh: .vectors holds ${#vector_words[@]} entries, expected 48" >&2
    exit 1
fi
vectors_reset=$((16#${vector_words[1]}))
if ((vectors_reset < 0x10000000 || vectors_reset >= 0x20000000)); then
    echo "era_residency_gate.sh: the .vectors reset slot is not in flash: 0x${vector_words[1]}" >&2
    echo "  boot2 fetches the reset PC from flash; an SRAM value there does not boot." >&2
    exit 1
fi
vectors_outside=()
for vector_index in "${!vector_words[@]}"; do
    ((vector_index == 1)) && continue
    vector_value=$((16#${vector_words[vector_index]}))
    if ((vector_value < 0x20000000 || vector_value >= 0x20042000)); then
        vectors_outside+=("[$vector_index]=0x${vector_words[vector_index]}")
    fi
done
if ((${#vectors_outside[@]} > 0)); then
    echo "era_residency_gate.sh: ${#vectors_outside[@]} .vectors entries are outside SRAM:" >&2
    printf '  %s\n' "${vectors_outside[@]}" >&2
    echo "  A slot nobody installs falls back to the ChibiOS weak default pinned in" >&2
    echo "  .flash_startup. Add the matching symbol to era_vector_defaults.c." >&2
    exit 1
fi
rm -f "$vectors_bin"
trap - EXIT

printf '%s\n' "$budget_fields"
printf 'vectors_gate=%s entries, reset slot 0x%s in flash, %s in SRAM\n' \
    "${#vector_words[@]}" "${vector_words[1]}" "$((${#vector_words[@]} - 1))"
