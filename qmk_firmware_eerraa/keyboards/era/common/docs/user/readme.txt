======================================================================
Firmware Guide
======================================================================

----------------------------------------------------------------------
한국어
----------------------------------------------------------------------

■ 펌웨어 업데이트

메이커와 보드 이름이 일치하는 ZIP 파일을 사용하십시오. 업데이트·백업 안내와
기능별 상세 설명(Help)은 https://usekb.cc 에서 확인하십시오.

1. 키보드를 Bootloader 모드로 진입시킵니다.
   - 설정 화면: SYSTEM -> BOOT -> Jump To BOOT
   - 키맵의 QK_BOOT 키
   - 가장 왼쪽 위 물리 키를 누른 채 USB 연결(Bootmagic)
   - 보드 Reset 버튼 두 번 연타(또는 RST-GND 두 번 짧게 연결)
2. RPI-RP2 드라이브에 일반 펌웨어 .uf2를 복사합니다(완료 후 자동 재시작).
3. 브라우저에서 키보드 연결을 다시 허용하고 SYSTEM -> VERSION을 확인합니다.

스플릿 보드는 양쪽에 같은 .uf2 이미지를 설치하십시오. 전송이 중단되거나
정상 작동하지 않으면 Bootloader로 다시 진입해 같은 파일을 다시 복사하십시오.
일반 업데이트는 같은 저장 형식의 유효한 설정을 유지합니다. 형식 변경이나
잘못된 저장 데이터는 초기화를 일으킬 수 있으므로 먼저 백업하십시오.
이전 펌웨어의 Bootmagic은 초기화할 수 있으므로 첫 업데이트에는 VIA BOOT나
Reset 두 번을 사용하십시오.

■ Reset.uf2 주의 (설정 초기화)

설정 초기화가 필요하면 같은 ZIP의 Reset.uf2를 업로드하십시오.
설치 후 최초 실행에서만 키맵·매크로·사용자 설정을 모두 초기화합니다.
그대로 사용하며 일반 펌웨어를 다시 올릴 필요가 없습니다. 이후 재부팅에는
새 설정이 유지되고, 같은 Reset.uf2를 다시 설치하면 다시 한 번 초기화됩니다.
전송 중단은 설치 완료가 아닙니다. 초기화 중 전원이 끊기면 재시도하며, 정상
사용이 시작되지 않으면 하드웨어 Bootloader로 같은 파일을 다시 설치하십시오.
보안 삭제나 물리적 플래시 고장 수리를 보장하지 않습니다.

스플릿 보드를 초기화할 때는 양쪽 연결을 분리하고 각각 같은 Reset.uf2 설치와
초기화를 모두 마친 뒤 다시 연결하십시오.

■ 키보드 설정

일반 설정과 고급 기능은 https://usekb.cc 와 각 메뉴 옆 Help를 권장합니다.
공식 VIA(https://usevia.app) 사용법은 usevia.app/usevia.txt를 참고하십시오.

----------------------------------------------------------------------
English
----------------------------------------------------------------------

■ Firmware Update

Use the ZIP matching your maker and board. See https://usekb.cc for update and
backup guidance and per-menu Help.

1. Enter Bootloader mode:
   - Configuration UI: SYSTEM -> BOOT -> Jump To BOOT
   - QK_BOOT key in your keymap
   - Hold the physical top-left key while connecting USB (Bootmagic)
   - Double-tap Reset on the board (or briefly bridge RST to GND twice)
2. Copy the normal firmware .uf2 to the RPI-RP2 drive (restarts automatically).
3. Re-authorize the keyboard in your browser and check SYSTEM -> VERSION.

For split boards, install the same .uf2 image on both halves. If a transfer is
interrupted or the board does not start normally, re-enter the bootloader and
copy the same file again. Normal updates preserve valid same-format settings.
A format change or invalid store can cause defaults, so back up first.
Older firmware may erase settings through Bootmagic; use VIA BOOT or Reset
double-tap for the first update.

■ Reset.uf2 Warning (Settings Reset)

Upload Reset.uf2 from the same ZIP when you need to reset the settings.
Only its first execution after installation clears keymaps, macros and custom
settings. Keep using it; no second normal-firmware upload is needed. Later
reboots preserve new settings; reinstalling the same Reset.uf2 resets again.
An interrupted transfer is not a completed installation. An unfinished erase
retries after power loss; if normal use does not start, re-enter the hardware
bootloader and reinstall the same file. Secure erasure and physical flash
fault repair are not guaranteed.

To reset a split board, disconnect both halves and finish installing and
initializing the same Reset.uf2 on each half before reconnecting them.

■ Keyboard Configuration

https://usekb.cc and its per-menu Help are recommended for configuration and
advanced features. For official VIA (https://usevia.app), see
usevia.app/usevia.txt.
