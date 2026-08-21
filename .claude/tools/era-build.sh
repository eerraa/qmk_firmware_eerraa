#!/usr/bin/env bash
# era-build - the whole edit-on-Windows, build-in-WSL loop in one command.
#
#   era-build                 all four profiles
#   era-build wire            one profile
#   era-build release wire    a subset
#
# Sync, build each profile through the gate launcher, then copy the artifacts
# back to the Windows .era-artifacts. Run from anywhere:
#
#   wsl -d Ubuntu -e bash -lc 'era-build wire'
#
# The sync is not optional. Skipping it is the one silent failure this setup
# has - a stale build succeeds, passes every gate, and reports the wrong
# source - so it runs first, every time.
#
# Installed as ~/bin/era-build, a symlink to this file in the EDIT tree; see
# era-sync.sh for why the link does not point into the build tree.
set -euo pipefail

# --- machine-specific, and the only part a new machine has to change --------
WIN_POSIX=/mnt/d/Engineering/qmk_firmware_eerraa
WSL="$HOME/projects/qmk_firmware_eerraa"
# ---------------------------------------------------------------------------
LAUNCHER=keyboards/era/common/tools/era_tomak79h_build.sh

profiles=("$@")
[ ${#profiles[@]} -eq 0 ] && profiles=(release wire qwin cause)
for p in "${profiles[@]}"; do
    case "$p" in
        release|wire|qwin|cause) ;;
        # Injection proof image, buildable by name and deliberately outside the
        # default set: never part of a gate sweep and never flashed as a
        # production build. `stale` is the era-mirror injection. R7's `kill`
        # profile was accepted here until 2026-08-11 and had been retired with
        # its selector on 2026-08-07, so `era-build kill` passed this validator
        # and died inside the launcher, which rejects it.
        stale) ;;
        # qwin plus the pass-phase itemisation; a rung, outside the default
        # sweep, and never a comparison point. Performance batch 1's two
        # bisect rungs (qwin_piooff, qwin_wfeoff) were accepted here until the
        # batch closed on 2026-08-16.
        qwin_phase) ;;
        *) echo "era-build: unknown profile: $p" >&2; exit 2 ;;
    esac
done

start=$SECONDS
era-sync
cd "$WSL"

# Take each manifest path from the launcher's own output. Globbing the
# artifact directory instead would match a previous run's file - the clean and
# the _dirty name for one commit differ only by a suffix - and report the wrong
# build.
declare -A manifest_of
runlog=$(mktemp)
trap 'rm -f "$runlog"' EXIT
for p in "${profiles[@]}"; do
    printf '\n=== %s ===\n' "$p"
    # tee to a real file, not /dev/stderr: under `wsl -e bash -lc` stderr is a
    # pipe that tee cannot reopen, and it fails with EACCES.
    "$LAUNCHER" "$p" | tee "$runlog"
    manifest_of[$p]=$(sed -n 's/^Manifest: //p' "$runlog" | tail -1)
done

# Copying back is cheap - drvfs is slow at scanning many files, not at writing
# a few - so the artifacts always land beside the source the agent edits.
# No --delete: older artifacts in the Windows tree are the user's to keep.
mkdir -p "$WIN_POSIX/.era-artifacts"
rsync -rlt "$WSL/.era-artifacts/" "$WIN_POSIX/.era-artifacts/"

printf '\n=== returned to %s ===\n' "$WIN_POSIX/.era-artifacts"
for p in "${profiles[@]}"; do
    m=${manifest_of[$p]:-}
    if [ -z "$m" ] || [ ! -f "$m" ]; then
        printf '  %-8s manifest not reported by the launcher\n' "$p"
        continue
    fi
    printf '  %-8s dirty=%-3s edit_tree=%-28s ram0_free=%-7s %s\n' \
        "$p" \
        "$(sed -n 's/^worktree_dirty=//p' "$m")" \
        "$(sed -n 's/^edit_tree_check=//p' "$m")" \
        "$(sed -n 's/^ram0_free_bytes=//p' "$m")" \
        "$(basename "${m%.manifest.txt}.uf2")"
done
printf 'total %ss\n' "$((SECONDS - start))"
