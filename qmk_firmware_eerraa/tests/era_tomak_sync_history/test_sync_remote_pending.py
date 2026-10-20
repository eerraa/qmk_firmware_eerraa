#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Real remote-pending consumer and scheduler work expression feed runtime.

The wire, SESSION identity and clock-adoption inputs remain mocked. This test
does not establish that a physical peer received or retained those inputs.
"""
import ast
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
split = root / "keyboards/era/common/split"
system = root / "keyboards/era/common/system"
tree = ast.parse(Path(__file__).with_name("test_sync_begin_pipeline.py").read_text())
strings = {n.targets[0].id: ast.literal_eval(n.value) for n in tree.body
           if isinstance(n, ast.Assign) and isinstance(n.targets[0], ast.Name)
           and isinstance(n.value, ast.Constant) and isinstance(n.value.value, str)}


def function(source, name):
    start = re.search(r"^(?:static )?(?:bool|void) " + name + r"\(", source, re.M).start()
    brace = source.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def implementation(path):
    return "\n".join(s for s in path.read_text().splitlines() if not s.startswith("#include"))


prefix = strings["prefix"]
prefix = prefix.replace(
    "return 4U | (era_tomak_sync_admission_pending() ? 1U : 0U);",
    "return remote_indicator;")
prefix = prefix.replace("static unsigned side,", "static uint8_t remote_indicator;\nstatic unsigned side,")
header = (root / "quantum/via.h").read_text()
enums = "\n".join(re.search(r"enum " + name + r" \{.*?\};", header, re.S).group()
                  for name in ("via_command_id", "via_keyboard_value_id"))
storage = (split / "era_host_peer_storage.c").read_text()
scheduler = (split / "era_split_transport_scheduler.c").read_text()
start = scheduler.index("        bool work = era_host_peer_storage_advertised_pending()")
work = scheduler[start:scheduler.index(";", start) + 1]
extra = r"""
#define ERA_HOST_PEER_STORAGE_INDICATOR_PEER_PENDING 2U
static struct { bool initialized; } g_era_host_peer_storage_local={true};
static struct { uint8_t indicator_bits; } g_era_host_peer_storage_relation;
static bool era_host_peer_storage_advertised_pending(void) { return false; }
"""
main = r"""
int main(void) {
    reset(true,true);
    assert(!era_tomak_sync_admission_pending());
    era_host_peer_storage_note_peer_pending(true);
    remote_indicator=g_era_host_peer_storage_relation.indicator_bits;
    WORK
    assert(work); /* Remote-only edit must not require a local dirty arm. */
    era_tomak_sync_wire_t request;
    assert(era_tomak_sync_runtime_request(work,&request));
    assert(request.operation==ERA_TOMAK_SYNC_WIRE_OFFER && request.target);
    assert(request.owner_epoch==owner[0] && request.relation_generation==relation[0]);
    era_host_peer_storage_note_peer_pending(false);
    assert(!(g_era_host_peer_storage_relation.indicator_bits & 2U));
    puts("PASS actual remote pending consumer -> scheduler work -> ON OFFER with no local mutation");
}
""".replace("WORK", work)
with tempfile.TemporaryDirectory(prefix="tomak-sync-remote-pending-") as directory:
    directory = Path(directory)
    (directory / "matrix.h").write_text(
        "#pragma once\n#define MATRIX_ROWS_PER_HAND 6\n#define MATRIX_COLS 11\n")
    c, exe = directory / "remote.c", directory / "remote.exe"
    c.write_text(prefix + enums + "\n" +
                 implementation(split / "era_tomak_sync_runtime.c") + "\n" +
                 implementation(system / "era_tomak_sync_via.c") + "\n" +
                 strings["cases"].split("int main(void)")[0] + extra +
                 function(storage, "era_host_peer_storage_note_peer_pending") + "\n" + main)
    subprocess.run(["gcc", "-I", str(directory), "-I", str(root), str(c),
                    str(system / "era_tomak_sync_admission.c"),
                    *(str(split / name) for name in
                      ("era_tomak_sync_wire.c", "era_tomak_sync_transition.c", "era_tomak_sync_receiver.c")),
                    "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
