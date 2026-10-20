#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute the production explicit-commit and peer-proof boundaries."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "keyboards/era/common/split/era_host_peer_storage.c").read_text()


def function(name):
    start = source.index(name + "(")
    start = source.rfind("\n", 0, start) + 1
    body = source.index("{", start)
    depth = 1
    end = body + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


prefix = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define ERA_MACRO_REPLACE_ENABLE
typedef unsigned era_split_eeprom_sync_domain_t;
enum { ERA_SPLIT_EEPROM_SYNC_DOMAIN_DYNAMIC_MACRO = 1, DOMAINS = 2 };
static struct { bool initialized; uint32_t dirty_domain_mask; } g_era_host_peer_storage_local;
static struct { uint32_t image_crc32; } g_era_host_peer_storage_manifest[DOMAINS];
static bool metadata_ok = true;
static unsigned proof, metadata_writes, dirty_calls, dirty_domain, quiet;
static bool era_host_peer_storage_domain_valid(unsigned d) { return d < DOMAINS; }
static uint32_t era_host_peer_storage_domain_mask(unsigned d) { return 1U << d; }
static bool era_host_peer_storage_write_convergence_metadata(unsigned d, uint32_t crc) {
    (void)d; (void)crc; metadata_writes++; return metadata_ok;
}
static bool era_host_peer_storage_recency_convergence_can_retire(bool ok) { return ok; }
static void era_host_peer_storage_indicator_retire_provisional_domain(unsigned d) { (void)d; }
static void era_host_peer_storage_note_changed_shadow(unsigned d, bool changed) { (void)d; (void)changed; }
static void era_macro_replace_note_converged(void) { proof++; }
static void era_host_peer_storage_note_domain_dirty(unsigned d, uint16_t ms) {
    dirty_calls++; dirty_domain = d; quiet = ms;
}
"""
cases = r"""
int main(void) {
    era_host_peer_storage_note_macro_commit_ready();
    assert(!dirty_calls);
    g_era_host_peer_storage_local.initialized = true;
    era_host_peer_storage_note_macro_commit_ready();
    assert(dirty_calls == 1 && dirty_domain == 1 && quiet == 0);
    g_era_host_peer_storage_manifest[1].image_crc32 = 0x12345678;
    metadata_ok = false;
    era_host_peer_storage_note_domain_converged(1, 0x12345678);
    assert(!proof);
    metadata_ok = true;
    g_era_host_peer_storage_local.dirty_domain_mask = 2;
    era_host_peer_storage_note_domain_converged(1, 0x12345678);
    assert(!proof);
    g_era_host_peer_storage_local.dirty_domain_mask = 0;
    era_host_peer_storage_note_domain_converged(1, 0x87654321);
    assert(!proof);
    era_host_peer_storage_note_domain_converged(0, 0x12345678);
    assert(!proof);
    era_host_peer_storage_note_domain_converged(1, 0x12345678);
    assert(proof == 1 && metadata_writes == 5);
    era_host_peer_storage_note_domain_converged(DOMAINS, 0);
    assert(proof == 1 && metadata_writes == 5);
    puts("PASS explicit COMMIT ready without quiet delay; peer receipt requires durable same-image proof with no newer dirty write");
}
"""
with tempfile.TemporaryDirectory(prefix="era-macro-boundary-") as directory:
    c = Path(directory) / "boundary.c"
    exe = Path(directory) / "boundary"
    c.write_text(prefix + function("era_host_peer_storage_note_domain_converged")
                 + function("era_host_peer_storage_note_macro_commit_ready") + cases)
    subprocess.run(["cc", str(c), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
