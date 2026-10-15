#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build the current Git worktree, using an external machine profile.

Backend interface: connect() establishes an explicit, profile-independent Linux
environment; host_path(path) translates an absolute host path; run(argv, cwd,
env=None, input=None, check=True) executes argv without a shell in the specified
backend cwd, with the explicit base environment plus env; describe() identifies
the execution environment for manifests. run returns a CompletedProcess with
byte streams. profile_keys, validate_section() and profile_section() own the
backend's profile fields and initialization. Add one implementation and
register it in BACKENDS to add a host.
Synchronization, locking, validation and artifact return do not select backends.

The host's Git, never a Git process traversing a mounted edit tree, owns edit
HEAD/status enumeration. In particular this avoids the very expensive drvfs
untracked scan. The script's own location does not select the edit worktree.
"""

from __future__ import annotations

import argparse
import contextlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import socket
import subprocess
import sys
import tempfile
import time
import uuid

FRONTEND = "keyboards/era/common/tools/era_build.py"
LAUNCHER = "keyboards/era/common/tools/era_qmk_build.sh"
SYSTEM_PATH = "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
LOCAL_FILESYSTEMS = {
    "ext2/ext3", "ext4", "btrfs", "xfs", "f2fs", "zfs", "tmpfs", "ramfs",
    "overlayfs", "jfs", "reiserfs", "bcachefs",
}
ARTIFACT_FIELDS = ("firmware", "elf", "map", "build_log", "gate_log")


class BuildError(RuntimeError):
    pass


def text(data):
    return data.decode("utf-8", errors="surrogateescape").rstrip("\r\n")


def execute(argv, *, cwd=None, env=None, input=None, check=True):
    try:
        result = subprocess.run(
            [str(arg) for arg in argv], cwd=cwd, env=env, input=input,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        )
    except OSError as exc:
        raise BuildError(f"cannot execute {argv[0]}: {exc}") from exc
    if check and result.returncode:
        raise BuildError(
            f"command failed ({result.returncode}): {list(argv)!r}\n"
            f"{text(result.stdout)}\n{text(result.stderr)}"
        )
    return result


def init_command(backend=None):
    prefix = f"python {FRONTEND} profile init"
    if backend == "local":
        return prefix + " --backend local --build-root POSIX_PATH"
    return prefix + " --backend wsl --distro NAME --build-root POSIX_PATH"


def profile_error(path, field, reason, backend=None):
    return BuildError(
        f"profile {path}: field {field}: {reason}\nRun: {init_command(backend)}"
    )


def profile_path(environ=None, windows=None, home=None):
    environ = os.environ if environ is None else environ
    windows = os.name == "nt" if windows is None else windows
    home = Path.home() if home is None else Path(home)
    if "ERA_BUILD_PROFILE" in environ:
        value = environ["ERA_BUILD_PROFILE"]
        if not value.strip():
            raise profile_error("ERA_BUILD_PROFILE", "ERA_BUILD_PROFILE", "empty path")
        return Path(value).expanduser().resolve()
    if windows:
        if not environ.get("APPDATA"):
            raise profile_error("%APPDATA%", "APPDATA", "unset; set ERA_BUILD_PROFILE")
        base = Path(environ["APPDATA"])
    else:
        base = Path(environ.get("XDG_CONFIG_HOME") or home / ".config")
    return (base / "era-build" / "profile.json").resolve()


def validate_profile(value, path):
    def fail(field, reason):
        backend = value.get("backend") if isinstance(value, dict) else None
        raise profile_error(path, field, reason, backend)

    if not isinstance(value, dict):
        fail("$", "expected a JSON object")
    if type(value.get("schema")) is not int or value["schema"] != 1:
        fail("schema", "expected integer 1")
    backend = value.get("backend")
    if not isinstance(backend, str) or backend not in BACKENDS:
        fail("backend", "expected one of: " + ", ".join(BACKENDS))
    for key in value.keys() - {"schema", "backend", backend}:
        fail(key, "unknown key")
    section = value.get(backend)
    if not isinstance(section, dict):
        fail(backend, "expected an object")
    implementation = BACKENDS[backend]
    for key in section.keys() - implementation.profile_keys:
        fail(f"{backend}.{key}", "unknown key")
    implementation.validate_section(
        section, lambda field, reason: fail(f"{backend}.{field}", reason),
    )
    return value


def load_profile(path):
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        raise profile_error(path, "$", str(exc)) from exc
    return validate_profile(value, path)


def write_profile(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        mode="w", encoding="utf-8", newline="\n", dir=path.parent, delete=False,
    ) as stream:
        temporary = Path(stream.name)
        json.dump(value, stream, indent=2)
        stream.write("\n")
    try:
        os.replace(temporary, path)
    finally:
        if temporary.exists():
            temporary.unlink()


BOOTSTRAP = """\
import json, os, pwd, socket, sys
user = pwd.getpwuid(os.getuid())
print(json.dumps(dict(home=user.pw_dir, user=user.pw_name,
                     hostname=socket.gethostname(), platform=sys.platform)))
