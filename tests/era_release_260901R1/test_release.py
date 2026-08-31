#!/usr/bin/env python3
"""Focused host tests for the deterministic ERA 260901R1 release tools."""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
TOOLS = REPO_ROOT / "keyboards/era/common/tools"
sys.path.insert(0, str(TOOLS))

import era_release_260901R1_lib as release  # noqa: E402


def run_git(repo: Path, *arguments: str) -> str:
    result = subprocess.run(
        ["git", "-C", str(repo), *arguments],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )
    if result.returncode:
        raise AssertionError(f"git {' '.join(arguments)} failed:\n{result.stderr}")
    return result.stdout.strip()


def write_repo_file(repo: Path, relative: str, data: bytes) -> None:
    path = repo / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def inventory_from_workspace() -> dict:
    path = REPO_ROOT / release.INVENTORY_REPO_PATH
    return json.loads(path.read_bytes())


def create_source_repo(root: Path) -> tuple[Path, str, dict]:
    repo = root / "source"
    repo.mkdir()
    inventory = inventory_from_workspace()
    write_repo_file(
        repo,
        release.INVENTORY_REPO_PATH,
        (REPO_ROOT / release.INVENTORY_REPO_PATH).read_bytes(),
    )
    write_repo_file(repo, inventory["guides"]["non_split"], b"non-split guide\n")
    write_repo_file(repo, inventory["guides"]["split"], b"split guide\n")
    write_repo_file(repo, inventory["guides"]["via_keycodes"], b"keycodes guide\n")
    for package in inventory["packages"]:
        keyboard = release.package_keyboard(package)
        write_repo_file(repo, f"keyboards/{keyboard}/keyboard.json", b'{"processor":"RP2040"}\n')
        for source_path in package["via_json"]:
            body = json.dumps(
                {"name": Path(source_path).name, "vendorId": "0x0001", "productId": "0x0001"},
                sort_keys=True,
            ).encode("utf-8") + b"\n"
            write_repo_file(repo, source_path, body)
    write_repo_file(
        repo,
        "keyboards/era/sirind/brick65/keyboard.json",
        b'{"processor":"atmega32u4"}\n',
    )
    write_repo_file(
        repo,
        "keyboards/era/sirind/brick65/keymaps/via/BRICK65-VIA.json",
        b'{"name":"BRICK65"}\n',
    )
    run_git(repo, "init", "-b", "main")
    run_git(repo, "config", "user.name", "ERA Release Test")
    run_git(repo, "config", "user.email", "era-release-test@example.invalid")
    run_git(repo, "add", ".")
    run_git(repo, "commit", "-m", "fixture source")
    source_sha = run_git(repo, "rev-parse", "HEAD")
    run_git(repo, "update-ref", "refs/remotes/origin/main", source_sha)
    run_git(repo, "tag", "-a", release.RELEASE, "-m", "fixture annotated release")
    return repo, source_sha, inventory


def create_app_repo(root: Path) -> tuple[Path, str]:
    app_repo = root / "the-via-eerraa"
    app_repo.mkdir()
    write_repo_file(
        app_repo,
        release.APP_VALIDATOR_REPO_PATH,
        b"// fixture for bun scripts/validate-external-v3.ts --format json -- <paths>\n",
    )
    run_git(app_repo, "init", "-b", "app-local")
    run_git(app_repo, "config", "user.name", "ERA App Test")
    run_git(app_repo, "config", "user.email", "era-app-test@example.invalid")
    run_git(app_repo, "add", ".")
    run_git(app_repo, "commit", "-m", "fixture external V3 validator")
    return app_repo, run_git(app_repo, "rev-parse", "HEAD")


def build_manifest_bytes(package: dict, source_sha: str, firmware_sha: str, elf_sha: str) -> bytes:
    fields = [
        ("requested_variant", "standard"),
        ("variant", "standard"),
        ("resolved_tuple", release.EXPECTED_BUILD_TUPLE),
        ("compiled_tuple", release.EXPECTED_BUILD_TUPLE),
        ("target", package["target"]),
        ("git_head", source_sha),
        ("worktree_dirty", "no"),
        ("edit_tree_check", "matched by era-sync"),
        ("build_environment", "WSL local filesystem"),
        ("build_date", release.EXPECTED_BUILD_DATE),
        ("qmk_cli_version", "1.1-test"),
        ("qmk_firmware_version", "fixture"),
        ("compiler", "arm-none-eabi-gcc fixture"),
        ("make", "GNU Make fixture"),
        ("firmware", f"/build/{package['stem']}.uf2"),
        ("firmware_format", "uf2"),
        ("firmware_sha256", firmware_sha),
        ("artifact_id", firmware_sha[:16]),
        ("uf2", f"/build/{package['stem']}.uf2"),
        ("uf2_sha256", firmware_sha),
        ("elf", f"/build/{package['stem']}.elf"),
        ("elf_sha256", elf_sha),
        ("residency_gate", "passed"),
        ("ram0_resident_bytes", "200000"),
        ("ram0_free_bytes", "62144"),
        ("vectors_gate", "48 entries, reset slot 0x100002c5 in flash, 47 in SRAM"),
        ("build_log", f"/build/{package['stem']}.build.log"),
        ("command", " qmk compile -e ERA_BUILD_VARIANT=standard"),
    ]
    return "".join(f"{key}={value}\n" for key, value in fields).encode("utf-8")


