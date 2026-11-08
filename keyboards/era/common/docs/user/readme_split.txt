======================================================================
Split Firmware Guide
======================================================================

----------------------------------------------------------------------
한국어
----------------------------------------------------------------------

■ 펌웨어 업데이트

사용 중인 키보드에 맞는 펌웨어 ZIP을 사용하고, 분리형 키보드의 왼쪽과 오른쪽 유닛에 반드시 같은 .uf2 파일을 설치하십시오. 서로 다른 펌웨어 버전 조합은 지원하지 않습니다.

한쪽씩 다음 절차를 진행합니다.

1. 유닛을 Bootloader 모드로 전환합니다.
   - 키보드 설정 화면: SYSTEM -> BOOT -> Jump To BOOT
   - 키맵에 배치한 QK_BOOT 키
   - 플래시할 유닛의 가장 왼쪽 위 물리 키를 누른 채 USB 연결(Bootmagic)
   - 해당 유닛의 Reset 버튼을 빠르게 두 번 누르거나 RST와 GND를 빠르게 두 번 짧게 연결(각 연결 사이에는 완전히 뗌)
2. PC에 RPI-RP2 드라이브가 나타나면 일반 펌웨어 .uf2를 복사합니다(완료 후 자동 재시작).
3. 반대쪽 유닛에도 같은 .uf2 파일을 설치하고, 각 유닛의 SYSTEM -> VERSION에서 버전을 확인합니다.

이전 펌웨어는 Bootmagic 진입 시 설정이 초기화될 수 있으므로 각 유닛의 첫 업데이트에는 VIA BOOT 또는 Reset 두 번을 사용하십시오. 일반 업데이트는 같은 저장 형식의 유효한 키맵·매크로·설정을 유지하지만, 저장 형식 변경이나 유효하지 않은 저장 데이터는 진입 방법과 관계없이 기본값으로 초기화될 수 있습니다. 업데이트나 EEPROM CLEAN 전에 VIA SAVE + LOAD로 키맵을 백업해 두십시오. 일반 업데이트에 Reset.uf2를 사용하지 마십시오.

■ 설정 복구 초기화

같은 ZIP의 Reset.uf2는 일반 펌웨어의 모든 기능을 포함합니다. 설치 후 최초 실행에서만 현재 설정(키맵·매크로·사용자 설정)을 모두 초기화하며, 일반 펌웨어를 다시 올리지 않고 그대로 키보드로 사용할 수 있습니다. 가능하면 먼저 백업하십시오.

스플릿 케이블을 분리하고 각 유닛을 따로 초기화하십시오. 한쪽만 초기화하면 초기화하지 않은 반대쪽이 이전 설정을 복원할 수 있습니다.

1. 각 유닛을 Bootloader에 진입시켜 같은 Reset.uf2를 복사합니다.
2. 각 유닛의 초기화가 끝나고 키보드로 다시 연결될 때까지 기다립니다.
3. 양쪽 모두 끝나면 스플릿 케이블을 다시 연결하고 새로 설정하여 사용합니다.

이후 재부팅에는 새로 저장한 설정이 유지되며, 같은 Reset.uf2를 다시 설치하면 다시 한 번 초기화됩니다. 전송 중단은 설치 완료가 아니므로 다시 업로드해야 합니다. 초기화 중 전원이 끊기면 재시도하며, 정상 사용이 시작되지 않으면 해당 유닛의 하드웨어 Bootloader 진입 방법으로 같은 Reset.uf2를 다시 업로드하십시오. 전체 칩이나 부트로더를 지우지 않으며 보안 삭제나 물리적 플래시 고장 수리를 보장하지 않습니다.

■ 키보드 설정

키보드 설정과 고급 기능은 https://usekb.cc 와 각 설정 메뉴 옆 Help를 권장합니다. Tap Dance의 고급 입력 모드와 홀드 설정은 usekb.cc에서 지원하며, 공식 VIA는 기본 동작과 Term만 편집합니다.

공식 VIA(https://usevia.app)를 직접 사용하려면 배포 ZIP의 usevia.app/usevia.txt에서 좌/우 Draft Definition 불러오기, 스플릿 전용 설정, 링크·동기화 STATUS 표시를 확인하십시오.

----------------------------------------------------------------------
English
----------------------------------------------------------------------

■ Firmware Update

Use the firmware ZIP that matches your keyboard, and always install the exact same .uf2 file on both the left and right halves of the split keyboard. Mixing different firmware versions is not supported.

Follow these steps one half at a time.

1. Switch the half to Bootloader mode.
   - Keyboard settings screen: SYSTEM -> BOOT -> Jump To BOOT
   - QK_BOOT key placed on the keymap
   - Connect USB while holding the top-left physical key of the half being flashed (Bootmagic)
   - Quickly double-press the Reset button on that half, or briefly short RST and GND twice quickly (releasing completely between shorts)
2. When the RPI-RP2 drive appears on your PC, copy the standard firmware .uf2 (restarts automatically when finished).
3. Install the same .uf2 file on the other half, and check the version in SYSTEM -> VERSION on each half.

Earlier firmware may reset settings when entering via Bootmagic, so use VIA BOOT or double-press Reset for the first update on each half. A standard update preserves valid keymaps, macros, and settings of the same storage format, but a storage format change or invalid saved data may reset them to defaults regardless of entry method. Back up your keymap with VIA SAVE + LOAD before updating or running EEPROM CLEAN. Do not use Reset.uf2 for a standard update.

■ Settings Recovery Reset

Reset.uf2 in the same ZIP includes all features of the standard firmware. It resets all current settings (keymaps, macros, and user settings) only on the first run after installation, and you can continue using the keyboard as is without flashing the standard firmware again. Back up first if possible.

Disconnect the split cable and reset each half separately. If you reset only one half, the unreset half may restore the previous settings.

1. Enter the Bootloader on each half and copy the same Reset.uf2.
2. Wait until each half finishes resetting and reconnects as a keyboard.
3. Once both halves are finished, reconnect the split cable and configure your settings.

Subsequent reboots keep newly saved settings, and reinstalling the same Reset.uf2 resets settings once more. An interrupted transfer is not a completed installation, so you must upload it again. If power is lost during reset, it will retry; if normal operation does not start, upload the same Reset.uf2 again using the hardware Bootloader entry method on that half. It does not erase the entire chip or bootloader, and does not guarantee secure erasure or repair of physical flash faults.

■ Keyboard Configuration

For keyboard configuration and advanced features, https://usekb.cc and Help next to each settings menu are recommended. Advanced input modes and hold settings for Tap Dance are supported on usekb.cc, while official VIA edits only basic actions and Term.

To use official VIA (https://usevia.app) directly, see usevia.app/usevia.txt in the release ZIP for loading left/right Draft Definitions, split-specific settings, and link/sync STATUS indicators.
