======================================================================
Firmware Guide
======================================================================

----------------------------------------------------------------------
한국어
----------------------------------------------------------------------

■ 펌웨어 업데이트

메이커와 보드 이름이 모두 일치하는 ZIP을 사용하고, 업데이트 후 브라우저에서
기기 연결을 다시 허용하십시오.
이 펌웨어는 레이어 6개(0–5)를 지원합니다. usekb.cc에서 예전 4레이어 백업을
불러오면 파일에 없는 레이어 4·5는 현재 키보드의 값을 유지합니다.

1. 키보드를 Bootloader 모드로 진입시킵니다.
2. PC에 RPI-RP2 드라이브가 나타나면 제공된 .uf2 파일을 복사합니다.
3. 복사가 끝나면 키보드가 자동으로 재시작합니다.

Bootloader 진입 방법은 다음 중 하나를 사용하십시오.
- 키보드 설정 화면의 SYSTEM -> BOOT -> Jump To BOOT
- 키맵에 배치한 QK_BOOT
- 가장 왼쪽 위 물리 키를 누른 채 USB 연결(Bootmagic)
- 보드의 Reset 버튼을 빠르게 두 번 누르거나 RST와 GND를 빠르게 두 번
  짧게 연결합니다. 각 연결 사이에는 완전히 떼십시오.

Bootmagic으로 진입하면 저장된 키맵과 모든 설정이 공장 초기값으로 돌아갑니다.

일반 업데이트는 키맵·매크로·VIA 설정을 그대로 유지합니다. 저장 형식이 바뀐
릴리스만 첫 부팅에서 설정을 공장 초기값으로 되돌리며, 그 사실을 릴리스 안내에
적습니다. 업데이트나 EEPROM CLEAN 전에 VIA SAVE + LOAD로 키맵을 백업해
두십시오.

사용 중인 보드 이름과 일치하는 UF2를 사용하십시오. 업데이트 뒤 SYSTEM ->
VERSION에서 버전을 확인할 수 있습니다.

■ 키보드 설정

일반 사용자는 https://usekb.cc 를 권장합니다.
각 설정 메뉴 옆에 Help가 있으므로 별도의 상세 설명서 없이 기능과 설정 방법을
확인할 수 있습니다.
Tap Dance의 고급 입력 모드와 홀드 설정은 usekb.cc에서 사용합니다. 공식 VIA는
Legacy 슬롯의 기본 동작과 Term만 편집하고, 고급 슬롯에는 usekb.cc 안내만 표시합니다.

공식 VIA(https://usevia.app)를 직접 사용하려면 배포 ZIP의 usevia.app/ 폴더를
참고하십시오. 그 폴더의 usevia.txt에 Draft Definition을 불러오는 방법과
펌웨어 기능별 설명이 들어 있습니다.


----------------------------------------------------------------------
English
----------------------------------------------------------------------

■ Firmware Update

Choose the ZIP matching both the maker and board, then authorize the keyboard
again in the browser after updating.
This firmware supports six layers (0–5). When usekb.cc imports an older
four-layer backup, layers 4 and 5 keep their current values on the keyboard.

1. Put the keyboard into Bootloader mode.
2. When the RPI-RP2 drive appears, copy the provided .uf2 file onto it.
3. The keyboard restarts automatically after the copy finishes.

Use any of these methods to enter the bootloader:
- SYSTEM -> BOOT -> Jump To BOOT in the keyboard configuration UI
- QK_BOOT if it is present in your keymap
- Hold the physical top-left key while connecting USB (Bootmagic)
- Double-tap Reset, or briefly connect RST to GND twice in quick succession.
  Fully release the connection between the two contacts.

Entering through Bootmagic also resets the stored keymap and every setting to
factory defaults.

A normal update keeps the keymap, macros and VIA settings. Only a release that
changes the stored format resets settings to factory defaults on first boot,
and its release notes say so. Back up the keymap with VIA SAVE + LOAD before
updating or running EEPROM CLEAN.

Use the UF2 matching the board name. After updating, check the version under
SYSTEM -> VERSION.

■ Keyboard Configuration

For normal use, https://usekb.cc is recommended.
Each configuration menu has Help beside it, so a separate detailed manual is
not required.
Advanced Tap Dance input modes and hold settings use usekb.cc. Official VIA
edits basic actions and Term only for Legacy slots; advanced slots show only
the usekb.cc guidance.

If you want to use the official VIA app at https://usevia.app directly, see the
usevia.app/ folder in the distribution ZIP. Its usevia.txt explains how to load
the Draft Definition and describes the firmware-specific controls.
