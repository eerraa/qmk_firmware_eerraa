#!/usr/bin/env python3
# Copyright 2026 Hyojin Bak (@eerraa)
# SPDX-License-Identifier: GPL-2.0-or-later
"""Everything the ERA agent document layer points at, resolved against the tree.

One purpose: a claim in these documents must be **locatable**. A pointer must
lead somewhere, and a paragraph saying what a function does must say where that
function lives, because a claim nobody can find is a claim nobody re-checks.
This repository keeps no history to recover a moved target from
(`era_active_index.md`, **What This Repository Does Not Carry**), so a pointer
that stopped resolving cannot be repaired by reading around it -- the fact it
led to is simply gone. Nothing else in the tree checks either half.

Read-only. Exit 1 on any finding. Tool-neutral: it assumes no editor and no
agent, and is meant to be runnable by hand.

Eight checks, run by default:

  * **paths** -- a backticked or linked repository path resolves, in any
    spelling the documents legitimately use: repo-root (`quantum/keyboard.c`),
    common-layer-relative (`split/era_host_peer_storage.c`, which AGENTS.md
    mandates for ERA files), doc-root-relative (`maps/era_walkthrough.md`), or
    a directory;
  * **citations** -- a `path:line` or `path:line-line` address resolves and the
    line is inside the file. A line address is right until the next insertion
    above it and silently wrong after;
  * **sections** -- a document routed to by section name has that section.
    The set routes by section constantly and a renamed heading leaves a
    pointer that reads as live;
  * **headers** -- every document under `docs/` declares `Status:`, `Genre:`,
    `Canonical for:` and `Read when:`, and the genre is one AGENTS.md names.
    The header is how a document states its own scope and sentence kind;
  * **index** -- every document `era_active_index.md` names exists, and every
    agent document is reachable from it. AGENTS.md requires the two to move
    together;
  * **claims** -- a paragraph that says what a resolvable function *does* names
    a file it could live in. AGENTS.md's **A claim about what a function does
    names the file it lives in** is the rule; measured 2026-08-15 the set held
    41 such paragraphs naming no file against 38 that named one, and the two
    worst defects a cross-document sweep found were both of this shape. The
    unit is the paragraph, not the sentence: a path in every sentence would
    cost more context than the check saves;
  * **source comments** -- the same locatability rule over ERA comments, in
    `keyboards/era` and in the QMK core files that carry ERA prose. Comments
    only; an identifier in code is resolved by the compiler and a string
    literal is data;
  * **constant values** -- a document that states a constant's value states the
    tree's value. The one numeric claim a machine can decide, and narrow: see
    `check_constant_values`.

And one on request:

  * `--homeless [A..B]` -- the deletion safety net. Over the staged doc diff, or
    a given range, it reports every identifier the removed lines carried that
    now has no home anywhere in `keyboards/era` or the entry layer. **A homeless
    token is a promotion candidate, not a deletion candidate**: move it to an
    owning document before dropping it. Deletion is the one unrecoverable move
    available here, and no history exists to undo one from. Commit SHAs,
    per-build addresses and one-run measurements are expected non-findings and
    need no home; the report is evidence for that judgment, not the judgment.

What this deliberately does not try to catch is the claim itself. A resolvable
reference and a true statement are different questions, and only the first is
mechanical: prose asserting what a function no longer does shares no token with
the source that stopped doing it. Naming the file is what makes finding out
cheap, and finding out stays a reader's job.

The inclusion rule for a path claim is narrow rather than the exclusion list
being long. This prose is full of slash-joined field names -- `PUSH_CTL/open`,
`rrx/hrx/srx`, `apply/complete` -- that are not paths and never were. A token
counts only when it carries a known source or document suffix, ends in `/`, or
begins with a directory this repository has.
"""

import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[4]
DOC_ROOT = "keyboards/era/common/docs"
INDEX = f"{DOC_ROOT}/era_active_index.md"
ENTRY = ("AGENTS.md", "CLAUDE.md")

