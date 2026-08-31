#!/usr/bin/env python3
"""Shared, release-locked implementation for the ERA 260901R1 artifacts."""

from __future__ import annotations

import hashlib
import json
import os
import re
import shutil
import subprocess
import tempfile
import zipfile
from pathlib import Path, PurePosixPath
from typing import Any, Iterable, Mapping, Sequence


RELEASE = "260901R1"
INVENTORY_REPO_PATH = "keyboards/era/common/release/260901R1.json"
APP_VALIDATOR_REPO_PATH = "scripts/validate-external-v3.ts"
EXPECTED_BUILD_DATE = "2026-09-01-00:00:00"
EXPECTED_BUILD_TUPLE = "wire=no,qwin=no,phase=no,cause=no,stale=no"
EXPECTED_VERSION = {
    "text": RELEASE,
    "length": 8,
    "channel": 8,
    "value_id": 1,
    "payload_size": 9,
    "symbol": "era_firmware_version",
    "encoding": "ascii-nul",
}
EXPECTED_ZIP = {
    "compression": "stored",
    "timestamp": "1980-01-01T00:00:00",
    "create_system": 3,
    "mode": "0100644",
}
ZIP_DATE_TIME = (1980, 1, 1, 0, 0, 0)
ZIP_EXTERNAL_ATTR = 0o100644 << 16
SOURCE_SHA_HEADER = "Source SHA: {source_sha}\n\n"
HEX40_RE = re.compile(r"^[0-9a-f]{40}$")
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
SET_ID_RE = re.compile(r"^[0-9a-f]{32}$")
STEM_RE = re.compile(r"^[A-Z0-9][A-Z0-9_-]*$")
TARGET_RE = re.compile(r"^era/[A-Za-z0-9_/-]+:via$")
FORBIDDEN_RELEASE_SUFFIXES = (
    ".elf",
    ".hex",
    ".map",
    ".log",
    ".manifest.txt",
    ".receipt.json",
)


class ReleaseError(RuntimeError):
    """A deterministic release precondition or verification failure."""


