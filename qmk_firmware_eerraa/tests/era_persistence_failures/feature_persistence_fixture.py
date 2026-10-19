"""Production report and board-owner fixtures; only peripheral boundaries are modeled."""
from pathlib import Path
import hashlib
import json
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
OWNERS = {
    "keyboards/era/common/features/era_nkro_via.c",
    "keyboards/era/common/features/era_rgb_sleep.c",
    "keyboards/era/newone/common/odessey_common.c",
    "keyboards/era/comm/riley/riley_common.c",
    "quantum/process_keycode/process_magic.c",
}


class Fixture:
    def __init__(self, output, previous=None):
        self.out = Path(output)
        self.out.mkdir(parents=True, exist_ok=True)
        self.previous = Path(previous) if previous else None
        self.sources = []
        self.commands = []
        self.run([sys.executable, str(ROOT / "tests/era_persistence_failures/callers.py"), str(self.out)], "base")
        self.command = json.loads((self.out / "compile-command.json").read_text())
        self.base = (self.out / "callers.c").read_text().split("int main(")[0]

    def run(self, command, label, expected=0, failure=None):
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, encoding="utf-8", errors="replace")
        row = dict(label=label, argv=command, exit_code=result.returncode, stdout=result.stdout, stderr=result.stderr)
        self.commands.append(row)
        (self.out / "feature-commands.json").write_text(json.dumps(self.commands, indent=2), encoding="utf-8")
        if expected == "nonzero":
            assert result.returncode != 0, label + ": missing runtime rejection"
            if failure is not None:
                assert failure in result.stdout + result.stderr, label + ": wrong runtime rejection\n" + result.stdout + result.stderr
        else:
            assert result.returncode == expected, label + ": exit=" + str(result.returncode) + "\n" + result.stdout + result.stderr
        return result

    def source(self, path):
        if self.previous and path in OWNERS:
            candidate = self.previous / path
            if candidate.is_file():
                return candidate.read_text(encoding="utf-8"), str(candidate)
            result = subprocess.run(["git", "show", "HEAD:" + path], cwd=ROOT, capture_output=True, text=True, check=True)
            return result.stdout, "HEAD:" + path
        return (ROOT / path).read_text(encoding="utf-8"), str(ROOT / path)

    def extract(self, path, name):
        source, origin = self.source(path)
        match = re.search(r"^[ \t]*(?:(?:static|inline)\s+)*(?:__attribute__\(\(weak\)\)\s+)?\w+[ \t*]+" + name + r"\([^;{}]*\)\s*\{", source, re.M)
        assert match, name
        depth = 1
        for token in re.finditer(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\n]*|[{}]', source[match.end():], re.S):
            if token[0] == "{":
                depth += 1
            elif token[0] == "}":
                depth -= 1
            if not depth:
                end = match.end() + token.end()
                body = source[match.start():end]
                self.sources.append(dict(path=path, origin=origin, function=name,
                    lines=[source[:match.start()].count("\n") + 1, source[:end].count("\n") + 1],
                    source_sha256=hashlib.sha256(Path(origin).read_bytes() if not origin.startswith("HEAD:") else source.encode()).hexdigest(), body_sha256=hashlib.sha256(body.encode()).hexdigest()))
                return body + "\n"
        raise AssertionError(name)

    def write(self, name, source):
        path = self.out / name
        path.write_text(source, encoding="utf-8")
        return str(path)

    def compile(self, command, label):
        self.write(label + "-compile-command.json", json.dumps(command, indent=2))
        self.write("feature-source-manifest.json", json.dumps(self.sources, indent=2))
        self.run(command, label + "-compile")
        return command[-1]

    def hid(self, mutant=None, magic=False):
        unit = self.base.replace("void clear_keyboard(void) {}", "").replace("void send_keyboard_report(void) {}", "")
        header = (ROOT / "tmk_core/protocol/report.h").read_text()
        first = header.index("typedef struct {\n#ifdef KEYBOARD_SHARED_EP")
        last = header.index("} PACKED report_nkro_t;") + len("} PACKED report_nkro_t;")
        unit += HID_PREAMBLE + header[first:last] + HID_BOUNDARIES
        for name in "add_key_byte del_key_byte add_key_bit del_key_bit add_key_to_report del_key_from_report clear_keys_from_report rebuild_6kro_report".split():
            unit += self.extract("tmk_core/protocol/report.c", name)
        unit += self.extract("quantum/action_util.h", "clear_keys").replace("inline void", "static inline void", 1)
        for name in "clear_keyboard clear_keyboard_but_mods clear_keyboard_but_mods_and_keys".split():
            unit += self.extract("quantum/action.c", name)
        for name in "keyboard_report_filter nkro_report_filter resend_keyboard_report force_send_keyboard_report send_6kro_report send_nkro_report send_keyboard_report".split():
            unit += self.extract("quantum/action_util.c", name)
        unit += self.extract("keyboards/era/common/system/era_hid_report_interval_chibios.c", "era_hid_report_interval_select_format")
        if magic:
            unit += '#include "quantum/keycodes.h"\nvoid eeconfig_update_handedness(bool left) {(void)left;}\n'
            body = self.extract("quantum/process_keycode/process_magic.c", "process_magic")
            if mutant == "magic_old_clear":
                marker = "clear_keyboard(); // release active format before EEPROM may reload it"
                assert marker in body
                body = body.replace(marker, "", 1)
            unit += body
        else:
            unit += "bool process_magic(uint16_t keycode,keyrecord_t *record) {(void)keycode;(void)record;return true;}\n"
        unit += HID_TESTS
        label = "hid" + ("-magic" if magic else "") + ("-" + mutant if mutant else "")
        harness = self.write(label + ".c", unit)
        command = self.command.copy()
        command[command.index(str(self.out / "callers.c"))] = harness
        command.insert(-2, str(ROOT / "keyboards/era/common/system/era_hid_report_interval.c"))
        command[-1] = str(self.out / (label + ".exe"))
        command.insert(1, "-I" + str(ROOT / "keyboards/era/common/features"))
        for path in sorted(OWNERS):
            if "common/features" not in path:
                continue
            original = str(ROOT / path)
            source, _ = self.source(path)
            if mutant == "unsafe_get" and path.endswith("era_nkro_via.c"):
                source = source.replace("case id_custom_get_value:\n", "case id_custom_get_value:\n            eeconfig_read_keymap(&keymap_config);\n", 1)
            if mutant == "durable_compare" and path.endswith("era_nkro_via.c"):
                source = source.replace("bool changed = keymap_config.nkro != enabled;", "eeconfig_read_keymap(&keymap_config);\n    bool changed = keymap_config.nkro != enabled;", 1)
            if self.previous or mutant:
                command[command.index(original)] = self.write(label + "-" + Path(path).name, source)
        return self.compile(command, label)

    def wl(self, board, mutant=None):
        quantum = self.out / "stub/quantum.h"
        content = quantum.read_text()
        if "typedef struct {uint8_t h,s,v;} HSV;" not in content:
            quantum.write_text(content + "\ntypedef struct {uint8_t h,s,v;} HSV;\nvoid eeconfig_flush_rgblight_deferred_task(void);\nvoid eeconfig_flush_rgblight_deferred_now(void);\n", encoding="utf-8")
        (self.out / "stub/rgblight.h").write_text("#pragma once\n#include <stdbool.h>\nbool rgblight_velocikey_enabled(void);\nvoid rgblight_velocikey_toggle(void);\nvoid eeconfig_update_rgblight_current(void);\nvoid eeconfig_flush_rgblight_deferred_task(void);\nvoid eeconfig_flush_rgblight_deferred_now(void);\n", encoding="utf-8")
        eeh = self.out / "stub/eeconfig.h"
        declarations = "\nvoid eeconfig_flush_rgblight_deferred_task(void);\nvoid eeconfig_flush_rgblight_deferred_now(void);\n"
        if "eeconfig_flush_rgblight_deferred_task" not in eeh.read_text():
            eeh.write_text(eeh.read_text() + declarations, encoding="utf-8")
        rgb_header = (ROOT / "quantum/rgblight/rgblight.h").read_text()
        rgb_type = rgb_header[rgb_header.index("typedef union rgblight_config_t"):rgb_header.index("} rgblight_config_t;") + len("} rgblight_config_t;")]
        owner = WL_OWNER_PREAMBLE + rgb_type + WL_RGB_BOUNDARIES
        for path, names in [("quantum/nvm/eeprom/nvm_eeconfig.c", "nvm_eeconfig_update_rgblight"),
                            ("quantum/eeconfig.c", "eeconfig_update_rgblight"),
                            ("quantum/rgblight/rgblight.c", "rgblight_check_config eeconfig_update_rgblight_current rgblight_velocikey_enabled rgblight_velocikey_toggle")]:
            for name in names.split():
                owner += self.extract(path, name)
        helper = ROOT / "keyboards/era/common/features/era_rgblight_persistence.h"
        if mutant:
            source = helper.read_text()
            if mutant == "equal_skip":
                source = source.replace("if (!eeprom_begin_transaction())", "if (enabled == rgblight_velocikey_enabled()) return true;\n    if (!eeprom_begin_transaction())", 1)
            elif mutant == "ignore_receipt":
                source = source.replace("return eeprom_end_transaction();", "(void)eeprom_end_transaction();\n    return true;", 1)
            owner += '#include "' + self.write(board + "-" + mutant + "-helper.h", source).replace("\\", "/") + '"\n'
        else:
            owner += '#include "keyboards/era/common/features/era_rgblight_persistence.h"\n'
        self.sources.append(dict(path=str(helper.relative_to(ROOT)), origin=str(helper), mode="direct header include",
                                 source_sha256=hashlib.sha256(helper.read_bytes()).hexdigest()))
        owner += "void feature_rgb_init(void) {rgblight_config.raw=0;rgblight_config.mode=1;eeconfig_update_rgblight_current();}\n"
        if board == "odessey":
            path = "keyboards/era/newone/common/odessey_common.c"
            owner += '#include "keyboards/era/newone/common/odessey_common.h"\nodessey_config_t g_odessey_config;\nstatic void odessey_apply_indicator(void) {}\n'
            names = "odessey_normalized_indicator_mode odessey_config_set_indicator_mode odessey_set_color odessey_get_color era_board_via_set_value era_board_via_get_value"
            field = "id_custom_velocikey_enable"
            brightness = "id_custom_indicator_brightness"
        else:
            path = "keyboards/era/comm/riley/riley_common.c"
            source, _ = self.source(path)
            enum = source[source.index("enum {"):source.index("};", source.index("enum {")) + 2]
            owner += '#include "keyboards/era/comm/riley/riley_common.h"\n' + enum + '\nriley_config_t g_riley_config;\nstatic void riley_refresh_indicator_layers(int ignored) {(void)ignored;}\nstatic int host_keyboard_led_state(void) {return 0;}\n'
            names = "riley_indicator_mode riley_indicator_only riley_set_indicator_mode riley_set_indicator_only riley_value_to_slot riley_set_color riley_get_color era_board_via_set_value era_board_via_get_value"
            field = "id_custom_riley_velocikey_enable"
            brightness = "id_custom_riley_ind1_brightness"
        for name in names.split():
            owner += self.extract(path, name)
        owner += "uint8_t feature_velocikey_id(void) {return " + field + ";}\n"
        owner += "uint8_t feature_indicator_brightness_id(void) {return " + brightness + ";}\n"
        owner += "void feature_rgb_init(void);\nvoid feature_rgb_get_from_cold(void) {uint8_t byte=eeprom_read_byte((void*)8);rgblight_config.velocikey=(byte&2)!=0;}\n"
        label = board + ("-" + mutant if mutant else "")
        owner_file = self.write(label + "-owner.c", owner)
        base = self.base.replace("void era_board_via_save(void) { approved_saves++; }", "void era_board_via_save(void) {approved_saves++;}")
        start, end = base.index("static uint8_t flash_image"), base.index("void raw_hid_send")
        base = base[:start] + WL_STORAGE_BOUNDARY + base[end:]
        first = base.index("static void fresh(void)")
        base = base[:first] + WL_TESTS
        harness = self.write(label + "-wl.c", base)
        engine = self.write("feature-wl-engine.c", '#include <assert.h>\n#include "' + str(ROOT / "quantum/wear_leveling/wear_leveling.c").replace("\\", "/") + '"\n' + WL_COLD_READ)
        command = [arg for arg in self.command if not arg.replace("\\", "/").endswith(("storage/era_nvm.c", "storage/era_eeprom_driver.c"))]
        command = [arg.replace("-DEEPROM_CUSTOM", "-DEEPROM_WEAR_LEVELING").replace("-DEEPROM_SIZE=24576", "-DEEPROM_SIZE=4096").replace("-DDYNAMIC_KEYMAP_MACRO_EEPROM_SIZE=16384", "-DDYNAMIC_KEYMAP_MACRO_EEPROM_SIZE=1024") for arg in command]
        command = [arg for arg in command if arg != "-DSPLIT_KEYBOARD"]
        command[command.index(str(self.out / "callers.c"))] = harness
        command[1:1] = ["-DVELOCIKEY_ENABLE", "-DRGBLIGHT_ENABLE", "-DBACKING_STORE_WRITE_SIZE=2", "-DWEAR_LEVELING_BACKING_SIZE=8192", "-DWEAR_LEVELING_LOGICAL_SIZE=4096", "-I" + str(ROOT / "quantum/wear_leveling"), "-I" + str(ROOT / "lib/fnv")]
        command[-2:-2] = ["-I" + str(ROOT / "quantum")]
        command[-2:-2] = [str(ROOT / "drivers/eeprom/eeprom_wear_leveling.c"), str(ROOT / "lib/fnv/hash_64a.c"), engine, owner_file]
        command[-1] = str(self.out / (label + "-wl.exe"))
        return self.compile(command, label)