# The spellings the documents use, tried in this order.
BASES = ("", "keyboards/era/common/", "keyboards/era/", "keyboards/",
         DOC_ROOT + "/")
GENRES = {"contract", "map", "manual", "state", "entry"}
HEADERS = ("Status:", "Genre:", "Canonical for:", "Read when:")

# Suffixes that make a token a path claim on their own. Build outputs name no
# tracked file by design and are not claims about the tree.
SRC_SUFFIX = (".c", ".h", ".md", ".py", ".sh", ".mk", ".ld", ".json",
              ".txt", ".S", ".lds", ".yml", ".yaml")
BUILD_SUFFIX = (".o", ".elf", ".uf2", ".hex", ".bin", ".map", ".d", ".lst")
# QMK generates these into `.build/`; a document names them because the reader
# will meet them, and no tree ever contains one.
GENERATED = {"info_config.h", "info_rules.mk", "info_deps.mk"}
SKIP_PREFIX = ("http://", "https://", "~/", "/mnt/", "/usr/", "/etc/",
               "C:", "D:", "$", "%")

TOKEN_OK = re.compile(r"^[A-Za-z0-9._/-]+$")
BACKTICK = re.compile(r"`([^`\n]+)`")
MDLINK = re.compile(r"\[[^\]]*\]\(([^)\s]+)\)")
CITE = re.compile(
    r"([A-Za-z0-9._/-]+\.(?:c|h|md|py|sh|mk|ld|json|txt|S)):(\d+)(?:-(\d+))?")
# `era_x.md`'s **Section** / `era_x.md`, **Section** / `era_x.md` **Section**
SECTION = re.compile(r"`([A-Za-z0-9._/-]+\.md)`(?:'s|,)?\s+\*\*([^*\n]+)\*\*")
# The parenthetical form: ``(`era_active_index.md`, Retired names)``.
SECTION_PAREN = re.compile(
    r"\(`([A-Za-z0-9._/-]+\.md)`,\s*([A-Z][A-Za-z0-9 '’-]{2,50}?)\s*\)")
HEADING = re.compile(r"^#{1,6}\s+(.+?)\s*$")
# Bold that says how to read rather than what to read: `**first**` after a
# document name is an order, not a section.
ROUTING_WORDS = {"first", "second", "whole", "both", "alone", "only",
                 "last", "always", "never", "instead", "then"}


def git(*args, ok=(0,)):
    """Run git, and refuse to read a failure as an empty answer.

    The return code used to be discarded, and the realistic path that exposes
    it is the one a stranger takes: a source ZIP with no `.git`, where
    `git ls-files` exits 128 with empty stdout. `TRACKED` then came out empty,
    every check scanned zero files, and this program printed its success
    sentence and exited 0. A walk that finds nothing has not measured an empty
    tree, it has failed to measure -- the same rule `era_residency_gate.sh`
    states at its zero-section guard.

    `git grep` exits 1 for "no match", which is an answer and not a failure,
    so those callers pass `ok=(0, 1)`.
    """
    proc = subprocess.run(["git", *args], cwd=str(REPO), capture_output=True,
                          encoding="utf-8", errors="replace")
    if proc.returncode not in ok:
        sys.exit(
            f"git {' '.join(args)} failed (rc={proc.returncode})"
            + (": " + (proc.stderr or "").strip() if (proc.stderr or "").strip() else "")
            + "\nThis check reads the tracked file set from git and cannot run "
              "without it. Run it inside a clone, not an unpacked archive."
        )
    return proc.stdout


# The check runs before a commit as well as on one. Start from Git's cached and
# untracked, non-ignored names, then keep only paths that exist in this working
# tree: a deleted tracked source must not crash the comment pass or remain a
# false-positive resolver target, and a newly added source must not escape the
# same check merely because it has not been staged yet. Submodule directory
# entries remain because `exists()`, unlike `is_file()`, accepts them.
TRACKED = {
    line.strip()
    for line in git("ls-files", "--cached", "--others", "--exclude-standard").splitlines()
    if line.strip() and (REPO / line.strip()).exists()
}
if not TRACKED:
    sys.exit("git ls-files reported no live working-tree files, so every check below "
             "would scan nothing and pass. That is a failure to measure, not "
             "a clean tree.")
