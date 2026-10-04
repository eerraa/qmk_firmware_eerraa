"""Execute the complete production Apply boundary with actual ERA NVM storage."""
from pathlib import Path
import tempfile
import unittest
from test_persistence import ROOT, HERE, nor_fixture, execute
from test_macro_remount import extract

PRELUDE = r"""
#define main existing_nor_suite_main
#include "nor.c"
#undef main
#define ERA_SPLIT_EEPROM_SYNC_DOMAIN_ERA_CONFIG 3
#define ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_TARGET_DIRTY 1
#define ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_ROUTE_EXCLUSIVE 2
#define ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_SNAPSHOT_DIRTY 4
#define ERA_SPLIT_EEPROM_SYNC_STATUS_STALE 1
#define ERA_SPLIT_EEPROM_SYNC_STATUS_SOURCE_CHANGED 2
#define ERA_SPLIT_EEPROM_SYNC_STATUS_INTEGRITY_FAIL 3
#define ERA_SPLIT_EEPROM_SYNC_STATUS_BUSY 4
#define ERA_HOST_PEER_STORAGE_RUNTIME_HOST_PUSH_DURABLE 5
#define ERA_SPLIT_EEPROM_SYNC_OP_PUSH_CTL_REQ 6
#define ERA_HOST_PEER_STORAGE_RUNTIME_PEER_REVALIDATE 7
#define ERA_HOST_PEER_STORAGE_RETRY_MS 10

typedef uint8_t era_split_eeprom_sync_domain_t;
typedef struct { uint32_t owner_epoch, relation_generation, policy_generation, peer_usb_epoch,
 peer_host_open_generation, peer_host_close_generation, now_ms; } era_host_peer_storage_runtime_context_t;
typedef struct { uint32_t address, size; } era_host_peer_storage_domain_descriptor_t;
static const era_host_peer_storage_domain_descriptor_t g_era_host_peer_storage_domains[] = {{801, 16384}};
static struct { uint32_t domain, owner_epoch, relation_generation, policy_generation, peer_usb_epoch,
 peer_host_open_generation, peer_host_close_generation, image_size, next_chunk, staged_bytes, expected_crc32,
 flags, last_status, state, pending_operation, retry_count, retry_deadline_ms; } g_era_host_peer_storage_runtime;
static struct { bool image_stale, image_valid; } g_era_host_peer_storage_local;
static struct { unsigned integrity_reject_count, apply_count; } g_era_host_peer_storage_diagnostics;
static uint8_t g_era_host_peer_storage_image[16384];
static bool refuse_manifest;
static bool era_host_peer_storage_domain_valid(unsigned domain) { return domain == 0; }
static bool era_host_peer_storage_context_host(const era_host_peer_storage_runtime_context_t *c) { return c != NULL; }
static bool era_host_peer_storage_context_peer(const era_host_peer_storage_runtime_context_t *c) { return c != NULL; }
static unsigned era_host_peer_storage_chunk_length(unsigned size, unsigned chunk) { (void)size; (void)chunk; return 0; }
static unsigned era_split_wire_crc32(const void *data, size_t size) { const uint8_t *p=data; unsigned crc=0; while(size--) crc=crc*31 + *p++; return crc; }
static bool era_host_peer_storage_reserved_era_config_is_zero(void) { return true; }
static bool era_host_peer_storage_apply_publication_preflight(unsigned domain) { return domain == 0; }
static uint8_t era_host_peer_storage_nvm_failure_status(era_nvm_result_t r) { return r == ERA_NVM_RESULT_BUSY ? ERA_SPLIT_EEPROM_SYNC_STATUS_BUSY : ERA_SPLIT_EEPROM_SYNC_STATUS_INTEGRITY_FAIL; }
static void era_split_eeprom_sync_reload_domain_kb(unsigned domain) { (void)domain; }
static bool era_host_peer_storage_publish_current_image(unsigned domain, unsigned crc) { (void)domain; (void)crc; return !refuse_manifest; }
static void era_host_peer_storage_note_domain_converged(unsigned domain, unsigned crc) { (void)domain; (void)crc; }
/* Domain routing is covered by era_via_exact_ms. This seam uses the real MACRO
 * token owner while exercising Apply's actual notification call and order. */
#define era_state_sync_note_storage_domain(domain) ((void)(domain), era_state_sync_note_macro_public_change())
"""
EPILOGUE = r"""
int main(void) {
 for (unsigned host = 0; host < 2; ++host) for (unsigned failure = 0; failure < 3; ++failure) {
    fresh(); macro_seed(); era_state_sync_set_revisions_for_testing(1,1,1);
    memset(&g_era_host_peer_storage_runtime,0,sizeof(g_era_host_peer_storage_runtime));
    memset(&g_era_host_peer_storage_local,0,sizeof(g_era_host_peer_storage_local));
    memset(&g_era_host_peer_storage_diagnostics,0,sizeof(g_era_host_peer_storage_diagnostics));
    memset(g_era_host_peer_storage_image,0,sizeof(g_era_host_peer_storage_image));
    g_era_host_peer_storage_image[0] = 0x62;
    g_era_host_peer_storage_runtime.image_size = g_era_host_peer_storage_runtime.staged_bytes = sizeof(g_era_host_peer_storage_image);
    g_era_host_peer_storage_runtime.expected_crc32 = era_split_wire_crc32(g_era_host_peer_storage_image,sizeof(g_era_host_peer_storage_image));
    era_host_peer_storage_runtime_context_t context = {0};
    refuse_manifest = failure == 1;
    if (failure == 2) { fail_at = programs + 1; fail_mode = 0; }
    bool result = era_host_peer_storage_apply_commit(&context, host != 0);
    assert(result == (failure == 0));
    assert(eeprom_read_byte((void *)801) == (failure == 2 ? 0x31 : 0x62));
    assert(era_state_sync_macro_revision() == (failure == 2 ? 1 : 2));
    assert(g_era_host_peer_storage_diagnostics.apply_count == (failure == 0));
 }
 puts("PASS remote Apply visibility survives manifest failure; storage failure publishes no revision, both roles");
 return 0;
}
"""

class RemoteVisibility(unittest.TestCase):
    def run_boundary(self, mutation=False):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            flags = nor_fixture(out)
            receipts = []
            body = extract(ROOT / 'keyboards/era/common/split/era_host_peer_storage.c', 'era_host_peer_storage_apply_commit', receipts)
            if mutation:
                text = '    era_state_sync_note_storage_domain((uint8_t)domain);'
                self.assertIn(text, body)
                body = body.replace(text, '')
                body = body.replace('    g_era_host_peer_storage_diagnostics.apply_count++;', text + '\n    g_era_host_peer_storage_diagnostics.apply_count++;')
            (out / 'remote.c').write_text(PRELUDE + body + EPILOGUE, encoding='utf-8')
            sources = ['drivers/eeprom/eeprom_driver.c', 'keyboards/era/common/storage/era_eeprom_config_io.c', 'keyboards/era/common/system/era_state_sync.c']
            exe = out / 'remote.exe'
            execute([*flags, str(out/'remote.c'), *[str(ROOT/p) for p in sources], '-o', str(exe)])
            execute([str(exe)], failure='Assertion' if mutation else None)

    def test_remote_apply_post_commit_manifest_failure(self): self.run_boundary()
    def test_negative_old_notification_order(self): self.run_boundary(True)
