======================================================================
Split Firmware Guide
======================================================================

----------------------------------------------------------------------
한국어
----------------------------------------------------------------------

■ 펌웨어 업데이트

V261004R1은 MOUSE 정밀 설정을 위해 EEPROM 형식을 변경합니다. 이전 버전에서
업데이트하면 키맵·매크로·모든 설정이 초기화됩니다. 먼저 백업하십시오.

V260916R1 및 그 이전 배포본에서 V260929R1 이후 버전으로 처음 업데이트하면 저장 형식
변경으로 키맵·매크로·설정이 초기화됩니다. 먼저 SAVE + LOAD로 백업하고,
백업 파일에 포함되지 않는 설정과 탭댄스는 따로 기록하십시오.
이 배포본은 레이어 6개(0–5)를 지원합니다. usekb.cc에서 예전 4레이어 백업을
불러오면 파일에 없는 레이어 4·5는 현재 키보드의 값을 유지합니다.
V260929R1부터 메이커별 USB 식별자를 사용합니다. 메이커와 보드 이름이 모두
일치하는 ZIP을 사용하고, 업데이트 후 브라우저에서 기기 연결을 다시 허용하십시오.

분리형 키보드는 왼쪽과 오른쪽 유닛에 반드시 같은 .uf2 파일을 플래시하십시오.
서로 다른 펌웨어 버전으로 남겨 둔 조합은 지원하지 않습니다.

한쪽씩 다음 절차를 반복합니다.

1. 유닛을 Bootloader 모드로 진입시킵니다.
2. PC에 RPI-RP2 드라이브가 나타나면 같은 .uf2 파일을 복사합니다.
3. 복사가 끝나면 유닛이 자동으로 재시작합니다.
4. 반대쪽 유닛에도 같은 .uf2 파일을 플래시합니다.

Bootloader 진입 방법은 다음 중 하나를 사용하십시오.
- 키보드 설정 화면의 SYSTEM -> BOOT -> Jump To BOOT
- 키맵에 배치한 QK_BOOT
- 플래시할 유닛의 가장 왼쪽 위 물리 키를 누른 채 USB 연결(Bootmagic)
- 해당 유닛의 Reset 버튼을 빠르게 두 번 누르거나 RST/GND를 짧게 접촉

Bootmagic으로 진입한 유닛은 저장된 키맵과 모든 설정이 공장 초기값으로
돌아갑니다.

일반 업데이트는 키맵·매크로·VIA 설정을 그대로 유지합니다. 저장 형식이 바뀐
릴리스만 첫 부팅에서 설정을 공장 초기값으로 되돌리며, 그 사실을 릴리스 안내에
적습니다. 업데이트나 EEPROM CLEAN 전에 VIA SAVE + LOAD로 키맵을 백업해
두십시오.

사용 중인 보드 이름과 일치하는 UF2를 사용하십시오. 업데이트 뒤 SYSTEM ->
VERSION에서 각 유닛의 버전을 확인하십시오.

■ 키보드 설정

일반 사용자는 https://usekb.cc 를 권장합니다.
각 설정 메뉴 옆에 Help가 있으므로 별도의 상세 설명서 없이 기능과 설정 방법을
확인할 수 있습니다.
Tap Dance의 고급 입력 모드와 홀드 설정은 usekb.cc에서 사용합니다. 공식 VIA는
Legacy 슬롯의 기본 동작과 Term만 편집하고, 고급 슬롯에는 상태와 안내를 표시합니다.

공식 VIA(https://usevia.app)를 직접 사용하려면 배포 ZIP의 usevia.app/ 폴더를
참고하십시오. 그 폴더의 usevia.txt에 좌/우 Draft Definition을 불러오는 방법과
스플릿 전용 설정을 포함한 상세 설명이 들어 있습니다. 링크·동기화 STATUS의
색과 점멸 횟수도 그 안내문에서 확인하십시오.


----------------------------------------------------------------------
English
----------------------------------------------------------------------

■ Firmware Update

V261004R1 changes the MOUSE storage format. Updating from an older version
resets all keymaps, macros and settings. Back up first.

The first upgrade from V260916R1 or earlier to V260929R1 or later resets
keymaps, macros and settings because the stored format changed. Back up with
SAVE + LOAD first, and separately record settings and Tap Dance actions that
the backup file does not include.
This release supports six layers (0–5). When usekb.cc imports an older four-layer
backup, layers 4 and 5 keep their current values on the keyboard.
Since V260929R1, releases use maker-specific USB identities. Choose the ZIP matching both
the maker and board, then authorize the keyboard again in the browser.

On a split keyboard, flash the exact same .uf2 file to both the left and right
units. A pair left on different firmware versions is not supported.

Repeat these steps for one unit at a time:

1. Put the unit into Bootloader mode.
2. When the RPI-RP2 drive appears, copy the same .uf2 file onto it.
3. The unit restarts automatically after the copy finishes.
4. Flash the same .uf2 file to the other unit.

Use any of these methods to enter the bootloader:
- SYSTEM -> BOOT -> Jump To BOOT in the keyboard configuration UI
- QK_BOOT if it is present in your keymap
- Hold that unit's physical top-left key while connecting USB (Bootmagic)
- Double-tap that unit's Reset, or briefly short its RST/GND

A unit entered through Bootmagic also resets its stored keymap and every
setting to factory defaults.

A normal update keeps the keymap, macros and VIA settings. Only a release that
changes the stored format resets settings to factory defaults on first boot,
and its release notes say so. Back up the keymap with VIA SAVE + LOAD before
updating or running EEPROM CLEAN.

Use the UF2 matching the board name. After updating, check the version under
SYSTEM -> VERSION on each unit.

■ Keyboard Configuration

For normal use, https://usekb.cc is recommended.
Each configuration menu has Help beside it, so a separate detailed manual is
not required.
Advanced Tap Dance input modes and hold settings use usekb.cc. Official VIA
edits basic actions and Term only for Legacy slots; advanced slots show status
and guidance.

If you want to use the official VIA app at https://usevia.app directly, see the
usevia.app/ folder in the distribution ZIP. Its usevia.txt explains how to load
the left/right Draft Definitions, the split-specific controls, and the colours
and flash patterns of link/synchronization STATUS signals.