DIRS = set()
for _p in TRACKED:
    _parts = _p.split("/")
    for _i in range(1, len(_parts)):
        DIRS.add("/".join(_parts[:_i]))
TOPDIRS = tuple(sorted(d for d in DIRS if "/" not in d))

_submodule_tails = None


def submodule_tails():
    """Filenames under `lib/`, which `git ls-files` cannot see into.

    The documents cite ChibiOS startup and HAL files by name and a submodule's
    contents are not in the index, so the filesystem is the only witness. Built
    on first miss: most runs never need it.
    """
    global _submodule_tails
    if _submodule_tails is None:
        _submodule_tails = {}
        base = REPO / "lib"
        if base.is_dir():
            for f in base.rglob("*"):
                if f.is_file():
                    _submodule_tails.setdefault(f.name, []).append(
                        str(f.relative_to(REPO)).replace("\\", "/"))
    return _submodule_tails


def norm(path):
    while "/../" in path:
        path = re.sub(r"[^/]+/\.\./", "", path, count=1)
    return path.replace("./", "")


def is_path_claim(token):
    if token.endswith(SRC_SUFFIX) or token.endswith("/"):
        return True
    return "/" in token and token.split("/", 1)[0] in TOPDIRS


def resolve(token, origin):
    """Repo-relative target, 'skip' when not a path claim, or None."""
    token = token.strip().rstrip(".,;:)")
    if not token or token.startswith(SKIP_PREFIX) or not TOKEN_OK.match(token):
        return "skip"
    if token.endswith(BUILD_SUFFIX) or set(token) <= set("./"):
        return "skip"
    if re.fullmatch(r"\.[A-Za-z]+", token):  # prose: "the board's `.c`"
        return "skip"
    if not is_path_claim(token):
        return "skip"
    bare = token.rstrip("/")
    if bare.rsplit("/", 1)[-1] in GENERATED:
        return "skip"
    candidates = [norm(base + bare) for base in BASES]
    candidates.append(norm(origin.rsplit("/", 1)[0] + "/" + bare))
    for cand in candidates:
        if cand in TRACKED or cand in DIRS or (REPO / cand).exists():
            return cand
    if bare.endswith(SRC_SUFFIX):
        # A filename the documents cite without its directory: a reader finds
        # it by name, so a tail match is what resolving it means.
        hits = [p for p in TRACKED if p.endswith("/" + bare)]
        if len(hits) == 1 or ("/" not in bare and hits):
            return hits[0]
        tail = bare.rsplit("/", 1)[-1]
        found = submodule_tails().get(tail, [])
        matching = [
            path
            for path in found
            if ("/" not in bare) or path.endswith("/" + bare)
        ]
        if matching:
            return matching[0]
    return None


_headings = {}


def headings(rel):
    """A document's headings, plus the bolded lead-ins prose points at by name."""
    if rel not in _headings:
        found = set()
        try:
            text = (REPO / rel).read_text(encoding="utf-8", errors="replace")
        except OSError:
            text = ""
        for line in text.splitlines():
            match = HEADING.match(line)
            if match:
                found.add(match.group(1).strip().strip("`*").lower())
        for match in re.finditer(r"\*\*([^*\n]+)\*\*", text):
            found.add(match.group(1).strip().rstrip(".:,").strip("`").lower())
        _headings[rel] = found
    return _headings[rel]


def linecount(rel):
    try:
        return len((REPO / rel).read_bytes().split(b"\n"))
    except OSError:
        return 0


CLAIM_FN = re.compile(r"`([a-z_][a-z0-9_]{4,})\(\)`")
CLAIM_VERB = re.compile(
    r"\b(returns?|calls?|sets?|clears?|holds?|reads?|writes?|advances?|gates?|"
    r"excludes?|raises?|counts?|runs?|fires?|owns?)\b")
