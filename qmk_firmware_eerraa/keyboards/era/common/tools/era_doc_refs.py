#!/usr/bin/env python3
# Copyright 2026 Hyojin Bak (@eerraa)
# SPDX-License-Identifier: GPL-2.0-or-later
"""Validate ERA agent-document pointers and reachability.

The default pass checks repository/document paths, line citations, routed
sections, document role headers, active-index reachability, and ERA-owned
source-comment pointers. `--index` reads the staged Git index so the pre-commit
hook validates exactly what the commit contains instead of requiring unrelated
working-tree changes to be staged.

`--homeless [A..B]` is an advisory deletion review. It reports identifiers that
disappear from the inspected documentation diff and have no remaining mention.
Those names are prompts to decide whether a unique product requirement lost its
owner, a source-owned identifier was merely de-duplicated, or a retired name
was intentionally removed. Findings are not preservation requirements.

Read failures and broken references fail closed. The tool does not duplicate or
re-derive implementation values from source.
"""

import re
import subprocess
import sys
from pathlib import Path

# --- repository-specific, and the only part another repository has to change ---
REPO = Path(__file__).resolve().parents[4]
DOC_ROOT = "keyboards/era/common/docs"
INDEX = f"{DOC_ROOT}/era_active_index.md"
ENTRY = ("AGENTS.md", "CLAUDE.md")
RULES_PREFIX = ".claude/rules/"
# The spellings the documents use, tried in this order.
BASES = ("", "keyboards/era/common/", "keyboards/era/", "keyboards/",
         DOC_ROOT + "/")
GENRES = {"contract", "map", "manual", "state", "entry"}
HEADERS = ("Genre:", "Canonical for:")
FORBIDDEN_HEADERS = ("Status:", "Read when:")
FOREIGN_REPOS = ("the-via-eerraa/", "eerraa-qmk-h7s-fw/", "eerraa-54lm20-fw/",
                 "eerraa-agent-docs/")
SRC_SUFFIX = (".c", ".h", ".md", ".py", ".sh", ".mk", ".ld", ".json",
              ".txt", ".S", ".lds", ".yml", ".yaml")
SKIP_PREFIX = ("http://", "https://", "~/", "/mnt/", "/usr/", "/etc/",
               "C:", "D:", "$", "%")
CORE_COMMENT_ROOTS = ("quantum/", "platforms/", "tmk_core/", "drivers/",
                      "builddefs/")
# The identifier alphabet the safety net tracks out of a deletion.
TOKEN_ALPHABET = (
    r"ERA_[A-Z0-9_]+"          # ERA macros
    r"|[A-Z][A-Z0-9_]{3,}"     # other macros / enum names
    r"|0x[0-9A-Fa-f]{2,}"      # hex constants / addresses
    r"|era_[a-z0-9_]+"         # symbols, files, doc stems
    r"|\b\d{3,}\b"             # sizes, cadences, counts
)
TOKEN_CHARS = r"^[A-Za-z0-9._/-]+$"
# --- end repository-specific ---

# Suffixes that make a token a path claim on their own. Build outputs name no
# tracked file by design and are not claims about the tree.
BUILD_SUFFIX = (".o", ".elf", ".uf2", ".hex", ".bin", ".map", ".d", ".lst")
# QMK generates these into `.build/`; a document names them because the reader
# will meet them, and no tree ever contains one.
GENERATED = {"info_config.h", "info_rules.mk", "info_deps.mk"}
GENERATED_DIRS = {".era-artifacts"}

TOKEN_OK = re.compile(TOKEN_CHARS)
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


INDEX_MODE = sys.argv[1:] == ["--index"]


def git(*args, ok=(0,)):
    """Run git and refuse to treat an execution failure as an empty answer."""
    if args[0] == "show" and args[1].startswith(":"):
        args = ("show", ":./" + args[1][1:], *args[2:])
    elif args[0] == "diff":
        args = ("diff", "--relative", *args[1:])
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


_text_cache = {}


def read_text(rel):
    if rel in _text_cache:
        return _text_cache[rel]
    if INDEX_MODE:
        for link, object_id in GITLINK_OBJECTS.items():
            if rel.startswith(link + "/") and link in INDEX_OVERRIDES:
                subpath = rel[len(link) + 1:]
                proc = subprocess.run(
                    ["git", "-C", str(REPO / link), "show", f"{object_id}:{subpath}"],
                    capture_output=True, encoding="utf-8", errors="replace",
                )
                if proc.returncode != 0:
                    sys.exit(
                        f"cannot read staged submodule path {rel} at {object_id}: "
                        + (proc.stderr or "git show failed").strip()
                    )
                _text_cache[rel] = proc.stdout
                return proc.stdout
        if rel in INDEX_OVERRIDES:
            _text_cache[rel] = git("show", f":{rel}")
            return _text_cache[rel]
    try:
        _text_cache[rel] = (REPO / rel).read_text(
            encoding="utf-8", errors="replace"
        )
        return _text_cache[rel]
    except OSError as exc:
        sys.exit(f"cannot read {rel}: {exc}")