def create_receipt_set(root: Path, repo: Path, source_sha: str, inventory: dict) -> Path:
    receipt_root = root / "receipt-set"
    set_id = "0123456789abcdef0123456789abcdef"
    receipt_paths: list[Path] = []
    for package in inventory["packages"]:
        stem = package["stem"]
        firmware = b"UF2 fixture: " + package["target"].encode("ascii") + b"\n"
        elf = b"ELF fixture: " + package["target"].encode("ascii") + b"\n"
        firmware_sha = release.sha256_bytes(firmware)
        elf_sha = release.sha256_bytes(elf)
        manifest = build_manifest_bytes(package, source_sha, firmware_sha, elf_sha)
        manifest_fields = release.parse_build_manifest(manifest, label=stem)
        firmware_rel = f"artifacts/{release.package_uf2_name(package)}"
        elf_rel = f"evidence/{stem}-{release.RELEASE}.elf"
        manifest_rel = f"build-manifests/{stem}.manifest.txt"
        receipt_rel = f"receipts/{stem}.receipt.json"
        write_repo_file(receipt_root, firmware_rel, firmware)
        write_repo_file(receipt_root, elf_rel, elf)
        write_repo_file(receipt_root, manifest_rel, manifest)
        receipt = {
            "schema_version": 1,
            "release": release.RELEASE,
            "source_sha": source_sha,
            "receipt_set_id": set_id,
            "target": package["target"],
            "variant": "standard",
            "stem": stem,
            "freshness": {
                "not_before_ns": 0,
                "build_manifest_mtime_ns": 1,
                "firmware_mtime_ns": 1,
                "elf_mtime_ns": 1,
            },
            "build_manifest": {
                "file": manifest_rel,
                "sha256": release.sha256_bytes(manifest),
                "size": len(manifest),
                "fields": release._stable_manifest_provenance(manifest_fields),
            },
            "firmware": {"file": firmware_rel, "sha256": firmware_sha, "size": len(firmware)},
            "elf": {"file": elf_rel, "sha256": elf_sha, "size": len(elf)},
            "release_version": release.RELEASE,
            "release_version_symbol": release.EXPECTED_VERSION["symbol"],
            "release_version_encoding": release.EXPECTED_VERSION["encoding"],
            "elf_witness": {
                "result": "passed",
                "address": "0x20000000",
                "section": ".rodata",
                "size": 9,
                "bytes_hex": (release.RELEASE.encode("ascii") + b"\0").hex(),
            },
        }
        receipt_path = receipt_root / receipt_rel
        write_repo_file(receipt_root, receipt_rel, release.canonical_json_bytes(receipt))
        receipt_paths.append(receipt_path)
    return release.finalize_receipt_set(
        repo=repo,
        source_sha=source_sha,
        receipt_set_id=set_id,
        output_dir=receipt_root,
        receipt_paths=receipt_paths,
    )


def create_fake_objdump(root: Path) -> Path:
    script = root / "fake_objdump.py"
    script.write_text(
        """import pathlib, sys
elf = pathlib.Path(sys.argv[-1]).read_bytes()
if '-t' in sys.argv:
    if b'ABSENT' not in elf:
        print('20000000 g O .rodata 00000009 era_firmware_version')
    if b'DUPLICATE' in elf:
        print('20000020 l O .rodata 00000009 era_firmware_version')
elif '-s' in sys.argv:
    if b'WRONG' in elf:
        print(' 20000000 57524f4e 4757524e 47        WRONGWRNG')
    else:
        print(' 20000000 32363039 30315231 00        260901R1.')
else:
    raise SystemExit(2)
""",
        encoding="utf-8",
    )
    return script