CLAIM_FILEREF = re.compile(
    r"`[A-Za-z0-9_./-]*\.(?:c|h|mk|py|ld|json)`|"
    r"[A-Za-z0-9_./-]+\.(?:c|h|mk|py|ld):\d")
CLAIM_DEF = r"^[A-Za-z_].*\b[a-z_][a-z0-9_]{4,}\("
CLAIM_ROOTS = ("keyboards/era", "quantum", "platforms", "tmk_core", "drivers")


def function_owners():
    """symbol -> {defining .c file}, over the trees ERA documents talk about."""
    owners = {}
    for line in git("grep", "-n", "-E", CLAIM_DEF, "--", *CLAIM_ROOTS,
                    ok=(0, 1)).splitlines():
        parts = line.split(":", 2)
        if len(parts) < 3 or not parts[0].endswith(".c"):
            continue
        for match in re.finditer(r"\b([a-z_][a-z0-9_]{4,})\s*\(", parts[2]):
            owners.setdefault(match.group(1), set()).add(parts[0])
    return owners


def resolves_to_a_function(name, owners):
    """Documents shorten `era_host_peer_storage_arm_summary_refresh()` to
    `arm_summary_refresh()`, so a suffix match is part of resolving one."""
    if name in owners:
        return True
    return any(sym.endswith("_" + name) for sym in owners)


def check_claims(fails):
    owners = function_owners()
    paragraphs = 0
    for rel in sorted(p for p in TRACKED
                      if p.startswith(DOC_ROOT) and p.endswith(".md")):
        lineno = 1
        text = (REPO / rel).read_text(encoding="utf-8", errors="replace")
        for para in re.split(r"\n\s*\n", text):
            start, lineno = lineno, lineno + para.count("\n") + 2
            flat = " ".join(para.split())
            names = list(dict.fromkeys(CLAIM_FN.findall(flat)))
            if not names or not CLAIM_VERB.search(flat):
                continue
            paragraphs += 1
            if CLAIM_FILEREF.search(flat):
                continue
            known = [n for n in names if resolves_to_a_function(n, owners)]
            if known:
                fails.append(
                    f"{rel}:{start}: says what "
                    + ", ".join(f"`{n}()`" for n in known[:3])
                    + " does and names no file it lives in")
    return paragraphs


# The identifier alphabet the safety net tracks out of a deletion.
TOKEN = re.compile(
    r"ERA_[A-Z0-9_]+"          # ERA macros
    r"|[A-Z][A-Z0-9_]{3,}"     # other macros / enum names
    r"|0x[0-9A-Fa-f]{2,}"      # hex constants / addresses
    r"|era_[a-z0-9_]+"         # symbols, files, doc stems
    r"|\b\d{3,}\b"             # sizes, cadences, counts
)


def homeless(range_spec):
    """Identifiers a doc deletion drops that have no remaining home."""
    targets = [DOC_ROOT, *ENTRY]
    diff = git("diff", "-U0", *( [range_spec] if range_spec else ["--cached"] ),
               "--", *targets)
    removed, added = [], []
    for line in diff.splitlines():
        if line.startswith("-") and not line.startswith("---"):
            removed.append(line[1:])
        elif line.startswith("+") and not line.startswith("+++"):
            added.append(line[1:])
    if not removed:
        print("no removed doc lines in the inspected diff")
        return 0
    tokens = set()
    for line in removed:
        tokens.update(TOKEN.findall(line))
    # A token the same diff re-adds elsewhere was moved, not dropped.
    tokens -= set(TOKEN.findall("\n".join(added)))
    findings = [t for t in sorted(tokens)
                if not git("grep", "-lIF", t, "--",
                           "keyboards/era", *ENTRY, ok=(0, 1)).strip()]
    if not findings:
        print("no homeless tokens: everything removed still has a home in "
              "keyboards/era or the entry layer")
        return 0
    print("tokens removed from docs with no remaining home "
          "(promotion candidates, or deliberately retired names):")
    for token in findings:
        print(f"  {token}")
    return 1


