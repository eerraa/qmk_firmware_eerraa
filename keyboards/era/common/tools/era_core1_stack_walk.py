#!/usr/bin/env python3
# Copyright 2026 Hyojin Bak (@eerraa)
# SPDX-License-Identifier: GPL-2.0-or-later
"""Core1 stack disassembly walk — the ELF gate's one unreported budget.

`era_performance_gates.md`'s ELF And Static-Capacity Gate asks for a measured
core1 stack figure whenever a change adds core1 call depth, and states why no
compile-time construct can supply it: neither `STACK_WORDS` nor
`MIN_STACK_WORDS` is derived from the call graph. The method it names is this
one — the deepest frame sum reachable from `era_split_communication_core_entry`,
summing push register counts plus stack-pointer adjustments along `bl` edges,
recursion reported rather than followed, plus the 32-byte Cortex-M0+ exception
frame, because core1 takes its deadline IRQ anywhere in that chain.

**This script exists because the instrument kept changing.** The gate records
three readings taken with three different instruments — hand-walked, scripted,
scripted again — and says in so many words that only the margin and the chain
*shape* carry across them, because a walker that stops its prologue scan at the
first `mov` under-reports by roughly 270 B on this image and reads as an
improvement. A committed walker makes the next reading comparable to this one
by construction. Do not "fix" it silently: a change to the method invalidates
every figure taken with it, so change the method and re-take the baseline in
the same commit, or do not change it.

Two accounting decisions worth stating, because both were wrong in a draft:

  * Frame size is the running maximum over the whole function, not the
    prologue. Cortex-M0 splits a prologue across `push`/`mov`/`push` pairs to
    reach the high registers, and a scan that stops early misses the second.
  * The stack-pointer adjustment has a register form. GCC allocates a frame
    larger than the 7-bit `sub sp, #imm` immediate by loading a negative word
    from the literal pool and issuing `add sp, rN` — which is exactly what
    `era_split_communication_core_standing_service_once` does for its 564-byte
    frame, the single deepest frame in the standing chain. A walker matching
    only `sub sp, #imm` reports 20 B for that function and misses 96% of it.

Usage:
    era_core1_stack_walk.py <elf-or-disassembly> [root symbol ...]

An ELF path is disassembled through `arm-none-eabi-objdump -d`; a text path is
read as objdump output directly. With no roots the four chains the gate names
are walked.

It does not judge the figure: the acceptance is the headroom against the linked
reservation and that is the reader's call, so a thin margin still exits 0. It
does refuse to look like a reading when it took none — exit 1 when not one
requested root was found in the image, because a walk that measured nothing has
not measured a shallow chain.

The reservation is read out of the image being measured, as the size of
`g_era_split_communication_core_stack`, and not restated here: it is an
`#ifndef` default a board may override, and a headroom figure taken against
some other build's reservation is worse than none. A disassembly text carries
no symbol sizes, so that input falls back to the source default and says so.
"""
import os
import re
import shutil
import subprocess
import sys
from collections import defaultdict

# The source default for ERA_SPLIT_COMMUNICATION_CORE_STACK_WORDS
# (communication_core/era_split_communication_core_lifecycle_rp2040.c), used
# only when the image cannot be asked -- see reservation_bytes().
DEFAULT_RESERVATION_BYTES = 512 * 4
STACK_SYMBOL = "g_era_split_communication_core_stack"
EXCEPTION_FRAME = 32  # Cortex-M0+ takes 8 words on exception entry

DEFAULT_ROOTS = (
    "era_split_communication_core_entry",
    "era_split_communication_core_process_initiator",
    "era_split_communication_core_standing_service_once",
    "era_split_communication_core_responder_service_once",
)

FUNC_RE = re.compile(r"^([0-9a-f]{8}) <([^>]+)>:$")
INSN_RE = re.compile(r"^\s*([0-9a-f]+):\s+(?:[0-9a-f]{4,8}\s)+\s*\t(.*)$")
WORD_RE = re.compile(r"^\s*([0-9a-f]+):\s+[0-9a-f]{8}\s+\.word\s+0x([0-9a-f]+)")

