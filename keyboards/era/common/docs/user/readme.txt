======================================================================
Firmware Guide
======================================================================

----------------------------------------------------------------------
한국어
----------------------------------------------------------------------

■ 펌웨어 업데이트

사용 중인 키보드에 맞는 펌웨어 ZIP 파일을 사용하십시오. 업데이트·백업 안내와 기능별 도움말(Help)은 https://usekb.cc 에서 확인할 수 있습니다.

1. 키보드를 Bootloader 모드로 전환합니다.
   - 설정 화면: SYSTEM -> BOOT -> Jump To BOOT
   - 키맵의 QK_BOOT 키
   - 가장 왼쪽 위 물리 키를 누른 채 USB 연결(Bootmagic)
   - 보드 Reset 버튼 두 번 연타(또는 RST-GND 두 번 짧게 연결)
2. RPI-RP2 드라이브에 일반 펌웨어 .uf2를 복사합니다(완료 후 자동 재시작).
3. 업데이트 후 SYSTEM -> VERSION에서 버전을 확인합니다.

스플릿 보드는 양쪽 유닛에 같은 .uf2 파일을 설치하십시오. 전송이 중단되거나 정상 작동하지 않으면 Bootloader로 다시 진입해 같은 파일을 다시 복사하십시오. 일반 업데이트는 같은 저장 형식의 유효한 설정을 유지하지만, 형식 변경이나 잘못된 저장 데이터는 초기화를 일으킬 수 있으므로 먼저 백업하십시오. 이전 펌웨어는 Bootmagic 진입 시 초기화될 수 있으므로 첫 업데이트에는 VIA BOOT나 Reset 두 번을 사용하십시오.

■ Reset.uf2 주의 (설정 초기화)

설정 초기화가 필요할 때만 같은 ZIP의 Reset.uf2를 설치하십시오. 설치 후 최초 실행에서만 키맵·매크로·사용자 설정을 모두 초기화하며, 일반 펌웨어를 다시 올릴 필요 없이 그대로 사용합니다. 이후 재부팅에는 새 설정이 유지되고, 같은 Reset.uf2를 다시 설치하면 다시 한 번 초기화됩니다. 전송 중단은 설치 완료가 아닙니다. 초기화 중 전원이 끊기면 재시도하며, 정상 사용이 시작되지 않으면 하드웨어 Bootloader로 같은 파일을 다시 설치하십시오. 보안 삭제나 물리적 플래시 고장 수리를 보장하지 않습니다.

스플릿 보드를 초기화할 때는 양쪽 연결을 분리하고 각각 같은 Reset.uf2 설치와 초기화를 모두 마친 뒤 다시 연결하십시오.

■ 키보드 설정

키보드 설정과 고급 기능은 https://usekb.cc 와 각 메뉴 옆 Help를 사용하십시오. 공식 VIA(https://usevia.app) 사용법은 usevia.app/usevia.txt를 참고하십시오.

----------------------------------------------------------------------
English
----------------------------------------------------------------------

■ Firmware Update

Use the firmware ZIP file that matches your keyboard. Update and backup instructions as well as per-feature Help are available at https://usekb.cc.

1. Switch the keyboard to Bootloader mode.
   - Settings screen: SYSTEM -> BOOT -> Jump To BOOT
   - QK_BOOT key on the keymap
   - Connect USB while holding the top-left physical key (Bootmagic)
   - Double-press the board Reset button quickly (or briefly short RST-GND twice)
2. Copy the standard firmware .uf2 to the RPI-RP2 drive (restarts automatically when finished).
3. After updating, check the version in SYSTEM -> VERSION.

For split boards, install the same .uf2 file on both halves. If the transfer is interrupted or the keyboard does not work properly, re-enter the Bootloader and copy the same file again. A standard update keeps valid settings of the same storage format, but a format change or invalid saved data may trigger a reset, so back up beforehand. Earlier firmware may reset settings when entering via Bootmagic, so use VIA BOOT or double-press Reset for the first update.

■ Reset.uf2 Caution (Settings Reset)

Install Reset.uf2 from the same ZIP only when you need to reset settings. It resets all keymaps, macros, and user settings only on the first run after installation, and you can continue using the keyboard as is without flashing the standard firmware again. Subsequent reboots keep new settings, and reinstalling the same Reset.uf2 resets settings once more. An interrupted transfer is not a completed installation. If power is lost during reset, it will retry; if normal operation does not start, re-install the same file via hardware Bootloader entry. It does not guarantee secure erasure or repair of physical flash hardware faults.

When resetting a split board, disconnect the link between both halves, complete installing the same Reset.uf2 and finishing the reset on each half separately, and then reconnect them.

■ Keyboard Configuration

For keyboard configuration and advanced features, use https://usekb.cc and Help next to each menu. For official VIA (https://usevia.app) instructions, see usevia.app/usevia.txt.