def scan_set():
    docs = sorted(p for p in TRACKED if p.startswith(DOC_ROOT))
    rules = sorted(p for p in TRACKED if p.startswith(".claude/rules/"))
    return docs + [e for e in ENTRY if e in TRACKED] + rules


def check_pointers(files, fails):
    counted = 0
    for rel in files:
        path = REPO / rel
        if not path.exists():
            fails.append(f"{rel}: named by the scan set, absent from the tree")
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for lineno, line in enumerate(text.splitlines(), 1):
            cited = {m.group(0) for m in CITE.finditer(line)}
            for match in CITE.finditer(line):
                counted += 1
                target = resolve(match.group(1), rel)
                if target in (None, "skip"):
                    fails.append(f"{rel}:{lineno}: citation `{match.group(0)}` "
                                 f"names a file that does not resolve")
                    continue
                total = linecount(target)
                if int(match.group(3) or match.group(2)) > total:
                    fails.append(f"{rel}:{lineno}: citation `{match.group(0)}` "
                                 f"-> {target} has only {total} lines")
            for match in list(BACKTICK.finditer(line)) + list(MDLINK.finditer(line)):
                token = match.group(1)
                if any(c in token for c in cited):
                    continue
                target = resolve(token, rel)
                if target == "skip":
                    continue
                counted += 1
                if target is None:
                    fails.append(f"{rel}:{lineno}: `{token}` resolves to "
                                 f"nothing in the tree")
            for match in list(SECTION.finditer(line)) + list(SECTION_PAREN.finditer(line)):
                doc, section = match.group(1), match.group(2).strip().rstrip(".:,")
                if section.lower() in ROUTING_WORDS:
                    continue
                target = resolve(doc, rel)
                if target in (None, "skip"):
                    continue
                counted += 1
                if section.lower() not in headings(target):
                    fails.append(f"{rel}:{lineno}: routes to `{doc}` "
                                 f"**{section}** and that document has no "
                                 f"such section")
    return counted


def resolve_beside(token, origin):
    """`resolve()`, plus the one spelling only a source comment uses.

    A comment names a sibling by its bare filename -- `routes.c` from inside
    `scheduler/` -- which no document ever does, so the shared BASES list does
    not carry it and should not: a bare filename in a document is ambiguous
    across the tree, and beside its own file it is not.
    """
    if not token.endswith(SRC_SUFFIX) and token.split("/", 1)[0] not in TOPDIRS:
        # A source comment is prose with slashes in it -- `boot2/crt0/`,
        # `apply/complete`. The documents' trailing-slash directory form is not
        # one a comment uses, so requiring a suffix or a repo top directory is
        # the whole filter.
        return "skip"
    if any(part.endswith(SRC_SUFFIX) for part in token.split("/")[:-1]):
        # `common_features.mk/generic_features.mk` is two names joined by a
        # slash, not one path: a real path suffixes only its last component.
        return "skip"
    if token.rsplit("/", 1)[-1].startswith("_"):
        # `_common.c` is the family pattern, not a file. No tracked name in
        # this tree begins with an underscore, which is what the prose is
        # borrowing it for.
        return "skip"
    target = resolve(token, origin)
    if target is not None:
        return target
    beside = norm(f"{origin.rsplit('/', 1)[0]}/{token}")
    return beside if beside in TRACKED else None


# `//` opens a comment unless a `:` sits in front of it, which is a URL scheme.
C_COMMENT = re.compile(r"/\*.*?\*/|(?<!:)//[^\n]*", re.S)
MK_COMMENT = re.compile(r"^[ \t]*#[^\n]*", re.M)
# `..._internal.h`, `scheduler/..._routes.c`: an elision is a name written for a
# reader who already has the directory, not an address.
ELIDED = ".."