if INDEX_MODE:
    TRACKED = {
        line.strip() for line in git("ls-files", "--cached").splitlines()
        if line.strip()
    }
else:
    # Manual validation includes non-ignored additions. The pre-commit path
    # uses the staged index above and never depends on unrelated worktree files.
    TRACKED = {
        line.strip()
        for line in git("ls-files", "--cached", "--others", "--exclude-standard").splitlines()
        if line.strip() and (REPO / line.strip()).exists()
    }
if not TRACKED:
    sys.exit("git ls-files reported no live working-tree files, so every check below "
             "would scan nothing and pass. That is a failure to measure, not "
              "a clean tree.")
GITLINK_OBJECTS = {
    parts[3]: parts[1]
    for line in git("ls-files", "--stage").splitlines()
    if len(parts := line.split(None, 3)) == 4 and parts[0] == "160000"
}
GITLINKS = set(GITLINK_OBJECTS)
INDEX_OVERRIDES = (
    {
        line.strip()
        for line in git("diff", "--name-only", "--ignore-submodules=none").splitlines()
        if line.strip()
    }
    if INDEX_MODE else set()
)
DIRS = set()
for _p in TRACKED:
    _parts = _p.split("/")
    for _i in range(1, len(_parts)):
        DIRS.add("/".join(_parts[:_i]))
TOPDIRS = tuple(sorted(d for d in DIRS if "/" not in d))

_submodule_paths = None
_submodule_tails = None


def staged_submodule_paths():
    """Paths in gitlink commits whose working trees differ from the index."""
    global _submodule_paths
    if _submodule_paths is None:
        _submodule_paths = set()
        for link, object_id in GITLINK_OBJECTS.items():
            if link not in INDEX_OVERRIDES:
                continue
            proc = subprocess.run(
                ["git", "-C", str(REPO / link), "ls-tree", "-r", "--name-only", object_id],
                capture_output=True, encoding="utf-8", errors="replace",
            )
            if proc.returncode != 0:
                sys.exit(
                    f"cannot enumerate staged submodule {link} at {object_id}: "
                    + (proc.stderr or "git ls-tree failed").strip()
                )
            _submodule_paths.update(
                f"{link}/{subpath}"
                for subpath in proc.stdout.splitlines() if subpath
            )
    return _submodule_paths


def submodule_tails():
    """Filenames inside gitlinks, using staged commits only where needed."""
    global _submodule_tails
    if _submodule_tails is None:
        _submodule_tails = {}
        paths = set(staged_submodule_paths()) if INDEX_MODE else set()
        for link in GITLINKS:
            if INDEX_MODE and link in INDEX_OVERRIDES:
                continue
            base = REPO / link
            if not base.is_dir():
                continue
            for f in base.rglob("*"):
                if f.is_file():
                    paths.add(str(f.relative_to(REPO)).replace("\\", "/"))
        for rel in paths:
            _submodule_tails.setdefault(rel.rsplit("/", 1)[-1], []).append(rel)
    return _submodule_tails


def submodule_path_exists(path):
    if not under_submodule(path):
        return False
    if INDEX_MODE:
        link = next(link for link in GITLINKS if path.startswith(link + "/"))
        if link in INDEX_OVERRIDES:
            return path in staged_submodule_paths()
    return (REPO / path).exists()


def under_submodule(path):
    return any(path.startswith(link + "/") for link in GITLINKS)


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
    if token.startswith(FOREIGN_REPOS):
        return "skip"
    if not token or token.startswith(SKIP_PREFIX) or not TOKEN_OK.match(token):
        return "skip"
    if token.endswith(BUILD_SUFFIX) or set(token) <= set("./"):
        return "skip"
    if re.fullmatch(r"\.[A-Za-z]+", token):  # prose: "the board's `.c`"
        return "skip"
    if not is_path_claim(token):
        return "skip"
    bare = token.rstrip("/")
    if bare in GENERATED_DIRS:
        return "skip"
    if bare.rsplit("/", 1)[-1] in GENERATED:
        return "skip"
    candidates = [norm(base + bare) for base in BASES]
    candidates.append(norm(origin.rsplit("/", 1)[0] + "/" + bare))
    for cand in candidates:
        # Ignored working-tree files are not part of a commit and must not make
        # a committed pointer appear valid. Live cached and non-ignored paths
        # are already in TRACKED; submodule contents have the explicit
        # filename fallback below.
        if (cand in TRACKED or cand in DIRS
                or submodule_path_exists(cand)):
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
        text = read_text(rel)
        for line in text.splitlines():
            match = HEADING.match(line)
            if match:
                found.add(match.group(1).strip().strip("`*").lower())
        for match in re.finditer(r"\*\*([^*\n]+)\*\*", text):
            found.add(match.group(1).strip().rstrip(".:,").strip("`").lower())
        _headings[rel] = found
    return _headings[rel]


