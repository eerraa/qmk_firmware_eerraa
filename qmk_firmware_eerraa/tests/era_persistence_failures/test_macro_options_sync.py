"""Options replacement: production Apply, NOR durability, decoder and CONFIG."""
from pathlib import Path
import re
import tempfile
import unittest

from test_persistence import ROOT, nor_fixture, execute
from test_macro_remount import extract
from test_remote_visibility import PRELUDE


class MacroOptionsSync(unittest.TestCase):
    def test_options_converge_and_failed_apply_keeps_old_runtime(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            flags = nor_fixture(out)
            via = out / 'stubs/via.h'
            via.write_text(via.read_text() + """
#define id_custom_get_value 8U
#define id_custom_set_value 7U
#define id_custom_save 9U
#define id_unhandled 255U
""")
            flags += ['-DDYNAMIC_KEYMAP_MACRO_COUNT=16',
                      '-DERA_STORAGE_DYNAMIC_MACRO_EEPROM_SIZE=16384',
                      '-DERA_MACRO_OPTIONS_ENABLE', '-I' + str(ROOT / 'quantum')]
            prelude = PRELUDE.replace(
                '#define ERA_SPLIT_EEPROM_SYNC_DOMAIN_ERA_CONFIG 3',
                '#include "keyboards/era/common/split/era_split_eeprom_sync.h"\n'
                '#include "keyboards/era/common/storage/era_storage_layout.h"\n'
                '#include "keyboards/era/common/system/era_macro_options.h"\n'
                '#include "quantum/era_macro.h"')
            prelude = prelude.replace('typedef uint8_t era_split_eeprom_sync_domain_t;', '')
            prelude = prelude.replace('{{801, 16384}}',
                '{[ERA_SPLIT_EEPROM_SYNC_DOMAIN_MACRO_OPTIONS] = {ERA_STORAGE_MACRO_OPTIONS_ADDR, ERA_STORAGE_MACRO_OPTIONS_BYTES}}')
            prelude = prelude.replace('domain == 0', 'domain == ERA_SPLIT_EEPROM_SYNC_DOMAIN_MACRO_OPTIONS')
            prelude = prelude.replace(
                'static void era_split_eeprom_sync_reload_domain_kb(unsigned domain) { (void)domain; }',
                'static void reload_options(unsigned domain) { assert(domain == ERA_SPLIT_EEPROM_SYNC_DOMAIN_MACRO_OPTIONS); era_macro_options_init(); }\n'
                '#define era_split_eeprom_sync_reload_domain_kb reload_options')
            prelude = prelude.replace(
                '#define era_state_sync_note_storage_domain(domain) ((void)(domain), era_state_sync_note_macro_public_change())', '')
            storage = ROOT / 'keyboards/era/common/split/era_host_peer_storage.c'
            text = storage.read_text()
            start = text.index('typedef struct {\n    uint32_t baseline_crc32')
            end = text.index('} era_host_peer_storage_baseline_record_t;', start)
            prelude += text[start:end] + '} era_host_peer_storage_baseline_record_t;\n'
            header = (ROOT / 'keyboards/era/common/split/era_host_peer_storage.h').read_text()
            prelude += re.search(r'^#define ERA_HOST_PEER_STORAGE_BASELINE_GUARD_XOR .+$', header, re.M)[0] + '\n'
            prelude += '#include "keyboards/era/common/split/era_split_sync_storage.h"\n'
            start = text.index('#define ERA_HOST_PEER_STORAGE_RECENCY_CONFIG_OFFSET')
            end = text.index('/* A convergence close', start)
            prelude += text[start:end]
            names = ('era_host_peer_storage_baseline_guard',
                     'era_host_peer_storage_read_baseline_record',
                     'era_host_peer_storage_metadata_result_ok',
                     'era_host_peer_storage_counter_config_offset',
                     'era_host_peer_storage_write_convergence_metadata',
                     'era_host_peer_storage_read_divergence_counter',
                     'era_host_peer_storage_write_divergence_counter',
                     'era_host_peer_storage_apply_commit')
            body = '\n'.join(extract(storage, name, []) for name in names)
            cases = r"""
static void set_option(uint8_t value, uint8_t data) {
    uint8_t frame[32] = {7, 0x11, value, 0, data};
    assert(era_macro_options_handle_via(frame, sizeof(frame)) && frame[0] == 7);
}
int main(void) {
    uint8_t source[ERA_STORAGE_MACRO_OPTIONS_BYTES];
    /* Independent half A: user SET/SAVE, then immutable storage transfer. */
    fresh(); era_macro_options_init();
    set_option(2, 0x48); set_option(4, 7);
    uint8_t save[32] = {9, 0x11};
    assert(era_macro_options_handle_via(save, sizeof(save)) && save[0] == 9);
    eeprom_read_block(source, (void *)(uintptr_t)ERA_STORAGE_MACRO_OPTIONS_ADDR, sizeof(source));
    for (unsigned host = 0; host < 2; ++host) {
        /* Independent half B: fresh NOR/public image and runtime table. */
        fresh(); era_macro_options_init();
        assert(era_macro_options_flags(0) == 0 && era_macro_options_repeat_count(0) == 1);
        memset(&g_era_host_peer_storage_runtime, 0, sizeof(g_era_host_peer_storage_runtime));
        g_era_host_peer_storage_runtime.episode_deadline_ms = 1000;
        memset(&g_era_host_peer_storage_local, 0, sizeof(g_era_host_peer_storage_local));
        g_era_host_peer_storage_runtime.domain = ERA_SPLIT_EEPROM_SYNC_DOMAIN_MACRO_OPTIONS;
        g_era_host_peer_storage_runtime.image_size = g_era_host_peer_storage_runtime.staged_bytes = sizeof(source);
        memcpy(g_era_host_peer_storage_image, source, sizeof(source));
        g_era_host_peer_storage_runtime.expected_crc32 = era_split_wire_crc32(source, sizeof(source));
        era_host_peer_storage_runtime_context_t context = {0};
        uint32_t config = era_state_sync_config_revision();
        /* Invalid complete-image proof cannot mutate bytes or runtime. */
        ++g_era_host_peer_storage_runtime.expected_crc32;
        assert(!era_host_peer_storage_apply_commit(&context, host != 0));
        --g_era_host_peer_storage_runtime.expected_crc32;
        assert(era_macro_options_repeat_count(0) == 1 && era_state_sync_config_revision() == config);
        fail_at = programs + 1; fail_mode = 0;
        assert(!era_host_peer_storage_apply_commit(&context, host != 0));
        assert(era_macro_options_repeat_count(0) == 1 && era_state_sync_config_revision() == config);
        assert(eeprom_read_byte((void *)(uintptr_t)ERA_STORAGE_MACRO_OPTIONS_ADDR) == 0);
        fail_at = 0;
        assert(era_host_peer_storage_apply_commit(&context, host != 0));
        assert(era_macro_options_flags(0) == 0x48 && era_macro_options_repeat_count(0) == 7);
        assert(era_state_sync_config_revision() == config + 1 && era_state_sync_macro_revision() == 1);
        uint8_t target[sizeof(source)];
        eeprom_read_block(target, (void *)(uintptr_t)ERA_STORAGE_MACRO_OPTIONS_ADDR, sizeof(target));
        assert(!memcmp(source, target, sizeof(source)));
        reboot(); era_macro_options_init();
        assert(era_macro_options_flags(0) == 0x48 && era_macro_options_repeat_count(0) == 7);
        /* The eighth arbitration counter and baseline have their own durable
         * seats, not the policy/link records or another domain's counter. */
        assert(era_host_peer_storage_write_divergence_counter(6, 3));
        assert(era_host_peer_storage_write_divergence_counter(7, 9));
        assert(era_host_peer_storage_read_divergence_counter(6) == 3);
        assert(era_host_peer_storage_read_divergence_counter(7) == 9);
        assert(era_host_peer_storage_write_convergence_metadata(7, 0x12345678));
        assert(era_host_peer_storage_read_divergence_counter(7) == 0);
        assert(era_host_peer_storage_read_divergence_counter(6) == 3);
        era_host_peer_storage_baseline_record_t baseline;
        assert(era_host_peer_storage_read_baseline_record(&baseline));
        assert(baseline.baseline_crc32[7] == 0x12345678);
        assert(era_state_sync_config_revision() == config + 1);
    }
    puts("PASS options convergence, both Apply roles, CRC refusal, failed durability, immediate reload and CONFIG");
    return 0;
}
"""
            (out / 'options.c').write_text(prelude + body + cases)
            # Enable the real receiving-domain routing without making the NOR
            # fixture invent the scheduler's unrelated notification callbacks.
            state = out / 'state.o'
            execute([*flags, '-DERA_HOST_PEER_STORAGE_V1_ENABLE', '-c',
                     str(ROOT / 'keyboards/era/common/system/era_state_sync.c'), '-o', str(state)])
            sources = ['drivers/eeprom/eeprom_driver.c',
                       'keyboards/era/common/storage/era_eeprom_config_io.c',
                       'keyboards/era/common/system/era_macro_options.c']
            exe = out / 'options'
            execute([*flags, str(out / 'options.c'), str(state),
                     *[str(ROOT / p) for p in sources], '-o', str(exe)])
            execute([str(exe)])