def source_comment_set():
    return sorted(p for p in TRACKED
                  if p.startswith("keyboards/era/")
                  and p.endswith((".c", ".h", ".mk"))
                  and "/docs/" not in "/" + p)


# ERA also writes comments in QMK core files, and a `keyboards/era/` prefix
# filter cannot see them. That gap is not hypothetical: a comment in
# `quantum/rgb_matrix/rgb_matrix.c` cited a `docs/active/` document tree this
# repository had deleted, and it reached the shipped orphan because this scan
# stopped at the ERA directory.
#
# Only ERA's own comment blocks are checked in these files, not upstream's: a
# block qualifies when it mentions `era` at all. Upstream comment prose is not
# this repository's to hold locatable, and scanning it would report findings
# nobody here can fix.
CORE_COMMENT_ROOTS = ("quantum/", "platforms/", "tmk_core/", "drivers/",
                      "builddefs/")
ERA_MENTION = re.compile(r"\bera\b|\bERA_|keyboards/era", re.I)

# `foo.[ch]` is one address written for two files. Expanded rather than skipped,
# so the form stays checked: without this the token scan sees `foo.` and reports
# a file that does not exist.
BRACKET_SUFFIX = re.compile(r"(?<=\.)\[([a-z]{2,4})\]")


def expand_bracket_suffixes(body):
    return BRACKET_SUFFIX.sub(lambda m: m.group(1)[0], body)


def core_comment_set():
    return sorted(p for p in TRACKED
                  if p.startswith(CORE_COMMENT_ROOTS)
                  and p.endswith((".c", ".h", ".mk")))


def check_source_comments(fails):
    """The same locatability rule, over ERA source comments.

    The rule was written for the document layer and is not about documents: a
    reference nobody can resolve is one nobody re-checks, and this repository
    has no history to recover a moved target from. Measured 2026-08-17, before
    this check existed, **six of the seven `path:line` citations in ERA source
    comments pointed somewhere else** and two of them named a file the tree does
    not contain -- against zero findings in the document layer the same day,
    which is what a checked surface looks like beside an unchecked one.

    Comments only. Code is not scanned: an identifier in code is resolved by the
    compiler and a string literal is data.
    """
    counted = 0
    era_only = set(core_comment_set())
    for rel in source_comment_set() + core_comment_set():
        text = (REPO / rel).read_text(encoding="utf-8", errors="replace")
        pattern = MK_COMMENT if rel.endswith(".mk") else C_COMMENT
        for block in pattern.finditer(text):
            lineno = text.count("\n", 0, block.start()) + 1
            body = expand_bracket_suffixes(block.group())
            is_core = rel in era_only
            if is_core and not ERA_MENTION.search(body):
                continue
            cited = {m.group(0) for m in CITE.finditer(body)}
            for match in CITE.finditer(body):
                if ELIDED in match.group(1):
                    continue
                counted += 1
                target = resolve_beside(match.group(1), rel)
                if target in (None, "skip"):
                    fails.append(f"{rel}:{lineno}: citation `{match.group(0)}` "
                                 f"names a file that does not resolve")
                    continue
                total = linecount(target)
                if int(match.group(3) or match.group(2)) > total:
                    fails.append(f"{rel}:{lineno}: citation `{match.group(0)}` "
                                 f"-> {target} has only {total} lines")
            for token in set(re.findall(r"[A-Za-z0-9._/-]+", body)):
                if ELIDED in token or any(token in c for c in cited):
                    continue
                # In a core file only an explicit path is an address. A bare
                # basename there is prose -- `tmpmr.h` is a struct member in a
                # sentence about `tmpmr.v || tmpmr.h`, not a header.
                if is_core and "/" not in token:
                    continue
                target = resolve_beside(token, rel)
                if target == "skip":
                    continue
                counted += 1
                if target is None:
                    fails.append(f"{rel}:{lineno}: `{token}` resolves to "
                                 f"nothing in the tree")
    return counted