"""


class Backend:
    profile_keys = {"buildRoot"}

    @staticmethod
    def validate_section(section, fail):
        root = section.get("buildRoot")
        if (not isinstance(root, str) or not PurePosixPath(root).is_absolute()
                or str(PurePosixPath(root)) == "/" or ".." in root.split("/")
                or "\\" in root or any(ord(c) < 32 for c in root)):
            fail("buildRoot", "expected an absolute, non-root POSIX path without '..'")

    @staticmethod
    def profile_section(arguments, path):
        if arguments.distro is not None:
            raise profile_error(
                path, f"{arguments.backend}.distro", "only valid with backend wsl",
                arguments.backend,
            )
        return {"buildRoot": arguments.build_root}

    def __init__(self, section):
        self.build_root = str(PurePosixPath(section["buildRoot"]))
        self.environment = {"PATH": SYSTEM_PATH, "LANG": "C.UTF-8", "LC_ALL": "C.UTF-8"}

    def connect(self):
        info = json.loads(self.run(["python3", "-c", BOOTSTRAP], cwd="/").stdout)
        if info["platform"] != "linux":
            raise BuildError("the backend must execute on Linux, not Windows/MSYS")
        self.hostname = info["hostname"]
        self.environment.update(
            HOME=info["home"], USER=info["user"], LOGNAME=info["user"],
            PATH=f'{info["home"]}/.local/bin:{info["home"]}/bin:{SYSTEM_PATH}',
        )

    def python(self, code, *args, **kwargs):
        return self.run(["python3", "-c", code, *args], cwd="/", **kwargs)


class WslBackend(Backend):
    profile_keys = Backend.profile_keys | {"distro"}

    @staticmethod
    def validate_section(section, fail):
        Backend.validate_section(section, fail)
        distro = section.get("distro")
        if (not isinstance(distro, str) or not distro.strip()
                or distro != distro.strip() or any(ord(c) < 32 for c in distro)):
            fail("distro", "expected a nonempty distribution name")

    @staticmethod
    def profile_section(arguments, path):
        return {"buildRoot": arguments.build_root, "distro": arguments.distro}

    def __init__(self, section):
        super().__init__(section)
        self.distro = section["distro"]

    def host_path(self, path):
        return text(execute([
            "wsl.exe", "-d", self.distro, "--exec", "wslpath", "-a", "-u", str(path),
        ]).stdout)

    def run(self, argv, cwd, env=None, input=None, check=True):
        environment = dict(self.environment, **(env or {}))
        return execute([
            "wsl.exe", "-d", self.distro, "--cd", cwd, "--exec",
            "/usr/bin/env", "-i",
            *(f"{key}={value}" for key, value in sorted(environment.items())), *argv,
        ], input=input, check=check)

    def describe(self):
        return f"wsl:{self.distro}"


class LocalBackend(Backend):
    def connect(self):
        if sys.platform != "linux":
            raise BuildError("backend local requires a native Linux host; no Windows fallback")
        super().connect()

    def host_path(self, path):
        return str(path)

    def run(self, argv, cwd, env=None, input=None, check=True):
        return execute(
            argv, cwd=cwd, env=dict(self.environment, **(env or {})),
            input=input, check=check,
        )

    def describe(self):
        return f"local:{self.hostname}"


BACKENDS = {"wsl": WslBackend, "local": LocalBackend}


def required_commands(launcher=None):
    launcher = Path(launcher) if launcher else Path(__file__).with_name("era_qmk_build.sh")
    source = launcher.read_text(encoding="utf-8")
    match = re.search(r"^for command_name in (.+); do$", source, re.M)
    if not match:
        raise BuildError(f"cannot read required command list from {launcher}")
    return sorted(set(match[1].split()) | {"bash", "python3", "git", "rsync"})


FILESYSTEM_PROBE = """\
import json, os, sys
root = os.path.realpath(sys.argv[1])
while not os.path.exists(root):
    root = os.path.dirname(root)