def create_fake_bun(root: Path, *, fail: bool = False) -> Path:
    script = root / ("bun_fail.py" if fail else "bun.py")
    script.write_text(
        """import json, os, pathlib, sys
assert sys.argv[1:5] == ['scripts/validate-external-v3.ts', '--format', 'json', '--']
paths = [pathlib.Path(value) for value in sys.argv[5:]]
assert len(paths) == 25
for path in paths:
    json.loads(path.read_bytes())
assert os.environ['ERA_RELEASE'] == '260901R1'
assert len(os.environ['ERA_RELEASE_APP_HEAD']) == 40
assert pathlib.Path(os.environ['ERA_RELEASE_APP_REPO']).resolve() == pathlib.Path.cwd().resolve()
log = os.environ.get('ERA_VALIDATOR_TEST_LOG')
if log:
    with open(log, 'a', encoding='utf-8') as stream:
        stream.writelines(path.name + '\\n' for path in paths)
print(json.dumps([{'ok': True, 'path': str(path)} for path in paths], sort_keys=True))
"""
        + ("raise SystemExit(7)\n" if fail else ""),
        encoding="utf-8",
    )
    return script


class Release260901R1Test(unittest.TestCase):
    def test_inventory_is_exact_and_git_derived(self) -> None:
        inventory = inventory_from_workspace()
        release.validate_inventory_static(inventory)
        self.assertEqual(len(inventory["packages"]), 22)
        self.assertEqual(sum(len(package["via_json"]) for package in inventory["packages"]), 25)
        with tempfile.TemporaryDirectory() as temp:
            repo, source_sha, _ = create_source_repo(Path(temp))
            from_git, _ = release.load_inventory(repo, source_sha)
            release.validate_inventory_against_git(repo, source_sha, from_git)

    def test_manifest_requires_the_production_vector_pass_record(self) -> None:
        package = inventory_from_workspace()["packages"][0]
        source_sha = "a" * 40
        fields = release.parse_build_manifest(
            build_manifest_bytes(package, source_sha, "b" * 64, "c" * 64),
            label="production-vector-record",
        )
        release.validate_build_manifest(fields, package=package, source_sha=source_sha)
        for invalid in (
            "passed",
            "47 entries, reset slot 0x100002c5 in flash, 46 in SRAM",
            "48 entries, reset slot 0x20000001 in flash, 47 in SRAM",
            "48 entries, reset slot 0x100002c5 in flash, 46 in SRAM",
        ):
            broken = dict(fields)
            broken["vectors_gate"] = invalid
            with self.assertRaisesRegex(release.ReleaseError, "vectors gate"):
                release.validate_build_manifest(broken, package=package, source_sha=source_sha)

    def test_objdump_version_reader_accepts_exact_and_rejects_bad_witnesses(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            reader = create_fake_objdump(root)
            command = [sys.executable, str(reader)]
            exact = root / "exact.elf"
            exact.write_bytes(b"ELF")
            witness = release.read_release_version_witness(
                exact,
                release.EXPECTED_VERSION,
                objdump_command=command,
            )
            self.assertEqual(witness["bytes_hex"], b"260901R1\0".hex())
            for marker, expected in (
                (b"ABSENT", "exactly once"),
                (b"DUPLICATE", "exactly once"),
                (b"WRONG", "bytes are"),
            ):
                elf = root / f"{marker.decode('ascii').lower()}.elf"
                elf.write_bytes(marker)
                with self.assertRaisesRegex(release.ReleaseError, expected):
                    release.read_release_version_witness(
                        elf,
                        release.EXPECTED_VERSION,
                        objdump_command=command,
                    )

    @unittest.skipUnless(
        shutil.which("arm-none-eabi-gcc") and shutil.which("arm-none-eabi-objdump"),
        "QMK ARM toolchain is not on PATH",
    )
    def test_documented_arm_objdump_reads_a_real_elf_symbol(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / "version.c"
            elf = root / "version.o"
            source.write_text('const char era_firmware_version[] = "260901R1";\n', encoding="ascii")
            result = subprocess.run(
                ["arm-none-eabi-gcc", "-c", str(source), "-o", str(elf)],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                check=False,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            witness = release.read_release_version_witness(
                elf,
                release.EXPECTED_VERSION,
                objdump_command=["arm-none-eabi-objdump"],
            )
            self.assertEqual(witness["size"], 9)
            self.assertEqual(witness["bytes_hex"], b"260901R1\0".hex())

    def test_one_manifest_becomes_one_fresh_receipt(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            repo, source_sha, inventory = create_source_repo(root)
            package = inventory["packages"][0]
            artifacts = root / "build-artifacts"
            artifacts.mkdir()
            firmware_bytes = b"fresh UF2"
            elf_bytes = b"fresh ELF"
            launcher_stem = release.launcher_artifact_stem(
                package,
                source_sha,
                release.sha256_bytes(firmware_bytes),
            )
            firmware = artifacts / f"{launcher_stem}.uf2"
            elf = artifacts / f"{launcher_stem}.elf"
            firmware.write_bytes(firmware_bytes)
            elf.write_bytes(elf_bytes)
            manifest_data = build_manifest_bytes(
                package,
                source_sha,
                release.sha256_file(firmware),
                release.sha256_file(elf),
            ).replace(f"/build/{package['stem']}.uf2".encode(), str(firmware).encode()).replace(
                f"/build/{package['stem']}.elf".encode(), str(elf).encode()
            )
            manifest = artifacts / f"{launcher_stem}.manifest.txt"
            manifest.write_bytes(manifest_data)
            reader = create_fake_objdump(root)
            receipt = release.create_receipt(
                repo=repo,
                source_sha=source_sha,
                manifest_path=manifest,
                receipt_set_id="fedcba9876543210fedcba9876543210",
                not_before_ns=0,
                output_dir=root / "one-receipt",
                objdump_command=[sys.executable, str(reader)],
            )
            parsed = json.loads(receipt.read_bytes())
            self.assertEqual(parsed["target"], package["target"])
            self.assertEqual(parsed["elf_witness"]["bytes_hex"], b"260901R1\0".hex())
            self.assertEqual(
                (root / "one-receipt" / parsed["firmware"]["file"]).read_bytes(),
                firmware.read_bytes(),
            )

    def test_packaging_is_byte_deterministic_and_has_exact_zip_surface(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            repo, source_sha, inventory = create_source_repo(root)
            app_repo, app_head = create_app_repo(root)
            receipts = create_receipt_set(root, repo, source_sha, inventory)
            first = root / "release-a"
            second = root / "release-b"
            release.build_release(
                repo=repo,
                source_sha=source_sha,
                app_repo=app_repo,
                app_head=app_head,
                receipt_set_path=receipts,
                output_dir=first,
            )
            release.build_release(
                repo=repo,
                source_sha=source_sha,
                app_repo=app_repo,
                app_head=app_head,
                receipt_set_path=receipts,
                output_dir=second,
            )
            release._compare_file_trees(first, second)
            self.assertEqual(len(release._tree_regular_files(first)), 45)
            manifest = json.loads((first / "release-manifest.json").read_bytes())
            self.assertEqual(manifest["via_validator"], release.validate_app_checkout(app_repo, app_head))
            self.assertEqual(manifest["via_validator"]["app_head"], app_head)
            self.assertEqual(len(manifest["via_validator"]["script"]["sha256"]), 64)
            split = next(package for package in inventory["packages"] if package["stem"] == "TOMAK-TKL")
            with zipfile.ZipFile(first / release.package_zip_name(split)) as archive:
                names = archive.namelist()
                self.assertEqual(
                    names,
                    sorted(
                        [
                            release.package_uf2_name(split),
                            "usevia.app/readme.txt",
                            "usevia.app/via_keycodes.txt",
                            "usevia.app/TOMAK-TKL-L-VIA.json",
                            "usevia.app/TOMAK-TKL-R-VIA.json",
                        ],
                        key=lambda name: name.encode("utf-8"),
                    ),
                )
                body = archive.read("usevia.app/readme.txt")
                source_body, _ = release.git_blob(repo, source_sha, inventory["guides"]["split"])
                self.assertEqual(body, release.source_sha_envelope(source_sha, source_body))
                for info in archive.infolist():
                    release.validate_zip_metadata(info)

    def test_full_verify_runs_25_external_validations_and_rebuilds(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            repo, source_sha, inventory = create_source_repo(root)
            app_repo, app_head = create_app_repo(root)
            receipts = create_receipt_set(root, repo, source_sha, inventory)
            release_dir = root / "release"
            release.build_release(
                repo=repo,
                source_sha=source_sha,
                app_repo=app_repo,
                app_head=app_head,
                receipt_set_path=receipts,
                output_dir=release_dir,
            )
            reader = create_fake_objdump(root)
            bun = create_fake_bun(root)
            validator_log = root / "validator.log"
            old_log = os.environ.get("ERA_VALIDATOR_TEST_LOG")
            os.environ["ERA_VALIDATOR_TEST_LOG"] = str(validator_log)
            try:
                counts = release.verify_release(
                    repo=repo,
                    source_sha=source_sha,
                    app_repo=app_repo,
                    app_head=app_head,
                    receipt_set_path=receipts,
                    release_dir=release_dir,
                    validator_command=[sys.executable, str(bun)],
                    objdump_command=[sys.executable, str(reader)],
                )
            finally:
                if old_log is None:
                    os.environ.pop("ERA_VALIDATOR_TEST_LOG", None)
                else:
                    os.environ["ERA_VALIDATOR_TEST_LOG"] = old_log
            self.assertEqual(counts, {"packages": 22, "via_json": 25, "output_files": 45})
            self.assertEqual(len(validator_log.read_text(encoding="utf-8").splitlines()), 25)
            failing_bun = create_fake_bun(root, fail=True)
            with self.assertRaisesRegex(release.ReleaseError, "command failed"):
                release.verify_release(
                    repo=repo,
                    source_sha=source_sha,
                    app_repo=app_repo,
                    app_head=app_head,
                    receipt_set_path=receipts,
                    release_dir=release_dir,
                    validator_command=[sys.executable, str(failing_bun)],
                    objdump_command=[sys.executable, str(reader)],
                )

    def test_negative_dry_runs_reject_dirty_refs_stale_files_tamper_and_traversal(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            repo, source_sha, inventory = create_source_repo(root)
            app_repo, app_head = create_app_repo(root)
            receipts = create_receipt_set(root, repo, source_sha, inventory)
            release_dir = root / "release"
            release.build_release(
                repo=repo,
                source_sha=source_sha,
                app_repo=app_repo,
                app_head=app_head,
                receipt_set_path=receipts,
                output_dir=release_dir,
            )

            dirty = repo / "untracked.txt"
            dirty.write_text("dirty", encoding="utf-8")
            with self.assertRaisesRegex(release.ReleaseError, "clean working tree"):
                release.verify_git_release_state(repo, source_sha, release.RELEASE)
            dirty.unlink()

            dirty_app = app_repo / "untracked-app.txt"
            dirty_app.write_text("dirty", encoding="utf-8")
            with self.assertRaisesRegex(release.ReleaseError, "clean app working tree"):
                release.validate_app_checkout(app_repo, app_head)
            dirty_app.unlink()
            with self.assertRaisesRegex(release.ReleaseError, "VIA app HEAD"):
                release.validate_app_checkout(app_repo, source_sha)

            run_git(repo, "update-ref", "refs/remotes/origin/main", f"{release.RELEASE}^{{tag}}")
            with self.assertRaisesRegex(release.ReleaseError, "origin/main"):
                release.verify_git_release_state(repo, source_sha, release.RELEASE)
            run_git(repo, "update-ref", "refs/remotes/origin/main", source_sha)

            stale = receipts.parent / "stale.uf2"
            stale.write_bytes(b"stale")
            with self.assertRaisesRegex(release.ReleaseError, "extra"):
                release.load_receipt_set(receipt_set_path=receipts, inventory=inventory, source_sha=source_sha)
            stale.unlink()

            malicious = root / "malicious.zip"
            with zipfile.ZipFile(malicious, "w", compression=zipfile.ZIP_STORED) as archive:
                archive.writestr("../escape", b"bad")
            with self.assertRaisesRegex(release.ReleaseError, "unsafe ZIP entry"):
                release.inspect_zip(malicious, ["../escape"])

            first = inventory["packages"][0]
            uf2 = release_dir / release.package_uf2_name(first)
            original_uf2 = uf2.read_bytes()
            uf2.write_bytes(original_uf2 + b"tampered")
            reader = create_fake_objdump(root)
            bun = create_fake_bun(root)
            with self.assertRaisesRegex(release.ReleaseError, "root UF2"):
                release.verify_release(
                    repo=repo,
                    source_sha=source_sha,
                    app_repo=app_repo,
                    app_head=app_head,
                    receipt_set_path=receipts,
                    release_dir=release_dir,
                    validator_command=[sys.executable, str(bun)],
                    objdump_command=[sys.executable, str(reader)],
                )
            uf2.write_bytes(original_uf2)
            manifest_path = release_dir / "release-manifest.json"
            manifest = json.loads(manifest_path.read_bytes())
            manifest["packages"][0]["outputs"]["uf2"]["sha256"] = "0" * 64
            manifest_path.write_bytes(release.canonical_json_bytes(manifest))
            with self.assertRaisesRegex(release.ReleaseError, "UF2 manifest hash/size"):
                release.verify_release(
                    repo=repo,
                    source_sha=source_sha,
                    app_repo=app_repo,
                    app_head=app_head,
                    receipt_set_path=receipts,
                    release_dir=release_dir,
                    validator_command=[sys.executable, str(bun)],
                    objdump_command=[sys.executable, str(reader)],
                )


if __name__ == "__main__":
    unittest.main(verbosity=2)