def _fail(message: str) -> None:
    raise ReleaseError(message)


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def canonical_json_bytes(value: Any) -> bytes:
    return (json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode("utf-8")


def write_exclusive(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    try:
        with path.open("xb") as stream:
            stream.write(data)
    except FileExistsError:
        _fail(f"refusing to overwrite existing output: {path}")


def copy_exclusive(source: Path, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    try:
        with source.open("rb") as incoming, destination.open("xb") as outgoing:
            shutil.copyfileobj(incoming, outgoing, length=1024 * 1024)
    except FileExistsError:
        _fail(f"refusing to overwrite existing receipt artifact: {destination}")


def _run(
    command: Sequence[str],
    *,
    cwd: Path | None = None,
    text: bool = False,
    env: Mapping[str, str] | None = None,
) -> bytes | str:
    try:
        result = subprocess.run(
            list(command),
            cwd=str(cwd) if cwd else None,
            env=dict(env) if env is not None else None,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            text=text,
        )
    except OSError as exc:
        _fail(f"could not execute {command[0]!r}: {exc}")
    if result.returncode != 0:
        stderr = result.stderr.strip() if text else result.stderr.decode("utf-8", "replace").strip()
        _fail(f"command failed ({result.returncode}): {' '.join(command)}\n{stderr}")
    return result.stdout


def _git(repo: Path, arguments: Sequence[str], *, text: bool = True) -> bytes | str:
    return _run(["git", "-C", str(repo), *arguments], text=text)


def require_source_sha(source_sha: str) -> None:
    if not HEX40_RE.fullmatch(source_sha):
        _fail(f"source SHA must be one full lowercase SHA-1: {source_sha!r}")


def safe_relative_path(value: str, *, label: str) -> PurePosixPath:
    if not isinstance(value, str) or not value:
        _fail(f"{label} must be a non-empty string")
    if "\\" in value or "\x00" in value or any(ord(char) < 32 for char in value):
        _fail(f"unsafe {label}: {value!r}")
    path = PurePosixPath(value)
    if path.is_absolute() or path.as_posix() != value or any(part in ("", ".", "..") for part in path.parts):
        _fail(f"unsafe {label}: {value!r}")
    if path.parts and ":" in path.parts[0]:
        _fail(f"unsafe {label}: {value!r}")
    return path


def under_root(root: Path, relative: str, *, label: str) -> Path:
    rel = safe_relative_path(relative, label=label)
    root_resolved = root.resolve()
    candidate = root_resolved.joinpath(*rel.parts).resolve(strict=False)
    if candidate != root_resolved and root_resolved not in candidate.parents:
        _fail(f"{label} escapes its root: {relative!r}")
    return candidate


def _json_object(data: bytes, *, label: str) -> dict[str, Any]:
    try:
        value = json.loads(data)
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        _fail(f"invalid JSON in {label}: {exc}")
    if not isinstance(value, dict):
        _fail(f"{label} must contain one JSON object")
    return value


def _canonical_json_object(path: Path, *, label: str) -> dict[str, Any]:
    raw = path.read_bytes()
    value = _json_object(raw, label=label)
    if raw != canonical_json_bytes(value):
        _fail(f"{label} is not canonical sorted JSON: {path}")
    return value


def git_blob(repo: Path, source_sha: str, source_path: str) -> tuple[bytes, str]:
    require_source_sha(source_sha)
    safe_relative_path(source_path, label="Git source path")
    object_name = f"{source_sha}:{source_path}"
    object_type = str(_git(repo, ["cat-file", "-t", object_name])).strip()
    if object_type != "blob":
        _fail(f"Git source is not a blob: {object_name} ({object_type})")
    blob_oid = str(_git(repo, ["rev-parse", object_name])).strip()
    data = bytes(_git(repo, ["cat-file", "blob", object_name], text=False))
    return data, blob_oid


def git_blob_record(repo: Path, source_sha: str, source_path: str) -> dict[str, Any]:
    data, blob_oid = git_blob(repo, source_sha, source_path)
    return {
        "path": source_path,
        "git_blob": blob_oid,
        "sha256": sha256_bytes(data),
        "size": len(data),
    }


def validate_app_checkout(app_repo: Path, app_head: str) -> dict[str, Any]:
    """Bind VIA validation to one clean local app commit, never app origin/main."""
    require_source_sha(app_head)
    app_repo = app_repo.resolve(strict=True)
    status_output = str(_git(app_repo, ["status", "--porcelain=v1", "--untracked-files=all"]))
    if status_output:
        _fail("VIA app validation requires a clean app working tree")
    actual_head = str(_git(app_repo, ["rev-parse", "HEAD"])).strip()
    if actual_head != app_head:
        _fail(f"VIA app HEAD is {actual_head}, not the explicitly recorded app commit {app_head}")
    script_data, script_oid = git_blob(app_repo, app_head, APP_VALIDATOR_REPO_PATH)
    script_path = under_root(app_repo, APP_VALIDATOR_REPO_PATH, label="VIA validator script")
    if not script_path.is_file() or script_path.is_symlink():
        _fail(f"VIA validator is not a regular app file: {script_path}")
    working_oid = str(
        _git(
            app_repo,
            ["hash-object", "--path", APP_VALIDATOR_REPO_PATH, "--", APP_VALIDATOR_REPO_PATH],
        )
    ).strip()
    if working_oid != script_oid:
        _fail("VIA validator working tree does not filter to the recorded app Git blob")
    return {
        "app_head": app_head,
        "worktree_clean": True,
        "script": {
            "path": APP_VALIDATOR_REPO_PATH,
            "git_blob": script_oid,
            "sha256": sha256_bytes(script_data),
            "size": len(script_data),
        },
    }


def load_inventory(repo: Path, source_sha: str) -> tuple[dict[str, Any], dict[str, Any]]:
    raw, blob_oid = git_blob(repo, source_sha, INVENTORY_REPO_PATH)
    inventory = _json_object(raw, label=INVENTORY_REPO_PATH)
    validate_inventory_static(inventory)
    record = {
        "path": INVENTORY_REPO_PATH,
        "git_blob": blob_oid,
        "sha256": sha256_bytes(raw),
        "size": len(raw),
    }
    return inventory, record


def package_keyboard(package: Mapping[str, Any]) -> str:
    return str(package["target"]).removesuffix(":via")


def package_uf2_name(package: Mapping[str, Any]) -> str:
    return f"{package['stem']}-{RELEASE}.uf2"


def package_zip_name(package: Mapping[str, Any]) -> str:
    return f"{package['stem']}-{RELEASE}.zip"


def validate_inventory_static(inventory: Mapping[str, Any]) -> None:
    if inventory.get("schema_version") != 1:
        _fail("inventory schema_version must be 1")
    if inventory.get("release") != RELEASE or inventory.get("annotated_tag") != RELEASE:
        _fail(f"inventory release and annotated tag must both be {RELEASE}")
    if inventory.get("build_date") != EXPECTED_BUILD_DATE:
        _fail(f"inventory build_date must be {EXPECTED_BUILD_DATE}")
    if inventory.get("excluded_keyboards") != ["era/sirind/brick65"]:
        _fail("inventory must exclude exactly era/sirind/brick65")
    if inventory.get("guide_envelope") != "source-sha-header-v1":
        _fail("inventory guide envelope must be source-sha-header-v1")
    if inventory.get("firmware_version") != EXPECTED_VERSION:
        _fail("inventory firmware VERSION tuple is not the 260901R1 tuple")
    if inventory.get("zip") != EXPECTED_ZIP:
        _fail("inventory ZIP metadata contract is not the 260901R1 contract")

    guides = inventory.get("guides")
    if not isinstance(guides, dict) or set(guides) != {"non_split", "split", "via_keycodes"}:
        _fail("inventory guides must name non_split, split, and via_keycodes")
    for name, path in guides.items():
        safe_relative_path(path, label=f"guide {name}")

    packages = inventory.get("packages")
    if not isinstance(packages, list) or len(packages) != 22:
        _fail("inventory must contain exactly 22 packages")
    stems = [package.get("stem") for package in packages if isinstance(package, dict)]
    if len(stems) != 22 or stems != sorted(stems, key=lambda value: value.encode("utf-8")):
        _fail("inventory packages must be sorted by canonical UTF-8 stem")
    if len(set(stems)) != 22:
        _fail("inventory canonical stems must be unique")

    targets: set[str] = set()
    json_paths: list[str] = []
    split_targets: list[str] = []
    for package in packages:
        if not isinstance(package, dict) or set(package) != {"stem", "target", "variant", "split", "via_json"}:
            _fail("each inventory package must have only stem, target, variant, split, and via_json")
        stem = package["stem"]
        target = package["target"]
        if not isinstance(stem, str) or not STEM_RE.fullmatch(stem):
            _fail(f"invalid canonical package stem: {stem!r}")
        if not isinstance(target, str) or not TARGET_RE.fullmatch(target):
            _fail(f"release target must be keyboard:via: {target!r}")
        if target in targets:
            _fail(f"duplicate release target: {target}")
        targets.add(target)
        if package["variant"] != "standard":
            _fail(f"release target is not standard: {target}")
        if not isinstance(package["split"], bool):
            _fail(f"split flag is not boolean: {target}")
        via_json = package["via_json"]
        expected_count = 2 if package["split"] else 1
        if not isinstance(via_json, list) or len(via_json) != expected_count:
            _fail(f"{stem} must carry exactly {expected_count} VIA JSON file(s)")
        if via_json != sorted(via_json, key=lambda value: value.encode("utf-8")):
            _fail(f"{stem} VIA JSON paths are not sorted")
        keyboard = package_keyboard(package)
        expected_prefix = f"keyboards/{keyboard}/keymaps/via/"
        expected_names = {f"{stem}-VIA.json"}
        if package["split"]:
            split_targets.append(target)
            expected_names = {f"{stem}-L-VIA.json", f"{stem}-R-VIA.json"}
        actual_names: set[str] = set()
        for path in via_json:
            safe_relative_path(path, label=f"{stem} VIA JSON path")
            if not path.startswith(expected_prefix):
                _fail(f"{stem} VIA JSON is outside its firmware-local via keymap: {path}")
            actual_names.add(PurePosixPath(path).name)
            json_paths.append(path)
        if actual_names != expected_names:
            _fail(f"{stem} VIA JSON names are not canonical: {sorted(actual_names)}")

    if len(json_paths) != 25 or len(set(json_paths)) != 25:
        _fail("inventory must contain exactly 25 unique official VIA JSON paths")
    if split_targets != [
        "era/sirind/tomak:via",
        "era/sirind/tomak79h:via",
        "era/sirind/tomak79s:via",
    ]:
        _fail("inventory split package set is not the three TOMAK release targets")


def _git_tree_paths(repo: Path, source_sha: str, prefix: str) -> list[str]:
    raw = bytes(_git(repo, ["ls-tree", "-r", "--name-only", "-z", source_sha, "--", prefix], text=False))
    return [part.decode("utf-8") for part in raw.split(b"\0") if part]


def validate_inventory_against_git(repo: Path, source_sha: str, inventory: Mapping[str, Any]) -> None:
    require_source_sha(source_sha)
    object_type = str(_git(repo, ["cat-file", "-t", f"{source_sha}^{{commit}}"])).strip()
    if object_type != "commit":
        _fail(f"source SHA is not a commit: {source_sha}")

    tree_paths = _git_tree_paths(repo, source_sha, "keyboards/era")
    keyboard_json_paths = sorted(path for path in tree_paths if path.endswith("/keyboard.json"))
    active_keyboards: list[str] = []
    inactive_keyboards: list[str] = []
    for path in keyboard_json_paths:
        body, _ = git_blob(repo, source_sha, path)
        keyboard_json = _json_object(body, label=f"{source_sha}:{path}")
        keyboard = path[len("keyboards/") : -len("/keyboard.json")]
        if keyboard_json.get("processor") == "RP2040":
            active_keyboards.append(keyboard)
        else:
            inactive_keyboards.append(keyboard)

    if len(keyboard_json_paths) != 23 or len(active_keyboards) != 22:
        _fail(
            f"source inventory is not 23 ERA keyboards / 22 RP2040 targets: "
            f"{len(keyboard_json_paths)} / {len(active_keyboards)}"
        )
    if inactive_keyboards != inventory["excluded_keyboards"]:
        _fail(f"non-RP2040 ERA keyboard set changed: {inactive_keyboards}")

    inventory_keyboards = [package_keyboard(package) for package in inventory["packages"]]
    if sorted(inventory_keyboards) != sorted(active_keyboards):
        missing = sorted(set(active_keyboards) - set(inventory_keyboards))
        extra = sorted(set(inventory_keyboards) - set(active_keyboards))
        _fail(f"inventory does not cover active RP2040 keyboards exactly; missing={missing}, extra={extra}")

    active_prefixes = tuple(f"keyboards/{keyboard}/" for keyboard in active_keyboards)
    official_json = sorted(
        path
        for path in tree_paths
        if "/keymaps/via/" in path and path.endswith("-VIA.json") and path.startswith(active_prefixes)
    )
    inventory_json = sorted(path for package in inventory["packages"] for path in package["via_json"])
    if len(official_json) != 25 or official_json != inventory_json:
        missing = sorted(set(official_json) - set(inventory_json))
        extra = sorted(set(inventory_json) - set(official_json))
        _fail(f"inventory does not cover the 25 RP2040 VIA JSONs exactly; missing={missing}, extra={extra}")


def source_sha_envelope(source_sha: str, body: bytes) -> bytes:
    require_source_sha(source_sha)
    return SOURCE_SHA_HEADER.format(source_sha=source_sha).encode("ascii") + body


def parse_build_manifest(data: bytes, *, label: str) -> dict[str, str]:
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as exc:
        _fail(f"build manifest is not UTF-8 ({label}): {exc}")
    fields: dict[str, str] = {}
    for line_number, line in enumerate(text.splitlines(), 1):
        if not line or "=" not in line:
            _fail(f"malformed build manifest line {line_number} in {label}")
        key, value = line.split("=", 1)
        if not re.fullmatch(r"[a-z0-9_]+", key):
            _fail(f"invalid build manifest key {key!r} in {label}")
        if key in fields:
            _fail(f"duplicate build manifest field {key!r} in {label}")
        fields[key] = value
    return fields


def validate_build_manifest(
    fields: Mapping[str, str],
    *,
    package: Mapping[str, Any],
    source_sha: str,
) -> None:
    required = {
        "requested_variant",
        "variant",
        "resolved_tuple",
        "compiled_tuple",
        "target",
        "git_head",
        "worktree_dirty",
        "edit_tree_check",
        "build_environment",
        "build_date",
        "firmware",
        "firmware_format",
        "firmware_sha256",
        "artifact_id",
        "uf2",
        "uf2_sha256",
        "elf",
        "elf_sha256",
        "residency_gate",
        "ram0_resident_bytes",
        "ram0_free_bytes",
        "vectors_gate",
        "build_log",
        "command",
    }
    missing = sorted(required - set(fields))
    if missing:
        _fail(f"build manifest is missing fields: {missing}")
    expected = {
        "requested_variant": "standard",
        "variant": "standard",
        "resolved_tuple": EXPECTED_BUILD_TUPLE,
        "compiled_tuple": EXPECTED_BUILD_TUPLE,
        "target": package["target"],
        "git_head": source_sha,
        "worktree_dirty": "no",
        "edit_tree_check": "matched by era-sync",
        "build_environment": "WSL local filesystem",
        "build_date": EXPECTED_BUILD_DATE,
        "firmware_format": "uf2",
        "residency_gate": "passed",
    }
    for key, value in expected.items():
        if fields.get(key) != value:
            _fail(f"build manifest {key}={fields.get(key)!r}; expected {value!r}")
    if fields["firmware"] != fields["uf2"]:
        _fail("build manifest firmware and uf2 paths differ")
    if fields["firmware_sha256"] != fields["uf2_sha256"]:
        _fail("build manifest firmware and uf2 hashes differ")
    for key in ("firmware_sha256", "uf2_sha256", "elf_sha256"):
        if not SHA256_RE.fullmatch(fields[key]):
            _fail(f"invalid {key} in build manifest")
    if fields["artifact_id"] != fields["firmware_sha256"][:16]:
        _fail("build manifest artifact_id is not the firmware SHA-256 prefix")
    if not fields["ram0_resident_bytes"].isdigit() or not fields["ram0_free_bytes"].isdigit():
        _fail("build manifest ram0 figures are not decimal")
    if int(fields["ram0_free_bytes"]) < 32768:
        _fail("build manifest reports less than 32768 bytes free in ram0")
    if not fields["vectors_gate"].startswith("passed"):
        _fail("build manifest vectors gate did not pass")


def _objdump_symbol_rows(output: str, symbol: str) -> list[tuple[int, int, str]]:
    rows: list[tuple[int, int, str]] = []
    for line in output.splitlines():
        tokens = line.split()
        if len(tokens) < 4 or tokens[-1] != symbol:
            continue
        if not re.fullmatch(r"[0-9A-Fa-f]+", tokens[0]) or not re.fullmatch(r"[0-9A-Fa-f]+", tokens[-2]):
            continue
        rows.append((int(tokens[0], 16), int(tokens[-2], 16), tokens[-3]))
    return rows


def _objdump_bytes(output: str) -> bytes:
    collected = bytearray()
    for line in output.splitlines():
        match = re.match(r"^\s*[0-9A-Fa-f]+\s+(.+)$", line)
        if not match:
            continue
        hex_area = re.split(r"\s{2,}", match.group(1), maxsplit=1)[0]
        groups = hex_area.split()
        if not groups:
            continue
        line_bytes = bytearray()
        for group in groups:
            if not re.fullmatch(r"[0-9A-Fa-f]{2,8}", group) or len(group) % 2:
                break
            line_bytes.extend(bytes.fromhex(group))
        collected.extend(line_bytes)
    return bytes(collected)


def read_release_version_witness(
    elf_path: Path,
    version: Mapping[str, Any],
    *,
    objdump_command: Sequence[str],
) -> dict[str, Any]:
    if not objdump_command:
        _fail("ELF reader command is empty")
    symbol = str(version["symbol"])
    table = str(_run([*objdump_command, "-t", str(elf_path)], text=True))
    rows = _objdump_symbol_rows(table, symbol)
    if len(rows) != 1:
        _fail(f"ELF must define {symbol} exactly once; found {len(rows)} in {elf_path}")
    address, size, section = rows[0]
    if size != version["payload_size"]:
        _fail(f"ELF {symbol} size is {size}; expected {version['payload_size']}")
    dump = str(
        _run(
            [
                *objdump_command,
                "-s",
                "-j",
                section,
                f"--start-address=0x{address:x}",
                f"--stop-address=0x{address + size:x}",
                str(elf_path),
            ],
            text=True,
        )
    )
    actual = _objdump_bytes(dump)
    expected = str(version["text"]).encode("ascii") + b"\0"
    if actual != expected:
        _fail(f"ELF {symbol} bytes are {actual.hex()}; expected {expected.hex()}")
    return {
        "result": "passed",
        "address": f"0x{address:08x}",
        "section": section,
        "size": size,
        "bytes_hex": actual.hex(),
    }


def _stable_manifest_provenance(fields: Mapping[str, str]) -> dict[str, str]:
    keys = (
        "target",
        "requested_variant",
        "variant",
        "resolved_tuple",
        "compiled_tuple",
        "git_head",
        "worktree_dirty",
        "edit_tree_check",
        "build_environment",
        "build_date",
        "qmk_cli_version",
        "qmk_firmware_version",
        "compiler",
        "make",
        "residency_gate",
        "ram0_resident_bytes",
        "ram0_free_bytes",
        "vectors_gate",
        "command",
    )
    return {key: fields[key] for key in keys if key in fields}


def launcher_artifact_stem(package: Mapping[str, Any], source_sha: str, firmware_sha256: str) -> str:
    keyboard = package_keyboard(package).replace("/", "_")
    fixed_date = EXPECTED_BUILD_DATE.translate({ord("-"): None, ord(":"): None})
    return f"{keyboard}_via_standard_{source_sha[:10]}_{fixed_date}_{firmware_sha256[:16]}"


def create_receipt(
    *,
    repo: Path,
    source_sha: str,
    manifest_path: Path,
    receipt_set_id: str,
    not_before_ns: int,
    output_dir: Path,
    objdump_command: Sequence[str],
) -> Path:
    require_source_sha(source_sha)
    if not SET_ID_RE.fullmatch(receipt_set_id):
        _fail("receipt set id must be 32 lowercase hexadecimal characters")
    if not isinstance(not_before_ns, int) or not_before_ns < 0:
        _fail("receipt not-before timestamp must be a non-negative integer")
    inventory, _ = load_inventory(repo, source_sha)
    validate_inventory_against_git(repo, source_sha, inventory)

    manifest_path = manifest_path.resolve(strict=True)
    if not manifest_path.is_file() or manifest_path.is_symlink():
        _fail(f"build manifest is not a regular file: {manifest_path}")
    if manifest_path.stat().st_mtime_ns < not_before_ns:
        _fail(f"build manifest predates this adapter invocation: {manifest_path}")
    manifest_data = manifest_path.read_bytes()
    fields = parse_build_manifest(manifest_data, label=str(manifest_path))
    packages = {package["target"]: package for package in inventory["packages"]}
    if fields.get("target") not in packages:
        _fail(f"build manifest target is not in the release inventory: {fields.get('target')!r}")
    package = packages[fields["target"]]
    validate_build_manifest(fields, package=package, source_sha=source_sha)

    expected_launcher_stem = launcher_artifact_stem(package, source_sha, fields["firmware_sha256"])
    expected_manifest_name = f"{expected_launcher_stem}.manifest.txt"
    if manifest_path.name != expected_manifest_name:
        _fail(
            "build manifest filename is not the clean fixed-date launcher name; "
            f"expected {expected_manifest_name}, got {manifest_path.name}"
        )

    firmware_path = Path(fields["firmware"]).resolve(strict=True)
    elf_path = Path(fields["elf"]).resolve(strict=True)
    if firmware_path.name != f"{expected_launcher_stem}.uf2" or elf_path.name != f"{expected_launcher_stem}.elf":
        _fail("manifest-declared UF2/ELF filenames do not match the clean fixed-date launcher stem")
    for label, path in (("firmware", firmware_path), ("ELF", elf_path)):
        if not path.is_file() or path.is_symlink():
            _fail(f"manifest-declared {label} is not a regular file: {path}")
        if path.parent != manifest_path.parent:
            _fail(f"manifest-declared {label} is outside the manifest artifact directory: {path}")
        if path.stat().st_mtime_ns < not_before_ns:
            _fail(f"manifest-declared {label} predates this adapter invocation: {path}")
    if sha256_file(firmware_path) != fields["firmware_sha256"]:
        _fail("manifest-declared UF2 SHA-256 does not match its bytes")
    if sha256_file(elf_path) != fields["elf_sha256"]:
        _fail("manifest-declared ELF SHA-256 does not match its bytes")

    witness = read_release_version_witness(elf_path, inventory["firmware_version"], objdump_command=objdump_command)
    stem = package["stem"]
    firmware_rel = f"artifacts/{package_uf2_name(package)}"
    elf_rel = f"evidence/{stem}-{RELEASE}.elf"
    manifest_rel = f"build-manifests/{stem}.manifest.txt"
    receipt_rel = f"receipts/{stem}.receipt.json"
    output_dir.mkdir(parents=True, exist_ok=True)
    staged_firmware = under_root(output_dir, firmware_rel, label="receipt UF2 path")
    staged_elf = under_root(output_dir, elf_rel, label="receipt ELF path")
    staged_manifest = under_root(output_dir, manifest_rel, label="receipt build-manifest path")
    copy_exclusive(firmware_path, staged_firmware)
    copy_exclusive(elf_path, staged_elf)
    write_exclusive(staged_manifest, manifest_data)

    receipt = {
        "schema_version": 1,
        "release": RELEASE,
        "source_sha": source_sha,
        "receipt_set_id": receipt_set_id,
        "target": package["target"],
        "variant": "standard",
        "stem": stem,
        "freshness": {
            "not_before_ns": not_before_ns,
            "build_manifest_mtime_ns": manifest_path.stat().st_mtime_ns,
            "firmware_mtime_ns": firmware_path.stat().st_mtime_ns,
            "elf_mtime_ns": elf_path.stat().st_mtime_ns,
        },
        "build_manifest": {
            "file": manifest_rel,
            "sha256": sha256_bytes(manifest_data),
            "size": len(manifest_data),
            "fields": _stable_manifest_provenance(fields),
        },
        "firmware": {
            "file": firmware_rel,
            "sha256": fields["firmware_sha256"],
            "size": staged_firmware.stat().st_size,
        },
        "elf": {
            "file": elf_rel,
            "sha256": fields["elf_sha256"],
            "size": staged_elf.stat().st_size,
        },
        "release_version": RELEASE,
        "release_version_symbol": inventory["firmware_version"]["symbol"],
        "release_version_encoding": inventory["firmware_version"]["encoding"],
        "elf_witness": witness,
    }
    receipt_path = under_root(output_dir, receipt_rel, label="receipt JSON path")
    write_exclusive(receipt_path, canonical_json_bytes(receipt))
    return receipt_path


def _load_receipt(path: Path, *, label: str) -> dict[str, Any]:
    return _canonical_json_object(path, label=label)


def _validate_receipt_shape(
    receipt: Mapping[str, Any],
    *,
    package: Mapping[str, Any],
    source_sha: str,
    receipt_set_id: str,
) -> None:
    expected_scalars = {
        "schema_version": 1,
        "release": RELEASE,
        "source_sha": source_sha,
        "receipt_set_id": receipt_set_id,
        "target": package["target"],
        "variant": "standard",
        "stem": package["stem"],
        "release_version": RELEASE,
        "release_version_symbol": EXPECTED_VERSION["symbol"],
        "release_version_encoding": EXPECTED_VERSION["encoding"],
    }
    for key, value in expected_scalars.items():
        if receipt.get(key) != value:
            _fail(f"receipt {package['stem']} has {key}={receipt.get(key)!r}; expected {value!r}")
    freshness = receipt.get("freshness")
    if not isinstance(freshness, dict) or set(freshness) != {
        "not_before_ns",
        "build_manifest_mtime_ns",
        "firmware_mtime_ns",
        "elf_mtime_ns",
    }:
        _fail(f"receipt {package['stem']} has an invalid freshness record")
    if any(not isinstance(value, int) or value < freshness["not_before_ns"] for key, value in freshness.items() if key != "not_before_ns"):
        _fail(f"receipt {package['stem']} names an artifact older than its build invocation")
    witness = receipt.get("elf_witness")
    expected_bytes = (RELEASE.encode("ascii") + b"\0").hex()
    if not isinstance(witness, dict) or witness.get("result") != "passed" or witness.get("size") != 9 or witness.get("bytes_hex") != expected_bytes:
        _fail(f"receipt {package['stem']} lacks the exact 260901R1 ELF witness")
    if not isinstance(witness.get("address"), str) or not re.fullmatch(r"0x[0-9a-f]{8}", witness["address"]):
        _fail(f"receipt {package['stem']} has an invalid version-symbol address")
    if not isinstance(witness.get("section"), str) or not witness["section"]:
        _fail(f"receipt {package['stem']} has an invalid version-symbol section")


def finalize_receipt_set(
    *,
    repo: Path,
    source_sha: str,
    receipt_set_id: str,
    output_dir: Path,
    receipt_paths: Sequence[Path],
) -> Path:
    require_source_sha(source_sha)
    if not SET_ID_RE.fullmatch(receipt_set_id):
        _fail("receipt set id must be 32 lowercase hexadecimal characters")
    inventory, _ = load_inventory(repo, source_sha)
    validate_inventory_against_git(repo, source_sha, inventory)
    if len(receipt_paths) != 22 or len({path.resolve() for path in receipt_paths}) != 22:
        _fail("receipt set finalization requires exactly 22 explicit unique receipt paths")
    output_dir = output_dir.resolve(strict=True)
    expected_relatives: list[str] = []
    for package, supplied in zip(inventory["packages"], receipt_paths, strict=True):
        relative = f"receipts/{package['stem']}.receipt.json"
        expected_path = under_root(output_dir, relative, label="receipt path").resolve(strict=True)
        if supplied.resolve(strict=True) != expected_path:
            _fail(f"receipt path is not the inventory-ordered explicit path for {package['stem']}: {supplied}")
        receipt = _load_receipt(expected_path, label=f"{package['stem']} receipt")
        _validate_receipt_shape(receipt, package=package, source_sha=source_sha, receipt_set_id=receipt_set_id)
        expected_relatives.append(relative)

    receipt_set = {
        "schema_version": 1,
        "release": RELEASE,
        "source_sha": source_sha,
        "receipt_set_id": receipt_set_id,
        "receipts": expected_relatives,
    }
    receipt_set_path = output_dir / "receipt-set.json"
    write_exclusive(receipt_set_path, canonical_json_bytes(receipt_set))
    _validate_receipt_set_files(output_dir, inventory, include_set=True)
    return receipt_set_path


def _receipt_expected_files(inventory: Mapping[str, Any], *, include_set: bool) -> set[str]:
    expected = {"receipt-set.json"} if include_set else set()
    for package in inventory["packages"]:
        stem = package["stem"]
        expected.update(
            {
                f"receipts/{stem}.receipt.json",
                f"artifacts/{package_uf2_name(package)}",
                f"evidence/{stem}-{RELEASE}.elf",
                f"build-manifests/{stem}.manifest.txt",
            }
        )
    return expected


def _tree_regular_files(root: Path) -> set[str]:
    files: set[str] = set()
    for path in root.rglob("*"):
        if path.is_symlink():
            _fail(f"symlink is forbidden in release evidence: {path}")
        if path.is_file():
            files.add(path.relative_to(root).as_posix())
        elif not path.is_dir():
            _fail(f"non-regular release evidence entry: {path}")
    return files


def _tree_directories(root: Path) -> set[str]:
    directories: set[str] = set()
    for path in root.rglob("*"):
        if path.is_symlink():
            _fail(f"symlink is forbidden in release evidence: {path}")
        if path.is_dir():
            directories.add(path.relative_to(root).as_posix())
    return directories


def _validate_receipt_set_files(root: Path, inventory: Mapping[str, Any], *, include_set: bool) -> None:
    actual = _tree_regular_files(root)
    expected = _receipt_expected_files(inventory, include_set=include_set)
    if actual != expected:
        _fail(f"receipt set file inventory differs; missing={sorted(expected - actual)}, extra={sorted(actual - expected)}")
    expected_directories = {"artifacts", "build-manifests", "evidence", "receipts"}
    actual_directories = _tree_directories(root)
    if actual_directories != expected_directories:
        _fail(
            "receipt set directory inventory differs; "
            f"missing={sorted(expected_directories - actual_directories)}, "
            f"extra={sorted(actual_directories - expected_directories)}"
        )


def load_receipt_set(
    *,
    receipt_set_path: Path,
    inventory: Mapping[str, Any],
    source_sha: str,
) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    receipt_set_path = receipt_set_path.resolve(strict=True)
    receipt_set = _canonical_json_object(receipt_set_path, label="receipt set")
    receipt_set_id = receipt_set.get("receipt_set_id")
    expected_set = {
        "schema_version": 1,
        "release": RELEASE,
        "source_sha": source_sha,
    }
    for key, value in expected_set.items():
        if receipt_set.get(key) != value:
            _fail(f"receipt set {key}={receipt_set.get(key)!r}; expected {value!r}")
    if not isinstance(receipt_set_id, str) or not SET_ID_RE.fullmatch(receipt_set_id):
        _fail("receipt set id is invalid")
    expected_receipts = [f"receipts/{package['stem']}.receipt.json" for package in inventory["packages"]]
    if receipt_set.get("receipts") != expected_receipts:
        _fail("receipt set does not name the 22 receipts once in inventory order")

    root = receipt_set_path.parent
    receipts: list[dict[str, Any]] = []
    for package, relative in zip(inventory["packages"], expected_receipts, strict=True):
        path = under_root(root, relative, label="receipt path")
        receipt = _load_receipt(path, label=f"{package['stem']} receipt")
        _validate_receipt_shape(receipt, package=package, source_sha=source_sha, receipt_set_id=receipt_set_id)
        for field_name in ("build_manifest", "firmware", "elf"):
            record = receipt.get(field_name)
            if not isinstance(record, dict):
                _fail(f"receipt {package['stem']} lacks {field_name}")
            file_path = under_root(root, record.get("file", ""), label=f"receipt {field_name} path")
            if not file_path.is_file() or file_path.is_symlink():
                _fail(f"receipt {package['stem']} {field_name} is not a regular file")
            if record.get("size") != file_path.stat().st_size or record.get("sha256") != sha256_file(file_path):
                _fail(f"receipt {package['stem']} {field_name} size/hash mismatch")
        if receipt["firmware"]["file"] != f"artifacts/{package_uf2_name(package)}":
            _fail(f"receipt {package['stem']} does not name its canonical UF2")
        if receipt["elf"]["file"] != f"evidence/{package['stem']}-{RELEASE}.elf":
            _fail(f"receipt {package['stem']} does not name its canonical ELF evidence")
        if receipt["build_manifest"]["file"] != f"build-manifests/{package['stem']}.manifest.txt":
            _fail(f"receipt {package['stem']} does not name its build manifest")
        manifest_path = under_root(root, receipt["build_manifest"]["file"], label="receipt build manifest")
        manifest_fields = parse_build_manifest(manifest_path.read_bytes(), label=str(manifest_path))
        validate_build_manifest(manifest_fields, package=package, source_sha=source_sha)
        if receipt["build_manifest"].get("fields") != _stable_manifest_provenance(manifest_fields):
            _fail(f"receipt {package['stem']} build-manifest provenance fields differ")
        if manifest_fields["firmware_sha256"] != receipt["firmware"]["sha256"]:
            _fail(f"receipt {package['stem']} UF2 is not the manifest-declared UF2")
        if manifest_fields["elf_sha256"] != receipt["elf"]["sha256"]:
            _fail(f"receipt {package['stem']} ELF is not the manifest-declared ELF")
        receipts.append(receipt)
    _validate_receipt_set_files(root, inventory, include_set=True)
    return receipt_set, receipts


def _entry_sort_key(name: str) -> bytes:
    return name.encode("utf-8")


def expected_archive_entries(
    *,
    repo: Path,
    source_sha: str,
    inventory: Mapping[str, Any],
    package: Mapping[str, Any],
    receipt: Mapping[str, Any],
    receipt_root: Path,
) -> tuple[dict[str, bytes], list[dict[str, Any]]]:
    firmware_path = under_root(receipt_root, receipt["firmware"]["file"], label="receipt UF2")
    entries: dict[str, bytes] = {package_uf2_name(package): firmware_path.read_bytes()}
    provenance: dict[str, dict[str, Any]] = {
        package_uf2_name(package): {
            "kind": "build-receipt",
            "target": package["target"],
            "build_manifest_sha256": receipt["build_manifest"]["sha256"],
        }
    }

    guide_kind = "split" if package["split"] else "non_split"
    readme_path = inventory["guides"][guide_kind]
    readme_body, readme_oid = git_blob(repo, source_sha, readme_path)
    readme_archive = "usevia.app/readme.txt"
    entries[readme_archive] = source_sha_envelope(source_sha, readme_body)
    provenance[readme_archive] = {
        "kind": "git-blob-with-source-sha-header-v1",
        "source_path": readme_path,
        "git_blob": readme_oid,
        "source_sha256": sha256_bytes(readme_body),
        "source_size": len(readme_body),
    }

    keycodes_path = inventory["guides"]["via_keycodes"]
    keycodes_body, keycodes_oid = git_blob(repo, source_sha, keycodes_path)
    keycodes_archive = "usevia.app/via_keycodes.txt"
    entries[keycodes_archive] = source_sha_envelope(source_sha, keycodes_body)
    provenance[keycodes_archive] = {
        "kind": "git-blob-with-source-sha-header-v1",
        "source_path": keycodes_path,
        "git_blob": keycodes_oid,
        "source_sha256": sha256_bytes(keycodes_body),
        "source_size": len(keycodes_body),
    }

    for source_path in package["via_json"]:
        body, blob_oid = git_blob(repo, source_sha, source_path)
        parsed = _json_object(body, label=f"{source_sha}:{source_path}")
        if not parsed:
            _fail(f"official VIA JSON is an empty object: {source_path}")
        archive_path = f"usevia.app/{PurePosixPath(source_path).name}"
        entries[archive_path] = body
        provenance[archive_path] = {
            "kind": "git-blob",
            "source_path": source_path,
            "git_blob": blob_oid,
            "source_sha256": sha256_bytes(body),
            "source_size": len(body),
        }

    ordered_records = [
        {
            "path": name,
            "sha256": sha256_bytes(entries[name]),
            "size": len(entries[name]),
            "provenance": provenance[name],
        }
        for name in sorted(entries, key=_entry_sort_key)
    ]
    return entries, ordered_records


def write_deterministic_zip(path: Path, entries: Mapping[str, bytes]) -> None:
    for name in entries:
        safe_relative_path(name, label="ZIP entry")
    path.parent.mkdir(parents=True, exist_ok=True)
    try:
        archive = zipfile.ZipFile(path, mode="x", compression=zipfile.ZIP_STORED, allowZip64=False)
    except FileExistsError:
        _fail(f"refusing to overwrite existing archive: {path}")
    with archive:
        archive.comment = b""
        for name in sorted(entries, key=_entry_sort_key):
            info = zipfile.ZipInfo(name, date_time=ZIP_DATE_TIME)
            info.compress_type = zipfile.ZIP_STORED
            info.create_system = 3
            info.create_version = 20
            info.extract_version = 20
            info.external_attr = ZIP_EXTERNAL_ATTR
            info.internal_attr = 0
            info.flag_bits = 0
            info.extra = b""
            info.comment = b""
            archive.writestr(info, entries[name], compress_type=zipfile.ZIP_STORED)


def _stable_build_record(receipt: Mapping[str, Any]) -> dict[str, Any]:
    return {
        "manifest": {
            "sha256": receipt["build_manifest"]["sha256"],
            "size": receipt["build_manifest"]["size"],
            "fields": receipt["build_manifest"]["fields"],
        },
        "elf": {
            "sha256": receipt["elf"]["sha256"],
            "size": receipt["elf"]["size"],
        },
        "release_version": receipt["release_version"],
        "release_version_symbol": receipt["release_version_symbol"],
        "release_version_encoding": receipt["release_version_encoding"],
        "elf_witness": receipt["elf_witness"],
    }


def build_release(
    *,
    repo: Path,
    source_sha: str,
    app_repo: Path,
    app_head: str,
    receipt_set_path: Path,
    output_dir: Path,
) -> Path:
    require_source_sha(source_sha)
    repo = repo.resolve(strict=True)
    app_repo = app_repo.resolve(strict=True)
    inventory, inventory_record = load_inventory(repo, source_sha)
    validate_inventory_against_git(repo, source_sha, inventory)
    validator_record = validate_app_checkout(app_repo, app_head)
    receipt_set, receipts = load_receipt_set(
        receipt_set_path=receipt_set_path,
        inventory=inventory,
        source_sha=source_sha,
    )
    del receipt_set  # the random freshness set id must never enter deterministic release bytes
    output_dir = output_dir.resolve(strict=False)
    if output_dir.exists():
        _fail(f"release output directory already exists: {output_dir}")
    output_dir.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=f".{output_dir.name}.tmp-", dir=output_dir.parent))
    receipt_root = receipt_set_path.resolve(strict=True).parent
    package_records: list[dict[str, Any]] = []
    try:
        for package, receipt in zip(inventory["packages"], receipts, strict=True):
            uf2_name = package_uf2_name(package)
            zip_name = package_zip_name(package)
            receipt_uf2 = under_root(receipt_root, receipt["firmware"]["file"], label="receipt UF2")
            uf2_data = receipt_uf2.read_bytes()
            write_exclusive(staging / uf2_name, uf2_data)
            entries, entry_records = expected_archive_entries(
                repo=repo,
                source_sha=source_sha,
                inventory=inventory,
                package=package,
                receipt=receipt,
                receipt_root=receipt_root,
            )
            write_deterministic_zip(staging / zip_name, entries)
            package_records.append(
                {
                    "stem": package["stem"],
                    "target": package["target"],
                    "variant": "standard",
                    "split": package["split"],
                    "via_json": list(package["via_json"]),
                    "build": _stable_build_record(receipt),
                    "outputs": {
                        "uf2": {
                            "path": uf2_name,
                            "sha256": sha256_bytes(uf2_data),
                            "size": len(uf2_data),
                            "provenance": {
                                "kind": "build-receipt",
                                "build_manifest_sha256": receipt["build_manifest"]["sha256"],
                            },
                        },
                        "zip": {
                            "path": zip_name,
                            "sha256": sha256_file(staging / zip_name),
                            "size": (staging / zip_name).stat().st_size,
                            "compression": "ZIP_STORED",
                            "entries": entry_records,
                        },
                    },
                }
            )

        guide_records = {
            name: git_blob_record(repo, source_sha, path) for name, path in sorted(inventory["guides"].items())
        }
        release_manifest = {
            "schema_version": 1,
            "release": RELEASE,
            "source": {
                "sha": source_sha,
                "annotated_tag": inventory["annotated_tag"],
                "inventory": inventory_record,
                "guides": guide_records,
                "guide_envelope": {
                    "format": "source-sha-header-v1",
                    "header": SOURCE_SHA_HEADER.format(source_sha=source_sha),
                },
            },
            "firmware_version": dict(inventory["firmware_version"]),
            "via_validator": validator_record,
            "zip_metadata": dict(inventory["zip"]),
            "packages": package_records,
        }
        write_exclusive(staging / "release-manifest.json", canonical_json_bytes(release_manifest))
        expected_outputs = {"release-manifest.json"}
        for package in inventory["packages"]:
            expected_outputs.add(package_uf2_name(package))
            expected_outputs.add(package_zip_name(package))
        actual_outputs = _tree_regular_files(staging)
        if actual_outputs != expected_outputs:
            _fail(f"packager created an unexpected output set: {sorted(actual_outputs ^ expected_outputs)}")
        os.replace(staging, output_dir)
    except BaseException:
        shutil.rmtree(staging, ignore_errors=True)
        raise
    return output_dir / "release-manifest.json"


def verify_git_release_state(repo: Path, source_sha: str, annotated_tag: str) -> None:
    require_source_sha(source_sha)
    repo = repo.resolve(strict=True)
    status_output = str(_git(repo, ["status", "--porcelain=v1", "--untracked-files=all"]))
    if status_output:
        _fail("release verification requires a clean working tree")
    branch = str(_git(repo, ["symbolic-ref", "--quiet", "--short", "HEAD"])).strip()
    if branch != "main":
        _fail(f"release verification must run with local main checked out, not {branch!r}")
    refs = {
        "HEAD": str(_git(repo, ["rev-parse", "HEAD"])).strip(),
        "main": str(_git(repo, ["rev-parse", "refs/heads/main"])).strip(),
        "origin/main": str(_git(repo, ["rev-parse", "refs/remotes/origin/main"])).strip(),
    }
    for name, value in refs.items():
        if value != source_sha:
            _fail(f"{name} is {value}, not release source {source_sha}")
    tag_ref = f"refs/tags/{annotated_tag}"
    if str(_git(repo, ["cat-file", "-t", tag_ref])).strip() != "tag":
        _fail(f"release tag is not annotated: {annotated_tag}")
    tag_body = str(_git(repo, ["cat-file", "-p", tag_ref]))
    header: dict[str, str] = {}
    for line in tag_body.splitlines():
        if not line:
            break
        key, _, value = line.partition(" ")
        header[key] = value
    if header.get("type") != "commit" or header.get("object") != source_sha:
        _fail(f"annotated tag {annotated_tag} does not directly target source SHA {source_sha}")


def validate_zip_metadata(info: zipfile.ZipInfo) -> None:
    if info.compress_type != zipfile.ZIP_STORED:
        _fail(f"ZIP entry is not ZIP_STORED: {info.filename}")
    if info.date_time != ZIP_DATE_TIME:
        _fail(f"ZIP entry timestamp is not fixed: {info.filename}")
    if info.create_system != 3 or info.create_version != 20 or info.extract_version != 20:
        _fail(f"ZIP entry platform/version metadata differs: {info.filename}")
    if info.external_attr != ZIP_EXTERNAL_ATTR or info.internal_attr != 0:
        _fail(f"ZIP entry mode/attributes differ: {info.filename}")
    if info.extra or info.comment:
        _fail(f"ZIP entry carries extra metadata: {info.filename}")
    if info.flag_bits != 0:
        _fail(f"ZIP entry carries unexpected flags: {info.filename}")


def inspect_zip(path: Path, expected_names: Iterable[str]) -> dict[str, bytes]:
    expected = sorted(expected_names, key=_entry_sort_key)
    with zipfile.ZipFile(path, "r") as archive:
        if archive.comment:
            _fail(f"ZIP archive comment is forbidden: {path}")
        infos = archive.infolist()
        names = [info.filename for info in infos]
        for name in names:
            safe_relative_path(name, label="ZIP entry")
            if name.endswith("/"):
                _fail(f"explicit ZIP directory entry is forbidden: {name}")
        if len(names) != len(set(names)):
            _fail(f"duplicate ZIP entry name in {path}")
        if names != expected:
            _fail(f"ZIP entry set/order differs in {path}; expected={expected}, actual={names}")
        result: dict[str, bytes] = {}
        for info in infos:
            validate_zip_metadata(info)
            result[info.filename] = archive.read(info)
        return result


def fresh_extract(entries: Mapping[str, bytes], destination: Path) -> None:
    if destination.exists():
        _fail(f"fresh extraction destination already exists: {destination}")
    destination.mkdir(parents=True)
    for name in sorted(entries, key=_entry_sort_key):
        path = under_root(destination, name, label="extraction path")
        path.parent.mkdir(parents=True, exist_ok=True)
        write_exclusive(path, entries[name])
    actual = _tree_regular_files(destination)
    if actual != set(entries):
        _fail("fresh extraction did not produce the exact entry set")


def _run_via_validator(
    *,
    app_repo: Path,
    app_head: str,
    json_paths: Sequence[Path],
    validator_command: Sequence[str],
) -> dict[str, Any]:
    if not validator_command:
        _fail("the command for the external authored VIA V3 validator is empty")
    if len(json_paths) != 25 or len({path.resolve() for path in json_paths}) != 25:
        _fail("the external VIA V3 validator must receive exactly 25 unique extracted JSON paths")
    expected_record = validate_app_checkout(app_repo, app_head)
    env = os.environ.copy()
    env.update(
        {
            "ERA_RELEASE": RELEASE,
            "ERA_RELEASE_APP_HEAD": app_head,
            "ERA_RELEASE_APP_REPO": str(app_repo),
        }
    )
    output = str(
        _run(
            [
                *validator_command,
                APP_VALIDATOR_REPO_PATH,
                "--format",
                "json",
                "--",
                *(str(path) for path in json_paths),
            ],
            cwd=app_repo,
            text=True,
            env=env,
        )
    )
    try:
        parsed = json.loads(output)
    except json.JSONDecodeError as exc:
        _fail(f"external VIA V3 validator did not return JSON: {exc}")
    if not isinstance(parsed, list) or len(parsed) != 25:
        _fail("external VIA V3 validator JSON result must contain exactly 25 rows")
    for row, expected_path in zip(parsed, json_paths, strict=True):
        if not isinstance(row, dict) or row.get("ok") is not True or not isinstance(row.get("path"), str):
            _fail("external VIA V3 validator JSON contains a non-passing row")
        if Path(row["path"]).resolve() != expected_path.resolve():
            _fail("external VIA V3 validator JSON path/order differs from its 25 inputs")
    if validate_app_checkout(app_repo, app_head) != expected_record:
        _fail("VIA app provenance changed while the external validator ran")
    return expected_record


def _release_output_files(inventory: Mapping[str, Any]) -> set[str]:
    result = {"release-manifest.json"}
    for package in inventory["packages"]:
        result.add(package_uf2_name(package))
        result.add(package_zip_name(package))
    return result


def _compare_file_trees(left: Path, right: Path) -> None:
    left_files = _tree_regular_files(left)
    right_files = _tree_regular_files(right)
    if left_files != right_files:
        _fail(f"deterministic rebuild file set differs: {sorted(left_files ^ right_files)}")
    for relative in sorted(left_files, key=_entry_sort_key):
        if (left / relative).read_bytes() != (right / relative).read_bytes():
            _fail(f"deterministic rebuild is not byte-identical: {relative}")


def verify_release(
    *,
    repo: Path,
    source_sha: str,
    app_repo: Path,
    app_head: str,
    receipt_set_path: Path,
    release_dir: Path,
    validator_command: Sequence[str],
    objdump_command: Sequence[str],
) -> dict[str, int]:
    require_source_sha(source_sha)
    repo = repo.resolve(strict=True)
    app_repo = app_repo.resolve(strict=True)
    inventory, _ = load_inventory(repo, source_sha)
    validate_inventory_against_git(repo, source_sha, inventory)
    verify_git_release_state(repo, source_sha, inventory["annotated_tag"])
    validator_record = validate_app_checkout(app_repo, app_head)
    _, receipts = load_receipt_set(receipt_set_path=receipt_set_path, inventory=inventory, source_sha=source_sha)
    receipt_root = receipt_set_path.resolve(strict=True).parent
    release_dir = release_dir.resolve(strict=True)
    if release_dir.is_symlink() or not release_dir.is_dir():
        _fail(f"release directory is not a real directory: {release_dir}")
    actual_outputs = _tree_regular_files(release_dir)
    expected_outputs = _release_output_files(inventory)
    if actual_outputs != expected_outputs:
        _fail(f"release output set differs; missing={sorted(expected_outputs - actual_outputs)}, extra={sorted(actual_outputs - expected_outputs)}")
    if _tree_directories(release_dir):
        _fail(f"release output contains directories: {sorted(_tree_directories(release_dir))}")
    forbidden = sorted(path for path in actual_outputs if path.lower().endswith(FORBIDDEN_RELEASE_SUFFIXES))
    if forbidden:
        _fail(f"forbidden release artifacts are present: {forbidden}")

    manifest_path = release_dir / "release-manifest.json"
    manifest = _canonical_json_object(manifest_path, label="release manifest")
    if manifest.get("release") != RELEASE or manifest.get("source", {}).get("sha") != source_sha:
        _fail("release manifest release/source identity differs")
    if manifest.get("firmware_version") != EXPECTED_VERSION:
        _fail("release manifest VERSION tuple differs")
    if manifest.get("via_validator") != validator_record:
        _fail("release manifest VIA app HEAD/script provenance differs")
    if manifest.get("zip_metadata") != EXPECTED_ZIP:
        _fail("release manifest ZIP metadata differs")
    package_manifest = manifest.get("packages")
    if not isinstance(package_manifest, list) or [item.get("stem") for item in package_manifest] != [package["stem"] for package in inventory["packages"]]:
        _fail("release manifest package inventory differs")

    json_count = 0
    extracted_json_paths: list[Path] = []
    with tempfile.TemporaryDirectory(prefix="era-release-verify-extract-") as extraction_temp:
        extraction_root = Path(extraction_temp)
        for package, receipt, manifest_package in zip(inventory["packages"], receipts, package_manifest, strict=True):
            witness_path = under_root(receipt_root, receipt["elf"]["file"], label="receipt ELF")
            witness = read_release_version_witness(
                witness_path,
                inventory["firmware_version"],
                objdump_command=objdump_command,
            )
            if witness != receipt["elf_witness"] or witness != manifest_package.get("build", {}).get("elf_witness"):
                _fail(f"{package['stem']} ELF VERSION witness differs from receipt/manifest")

            receipt_uf2 = under_root(receipt_root, receipt["firmware"]["file"], label="receipt UF2").read_bytes()
            root_uf2_path = release_dir / package_uf2_name(package)
            root_uf2 = root_uf2_path.read_bytes()
            if root_uf2 != receipt_uf2:
                _fail(f"{package['stem']} root UF2 is not byte-equal to its receipt")
            uf2_record = manifest_package.get("outputs", {}).get("uf2", {})
            if uf2_record.get("path") != root_uf2_path.name or uf2_record.get("size") != len(root_uf2) or uf2_record.get("sha256") != sha256_bytes(root_uf2):
                _fail(f"{package['stem']} UF2 manifest hash/size differs")

            expected_entries, expected_entry_records = expected_archive_entries(
                repo=repo,
                source_sha=source_sha,
                inventory=inventory,
                package=package,
                receipt=receipt,
                receipt_root=receipt_root,
            )
            zip_path = release_dir / package_zip_name(package)
            zip_record = manifest_package.get("outputs", {}).get("zip", {})
            if zip_record.get("path") != zip_path.name or zip_record.get("size") != zip_path.stat().st_size or zip_record.get("sha256") != sha256_file(zip_path):
                _fail(f"{package['stem']} ZIP manifest hash/size differs")
            if zip_record.get("compression") != "ZIP_STORED" or zip_record.get("entries") != expected_entry_records:
                _fail(f"{package['stem']} ZIP entry provenance differs")
            archived = inspect_zip(zip_path, expected_entries)
            if archived != expected_entries:
                _fail(f"{package['stem']} ZIP bytes differ from source/receipt provenance")
            if archived[package_uf2_name(package)] != receipt_uf2:
                _fail(f"{package['stem']} archived UF2 is not byte-equal to its receipt")

            extraction_dir = extraction_root / package["stem"]
            fresh_extract(archived, extraction_dir)
            source_by_name = {PurePosixPath(path).name: path for path in package["via_json"]}
            for json_name, source_path in sorted(source_by_name.items()):
                extracted_json = extraction_dir / "usevia.app" / json_name
                parsed = _json_object(extracted_json.read_bytes(), label=f"extracted {json_name}")
                if not parsed:
                    _fail(f"extracted VIA JSON is an empty object: {json_name}")
                if source_path not in package["via_json"]:
                    _fail(f"extracted VIA JSON source mapping differs: {source_path}")
                extracted_json_paths.append(extracted_json)
                json_count += 1

        if _run_via_validator(
            app_repo=app_repo,
            app_head=app_head,
            json_paths=extracted_json_paths,
            validator_command=validator_command,
        ) != validator_record:
            _fail("external VIA V3 validator provenance differs from the release manifest")

    with tempfile.TemporaryDirectory(prefix="era-release-rebuild-") as rebuild_parent:
        rebuilt_dir = Path(rebuild_parent) / "release"
        build_release(
            repo=repo,
            source_sha=source_sha,
            app_repo=app_repo,
            app_head=app_head,
            receipt_set_path=receipt_set_path,
            output_dir=rebuilt_dir,
        )
        _compare_file_trees(release_dir, rebuilt_dir)
    return {"packages": 22, "via_json": json_count, "output_files": len(expected_outputs)}