def linecount(rel):
    return read_text(rel).count("\n") + 1



TOKEN = re.compile(TOKEN_ALPHABET)


def homeless(range_spec):
    """Advisory identifiers removed from docs with no remaining mention."""
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
    print("tokens removed from docs with no remaining mention "
          "(review only; preserve only unique requirements that lost an owner):")
    for token in findings:
        print(f"  {token}")
    return 0


def scan_set():
    docs = sorted(p for p in TRACKED if p.startswith(DOC_ROOT))
    rules = sorted(p for p in TRACKED if p.startswith(RULES_PREFIX))
    return docs + [e for e in ENTRY if e in TRACKED] + rules


def root_entry_findings(body, tracked):
    """Check root-entry file pointers using only Git-root-relative ownership."""
    findings = []
    for number, line in enumerate(body.splitlines(), 1):
        links = MDLINK.findall(line)
        files = [token for token in BACKTICK.findall(line)
                 if token.endswith(SRC_SUFFIX) or token in (".gitignore", ".gitmodules", ".gitattributes")]
        for token in set(links + files):
            if token.startswith(("http://", "https://", "#")):
                continue
            target = token.split("#", 1)[0]
            if target not in tracked:
                findings.append(f"Git-root AGENTS.md:{number}: `{token}` resolves to no root-relative file")
    return findings


def check_root_entry(fails):
    root = Path(git("rev-parse", "--show-toplevel").strip())
    if root == REPO:
        return
    args = ["git", "-C", str(root)]
    listing = subprocess.run(args + ["ls-files", "--cached"] if INDEX_MODE else
                             args + ["ls-files", "--cached", "--others", "--exclude-standard"],
                             capture_output=True, text=True, encoding="utf-8", check=True)
    tracked = set(listing.stdout.splitlines())
    if INDEX_MODE:
        body = subprocess.run(args + ["show", ":AGENTS.md"], capture_output=True,
                              text=True, encoding="utf-8", check=True).stdout
    else:
        body = (root / "AGENTS.md").read_text(encoding="utf-8")
        tracked = {p for p in tracked if (root / p).is_file()}
    fails.extend(root_entry_findings(body, tracked))


def check_pointers(files, fails):
    counted = 0
    for rel in files:
        text = read_text(rel)
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


# Core files can carry ERA-owned comments too. Only comment blocks that mention
# ERA are checked there; upstream prose is outside this repository's ownership.
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
    """Apply the pointer check to ERA-owned source comments."""
    counted = 0
    era_only = set(core_comment_set())
    for rel in source_comment_set() + core_comment_set():
        text = read_text(rel)
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



def check_headers(fails):
    docs = [p for p in TRACKED
            if p.startswith(DOC_ROOT) and p.endswith(".md")]
    for rel in docs:
        if rel.startswith(f"{DOC_ROOT}/user/"):
            continue
        head = "\n".join(read_text(rel).splitlines()[:14])
        for header in HEADERS:
            if header not in head:
                fails.append(f"{rel}: no `{header}` header in its first lines")
        for header in FORBIDDEN_HEADERS:
            if header in head:
                fails.append(
                    f"{rel}: `{header}` header is retired — the index owns "
                    "when a document is read")
        match = re.search(r"^Genre:\s*(\S+)", head, re.M)
        if match and match.group(1).strip().lower() not in GENRES:
            fails.append(f"{rel}: `Genre: {match.group(1)}` is not one of "
                         + ", ".join(sorted(GENRES)))
        canon = re.search(r"^Canonical for:\s*(.*)", head, re.M)
        if canon and not canon.group(1).strip():
            fails.append(f"{rel}: `Canonical for:` is empty")
    return len([p for p in docs if not p.startswith(f"{DOC_ROOT}/user/")])


def check_index(fails):
    index = read_text(INDEX)
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
                         f"does not name, so it is unreachable from the "
                         f"active entry router")
    return len(named)


def main():
    argv = sys.argv[1:]
    if argv and argv[0] == "--homeless":
        return homeless(argv[1] if len(argv) > 1 else None)
    if argv == ["--index"]:
        argv = []
    if argv:
        print(f"usage: {Path(__file__).name} [--index | --homeless [A..B]]")
        return 2

    files = scan_set()
    fails = []
    check_root_entry(fails)
    pointers = check_pointers(files, fails)
    documents = check_headers(fails)
    named = check_index(fails)
    comments = check_source_comments(fails)
    print(f"doc refs: {len(files)} agent-layer files, {pointers} pointers, "
          f"{documents} documents with headers, {named} named by the index, "
          f"{comments} source-comment references")
    for entry in fails:
        print("FAIL: " + entry)
    if fails:
        print(f"\n{len(fails)} broken document/source-comment pointer or ownership finding(s).")
        return 1
    print("agent-document links, reachability, and owned source-comment pointers are valid")
    return 0


if __name__ == "__main__":
    sys.exit(main())
