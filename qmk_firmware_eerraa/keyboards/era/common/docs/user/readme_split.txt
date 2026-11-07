======================================================================
Split Firmware Guide
======================================================================

----------------------------------------------------------------------
한국어
----------------------------------------------------------------------

■ 펌웨어 업데이트

메이커와 보드 이름이 모두 일치하는 ZIP을 사용하고, 업데이트 후 브라우저에서
기기 연결을 다시 허용하십시오.
이 펌웨어는 레이어 6개(0–5)를 지원합니다. usekb.cc에서 예전 4레이어 백업을
불러오면 파일에 없는 레이어 4·5는 현재 키보드의 값을 유지합니다.

분리형 키보드는 왼쪽과 오른쪽 유닛에 반드시 같은 .uf2 파일을 플래시하십시오.
서로 다른 펌웨어 버전으로 남겨 둔 조합은 지원하지 않습니다.

한쪽씩 다음 절차를 반복합니다.

1. 유닛을 Bootloader 모드로 진입시킵니다.
2. PC에 RPI-RP2 드라이브가 나타나면 같은 일반 펌웨어 .uf2를 복사합니다.
3. 복사가 끝나면 유닛이 자동으로 재시작합니다.
4. 반대쪽 유닛에도 같은 .uf2 파일을 플래시합니다.

Bootloader 진입 방법은 다음 중 하나를 사용하십시오.
- 키보드 설정 화면의 SYSTEM -> BOOT -> Jump To BOOT
- 키맵에 배치한 QK_BOOT
- 플래시할 유닛의 가장 왼쪽 위 물리 키를 누른 채 USB 연결(Bootmagic)
- 해당 유닛의 Reset 버튼을 빠르게 두 번 누르거나 RST와 GND를 빠르게
  두 번 짧게 연결합니다. 각 연결 사이에는 완전히 떼십시오.

설정 보존은 이 패키지의 펌웨어가 설치된 뒤의 진입 동작입니다. 이전 펌웨어의
Bootmagic은 초기화할 수 있으므로 각 유닛의 첫 업데이트에는 VIA BOOT 또는
Reset 두 번을 사용하십시오.

위의 어느 방법으로 진입해도 일반 업데이트는 유효한 같은 저장 형식의 키맵·
매크로·설정을 유지합니다. 저장 형식 업그레이드나 유효하지 않은 저장 데이터는
진입 방법과 관계없이 기본값으로 초기화될 수 있습니다. 업데이트나 EEPROM CLEAN
전에 VIA SAVE + LOAD로 키맵을 백업해 두십시오. 일반 업데이트에 Reset.uf2를
사용하지 마십시오.

사용 중인 보드 이름과 일치하는 UF2를 사용하십시오. 업데이트 뒤 SYSTEM ->
VERSION에서 각 유닛의 버전을 확인하십시오.

■ 설정 복구 초기화

같은 메이커·보드 ZIP의 Reset.uf2는 일반 펌웨어의 모든 기능을 포함합니다.
설치 후 최초 실행에서만 현재 활성 설정 저장소의 키맵·매크로·사용자 설정을
모두 초기화한 뒤 그대로 키보드로 사용할 수 있습니다. 가능하면 먼저 백업하십시오.
스플릿 케이블을 분리하고 각 유닛을 따로 처리하십시오. 한쪽만 초기화하면
초기화하지 않은 반대쪽이 이전 설정을 복원할 수 있습니다.

1. 각 유닛을 Bootloader에 진입시켜 해당 ZIP의 같은 Reset.uf2를 복사합니다.
2. 각 유닛의 초기화가 끝나고 키보드로 다시 연결될 때까지 기다립니다.
3. 양쪽 모두 끝나면 스플릿 케이블을 다시 연결합니다.
   일반 펌웨어를 다시 올리지 않고 새로 설정하여 그대로 사용합니다.

이후 재부팅에는 새로 저장한 설정이 유지됩니다. 같은 Reset.uf2를 다시 설치하면
다시 한 번 초기화됩니다. 전송 중단은 설치 완료가 아니며 다시 업로드해야 합니다.
초기화 중 전원이 끊기면 완료 전 작업을 재시도합니다. 완료 표식 손상이나 IO
실패로 정상 사용이 시작되지 않으면 해당 유닛의 하드웨어 Bootloader 진입 방법으로
같은 Reset.uf2를 다시 업로드하십시오. 전체 칩, 부트로더 또는 이전 롤백 저장소를
지우지 않으며 보안 삭제나 물리적 플래시 고장 수리를 보장하지 않습니다.

