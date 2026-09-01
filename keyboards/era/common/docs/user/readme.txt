======================================================================
EERRAA Firmware Guide
======================================================================

----------------------------------------------------------------------
한국어
----------------------------------------------------------------------

■ 펌웨어 업데이트

1. 키보드를 Bootloader 모드로 진입시킵니다.
2. PC에 RPI-RP2 드라이브가 나타나면 제공된 .uf2 파일을 복사합니다.
3. 복사가 끝나면 키보드가 자동으로 재시작합니다.

Bootloader 진입 방법은 다음 중 하나를 사용하십시오.
- 키보드 설정 화면의 SYSTEM -> BOOT -> Jump To BOOT
- 키맵에 배치한 QK_BOOT
- 가장 왼쪽 위 물리 키를 누른 채 USB 연결(Bootmagic)
- 보드의 Reset 버튼을 빠르게 두 번 누르거나 RST/GND를 짧게 접촉

일반 UF2 업데이트는 EERRAA의 저장 영역을 지우는 절차가 아닙니다. 다만
중요한 키맵은 업데이트나 EEPROM CLEAN 전에 별도로 백업해 두는 것을 권장합니다.

■ 키보드 설정

일반 사용자는 https://usekb.cc 를 권장합니다.
각 설정 메뉴 옆에 Help가 있으므로 별도의 상세 설명서 없이 기능과 설정 방법을
확인할 수 있습니다.

공식 VIA(https://usevia.app)를 직접 사용하려면 배포 ZIP의 usevia.app/ 폴더를
참고하십시오. 그 폴더의 usevia.txt에 Draft Definition을 불러오는 방법과
펌웨어 기능별 설명이 들어 있습니다.


----------------------------------------------------------------------
English
----------------------------------------------------------------------

■ Firmware Update

1. Put the keyboard into Bootloader mode.
2. When the RPI-RP2 drive appears, copy the provided .uf2 file onto it.
3. The keyboard restarts automatically after the copy finishes.

Use any of these methods to enter the bootloader:
- SYSTEM -> BOOT -> Jump To BOOT in the keyboard configuration UI
- QK_BOOT if it is present in your keymap
- Hold the physical top-left key while connecting USB (Bootmagic)
- Double-tap Reset, or briefly short RST/GND

A normal UF2 update is not an EERRAA storage erase. Backing up an important
keymap before an update or EEPROM CLEAN is still recommended.

■ Keyboard Configuration

For normal use, https://usekb.cc is recommended.
Each configuration menu has Help beside it, so a separate detailed manual is
not required.

If you want to use the official VIA app at https://usevia.app directly, see the
usevia.app/ folder in the distribution ZIP. Its usevia.txt explains how to load
the Draft Definition and describes the firmware-specific controls.
