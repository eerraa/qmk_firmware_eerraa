#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../.." && pwd)"
fixture=tests/era_build_variant_rules/fixture.mk

fail() {
    echo "era-build-variant-test: $1" >&2
    exit 1
}

expect_line() {
    local label=$1 expected=$2
    shift 2
    local output
    output=$(cd "$repo_root" && "$@") || fail "$label unexpectedly failed"
    local actual
    actual=$(sed -n 's/^FIXTURE //p' <<<"$output" | tail -1)
    [[ $actual == "$expected" ]] || fail "$label: expected '$expected', got '$actual'"
}

expect_fail() {
    local label=$1
    shift
    if (cd "$repo_root" && "$@") >/dev/null 2>&1; then
        fail "$label unexpectedly succeeded"
    fi
}

declare -A tuples=(
    [standard]='wire=no,reset=no'
    [reset]='wire=no,reset=yes'
    [wire]='wire=yes,reset=no'
)

for variant in standard reset wire; do
    expect_line "normal $variant" "variant=$variant tuple=${tuples[$variant]}" \
        make --no-print-directory -f "$fixture" "ERA_BUILD_VARIANT=$variant" print
done

# Direct option assignments, MAKEFLAGS, exported variables, and make -e all
# have higher precedence than ordinary makefile assignments. The selected
# fragment uses GNU make's `override` specifically so each attack still emits
# the canonical tuple for its name.
expect_line 'direct option overrides' "variant=wire tuple=${tuples[wire]}" \
    make --no-print-directory -f "$fixture" ERA_BUILD_VARIANT=wire \
        ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE=no ERA_SETTINGS_RESET_IMAGE=yes print

expect_line 'MAKEFLAGS overrides' "variant=standard tuple=${tuples[standard]}" \
    env 'MAKEFLAGS=ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE=yes ERA_SETTINGS_RESET_IMAGE=yes' \
        make --no-print-directory -f "$fixture" ERA_BUILD_VARIANT=standard print

expect_line 'exported environment overrides' "variant=wire tuple=${tuples[wire]}" \
    env ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE=no ERA_SETTINGS_RESET_IMAGE=yes \
        make --no-print-directory -f "$fixture" ERA_BUILD_VARIANT=wire print

expect_line 'make -e overrides' "variant=reset tuple=${tuples[reset]}" \
    env ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE=yes ERA_SETTINGS_RESET_IMAGE=no \
        make -e --no-print-directory -f "$fixture" ERA_BUILD_VARIANT=reset print

# Reset must work on non-split RP2040 too.
expect_line 'non-split standard' "variant=standard tuple=${tuples[standard]}" \
    make --no-print-directory -f "$fixture" SPLIT_KEYBOARD=no ERA_BUILD_VARIANT=standard print
expect_line 'non-split reset' "variant=reset tuple=${tuples[reset]}" \
    make --no-print-directory -f "$fixture" SPLIT_KEYBOARD=no ERA_BUILD_VARIANT=reset print
expect_fail 'non-split diagnostic variant' \
    make --no-print-directory -f "$fixture" SPLIT_KEYBOARD=no ERA_BUILD_VARIANT=wire print
expect_fail 'retired board profile' \
    make --no-print-directory -f "$fixture" ERA_TOMAK79H_BUILD_PROFILE=wire print

echo 'era-build-variant-test: all cases passed'
