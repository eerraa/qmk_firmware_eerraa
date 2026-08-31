#!/usr/bin/env bash
# Host adapter for one fresh, clean 260901R1 receipt set. It deliberately uses
# the documented era-build entry point once per inventory target and consumes
# only the exact Manifest: path printed by that invocation.
set -euo pipefail

# --- machine-specific, matching the existing ERA WSL adapter ----------------
WIN_POSIX=/mnt/d/Engineering/qmk_firmware_eerraa
WIN_NATIVE='D:\Engineering\qmk_firmware_eerraa'
WINGIT='/mnt/c/Program Files/Git/cmd/git.exe'
WSL="$HOME/projects/qmk_firmware_eerraa"
# -----------------------------------------------------------------------------

RELEASE=260901R1
BUILD_DATE=2026-09-01-00:00:00
RECEIPT_TOOL=keyboards/era/common/tools/era_release_260901R1_receipt.py

usage() {
    cat <<'EOF'
Usage: era-release-260901R1-build SOURCE_SHA

SOURCE_SHA is the full final 40-hex commit. The adapter requires a clean local
main at that SHA, synchronises the WSL-local build tree, runs every inventory
target as `era-build --build-date 2026-09-01-00:00:00 TARGET standard`, and
prints one new receipt-set.json path. A failed/partial directory is never a
receipt set and cannot be packaged.
EOF
}

fail() {
    echo "era-release-260901R1-build: $1" >&2
    exit "${2:-1}"
}

[[ $# -eq 1 ]] || { usage >&2; exit 2; }
source_sha=$1
[[ $source_sha =~ ^[0-9a-f]{40}$ ]] || fail "SOURCE_SHA must be 40 lowercase hexadecimal characters" 2

for path in "$WIN_POSIX" "$WSL/.git"; do
    [[ -e $path ]] || fail "configured path does not exist: $path"
done
[[ -x $WINGIT ]] || fail "configured Windows git does not exist: $WINGIT"
for command_name in era-sync era-build python3 arm-none-eabi-objdump git sed date mktemp tee; do
    command -v "$command_name" >/dev/null 2>&1 || fail "required command was not found: $command_name"
done

wingit() { "$WINGIT" -C "$WIN_NATIVE" --no-optional-locks "$@" | sed -e 's/\r$//'; }
[[ $(wingit symbolic-ref --quiet HEAD) == refs/heads/main ]] || fail "the Windows edit tree must have local main checked out"
[[ $(wingit rev-parse HEAD) == "$source_sha" ]] || fail "the Windows edit tree HEAD is not SOURCE_SHA"
[[ -z $(wingit status --porcelain=v1 --untracked-files=all) ]] || fail "the Windows edit tree is not clean"

# Establish the exact source tree before the inventory itself is consulted.
era-sync
cd "$WSL"
[[ $(git rev-parse HEAD) == "$source_sha" ]] || fail "the synchronized WSL build tree is not SOURCE_SHA"
[[ -z $(git status --porcelain=v1 --untracked-files=all) ]] || fail "the synchronized WSL build tree is not clean"

mapfile -t inventory_rows < <(python3 "$RECEIPT_TOOL" inventory-targets --repo "$WSL" --source-sha "$source_sha")
[[ ${#inventory_rows[@]} -eq 22 ]] || fail "release inventory did not return exactly 22 targets"

release_stage_parent="$WIN_POSIX/.era-artifacts/$RELEASE"
mkdir -p "$release_stage_parent"
receipt_dir=$(mktemp -d "$release_stage_parent/receipt-set.XXXXXXXX")
receipt_set_id=$(python3 -c 'import secrets; print(secrets.token_hex(16))')
runlog=$(mktemp)
trap 'rm -f "$runlog"' EXIT
receipt_paths=()

for row in "${inventory_rows[@]}"; do
    IFS=$'\t' read -r target stem extra <<<"$row"
    [[ -n $target && -n $stem && -z ${extra:-} ]] || fail "malformed inventory row: $row"
    not_before_ns=$(date +%s%N)
    : >"$runlog"
    printf '\n=== release receipt %s (%s) ===\n' "$stem" "$target"
    era-build --build-date "$BUILD_DATE" "$target" standard | tee "$runlog"
    mapfile -t manifests < <(sed -n 's/^Manifest: //p' "$runlog")
    [[ ${#manifests[@]} -eq 1 ]] || fail "era-build reported ${#manifests[@]} manifests for $target; expected one"
    manifest=${manifests[0]}
    [[ -f $manifest ]] || fail "era-build manifest does not exist: $manifest"
    python3 "$RECEIPT_TOOL" create \
        --repo "$WSL" \
        --source-sha "$source_sha" \
        --manifest "$manifest" \
        --receipt-set-id "$receipt_set_id" \
        --not-before-ns "$not_before_ns" \
        --output-dir "$receipt_dir"
    receipt_paths+=("$receipt_dir/receipts/$stem.receipt.json")
done

finalize_args=(
    "$RECEIPT_TOOL" finalize
    --repo "$WSL"
    --source-sha "$source_sha"
    --receipt-set-id "$receipt_set_id"
    --output-dir "$receipt_dir"
)
for receipt in "${receipt_paths[@]}"; do
    finalize_args+=(--receipt "$receipt")
done
python3 "${finalize_args[@]}"