BL_RE = re.compile(r"^bl(?:\.w)?\s+[0-9a-f]+ <([^>+]+)(?:\+0x[0-9a-f]+)?>")
BLX_REG_RE = re.compile(r"^blx\s+r\d+")
PUSH_RE = re.compile(r"^push\s+\{(.*)\}")
POP_RE = re.compile(r"^pop\s+\{(.*)\}")
SP_IMM_RE = re.compile(r"^(add|sub)\s+sp,\s*(?:sp,\s*)?#(\d+)")
SP_REG_RE = re.compile(r"^(add|sub)\s+sp,\s*(?:sp,\s*)?r(\d+)\b")
SP_MOV_RE = re.compile(r"^mov\s+sp,\s*r(\d+)\b")
LDR_PC_RE = re.compile(r"^ldr\s+r(\d+),\s*\[pc,.*@ \(([0-9a-f]+) <")
MOVS_RE = re.compile(r"^movs\s+r(\d+),\s*#(\d+)")
LSLS_RE = re.compile(r"^lsls\s+r(\d+),\s*(?:r(\d+),\s*)?#(\d+)")
MOV_REG_RE = re.compile(r"^mov(?:s)?\s+r(\d+),\s*r(\d+)$")

_REG_ALIAS = {"sl": 10, "fp": 11, "ip": 12, "sp": 13, "lr": 14, "pc": 15}


def _reg_num(name):
    name = name.strip()
    return _REG_ALIAS.get(name, None) if not name.startswith("r") else int(name[1:])


def _reglist_len(text):
    total = 0
    for part in text.split(","):
        part = part.strip()
        if not part:
            continue
        if "-" in part:
            lo, hi = part.split("-")
            total += _reg_num(hi) - _reg_num(lo) + 1
        else:
            total += 1
    return total


def _signed32(value):
    return value - (1 << 32) if value >= (1 << 31) else value


def is_elf(path):
    with open(path, "rb") as fh:
        return fh.read(4) == b"\x7fELF"


def reservation_bytes(path):
    """-> (bytes, note). The linked size of the core1 stack, from the image."""
    if not is_elf(path):
        return DEFAULT_RESERVATION_BYTES, (
            "reservation is the source default, not this input: a disassembly "
            "text carries no symbol sizes")
    nm = os.environ.get("NM", "arm-none-eabi-nm")
    if shutil.which(nm) is None:
        return DEFAULT_RESERVATION_BYTES, (
            f"{nm} not found; reservation is the source default and may not be "
            f"this image's")
    out = subprocess.run([nm, "-S", path], check=True, capture_output=True,
                         text=True).stdout
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 4 and parts[3] == STACK_SYMBOL:
            return int(parts[1], 16), None
    return DEFAULT_RESERVATION_BYTES, (
        f"{STACK_SYMBOL} carries no size in this image; reservation is the "
        f"source default")


def disassemble(path):
    """Return objdump text for an ELF, or the file's own text if it is one."""
    if not is_elf(path):
        with open(path, "r", errors="replace") as fh:
            return fh.read()
    objdump = os.environ.get("OBJDUMP", "arm-none-eabi-objdump")
    if shutil.which(objdump) is None:
        sys.exit(
            f"{objdump} not found. Disassemble in the build environment and pass\n"
            f"the text, or set OBJDUMP. (The gate's build environment is the tool\n"
            f"adapter's business, not this script's.)"
        )
    return subprocess.run(
        [objdump, "-d", path], check=True, capture_output=True, text=True
    ).stdout


