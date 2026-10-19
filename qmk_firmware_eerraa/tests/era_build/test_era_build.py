#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Host frontend tests: no WSL, QMK toolchain, or shell is required.

Run: python -m unittest discover -s tests/era_build -v
"""
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("era_build", ROOT / "keyboards/era/common/tools/era_build.py")
era = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(era)


class ProfileTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.path = Path(self.temporary.name) / "profile.json"
        self.valid = {"schema": 1, "backend": "wsl", "wsl": {"distro": "Test Distro", "buildRoot": "/builds"}}

    def test_round_trip_and_local(self):
        era.write_profile(self.path, self.valid)
        self.assertEqual(era.load_profile(self.path), self.valid)
        local = {"schema": 1, "backend": "local", "local": {"buildRoot": "/builds"}}
        self.assertEqual(era.validate_profile(local, self.path), local)

    def test_default_and_override_locations(self):
        home = Path(self.temporary.name)
        self.assertEqual(era.profile_path({}, False, home), home / ".config/era-build/profile.json")
        self.assertEqual(era.profile_path({"XDG_CONFIG_HOME": str(home / "xdg")}, False, home),
                         home / "xdg/era-build/profile.json")
        self.assertEqual(era.profile_path({"APPDATA": str(home)}, True, home),
                         home / "era-build/profile.json")
        self.assertEqual(era.profile_path({"ERA_BUILD_PROFILE": str(self.path)}, True, home), self.path)

    def test_missing_and_invalid_json_include_repair_command(self):
        for data in (None, "{not json"):
            if data is not None:
                self.path.write_text(data, encoding="utf-8")
            with self.assertRaises(era.BuildError) as raised:
                era.load_profile(self.path)
            self.assertIn(str(self.path), str(raised.exception))
            self.assertIn("field $", str(raised.exception))
            self.assertIn("profile init --backend wsl --distro NAME --build-root POSIX_PATH", str(raised.exception))

    def test_unknown_keys_and_invalid_values_are_refused(self):
        cases = [
            ([], "$"), ({}, "schema"),
            (dict(self.valid, schema=True), "schema"),
            (dict(self.valid, schema=2), "schema"),
            (dict(self.valid, backend="other"), "backend"),
            (dict(self.valid, surprise=True), "surprise"),
            (dict(self.valid, local={}), "local"),
            (dict(self.valid, wsl=None), "wsl"),
        ]
        for field, value in (("distro", ""), ("distro", None), ("distro", "a\nb"),
                             ("buildRoot", "/"), ("buildRoot", "relative"),
                             ("buildRoot", "/a/../b"), ("buildRoot", 3),
                             ("buildRoot", "/a\\b"), ("extra", True)):
            section = dict(self.valid["wsl"], **{field: value})
            cases.append((dict(self.valid, wsl=section), "wsl." + field))
        for value, field in cases:
            with self.subTest(value=value), self.assertRaises(era.BuildError) as raised:
                era.validate_profile(value, self.path)
            self.assertIn(str(self.path), str(raised.exception))
            self.assertIn("field " + field, str(raised.exception))
            self.assertIn("profile init", str(raised.exception))

    def test_local_profile_repair_command_has_no_distro(self):
        with self.assertRaises(era.BuildError) as raised:
            era.validate_profile({"schema": 1, "backend": "local", "local": {"buildRoot": "."}}, self.path)
        self.assertIn("profile init --backend local --build-root POSIX_PATH", str(raised.exception))


class BackendTests(unittest.TestCase):
    def result(self, stdout=b"", returncode=0):
        return subprocess.CompletedProcess([], returncode, stdout=stdout, stderr=b"")

    def test_wsl_translation_is_an_argv_not_a_shell(self):
        backend = era.WslBackend({"distro": "Test Distro", "buildRoot": "/builds"})
        native = str(Path("edit with spaces").resolve())
        with patch.object(era.subprocess, "run", return_value=self.result(b"/translated/edit\n")) as run:
            self.assertEqual(backend.host_path(native), "/translated/edit")
        self.assertEqual(run.call_args.args[0],
                         ["wsl.exe", "-d", "Test Distro", "--exec", "wslpath", "-a", "-u", native])

    def test_wsl_cwd_and_environment_are_explicit(self):
        backend = era.WslBackend({"distro": "Test Distro", "buildRoot": "/builds"})
        with patch.object(era.subprocess, "run", return_value=self.result()) as run:
            backend.run(["tool", "space ; $(not shell)"], cwd="/builds/repo space",
                        env={"ERA_EDIT_HEAD": "abc"}, input=b"delta\0")
        argv = run.call_args.args[0]
        self.assertEqual(argv[:8], ["wsl.exe", "-d", "Test Distro", "--cd",
                                   "/builds/repo space", "--exec", "/usr/bin/env", "-i"])
        self.assertIn("ERA_EDIT_HEAD=abc", argv)
        self.assertEqual(argv[-2:], ["tool", "space ; $(not shell)"])
        self.assertEqual(run.call_args.kwargs["input"], b"delta\0")
        self.assertNotIn("shell", run.call_args.kwargs)
        self.assertEqual(backend.describe(), "wsl:Test Distro")

    def test_local_identity_cwd_and_environment(self):
        backend = era.LocalBackend({"buildRoot": "/builds"})
        backend.hostname = "test-host"
        with patch.object(era.subprocess, "run", return_value=self.result()) as run:
            backend.run(["tool", "a b"], cwd="/builds/repo", env={"ONE": "1"})
        self.assertEqual(backend.host_path("/edit"), "/edit")
        self.assertEqual(run.call_args.args[0], ["tool", "a b"])
        self.assertEqual(run.call_args.kwargs["cwd"], "/builds/repo")
        self.assertEqual(run.call_args.kwargs["env"], dict(backend.environment, ONE="1"))
        self.assertEqual(backend.describe(), "local:test-host")

    def test_bootstrap_uses_backend_home_not_host_profile(self):
        backend = era.WslBackend({"distro": "Test", "buildRoot": "/builds"})
        metadata = json.dumps(dict(home="/users/build", user="build", hostname="test", platform="linux")).encode()
        with patch.object(era.subprocess, "run", return_value=self.result(metadata)) as run:
            backend.connect()
        self.assertEqual(backend.environment["HOME"], "/users/build")
        self.assertEqual(backend.environment["PATH"], "/users/build/.local/bin:/users/build/bin:" + era.SYSTEM_PATH)
        self.assertNotIn("HOME=", " ".join(run.call_args.args[0]))
        self.assertNotIn("bash", run.call_args.args[0])

    def test_required_commands_follow_launcher(self):
        commands = era.required_commands()
        for command in ("qmk", "python3", "rsync", "git", "arm-none-eabi-objcopy", "sha256sum"):
            self.assertIn(command, commands)

    def test_registered_implementation_owns_its_profile_without_frontend_branches(self):
        class AnotherBackend(era.LocalBackend):
            pass

        profile = {"schema": 1, "backend": "another", "another": {"buildRoot": "/builds"}}
        with patch.dict(era.BACKENDS, {"another": AnotherBackend}):
            self.assertEqual(era.validate_profile(profile, Path("profile.json")), profile)
            arguments = era.parser().parse_args([
                "profile", "init", "--backend", "another", "--build-root", "/builds",
            ])
            self.assertEqual(AnotherBackend.profile_section(arguments, Path("profile.json")),
                             profile["another"])

    def test_unreachable_and_nonlocal_filesystem_fail_doctor(self):
        profile = {"schema": 1, "backend": "wsl", "wsl": {"distro": "Test", "buildRoot": "/builds"}}
        backend = era.WslBackend(profile["wsl"])
        with patch.dict(era.BACKENDS, {"wsl": lambda section: backend}), \
                patch.object(backend, "connect", side_effect=era.BuildError("offline")), \
                contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertIsNone(era.validate_environment(profile, Path("profile.json")))
        self.assertIn("FAIL backend reachability", output.getvalue())
        with patch.dict(era.BACKENDS, {"wsl": lambda section: backend}), \
                patch.object(backend, "connect"), \
                patch.object(backend, "python", side_effect=[
                    self.result(json.dumps({"git": "/usr/bin/git", "rsync": None}).encode()),
                    self.result(b'"/mounted"'),
                ]), patch.object(backend, "run", return_value=self.result(b"9p\n")), \
                contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertIsNone(era.validate_environment(profile, Path("profile.json")))
        self.assertIn("FAIL tool rsync", output.getvalue())
        self.assertIn("FAIL wsl.buildRoot filesystem", output.getvalue())


class StatusTests(unittest.TestCase):
    def test_nul_parsing_with_rename_copy_deletion_and_odd_names(self):
        records = era.parse_porcelain(
            b" M path with spaces\0R  new\nname\0old name\0C  copy\0original\0"
            b" D removed\0D  staged-delete\0?? untracked\0"
        )
        self.assertEqual(records, [
            (" M", "path with spaces", None), ("R ", "new\nname", "old name"),
            ("C ", "copy", "original"), (" D", "removed", None),
            ("D ", "staged-delete", None), ("??", "untracked", None),
        ])

    def test_staging_and_rename_representations_normalize_equally(self):
        edit = era.parse_porcelain(b"R  new\0old\0M  file\0AD vanished\0MM reverted\0")
        replay = era.parse_porcelain(b" D old\0?? new\0 M file\0")
        self.assertEqual(era.normalized_status(edit, b"A\0new\0D\0old\0M\0file\0"),
                         era.normalized_status(replay, b"D\0old\0M\0file\0"))
        self.assertEqual(era.normalized_status([], b""), [])

    def test_truncation_unsafe_paths_and_conflicts_refuse(self):
        for data in (b"R  dest\0", b" M partial", b"?? ../escape\0", b"UU conflicted\0"):
            with self.subTest(data=data), self.assertRaises(era.BuildError):
                era.parse_porcelain(data)

    def test_repository_selected_by_cwd_and_main_checkout_name(self):
        root = str(Path("linked edit").resolve())
        main = str(Path("main-repository").resolve())
        common = str(Path(main) / ".git")
        outputs = [root.encode(), common.encode(), f"worktree {main}\0HEAD abc\0\0".encode(), b"a" * 40]
        with patch.object(era.subprocess, "run", side_effect=[
            subprocess.CompletedProcess([], 0, stdout=data, stderr=b"") for data in outputs
        ]) as run:
            repository = era.Repository.from_cwd()
        self.assertEqual(repository.root, Path(root))
        self.assertEqual(repository.name, "main-repository")
        self.assertEqual(repository.common, Path(common))
        self.assertEqual(run.call_args_list[0].args[0], ["git", "rev-parse", "--show-toplevel"])
        self.assertEqual(era.parser().parse_args(["build", "era/sirind/tomak:via", "--show-options"]).variants, [])


class PortableBackend:
    """Execute filesystem primitives with this test interpreter, not python3."""
    def python(self, code, *args, check=True):
        result = subprocess.run([sys.executable, "-c", code, *args], capture_output=True)
        if check and result.returncode:
            raise era.BuildError(result.stderr.decode("utf-8", "replace"))
        return result

    def host_path(self, path):
        return str(path)


class LockTests(unittest.TestCase):
    def test_lock_refuses_another_worktree_and_releases_on_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            backend = PortableBackend()
            first = era.BuildLock(backend, directory)
            with self.assertRaisesRegex(RuntimeError, "intentional"):
                with first:
                    with self.assertRaises(era.BuildError) as raised:
                        with era.BuildLock(backend, directory):
                            self.fail("second build entered")
                    self.assertIn(first.path, str(raised.exception))
                    self.assertIn(first.owner, str(raised.exception))
                    raise RuntimeError("intentional")
            self.assertFalse(Path(first.path).exists())
            with era.BuildLock(backend, directory):
                pass

    def test_existing_lock_is_never_removed_by_failed_acquisition(self):
        with tempfile.TemporaryDirectory() as directory:
            lock = era.BuildLock(PortableBackend(), directory)
            path = Path(lock.path)
            path.parent.mkdir()
            path.write_text("another owner", encoding="utf-8")
            with self.assertRaises(era.BuildError):
                with lock:
                    pass
            self.assertEqual(path.read_text(encoding="utf-8"), "another owner")


class ArtifactTests(unittest.TestCase):
    def test_return_uses_only_manifest_named_files_and_preserves_manifest(self):
        with tempfile.TemporaryDirectory() as directory:
            edit = Path(directory)
            tree = "/builds/main-repository"
            artifact_dir = tree + "/.era-artifacts/"
            values = {field: artifact_dir + "current." + field for field in era.ARTIFACT_FIELDS}
            manifest = artifact_dir + "current.manifest.txt"
            data = "".join(f"{key}={value}\n" for key, value in values.items()).encode()
            data += b"worktree_dirty=yes\nedit_tree_check=matched by era_build.py\nram0_free_bytes=123\n"
            returned = {}
            backend = PortableBackend()

            def read(code, *args):
                self.assertEqual(args, (manifest,))
                return subprocess.CompletedProcess([], 0, stdout=data, stderr=b"")

            def copy(argv, cwd):
                self.assertEqual(cwd, tree)
                self.assertEqual(argv[:3], ["rsync", "-lt", "--"])
                returned["sources"] = argv[3:-1]
                destination = Path(argv[-1])
                for source in returned["sources"]:
                    name = source.rsplit("/", 1)[-1]
                    (destination / name).write_bytes(data if source == manifest else name.encode())

            backend.python = read
            backend.run = copy
            result_manifest, firmware, fields = era.return_artifacts(backend, tree, edit, manifest)
            self.assertEqual(returned["sources"], [manifest, *values.values()])
            self.assertEqual(result_manifest.read_bytes(), data)
            self.assertEqual(firmware, edit / ".era-artifacts/current.firmware")
            self.assertTrue(firmware.is_file())
            self.assertEqual(fields["ram0_free_bytes"], "123")
            self.assertEqual(era.manifest_from_output(b"noise\nManifest: " + manifest.encode() + b"\n"), manifest)

    def test_missing_or_ambiguous_launcher_manifest_refused(self):
        for output in (b"no manifest\n", b"Manifest: one\nManifest: two\n"):
            with self.assertRaises(era.BuildError):
                era.manifest_from_output(output)

    def test_missing_firmware_and_escaping_artifacts_refused(self):
        backend = PortableBackend()
        with tempfile.TemporaryDirectory() as directory:
            for data in (b"elf=/builds/repo/.era-artifacts/file.elf\n",
                         b"firmware=/outside/file.uf2\nelf=x\nbuild_log=y\n"):
                with patch.object(backend, "python", return_value=subprocess.CompletedProcess([], 0, data, b"")):
                    with self.assertRaises(era.BuildError):
                        era.return_artifacts(backend, "/builds/repo", Path(directory),
                                             "/builds/repo/.era-artifacts/run.manifest.txt")


if __name__ == "__main__":
    unittest.main()