HID_PREAMBLE = r'''
#include <stdlib.h>
#undef KC_NO
#undef KC_TRANSPARENT
#include "quantum/keycodes.h"
#define NKRO_ENABLE
#define ERA_HID_REPORT_INTERVAL_ENABLE
#define KEYBOARD_REPORT_KEYS 6
#define NKRO_REPORT_BITS 30
#define PACKED __attribute__((packed))
#define USB_PROTOCOL_BOOT 0
#define dprintf(...) ((void)0)
'''
HID_BOUNDARIES = r'''
#include "keyboards/era/common/system/era_hid_report_interval.h"
report_keyboard_t *keyboard_report=&(report_keyboard_t){};
report_nkro_t *nkro_report=&(report_nkro_t){};
static report_keyboard_t last_host_6;
static report_nkro_t last_host_n;
static unsigned sent_6,sent_n,clear_count;
static uint8_t era_hid_format=UINT8_MAX;
static uint32_t era_hid_protocol_events,era_hid_protocol_seen;
static uint32_t era_hid_session_events,era_hid_format_session;
static uint8_t era_hid_retire_pending;
static uint8_t era_hid_retire_debt;
static bool era_hid_retire_reset;
/* This persistence fixture's host callbacks accept synchronously. Transport
 * retirement is executed with the real USB owner in the lifetime fixture. */
static void osalSysLock(void) {}
static void osalSysUnlock(void) {}
static uint8_t era_hid_report_interval_current_format(void) {return keymap_config.nkro?2:1;}
static bool era_report_force;
static bool host_can_send_nkro(void) {return true;}
static uint8_t usb_device_state_get_protocol(void) {return 1;}
static uint8_t get_mods_for_report(void) {return 0;}
static void clear_mods(void) {clear_count++;}
static void clear_weak_mods(void) {}
static void era_hid_report_interval_ensure_ready(void) {}
bool era_hid_report_interval_platform_post(uint8_t lane,const uint8_t *data,uint8_t size) {(void)lane;(void)data;(void)size;return true;}
uint32_t era_hid_report_interval_platform_now(void) {return now;}
static void host_keyboard_send(report_keyboard_t *r) {last_host_6=*r;sent_6++;}
static void host_nkro_send(report_nkro_t *r) {last_host_n=*r;sent_n++;}
void clear_keyboard_but_mods(void);
void clear_keyboard_but_mods_and_keys(void);
void send_keyboard_report(void);
bool era_hid_report_interval_select_format(uint8_t format);
'''
HID_TESTS = r'''
static bool feature_6_has_a(const report_keyboard_t *report) {
 for(unsigned i=0;i<KEYBOARD_REPORT_KEYS;i++)if(report->keys[i]==4)return true;
 return false;
}
int main(int argc,char **argv) {
 unsigned path=argc>1?(unsigned)atoi(argv[1]):0;
 fresh(); keymap_config.raw=0;eeconfig_update_keymap(&keymap_config);
 era_hid_report_interval_init(1);
 if(path==6) {
   fail_program=true;cmd(7,9,12,0,0);assert(response[0]==id_unhandled);
   assert(keymap_config.era_rgb_sleep_disabled);fail_program=false;
   cmd(8,9,12,0,0);assert(response[3]==0);assert(keymap_config.era_rgb_sleep_disabled);
   cmd(7,0,5,1,0);assert(response[0]==7);assert(keymap_config.era_rgb_sleep_disabled);
   keymap_config_t stored;eeconfig_read_keymap(&stored);
   assert(stored.nkro);assert(!stored.era_rgb_sleep_disabled);
   cmd(7,9,12,0,0);assert(response[0]==7);eeconfig_read_keymap(&stored);
   assert(stored.nkro);assert(stored.era_rgb_sleep_disabled);
   puts("PASS keymap owners preserve independent runtime intent and durable bits");return 0;
 }
 if(path==4||path==10) {cmd(7,0,5,1,0);assert(eeprom_persistence_ok());}
 else {fail_program=true;cmd(7,0,5,1,0);assert(response[0]==id_unhandled);assert(!eeprom_persistence_ok());fail_program=false;}
 assert(keymap_config.nkro);
 if(path==10) {fail_program=true;cmd(7,0,5,0,0);assert(response[0]==id_unhandled);assert(!keymap_config.nkro);fail_program=false;}
 add_key_to_report(4);send_keyboard_report();
 if(path==10)assert(feature_6_has_a(&last_host_6));else assert(last_host_n.bits[0]&16);
 unsigned old_sent_n=sent_n,old_sent_6=sent_6,old_clear=clear_count;
 keyrecord_t record={.event={.pressed=true}};
 switch(path) {
   case 0:cmd(8,0,5,0,0);break;
   case 1:cmd(7,0,5,0,0);break;
   case 2:cmd(7,9,12,0,0);break;
   case 3:cmd(8,9,12,0,0);break;
   case 5:cmd(7,0,5,1,0);assert(response[0]==7);assert(eeprom_persistence_ok());assert(eeprom_read_word((void*)4)&(1U<<7));break;
   case 7:assert(!process_magic(QK_MAGIC_NKRO_OFF,&record));break;
   case 8:assert(!process_magic(QK_MAGIC_GUI_OFF,&record));assert(keymap_config.no_gui);break;
   case 9:assert(!process_magic(QK_MAGIC_TOGGLE_NKRO,&record));break;
   case 10:assert(!process_magic(QK_MAGIC_NKRO_ON,&record));break;
   case 11:record.event.pressed=false;assert(process_magic(QK_MAGIC_NKRO_OFF,&record));break;
   case 12:assert(process_magic(KC_A,&record));break;
   default:cmd(7,0,5,0,0);break;
 }
 bool switched=path==1||path==4||path==7||path==8;
 bool old_format_A=path==10?feature_6_has_a(&last_host_6):!!(last_host_n.bits[0]&16);
 force_send_keyboard_report();del_key_from_report(4);send_keyboard_report();
 printf("path=%u runtime_nkro=%u clear_delta=%u old_nkro_A=%u internal_A=%u later_nkro=%u old_format_A_before_keyup=%u old_6_A=%u later_6=%u\n",path,keymap_config.nkro,clear_count-old_clear,!!(last_host_n.bits[0]&16),!!(nkro_report->bits[0]&16),sent_n-old_sent_n,old_format_A,feature_6_has_a(&last_host_6),sent_6-old_sent_6);
 assert(keymap_config.nkro==!switched);
 if(path>=7&&path<=10) {
   assert(!old_format_A);
   assert(clear_count-old_clear==(path==8?2U:3U));
   assert(!!(eeprom_read_word((void*)4)&(1U<<7))==!switched);
 } else assert(clear_count-old_clear==(switched?2U:0U));
 if(path==10) {
   assert(!feature_6_has_a(&last_host_6));assert(!feature_6_has_a(keyboard_report));assert(sent_6>old_sent_6);
 } else {
   assert(!(last_host_n.bits[0]&16));assert(!(nkro_report->bits[0]&16));assert(sent_n>old_sent_n);
 }
 return 0;
}
'''
WL_OWNER_PREAMBLE = '#include <string.h>\n#include "quantum.h"\n#include "keyboards/era/common/storage/era_eeprom_storage.h"\n#include "keyboards/era/common/system/era_state_sync.h"\n'
WL_RGB_BOUNDARIES = r'''
rgblight_config_t rgblight_config;
#define EECONFIG_KB_DATA_SIZE 0
#define EECONFIG_USER_DATA_SIZE 0
#define PACKED __attribute__((packed))
#define STATIC_ASSERT _Static_assert
#include "quantum/nvm/eeprom/nvm_eeprom_eeconfig_internal.h"
#define RGBLIGHT_MODE_STATIC_LIGHT 1
#define RGBLIGHT_MODES 63
#define RGBLIGHT_LIMIT_VAL 255
#define dprintf(...) ((void)0)
'''
WL_STORAGE_BOUNDARY = r'''
#include "wear_leveling.h"
#include "wear_leveling_internal.h"
static uint16_t physical[4096];
static bool fail_program,fail_init;
static uint32_t now;static uint8_t response[32];static unsigned writes,erases,dirty_calls,approved_saves;
keymap_config_t keymap_config;
uint8_t mk_delay,mk_wheel_delay,mk_interval,mk_wheel_interval,mk_move_delta,mk_wheel_delta,mk_cursor_top,mk_wheel_top,mk_max_speed,mk_wheel_max_speed,mk_time_to_max,mk_wheel_time_to_max;
uint16_t mk_cursor_ramp_ms,mk_wheel_ramp_ms;
bool backing_store_init(void) {return !fail_init;}
bool backing_store_unlock(void) {return true;}
bool backing_store_lock(void) {return true;}
bool backing_store_erase(void) {erases++;if(fail_program)return false;memset(physical,0,sizeof(physical));return true;}
bool backing_store_read(uint32_t address,backing_store_int_t *value) {assert(address+2<=sizeof(physical));*value=physical[address/2];return true;}
bool backing_store_write(uint32_t address,backing_store_int_t value) {writes++;if(fail_program)return false;physical[address/2]|=value;return physical[address/2]==value;}
uint8_t feature_cold_byte(uint32_t address);
void feature_rgb_init(void);
void feature_rgb_get_from_cold(void);
uint8_t feature_velocikey_id(void);
uint8_t feature_indicator_brightness_id(void);
bool rgblight_velocikey_enabled(void);
'''
WL_COLD_READ = r'''
uint8_t feature_cold_byte(uint32_t address) {
 unsigned char saved[sizeof(wear_leveling)];memcpy(saved,&wear_leveling,sizeof(saved));
 bool pending=wear_leveling_pending;assert(wear_leveling_init()!=WEAR_LEVELING_FAILED);
 uint8_t value=0;assert(wear_leveling_read(address,&value,1)!=WEAR_LEVELING_FAILED);
 memcpy(&wear_leveling,saved,sizeof(saved));wear_leveling_pending=pending;return value;
}
'''
WL_TESTS = r'''
void eeconfig_flush_rgblight_deferred_task(void) {}
void eeconfig_flush_rgblight_deferred_now(void) {}
void era_lighting_sleep_test_rgb_gate(bool sleep) {(void)sleep;}
void era_lighting_sleep_test_backlight_gate(bool sleep) {(void)sleep;}
void via_qmk_rgblight_command(uint8_t *data,uint8_t length) {(void)data;(void)length;}
void nvm_dynamic_keymap_macro_reset_session(void) {}
int main(int argc,char **argv) {
 unsigned path=argc>1?(unsigned)argv[1][0]-'0':0;
 memset(physical,0,sizeof(physical));eeprom_driver_init();assert(eeprom_persistence_ok());
 feature_rgb_init();assert(eeprom_persistence_ok());uint8_t field=feature_velocikey_id();
 cmd(7,0,field,1,0);assert(response[0]==7);assert(feature_cold_byte(8)&2);
 cmd(7,0,field,0,0);assert(response[0]==7);assert(!(feature_cold_byte(8)&2));
 if(path==3) {
   fail_init=true;eeprom_driver_init();unsigned before=writes;cmd(7,0,field,1,0);
   assert(response[0]==id_unhandled);assert(!rgblight_velocikey_enabled());assert(writes==before);
   fail_init=false;eeprom_driver_init();cmd(7,0,field,1,0);assert(response[0]==7);assert(feature_cold_byte(8)&2);return 0;
 }
 fail_program=true;unsigned before=writes;cmd(7,0,field,1,0);
 assert(writes>before);assert(!eeprom_persistence_ok());assert(rgblight_velocikey_enabled());assert(!(feature_cold_byte(8)&2));
 printf("failed SET reply=%02x runtime=1 durable=0\n",response[0]);
 /* Path 5 isolates same-value persistence from the initial error echo. */
 if(path!=5)assert(response[0]==id_unhandled);
 if(path==4) {
   before=writes;uint8_t indicator=feature_indicator_brightness_id();
   cmd(7,0,indicator,123,0);assert(response[0]==7);assert(writes==before);
   cmd(8,0,indicator,0,0);assert(response[3]==123);assert(rgblight_velocikey_enabled());assert(!eeprom_persistence_ok());
 }
 if(path==1) {before=writes+erases;cmd(7,0,field,1,0);assert(response[0]==id_unhandled);assert(writes+erases>before);assert(!(feature_cold_byte(8)&2));}
 fail_program=false;
 if(path==2) {eeprom_update_byte((void*)30,1);assert(eeprom_persistence_ok());assert(feature_cold_byte(8)&2);}
 before=writes;cmd(7,0,field,1,0);assert(response[0]==7);assert(eeprom_persistence_ok());assert(rgblight_velocikey_enabled());assert(feature_cold_byte(8)&2);
 if(path!=2)assert(writes>before);
 eeprom_driver_init();feature_rgb_get_from_cold();assert(rgblight_velocikey_enabled());
 printf("PASS path=%u same-value retry writes=%u cold=1\n",path,writes-before);
 return 0;
}
'''