# A constant's name, as a document writes it.
CONST_NAME = re.compile(r"\b((?:ERA_|RGB_MATRIX_|MATRIX_|RP2040_)[A-Z0-9_]{4,})\b")
CONST_DEF = re.compile(r"^#\s*define\s+([A-Z][A-Z0-9_]*)\s+(\S[^\n]*)$", re.M)
MK_ASSIGN = re.compile(r"^([A-Z][A-Z0-9_]*)\s*\??=\s*(\S[^\n]*)$", re.M)
# A bare integer in prose, not a version, a line number or part of a word.
PROSE_INT = re.compile(r"(?<![\w.])(\d{1,7})(?![\w.])")


def constant_table():
    """Every ERA constant, with the file that defines it.

    Sorted rather than set-ordered, and a name defined more than once with
    different bodies is dropped instead of resolved to whichever definition
    came first. Both are the same requirement: a gate that answers differently
    on the same tree is not a gate.
    """
    seen = {}
    for rel in sorted(TRACKED):
        if not rel.startswith("keyboards/era/") or "/keymaps/" in rel:
            continue
        if not rel.endswith((".c", ".h", ".mk")):
            continue
        text = (REPO / rel).read_text(encoding="utf-8", errors="replace")
        for match in CONST_DEF.finditer(text):
            body = match.group(2).split("/*")[0].split("//")[0].strip()
            seen.setdefault(match.group(1), []).append((body, rel))
        for match in MK_ASSIGN.finditer(text):
            seen.setdefault(match.group(1), []).append((match.group(2).strip(), rel))
    table = {}
    for name, entries in seen.items():
        bodies = {body for body, _ in entries}
        if len(bodies) == 1:
            table[name] = entries[0]
    return table


def constant_value(name, table, depth=0):
    """The integer a constant reduces to, or None when it is not one.

    Deliberately shallow: an alias chain and a parenthesised expression over
    resolvable names, and nothing else. A constant this cannot reduce is not
    checked rather than guessed at.
    """
    if depth > 6 or name not in table:
        return None
    body = table[name][0].strip().rstrip("Uu")
    if re.fullmatch(r"-?\d+", body):
        return int(body)
    if re.fullmatch(r"0[xX][0-9a-fA-F]+", body):
        return int(body, 16)
    if re.fullmatch(r"\(?\s*[A-Za-z_]\w*\s*\)?", body):
        return constant_value(body.strip("() "), table, depth + 1)
    if body.startswith("(") and body.endswith(")"):
        inner = body[1:-1]
        names = set(re.findall(r"[A-Za-z_]\w*", inner))
        if names and all(constant_value(n, table, depth + 1) is not None for n in names):
            for n in sorted(names, key=len, reverse=True):
                inner = re.sub(r"\b" + re.escape(n) + r"\b",
                               str(constant_value(n, table, depth + 1)), inner)
            inner = re.sub(r"[Uu]\b", "", inner)
            if re.fullmatch(r"[-+*/%()\s0-9<>]+", inner):
                try:
                    return int(eval(inner))  # noqa: S307 -- literals only, checked above
                except Exception:
                    return None
    return None