■ 키보드 설정

일반 사용자는 https://usekb.cc 를 권장합니다.
각 설정 메뉴 옆에 Help가 있으므로 별도의 상세 설명서 없이 기능과 설정 방법을
확인할 수 있습니다.
Tap Dance의 고급 입력 모드와 홀드 설정은 usekb.cc에서 사용합니다. 공식 VIA는
Legacy 슬롯의 기본 동작과 Term만 편집하고, 고급 슬롯에는 usekb.cc 안내만 표시합니다.

공식 VIA(https://usevia.app)를 직접 사용하려면 배포 ZIP의 usevia.app/ 폴더를
참고하십시오. 그 폴더의 usevia.txt에 좌/우 Draft Definition을 불러오는 방법과
스플릿 전용 설정을 포함한 상세 설명이 들어 있습니다. 링크·동기화 STATUS의
색과 점멸 횟수도 그 안내문에서 확인하십시오.


----------------------------------------------------------------------
English
----------------------------------------------------------------------

■ Firmware Update

Choose the ZIP matching both the maker and board, then authorize the keyboard
again in the browser after updating.
This firmware supports six layers (0–5). When usekb.cc imports an older
four-layer backup, layers 4 and 5 keep their current values on the keyboard.

On a split keyboard, flash the exact same .uf2 file to both the left and right
units. A pair left on different firmware versions is not supported.

Repeat these steps for one unit at a time:

1. Put the unit into Bootloader mode.
2. When the RPI-RP2 drive appears, copy the same normal firmware .uf2 onto it.
3. The unit restarts automatically after the copy finishes.
4. Flash the same .uf2 file to the other unit.

Use any of these methods to enter the bootloader:
- SYSTEM -> BOOT -> Jump To BOOT in the keyboard configuration UI
- QK_BOOT if it is present in your keymap
- Hold that unit's physical top-left key while connecting USB (Bootmagic)
- Double-tap that unit's Reset, or briefly connect its RST to GND twice in
  quick succession. Fully release the connection between the two contacts.

Entry preserves settings after this package's firmware is installed.
Older firmware may erase settings through Bootmagic; use VIA BOOT or Reset
double-tap for each unit's first update.

A normal update preserves valid keymaps, macros and settings in the same storage
format with any entry method above. A storage-format upgrade or invalid store
may independently cause defaults. Back up the keymap with VIA SAVE + LOAD before
updating or running EEPROM CLEAN. Do not use Reset.uf2 for normal updates.

Use the UF2 matching the board name. After updating, check the version under
SYSTEM -> VERSION on each unit.

■ Settings Reset Recovery

Reset.uf2 from the matching maker and board ZIP includes all normal firmware
features. Only its first execution after installation clears the active current
settings store, including keymaps, macros and custom settings, then continues as
a normal keyboard. Back up first if possible. Disconnect the split cable and
handle each unit separately: an unwiped peer can restore old settings.

1. Enter the bootloader on each unit and copy the same Reset.uf2 from that ZIP.
2. Wait for initialization on each unit to finish and the keyboard to reconnect.
3. Once both have finished, reconnect the split cable. Configure and keep using
   them; no second normal-firmware upload is needed.

Later reboots preserve newly saved settings. Installing the exact same Reset.uf2
again triggers another one-time reset. An interrupted transfer is not a completed
installation; upload again. An unfinished erase retries after power loss. If a
damaged completion receipt or IO failure prevents normal use, enter that unit's
hardware bootloader and re-upload Reset.uf2. This does not erase the entire chip,
bootloader or legacy rollback store, and does not guarantee secure erasure or
repair of physical flash faults.

■ Keyboard Configuration

For normal use, https://usekb.cc is recommended.
Each configuration menu has Help beside it, so a separate detailed manual is
not required.
Advanced Tap Dance input modes and hold settings use usekb.cc. Official VIA
edits basic actions and Term only for Legacy slots; advanced slots show only
the usekb.cc guidance.

If you want to use the official VIA app at https://usevia.app directly, see the
usevia.app/ folder in the distribution ZIP. Its usevia.txt explains how to load
the left/right Draft Definitions, the split-specific controls, and the colours
and flash patterns of link/synchronization STATUS signals.
