#!/usr/bin/env bash
# WSL host adapter for the release verifier's app-owned VIA V3 validator.
set -euo pipefail

# --- machine-specific, matching the other ERA WSL adapters -----------------
APP_REPO=/mnt/d/Engineering/the-via-eerraa
# ---------------------------------------------------------------------------

fail() {
    echo "era-release-260901R1-via-validator: $1" >&2
    exit "${2:-1}"
}

expected_script=scripts/validate-external-v3.ts
[[ $# -ge 1 && $1 == "$expected_script" ]] || fail "expected the app validator script as the first argument" 2
shift

[[ ${ERA_RELEASE_APP_REPO:-} == "$APP_REPO" ]] || fail "the verifier app repository does not match the configured WSL adapter path"
[[ -f "$APP_REPO/$expected_script" ]] || fail "configured app validator does not exist: $APP_REPO/$expected_script"
[[ -d "$APP_REPO/node_modules/typescript" ]] || fail "the app's pinned TypeScript compiler is not installed"
[[ -d "$APP_REPO/node_modules/@the-via/reader" ]] || fail "the app's pinned VIA reader is not installed"
for command_name in node mktemp ln rm; do
    command -v "$command_name" >/dev/null 2>&1 || fail "required command was not found: $command_name"
done

temp_dir=$(mktemp -d "/tmp/era-release-260901R1-via.XXXXXXXX")
cleanup() {
    if [[ -n ${temp_dir:-} && -d $temp_dir && ${temp_dir##*/} == era-release-260901R1-via.* ]]; then
        rm -rf -- "$temp_dir"
    fi
}
trap cleanup EXIT
compiled="$temp_dir/validate-external-v3.mjs"

# The app dependencies were installed on Windows, so tsx's optional esbuild
# binary cannot execute in WSL. TypeScript itself is pure JavaScript: transpile
# the already provenance-checked source without emitting into either checkout.
node - "$APP_REPO/node_modules/typescript" "$APP_REPO/$expected_script" "$compiled" <<'NODE'
const fs = require('node:fs');
const [typescriptPath, sourcePath, outputPath] = process.argv.slice(2);
const ts = require(typescriptPath);
const source = fs.readFileSync(sourcePath, 'utf8');
const result = ts.transpileModule(source, {
  compilerOptions: {
    module: ts.ModuleKind.ESNext,
    moduleResolution: ts.ModuleResolutionKind.Bundler,
    target: ts.ScriptTarget.ES2022,
  },
  fileName: sourcePath,
  reportDiagnostics: true,
});
const errors = (result.diagnostics ?? []).filter(
  ({category}) => category === ts.DiagnosticCategory.Error,
);
if (errors.length) {
  process.stderr.write(ts.formatDiagnostics(errors, {
    getCanonicalFileName: (name) => name,
    getCurrentDirectory: () => process.cwd(),
    getNewLine: () => '\n',
  }));
  process.exit(1);
}
fs.writeFileSync(outputPath, result.outputText, 'utf8');
NODE

ln -s "$APP_REPO/node_modules" "$temp_dir/node_modules"
cd "$APP_REPO"
node "$compiled" "$@"