def check_constant_values(fails):
    """A document that states a constant's value must state the tree's value.

    Not the locatability rule and not a style rule. A document is allowed to
    write a constant's value -- what it is not allowed to do is write a value
    the tree no longer holds, and that is decidable where the placement
    question is not.

    **The scope is narrow and saying so is part of the check.** It reaches only
    a constant that reduces to an integer: 21 stated values on 2026-08-18,
    against 91 named constants it could not reduce. So it is one guard on one
    shape, not coverage of the document layer's numbers -- the counts that
    caused this repository's worst numeric defects, "1178 files wide" against
    1200 and "the nine ERA RGB Matrix boards" against ten, are derived from the
    tree rather than defined in it and no `#define` check can see them.

    **It only reaches a number written after the name, on the same line, and
    widening that was tried and reverted.** A contract writes "exactly 136
    bytes\\n  (`ERA_HOST_PEER_STORAGE_CORE0_STATE_BYTES`)" with the value first
    and a line break between, and three stale values in
    `era_host_peer_storage_contract.md`'s capacity section are that shape. Going
    to whole-line matching did not reach them either -- the number is on the
    previous line -- and it produced a false positive on the first paragraph
    that happened to carry unrelated digits. Paragraph matching would accept
    anything. So the coupling between a value and its name lives in prose, this
    check reaches the half that does not, and the other half is found by
    reading. That is a limit of the check and not a gap to be closed by
    loosening it.

    It accepts any number in the window, and accepts a thousandfold difference
    because a document writes 10000 ms as "10 s". So a line that keeps the
    right number and adds a wrong one passes: this catches drift, where the
    value moved and the document did not, which is the way these have actually
    gone wrong.
    """
    table = constant_table()
    counted = 0
    for rel in scan_set():
        if not rel.endswith(".md"):
            continue
        for lineno, line in enumerate(
                (REPO / rel).read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            for match in CONST_NAME.finditer(line):
                value = constant_value(match.group(1), table)
                if value is None:
                    continue
                stated = [int(n) for n in PROSE_INT.findall(line[match.end():match.end() + 90])]
                if not stated:
                    continue
                counted += 1
                if any(n == value or n * 1000 == value or (n and value * 1000 == n)
                       for n in stated):
                    continue
                fails.append(f"{rel}:{lineno}: `{match.group(1)}` is {value} "
                             f"({table[match.group(1)][1]}), and the numbers "
                             f"written beside it are {stated}")
    return counted


def check_headers(fails):
    docs = [p for p in TRACKED
            if p.startswith(DOC_ROOT) and p.endswith(".md")]
    for rel in docs:
        head = "\n".join((REPO / rel).read_text(
            encoding="utf-8", errors="replace").splitlines()[:14])
        for header in HEADERS:
            if header not in head:
                fails.append(f"{rel}: no `{header}` header in its first lines")
        match = re.search(r"^Genre:\s*(\S+)", head, re.M)
        if match and match.group(1).strip().lower() not in GENRES:
            fails.append(f"{rel}: `Genre: {match.group(1)}` is not one of "
                         + ", ".join(sorted(GENRES)))
    return len(docs)


def check_index(fails):
    index = (REPO / INDEX).read_text(encoding="utf-8", errors="replace")
    named = set()
    for match in BACKTICK.finditer(index):
        target = resolve(match.group(1), INDEX)
        if target and target != "skip" and target.startswith(DOC_ROOT):
            named.add(target)
    for rel in TRACKED:
        if not rel.startswith(DOC_ROOT) or rel == INDEX:
            continue
        if rel.startswith(f"{DOC_ROOT}/user/") or not rel.endswith(".md"):
            continue
        if rel not in named:
            fails.append(f"{rel}: an agent document `era_active_index.md` "
                         f"does not name. AGENTS.md requires the index and "
                         f"the document set to move together")
    return len(named)


def main():
    argv = sys.argv[1:]
    if argv and argv[0] == "--homeless":
        return homeless(argv[1] if len(argv) > 1 else None)
    if argv:
        print(f"usage: {Path(__file__).name} [--homeless [A..B]]")
        return 2

    files = scan_set()
    fails = []
    pointers = check_pointers(files, fails)
    documents = check_headers(fails)
    named = check_index(fails)
    claims = check_claims(fails)
    comments = check_source_comments(fails)
    values = check_constant_values(fails)
    print(f"doc refs: {len(files)} agent-layer files, {pointers} pointers, "
          f"{claims} function claims, {documents} documents with headers, "
          f"{named} named by the index, {comments} source-comment references, "
          f"{values} stated constant values")
    for entry in fails:
        print("FAIL: " + entry)
    if fails:
        print(f"\n{len(fails)} finding(s). A claim nobody can locate is a claim "
              f"nobody re-checks, and this repository keeps no history to "
              f"recover a moved target from.")
        return 1
    print("every claim in the agent document layer is locatable")
    return 0


if __name__ == "__main__":
    sys.exit(main())