def parse(text):
    """-> (frames, calls, notes). frames[fn] is the peak stack bytes fn itself
    consumes; calls[fn] the direct `bl` targets in source order."""
    words = {}
    for line in text.splitlines():
        m = WORD_RE.match(line)
        if m:
            words[int(m.group(1), 16)] = _signed32(int(m.group(2), 16))

    frames, calls, notes = {}, defaultdict(list), []
    fn, running, peak, regs = None, 0, 0, {}

    def close():
        if fn is not None:
            frames[fn] = peak

    for line in text.splitlines():
        m = FUNC_RE.match(line)
        if m:
            close()
            fn, running, peak, regs = m.group(2), 0, 0, {}
            continue
        if fn is None:
            continue
        mi = INSN_RE.match(line)
        if not mi:
            continue
        insn = mi.group(2).split(";")[0].split("@")[0].strip()

        m2 = LDR_PC_RE.match(mi.group(2).strip())
        if m2:
            regs[int(m2.group(1))] = words.get(int(m2.group(2), 16))
            continue
        m2 = MOVS_RE.match(insn)
        if m2:
            regs[int(m2.group(1))] = int(m2.group(2))
            continue
        m2 = LSLS_RE.match(insn)
        if m2:
            dst, src, sh = int(m2.group(1)), m2.group(2), int(m2.group(3))
            base = regs.get(int(src) if src else dst)
            regs[dst] = None if base is None else base << sh
            continue
        m2 = MOV_REG_RE.match(insn)
        if m2:
            regs[int(m2.group(1))] = regs.get(int(m2.group(2)))
            continue

        m2 = PUSH_RE.match(insn)
        if m2:
            running += 4 * _reglist_len(m2.group(1))
            peak = max(peak, running)
            continue
        m2 = POP_RE.match(insn)
        if m2:
            running -= 4 * _reglist_len(m2.group(1))
            continue
        m2 = SP_IMM_RE.match(insn)
        if m2:
            delta = int(m2.group(2))
            running += delta if m2.group(1) == "sub" else -delta
            peak = max(peak, running)
            continue
        m2 = SP_REG_RE.match(insn)
        if m2:
            value = regs.get(int(m2.group(2)))
            if value is None:
                notes.append(f"{fn}: unresolved `{insn}` — frame under-reported")
                continue
            # `add sp, rN` with a negative rN is how a frame past the 7-bit
            # immediate is allocated; the same instruction with a positive rN
            # is the matching release.
            running += -value if m2.group(1) == "add" else value
            peak = max(peak, running)
            continue
        if SP_MOV_RE.match(insn):
            notes.append(f"{fn}: `{insn}` (frame-pointer restore) not modelled")
            continue

        m2 = BL_RE.match(insn)
        if m2:
            calls[fn].append(m2.group(1))
            continue
        if BLX_REG_RE.match(insn):
            calls[fn].append("<indirect>")
    close()
    return frames, calls, notes


def walk(root, frames, calls):
    memo, indirect, recursion = {}, set(), []

    def visit(fn, stack):
        if fn in stack:
            recursion.append(" -> ".join(stack[stack.index(fn):] + [fn]))
            return 0, []
        if fn == "<indirect>":
            indirect.add(stack[-1] if stack else "?")
            return 0, []
        if fn in memo:
            return memo[fn]
        own = frames.get(fn)
        if own is None:
            return 0, []
        deepest, path = 0, []
        for callee in dict.fromkeys(calls.get(fn, ())):
            depth, sub = visit(callee, stack + [fn])
            if depth > deepest:
                deepest, path = depth, sub
        result = (own + deepest, [f"{fn}({own})"] + path)
        memo[fn] = result
        return result

    total, chain = visit(root, [])
    return total, chain, sorted(indirect), recursion


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__.strip().splitlines()[-1])
    frames, calls, notes = parse(disassemble(sys.argv[1]))
    roots = sys.argv[2:] or list(DEFAULT_ROOTS)
    reservation, reservation_note = reservation_bytes(sys.argv[1])

    print(f"image:        {sys.argv[1]}")
    print(f"functions:    {len(frames)}")
    print(f"reservation:  {reservation} B ({STACK_SYMBOL})")
    if reservation_note:
        print(f"NOTE {reservation_note}")
    for note in dict.fromkeys(notes):
        print(f"NOTE {note}")

    walked = 0
    for root in roots:
        if root not in frames:
            near = sorted(f for f in frames if root in f)[:4]
            print(f"\n== {root}\n   NOT FOUND" + (f" (near: {near})" if near else ""))
            continue
        walked += 1
        total, chain, indirect, recursion = walk(root, frames, calls)
        with_frame = total + EXCEPTION_FRAME
        print(f"\n== {root}")
        print(f"   {total} B, {with_frame} B with the exception frame")
        if root == DEFAULT_ROOTS[0]:
            free = reservation - with_frame
            print(f"   headroom: {free} B free of {reservation} "
                  f"({100.0 * free / reservation:.1f}%)")
        print("   " + "\n     -> ".join(chain))
        if indirect:
            print(f"   indirect call sites, not followed: {', '.join(indirect)}")
        for cycle in dict.fromkeys(recursion):
            print(f"   RECURSION (reported, not followed): {cycle}")

    if walked == 0:
        print(f"\nnot one of the {len(roots)} requested root(s) is in this "
              f"image's {len(frames)} functions, so nothing was measured. "
              f"That is a failed reading, not a shallow one.")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
