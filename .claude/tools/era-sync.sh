#!/usr/bin/env bash
# era-sync - refresh the WSL build tree from the Windows edit tree.
#
#   era-sync        replay the edit tree's uncommitted delta onto the build
#                   tree, whole-tree and exact.
#   era-sync full   git fetch, then FORCE checkout the Windows HEAD branch.
#
# The delta is enumerated by the NATIVE Windows git, not by this WSL git. The
# tree is the same either way; the difference is drvfs. A whole-tree untracked
# scan costs 3m24s read from here and about 2 s through git.exe with
# core.untrackedCache and core.fscache enabled on the Windows side. That one
# fact is why this can cover the whole tree instead of just keyboards/era, so
# the edit tree's status is read exactly once and reused for the check.
#
# The build tree is disposable, so both modes reset it rather than merging.
#
# Installed as ~/bin/era-sync, a symlink to this file. The link points at the
# EDIT tree's copy on purpose: this script resets the build tree, and running
# from the copy it is about to `git checkout --force` would rewrite the script
# mid-execution.
set -euo pipefail

# --- machine-specific, and the only part a new machine has to change --------
WIN_POSIX=/mnt/d/Engineering/qmk_firmware_eerraa
WIN_NATIVE='D:\Engineering\qmk_firmware_eerraa'
WINGIT='/mnt/c/Program Files/Git/cmd/git.exe'
WSL="$HOME/projects/qmk_firmware_eerraa"
# ---------------------------------------------------------------------------

# The paths above are literal and this script is not portable. When one stops
# resolving the checkout has moved or this is a different machine, and the
# answer is to say so, not to guess a new path or fall back to Windows.
fail_setup() {
    cat >&2 <<SETUP_MISSING
era-sync: $1
  $2

The build environment is not where this script expects it. Stop and tell the
user which path failed. Do not search for a replacement, and do not build on
Windows instead.
SETUP_MISSING
    exit 1
}
[ -d "$WIN_POSIX" ] || fail_setup "edit tree not found"    "$WIN_POSIX"
[ -d "$WSL/.git" ]  || fail_setup "build tree not found"   "$WSL"
[ -x "$WINGIT" ]    || fail_setup "Windows git not found"  "$WINGIT"
cd "$WSL"
mode="${1:-fast}"

wingit() { "$WINGIT" -C "$WIN_NATIVE" --no-optional-locks "$@"; }
# Windows git emits CRLF; strip it before any comparison.
wingit_line() { wingit "$@" | sed -e 's/\r$//'; }
# NUL-delimited status, normalised to sorted lines so both trees compare alike.
normalise() { tr '\0' '\n' | sed -e 's/\r$//' | sed -e '/^$/d' | LC_ALL=C sort; }

# The edit tree's status is NUL-delimited, so it is held in a file rather than
# a variable: command substitution silently drops NUL bytes and would collapse
# every record onto one line.
edit_status=$(mktemp); copy=$(mktemp); gone=$(mktemp)
trap 'rm -f "$edit_status" "$copy" "$gone"' EXIT

case "$mode" in fast|full) ;; *) echo "usage: era-sync [fast|full]" >&2; exit 2;; esac

# Converge on the edit tree's commit whenever it has moved. This is not the
# caller's decision to make: the build tree is disposable, and a build against
# a different commit is exactly the silent failure this tool exists to prevent.
# `full` only forces the step when the heads already agree.
if [ "$mode" = full ] || [ "$(wingit_line rev-parse HEAD)" != "$(git rev-parse HEAD)" ]; then
    branch=$(wingit_line rev-parse --abbrev-ref HEAD)
    git fetch --quiet windows
    if [ "$branch" = HEAD ]; then
        git checkout --quiet --force --detach "$(wingit_line rev-parse HEAD)"
    else
        git checkout --quiet --force -B "$branch" "windows/$branch"
    fi
    git submodule --quiet update --init
    echo "era-sync: moved to $(git rev-parse --abbrev-ref HEAD) @ $(git rev-parse --short=10 HEAD)"
fi

# The single read of the edit tree. Everything below derives from it.
wingit status --porcelain=v1 --untracked-files=all -z >"$edit_status"

# Start from HEAD so the result is the edit tree's state rather than a merge of
# it with whatever a previous sync left behind. Ignored paths (.build,
# .era-artifacts) survive because -x is deliberately absent.
git checkout --quiet --force .
git clean --quiet -fd

while IFS= read -r -d '' entry; do
    x=${entry:0:1}; y=${entry:1:1}; path=${entry:3}
    case "$x" in
        R|C) IFS= read -r -d '' _old; printf '%s\n' "$path" >>"$copy" ;;
        D)   printf '%s\n' "$path" >>"$gone" ;;
        *)   if [ "$y" = D ]; then printf '%s\n' "$path" >>"$gone"
             else printf '%s\n' "$path" >>"$copy"; fi ;;
    esac
done <"$edit_status"

while IFS= read -r p; do
    [ -n "$p" ] && [ -e "$WSL/$p" ] && rm -f -- "$WSL/$p"
done <"$gone"
# -rlt, never -a. Over drvfs every file reads back as mode 777, so -a would
# copy that in and git would then report thousands of exec-bit changes.
#
# And this is a delta replay rather than a whole-tree copy for the same class
# of reason: core.autocrlf=true on the Windows side leaves every file not
# pinned by .gitattributes (*.yml, .clang-format, .editorconfig) as CRLF there
# and LF here. Windows git normalises on compare so it never reports them, and
# they never enter the list below -- but a whole-tree rsync would carry them
# over, dirty the build tree, and stamp every artifact _dirty.
# keyboards/era/ is exempt either way: everything under it is eol=lf.
[ -s "$copy" ] && rsync -rlt --files-from="$copy" "$WIN_POSIX/" "$WSL/"
printf 'era-sync: %s file(s) replayed, %s removed\n' \
    "$(wc -l <"$copy")" "$(wc -l <"$gone")"

# The build tree is correct when its own status equals the edit tree's. That
# equality is the check, not the copied count: a path the replay missed shows
# up here rather than silently reaching a build.
theirs=$(normalise <"$edit_status")
mine=$(git status --porcelain=v1 --untracked-files=all -z | normalise)
if [ "$mine" != "$theirs" ]; then
    echo "era-sync: MISMATCH against the edit tree after sync" >&2
    diff <(printf '%s\n' "$theirs") <(printf '%s\n' "$mine") | head -20 >&2
    exit 1
fi
n=$(printf '%s' "$mine" | grep -c . || true)
if [ "$n" -eq 0 ]; then
    echo "era-sync: clean, matches edit tree"
else
    echo "era-sync: matches edit tree ($n uncommitted; artifacts stamp _dirty)"
fi