if not os.path.isdir(root):
    raise SystemExit("buildRoot or its existing ancestor is not a directory: " + root)
print(json.dumps(root))
"""


def validate_environment(profile, path, launcher=None):
    backend = BACKENDS[profile["backend"]](profile[profile["backend"]])
    print(f"PASS profile: {path} (schema 1, {profile['backend']})")
    try:
        backend.connect()
    except (BuildError, ValueError) as exc:
        print(f"FAIL backend reachability: {exc}")
        return None
    print(f"PASS backend reachability: {backend.describe()} (Linux; no login shell)")
    tools = json.loads(backend.python(
        "import json,shutil,sys; print(json.dumps({x:shutil.which(x) for x in sys.argv[1:]}))",
        *required_commands(launcher),
    ).stdout)
    passed = True
    for name, resolved in sorted(tools.items()):
        print(f"{'PASS' if resolved else 'FAIL'} tool {name}: {resolved or 'not found'}")
        passed = passed and bool(resolved)
    try:
        ancestor = json.loads(backend.python(FILESYSTEM_PROBE, backend.build_root).stdout)
        filesystem = text(backend.run(["stat", "-f", "-c", "%T", "--", ancestor], cwd="/").stdout)
        if filesystem not in LOCAL_FILESYSTEMS:
            raise BuildError(f"{filesystem} is not a supported local Linux filesystem ({ancestor})")
        print(f"PASS {profile['backend']}.buildRoot filesystem: {filesystem} ({ancestor})")
    except (BuildError, ValueError) as exc:
        print(f"FAIL {profile['backend']}.buildRoot filesystem: {exc}")
        passed = False
    if not passed:
        print(f"Profile: {path}\nRun after correcting the environment: {init_command(profile['backend'])}")
    return backend if passed else None


class Repository:
    def __init__(self, root, common, main, head):
        self.root, self.common, self.main, self.head = root, common, main, head
        self.name = main.name

    def git(self, *args):
        return execute(["git", "--no-optional-locks", "-C", str(self.root), *args]).stdout

    @classmethod
    def from_cwd(cls):
        # This is deliberately host Git. Neither __file__ nor a backend's cwd
        # can select the edit tree.
        root = Path(text(execute(["git", "rev-parse", "--show-toplevel"]).stdout)).resolve()
        repository = cls(root, None, root, "")
        common = Path(text(repository.git("rev-parse", "--git-common-dir")))
        repository.common = (root / common).resolve()
        worktrees = repository.git("worktree", "list", "--porcelain", "-z").split(b"\0")
        main = os.fsdecode(worktrees[0][len(b"worktree "):])
        repository.main = Path(main).resolve()
        repository.name = repository.main.name
        repository.head = text(repository.git("rev-parse", "HEAD"))
        return repository


def git_path(raw):
    path = raw.decode("utf-8", errors="surrogateescape")
    if not path or path.startswith("/") or ".." in path.split("/") or "\0" in path:
        raise BuildError(f"unsafe Git path: {path!r}")
    return path


def parse_porcelain(data):
    """Parse v1 -z: rename/copy destination comes first, then its source."""
    if data and not data.endswith(b"\0"):
        raise BuildError("truncated porcelain -z status")
    fields = data.split(b"\0")
    records = []
    index = 0
    while index < len(fields) - 1:
        entry = fields[index]
        index += 1
        if len(entry) < 4 or entry[2:3] != b" ":
            raise BuildError(f"invalid porcelain status: {entry!r}")
        status = entry[:2].decode("ascii")
        path = git_path(entry[3:])
        old = None
        if "R" in status or "C" in status:
            if index >= len(fields) - 1:
                raise BuildError("missing rename/copy source in porcelain -z status")
            old = git_path(fields[index])
            index += 1
        if "U" in status or status in ("AA", "DD"):
            raise BuildError(f"unmerged edit path: {path}")
        records.append((status, path, old))
    return records


def normalized_status(records, diff):
    """Compare effective HEAD-to-worktree state, not index staging choices.

    A staged rename equals deletion + addition after replay; a staged change
    reverted in the working file is clean. Git's --no-renames diff supplies the
    effective tracked state, including these cases. Untracked entries still
    come from the single whole-tree status enumeration.
    """
    fields = diff.split(b"\0")
    if fields[-1] or (len(fields) - 1) % 2:
        raise BuildError("invalid --name-status -z diff")
    result = {(fields[i].decode("ascii"), git_path(fields[i + 1]))
              for i in range(0, len(fields) - 1, 2)}
    result.update(("A", path) for status, path, _ in records if status == "??")
    return sorted(result)


def status_snapshot(git):
    records = parse_porcelain(git("status", "--porcelain=v1", "-z", "--untracked-files=all"))
    diff = git("diff", "--no-ext-diff", "--no-renames", "--name-status", "-z", "HEAD", "--")
    return records, normalized_status(records, diff)


LOCK_ACQUIRE = """\
import os, sys
path, owner = sys.argv[1:]
os.makedirs(os.path.dirname(path), exist_ok=True)
try:
    fd = os.open(path, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
except FileExistsError:
    print(open(path, encoding="utf-8").read())
    raise SystemExit(3)
with os.fdopen(fd, "w", encoding="utf-8") as stream:
    stream.write(owner)
"""
LOCK_RELEASE = """\
import os, sys
path, owner = sys.argv[1:]
with open(path, encoding="utf-8") as stream:
    if stream.read() != owner:
        raise SystemExit("lock ownership changed; refusing to remove " + path)
os.unlink(path)
"""


class BuildLock:
    def __init__(self, backend, tree):
        self.backend = backend
        self.path = f"{tree}/.era-artifacts/.era-build.lock"
        self.owner = f"{socket.gethostname()} pid={os.getpid()} token={uuid.uuid4().hex}"

    def __enter__(self):
        result = self.backend.python(LOCK_ACQUIRE, self.path, self.owner, check=False)
        if result.returncode == 3:
            raise BuildError(
                f"build tree lock held: {self.path}\nOwner: {text(result.stdout)}\n"
                "Another worktree may be building. An interrupted run can leave "
                "this lock; confirm no build is running before removing it."
            )
        if result.returncode:
            raise BuildError(f"cannot acquire build lock {self.path}: {text(result.stderr)}")
        return self

    def __exit__(self, *exc):
        self.backend.python(LOCK_RELEASE, self.path, self.owner)


INITIALIZE_TREE = """\
import os, pathlib, subprocess, sys, tempfile
tree, common = map(pathlib.Path, sys.argv[1:])
if not (tree / ".git").exists():
    if set(os.listdir(tree)) - {".era-artifacts"}:
        raise SystemExit("refusing to initialize a nonempty non-Git build tree: " + str(tree))
    # Clone in an allocated temporary directory: the final tree already holds
    # our lock, and git clone correctly refuses a nonempty destination.
    with tempfile.TemporaryDirectory(prefix=".era-clone-", dir=tree.parent) as temporary:
        clone = pathlib.Path(temporary) / "clone"
        subprocess.run(["git", "clone", "--quiet", "--no-checkout", "--", str(common), str(clone)], check=True)
        subprocess.run(["git", "-C", str(clone), "remote", "remove", "origin"], check=True)
        os.replace(clone / ".git", tree / ".git")
"""


def build_tree(backend, repository):
    tree = str(PurePosixPath(backend.build_root) / repository.name)
    # Validate before creating a lock, let alone resetting anything. A profile
    # must never turn a real edit checkout (or any of its linked worktrees) into
    # the disposable build tree.
    candidates = [tree, backend.host_path(repository.root), backend.host_path(repository.main)]
    resolved = json.loads(backend.python(
        "import json,os,sys; print(json.dumps([os.path.realpath(p) for p in sys.argv[1:]]))",
        *candidates,
    ).stdout)
    if resolved[0] in resolved[1:]:
        raise BuildError(f"build tree is an edit checkout; correct profile buildRoot: {tree}")
    return tree


def sync_tree(backend, repository, tree):
    records, expected = status_snapshot(repository.git)
    common = backend.host_path(repository.common)
    backend.python(INITIALIZE_TREE, tree, common)

    def git(*args):
        return backend.run(["git", *args], cwd=tree).stdout

    actual_common = text(git("rev-parse", "--git-common-dir"))
    resolved_common = text(backend.python(
        "import os,sys; print(os.path.realpath(os.path.join(sys.argv[1], sys.argv[2])))",
        tree, actual_common,
    ).stdout)
    edit_common = text(backend.python("import os,sys; print(os.path.realpath(sys.argv[1]))", common).stdout)
    if resolved_common == edit_common:
        raise BuildError(f"build tree shares the edit repository's Git directory: {tree}")

    git("fetch", "--quiet", "--no-tags", common, "+refs/heads/*:refs/remotes/edit/*")
    present = backend.run(["git", "cat-file", "-e", f"{repository.head}^{{commit}}"], cwd=tree, check=False)
    if present.returncode:
        raise BuildError(f"edit HEAD {repository.head} is missing after fetch from {common}; refusing stale build")
    git("checkout", "--quiet", "--force", "--detach", repository.head)
    git("submodule", "update", "--init")
    git("checkout", "--quiet", "--force", ".")
    # No -x: ignored .build and .era-artifacts survive. Explicit exclusions also
    # protect the build cache, artifacts and lock if ignore rules change.
    git("clean", "--quiet", "-fd", "-e", ".build", "-e", ".era-artifacts")

    copy, gone = set(), set()
    for status, path, old in records:
        if old and "R" in status:
            gone.add(old)
        if os.path.lexists(repository.root / path):
            copy.add(path)
        else:
            gone.add(path)
    if gone:
        backend.python(
            "import os,shutil,sys\n"
            "for path in sys.argv[1:]:\n"
            " if os.path.isdir(path) and not os.path.islink(path): shutil.rmtree(path)\n"
            " elif os.path.lexists(path): os.unlink(path)\n",
            *(f"{tree}/{path}" for path in sorted(gone)),
        )
    if copy:
        # -rlt, NEVER -a: drvfs reports mode 777; copying it would manufacture
        # executable-bit changes. Replay only the host Git delta, not the whole
        # edit tree: core.autocrlf can leave unchanged Windows files as CRLF and
        # Linux files as LF. A whole-tree copy would dirty them all. ERA sources
        # are eol=lf in .gitattributes. --from0 preserves even newline filenames.
        backend.run([
            "rsync", "-rlt", "--from0", "--files-from=-", "--exclude=.git",
            backend.host_path(repository.root) + "/", tree + "/",
        ], cwd=tree, input=b"".join(path.encode("utf-8", "surrogateescape") + b"\0" for path in sorted(copy)))
    _, actual = status_snapshot(git)
    # Status equality, not the copied count, is the synchronization check.
    if actual != expected:
        raise BuildError(
            "MISMATCH against edit tree after sync; refusing build\n"
            f"Edit: {expected!r}\nBuild: {actual!r}"
        )
    print(f"era_build.py: synchronized {repository.head} ({len(expected)} uncommitted paths)", file=sys.stderr)


def manifest_from_output(output):
    paths = [line[len("Manifest: "):] for line in text(output).splitlines()
             if line.startswith("Manifest: ")]
    if len(paths) != 1 or not paths[0]:
        raise BuildError("launcher did not report exactly one Manifest: path")
    return paths[0]


def return_artifacts(backend, tree, edit_root, manifest):
    data = backend.python(
        "import pathlib,sys; sys.stdout.buffer.write(pathlib.Path(sys.argv[1]).read_bytes())",
        manifest,
    ).stdout
    fields = {}
    for line in text(data).splitlines():
        key, separator, value = line.partition("=")
        if separator:
            if key in fields:
                raise BuildError(f"duplicate manifest field {key}: {manifest}")
            fields[key] = value
    for key in ("firmware", "elf", "build_log"):
        if not fields.get(key):
            raise BuildError(f"manifest is missing {key}: {manifest}")
    sources = [manifest] + [fields[key] for key in ARTIFACT_FIELDS if fields.get(key)]
    names = {}
    for source in sources:
        path = PurePosixPath(source)
        if (not path.is_absolute() or ".." in path.parts
                or PurePosixPath(tree) / ".era-artifacts" not in path.parents):
            raise BuildError(f"manifest artifact is outside build artifacts: {source}")
        if path.name in names and names[path.name] != source:
            raise BuildError(f"manifest artifact basenames collide: {source}")
        names[path.name] = source
    destination = edit_root / ".era-artifacts"
    destination.mkdir(parents=True, exist_ok=True)
    # The manifest and exactly its declared files, never a directory glob.
    # rsync fails if a named file is absent; a stale output cannot substitute.
    backend.run(
        ["rsync", "-lt", "--", *dict.fromkeys(sources), backend.host_path(destination) + "/"],
        cwd=tree,
    )
    return destination / PurePosixPath(manifest).name, destination / PurePosixPath(fields["firmware"]).name, fields


def parser():
    result = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    commands = result.add_subparsers(dest="command", required=True)
    build = commands.add_parser("build", help="sync, build, gate and return named artifacts")
    build.add_argument("target")
    build.add_argument("variants", nargs="*")
    build.add_argument("--show-options", action="store_true")
    commands.add_parser("sync", help="sync only; print the backend build-tree path")
    commands.add_parser("doctor", help="validate profile, backend, tools and filesystem")
    profile = commands.add_parser("profile").add_subparsers(dest="profile_command", required=True)
    initialize = profile.add_parser("init")
    initialize.add_argument("--backend", choices=tuple(BACKENDS), required=True)
    initialize.add_argument("--distro")
    initialize.add_argument("--build-root", required=True)
    profile.add_parser("show")
    return result


def main(argv=None):
    arguments = parser().parse_args(argv)
    try:
        path = profile_path()
        if arguments.command == "profile" and arguments.profile_command == "init":
            section = BACKENDS[arguments.backend].profile_section(arguments, path)
            profile = validate_profile({"schema": 1, "backend": arguments.backend, arguments.backend: section}, path)
            if validate_environment(profile, path) is None:
                raise BuildError(f"environment validation failed; profile not written: {path}")
            write_profile(path, profile)
            print(f"Profile written: {path}")
            return 0
        profile = load_profile(path)
        if arguments.command == "profile":
            print(f"Profile: {path}")
            print(json.dumps(profile, indent=2))
            return 0
        if arguments.command == "doctor":
            return 0 if validate_environment(profile, path) else 1
        if arguments.command == "build":
            if not re.fullmatch(r"era/[A-Za-z0-9_/-]+:[A-Za-z0-9_-]+", arguments.target):
                raise BuildError(f"invalid target (expected keyboard:keymap): {arguments.target}")
            arguments.variants = arguments.variants or ["standard"]
            for variant in arguments.variants:
                if not re.fullmatch(r"[a-z][a-z0-9_]*", variant):
                    raise BuildError(f"invalid variant spelling: {variant}")
        repository = Repository.from_cwd()
        if path == repository.root or repository.root in path.parents:
            raise profile_error(path, "$", "machine profile must be outside the repository", profile["backend"])
        with contextlib.redirect_stdout(sys.stderr):
            backend = validate_environment(profile, path, repository.root / LAUNCHER)
        if backend is None:
            raise BuildError("environment validation failed; no sync or build attempted")
        tree = build_tree(backend, repository)
        start = time.monotonic()
        with BuildLock(backend, tree):
            sync_tree(backend, repository, tree)
            if arguments.command == "sync":
                print(tree)
                return 0
            summaries = []
            for variant in arguments.variants:
                print(f"\n=== {arguments.target} {variant} ===", flush=True)
                env = {
                    "ERA_BUILD_TREE_SYNCED": "yes", "ERA_EDIT_HEAD": repository.head,
                    "ERA_BUILD_ENVIRONMENT": backend.describe(),
                }
                if "ERA_BUILD_JOBS" in os.environ:
                    env["ERA_BUILD_JOBS"] = os.environ["ERA_BUILD_JOBS"]
                command = ["bash", LAUNCHER, arguments.target, variant]
                if arguments.show_options:
                    command.append("--show-options")
                result = backend.run(command, cwd=tree, env=env, check=False)
                print(text(result.stdout), flush=True)
                if result.stderr:
                    print(text(result.stderr), file=sys.stderr, flush=True)
                if result.returncode:
                    raise BuildError(f"launcher refused or failed ({result.returncode}); no artifacts returned for {variant}")
                manifest, firmware, fields = return_artifacts(
                    backend, tree, repository.root, manifest_from_output(result.stdout),
                )
                print(f"Returned manifest: {manifest}\nReturned firmware: {firmware}")
                summaries.append((variant, firmware, fields))
            print(f"\n=== returned to {repository.root / '.era-artifacts'} ===")
            for variant, firmware, fields in summaries:
                print(f"  {variant:<12} dirty={fields.get('worktree_dirty', ''):<3} "
                      f"edit_tree={fields.get('edit_tree_check', ''):<28} "
                      f"ram0_free={fields.get('ram0_free_bytes') or 'n/a':<7} {firmware.name}")
            print(f"total {int(time.monotonic() - start)}s")
        return 0
    except (BuildError, OSError, ValueError) as exc:
        label = "FAIL profile" if arguments.command == "doctor" else "era_build.py"
        print(f"{label}: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
