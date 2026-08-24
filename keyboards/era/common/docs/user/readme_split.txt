======================================================================
Firmware Quick Guide - 분리형 키보드 (TOMAK 시리즈)
======================================================================

이 문서는 분리형(스플릿) 키보드용 펌웨어 안내입니다.


----------------------------------------------------------------------
펌웨어
----------------------------------------------------------------------

수정 QMK 펌웨어입니다. RP2040 기반 키보드는 매트릭스 스캔에
PIO/DMA 구조를 도입하여 스캔 성능을 약 4 kHz에서 48 kHz 수준으로
높였고, 조명 효과가 스캔레이트에 미치는 영향도 줄였습니다. 통신 전용
코어를 도입하여 스플릿 케이블 연결 여부에 따른 스캔 성능 편차도
최소화했습니다. 그 밖에 TAPDANCE, SOCD, KKUK(꾹보드),
DEBOUNCE, TAPPING, MOUSE, NKRO와 스플릿 전용 듀얼 호스트,
EEPROM/INPUT/RGB SYNC, LINK SPEED, 빨간색 상태 알림 기능을 추가했습니다.


----------------------------------------------------------------------
제공 파일
----------------------------------------------------------------------

■ .uf2
   키보드에 올리는 펌웨어 파일입니다. 왼쪽과 오른쪽 반쪽에 같은 파일을
   올립니다.

■ VIA JSON (반쪽마다 하나씩, 2개)
   VIA(usevia.app)가 키보드를 인식하도록 하는 Draft Definition 파일입니다.
   VIA에서 키맵이나 FEATURE/TAPDANCE/SYSTEM 메뉴를 쓰려면 먼저
   로드해야 합니다.
   두 반쪽은 USB에 서로 다른 장치로 잡히므로 왼쪽용(…-L-VIA.json)과
   오른쪽용(…-R-VIA.json)이 따로 있습니다. 두 파일이 설명하는 자판 모양은
   같고 어느 반쪽인지만 다릅니다. 케이블 한 개로 쓸 때는 USB가 꽂힌 반쪽
   것만 있으면 되고, 듀얼 호스트로 쓸 때는 두 개 모두 로드해 두십시오.

■ via_keycodes.txt
   TAPDANCE 입력창에 넣을 수 있는 keycode 예시 문서입니다.
   VIA는 최신 QMK keycode 이름을 모두 인식하지 못할 수 있으므로,
   KC_A, MO(1), MT(MOD_LSFT,KC_A) 같은 값을 직접 입력할 때 먼저
   이 파일을 확인하세요.


----------------------------------------------------------------------
펌웨어 업데이트
----------------------------------------------------------------------

**양쪽 반쪽을 같은 펌웨어로 함께 올리십시오.** 두 반쪽이 서로 다른
버전으로 남은 상태는 지원하지 않습니다.

한쪽씩, 두 번 반복합니다:

1. 그 반쪽을 Bootloader 모드로 진입시킵니다.
2. PC에 RPI-RP2 드라이브가 나타나면 .uf2 파일을 복사합니다.
3. 잠시 후 드라이브가 사라지고 업데이트가 완료됩니다.

Bootloader 진입 방법:
- VIA: CONFIGURE -> SYSTEM -> BOOT에서 Jump To BOOT를 켜면 지금 USB가
  연결된 반쪽이 곧바로 부트로더로 들어갑니다. 켜는 즉시 동작하며,
  이 토글은 화면에 항상 꺼진 것으로 보입니다.
- Bootmagic: 케이블을 꽂는 그 반쪽의 가장 왼쪽 위 자리의 키를 누른 채
  USB 연결. 왼쪽 반쪽과 오른쪽 반쪽이 각자 자기 자리를 씁니다. 이 키는
  자판 배열이 아니라 물리 위치로 정해지므로, VIA에서 다른 키로 바꿔
  두어도 자리는 그대로입니다.
- 물리 리셋: 리셋 버튼을 빠르게 두 번 누르거나 RST/GND를 짧게 접촉
- 리셋 키코드: 키맵에 QK_BOOT이 배치되어 있을 때 사용합니다. `default`
  키맵에는 Fn 레이어의 각 반쪽 가장 왼쪽 위 자리에 들어 있습니다. VIA용
  키맵은 키보드마다 다르니, 없으면 VIA에서 직접 배치하거나 위의 다른
  방법을 쓰십시오.


----------------------------------------------------------------------
반쪽 사이 연결 포트
----------------------------------------------------------------------

두 반쪽을 잇는 USB-C 커넥터는 USB 데이터 포트가 아닙니다. 이 자리에는
반쪽끼리 주고받는 전용 신호선이 들어 있습니다.

- 반쪽끼리 잇는 케이블만 이 포트에 꽂으십시오.
- 이 포트를 PC나 충전기 같은 USB 호스트에 직접 꽂지 마십시오. 펌웨어가
  막아 줄 수 있는 범위에는 한계가 있습니다.


----------------------------------------------------------------------
VIA 사용
----------------------------------------------------------------------

1. https://usevia.app 접속
2. SETTINGS -> Show Design tab 활성화
3. DESIGN -> Load Draft Definition에서 제공된 VIA JSON 로드
   (듀얼 호스트로 쓴다면 L/R 두 개를 모두 로드)
4. CONFIGURE에서 키맵과 기능 설정
5. 설정을 보존하려면 VIA의 Save 기능 사용

펌웨어를 업데이트하면 기존 키맵이 삭제됩니다. 키맵을 유지하려면
VIA의 SAVE + LOAD에서 미리 백업하십시오.


----------------------------------------------------------------------
주요 기능
----------------------------------------------------------------------

■ TAPDANCE
   한 키에 Tap, Hold, Double Tap, Tap+Hold 동작을 지정합니다.
   VIA CONFIGURE -> TAPDANCE에서 TD0~TD7을 설정하고, KEYMAP -> CUSTOM의
   같은 TD 키를 원하는 위치에 배치하십시오. 입력 예시는
   via_keycodes.txt에 있습니다.

■ SOCD
   반대 방향키가 함께 눌리면 마지막 입력만 남기는 Last Input Wins
   기능입니다. VIA CONFIGURE -> FEATURE -> SOCD에서 사용할 키 조합을
   지정하십시오. 경쟁 환경에서는 게임과 대회 규칙을 먼저 확인하세요.

■ KKUK (꾹보드)   ※ 이전 이름: Anti-Ghosting
   기본 키 두 개 이상을 가만히 누르고 있으면 그 묶음 전체를 주기적으로
   뗐다가 다시 눌러 줍니다. asd를 누르고 있으면 asddddd가 아니라
   asdasdasd가 입력됩니다. 매트릭스 고스팅 방지와는 무관해 이름을
   바꿨습니다. VIA CONFIGURE -> FEATURE -> KKUK에서 켜고 Delay/Repeat
   값을 조정하십시오. SOCD에 지정한 키는 제외됩니다.

■ DEBOUNCE
   VIA CONFIGURE -> FEATURE -> DEBOUNCE에서 스위치 채터링을 조정합니다.
   기본값은 Balanced, 5 ms이며 채터링이 보일 때만 조금씩 늘리십시오.

■ TAPPING
   VIA CONFIGURE -> FEATURE -> TAPPING에서 Mod-Tap과 Layer-Tap의
   짧게/길게 누름 판정 시간을 조정합니다. 기본값은 200 ms입니다.

■ MOUSE (마우스 키)
   VIA CONFIGURE -> FEATURE -> MOUSE에서 마우스 키의 커서·휠 속도와
   가속을 조정합니다. 기본값으로 먼저 사용한 뒤 느리거나 빠를 때만
   조금씩 바꾸십시오. 키맵에 마우스 키가 없으면 아무 영향이 없습니다.

■ NKRO
   VIA CONFIGURE -> FEATURE -> NKRO에서 무제한 동시입력을 켭니다.
   오래된 BIOS나 부팅 메뉴에서 입력되지 않을 때는 끄십시오.

■ EEPROM CLEAN / SYNC
   CLEAN은 VIA CONFIGURE -> SYSTEM -> EEPROM의 확인 토글 세 개를 10초
   안에 모두 켜면 실행됩니다. 연결된 두 반쪽은 함께 초기화·재부팅하고,
   떨어진 반쪽은 각각 동작합니다. 실행 전 키맵을 백업하십시오.

   VIA CONFIGURE -> SYSTEM -> SYNC에서 EEPROM, INPUT, RGB 동기화를
   설정합니다. 모두 기본값은 켬입니다. EEPROM 변경 후에는 동기화가
   끝날 때까지 몇 초 기다리고, 반대쪽 VIA 화면은 새로고침하십시오.

■ LINK SPEED (두 반쪽을 잇는 케이블 속도)
   VIA CONFIGURE -> SYSTEM -> LINK에서 설정하며 기본값 High를 권장합니다.

   - High   460800 bps, 1 ms 주기 (기본값)
   - Medium 230400 bps, 2 ms 주기
   - Low    115200 bps, 4 ms 주기

   연결이 불안정할 때만 속도를 낮추십시오. 값을 고른 뒤 Apply를 켜면
   연결된 양쪽에 함께 적용됩니다. 부팅 시에는 Low로 먼저 연결한 뒤 저장된
   속도로 올라가며, 실패하면 이번 부팅은 Low로 유지되고 빨간색 LED가
   세 번 길게 깜빡입니다.

■ 빨간색 LED 신호
   RGB 설정과 관계없이 키 전체가 빨간색으로 상태를 알립니다.

   - 짧게 2번씩 3세트: 통신 코어 시작 실패. 전원을 다시 연결하고,
     반복되면 해당 반쪽을 다시 플래시하십시오.
   - 길게 3번: 저장된 LINK SPEED 적용 실패. 이번 부팅은 Low로 동작하므로
     케이블을 점검하거나 LINK SPEED를 낮추십시오.
   - 계속 켜짐: EEPROM SYNC 진행 중. 꺼질 때까지 전원과 연결 케이블을
     분리하지 마십시오.


======================================================================
Firmware Quick Guide - Split Keyboards (TOMAK series)
======================================================================

This guide covers the firmware for a split keyboard.


----------------------------------------------------------------------
Firmware
----------------------------------------------------------------------

This is modified QMK firmware. RP2040-based keyboards use a PIO/DMA matrix
scanner to raise performance from about 4 kHz to around 48 kHz, with further
optimization to reduce the scan-rate impact of lighting effects. A dedicated
communication core also minimizes scan-performance variation whether the split
cable is connected or not. It also adds TAPDANCE, SOCD, KKUK
(hold-and-cycle), DEBOUNCE, TAPPING, MOUSE, NKRO, plus split-only dual-host
operation, EEPROM/INPUT/RGB SYNC, LINK SPEED control, and red status alerts.


----------------------------------------------------------------------
Files
----------------------------------------------------------------------

■ .uf2
   Firmware file to flash to the keyboard. The same file goes on both halves.

■ VIA JSON (one per half, two files)
   Draft Definition file for VIA(usevia.app). Load it first to make VIA show
   KEYMAP, FEATURE, TAPDANCE, and SYSTEM controls.
   The two halves appear to USB as different devices, so there is one file for
   the left half (…-L-VIA.json) and one for the right (…-R-VIA.json). They
   describe the same board and differ only in which half they are. With one
   cable you need only the file for the half that is plugged in; in dual host,
   load both.

■ via_keycodes.txt
   Keycode examples for TAPDANCE fields. VIA may not recognize every latest
   QMK keycode name, so check this file before entering values such as KC_A,
   MO(1), or MT(MOD_LSFT,KC_A).


----------------------------------------------------------------------
Firmware Update
----------------------------------------------------------------------

**Flash both halves with the same firmware, together.** A pair left on two
different versions is not a supported configuration.

One half at a time, twice:

1. Enter Bootloader mode on that half.
2. Copy the .uf2 file to the RPI-RP2 drive.
3. The drive disappears when flashing is complete.

Bootloader options:
- VIA: turn on CONFIGURE -> SYSTEM -> BOOT -> Jump To BOOT and the half that
  currently holds the USB cable enters the bootloader at once. It acts the
  moment you switch it on, and the toggle always reads back off.
- Bootmagic: hold the key in that half's own top-left position while plugging
  in USB — each half uses its own. The key is a physical position, not a
  legend, so it stays there even if VIA remaps it.
- Physical reset: double-tap reset or briefly short RST/GND.
- Reset keycode: use QK_BOOT if it is placed in the keymap. The `default`
  keymap has it on the Fn layer at each half's own top-left position; a `via`
  keymap may not, so place it yourself in VIA or use one of the routes above.


----------------------------------------------------------------------
The Port Between The Halves
----------------------------------------------------------------------

The USB-C connector that joins the two halves is not a USB data port. It
carries a private signal line between the halves.

- Only the cable that joins the halves belongs in this port.
- Do not plug this port into a PC, a charger, or any other USB host. There is
  a limit to what firmware can protect against.


----------------------------------------------------------------------
Using VIA
----------------------------------------------------------------------

1. Open https://usevia.app.
2. Enable SETTINGS -> Show Design tab.
3. Load the provided VIA JSON in DESIGN -> Load Draft Definition. Load both
   the L and R files if you run dual host.
4. Configure keymap and features in CONFIGURE.
5. Use VIA Save when you want settings to persist.

A firmware update deletes the existing keymap. Back it up from SAVE + LOAD
first if you want to keep it.


----------------------------------------------------------------------
Key Features
----------------------------------------------------------------------

■ TAPDANCE
   Assign Tap, Hold, Double Tap, and Tap+Hold actions to one key. Configure
   TD0~TD7 in VIA CONFIGURE -> TAPDANCE, then place the matching TD key from
   KEYMAP -> CUSTOM. See via_keycodes.txt for input examples.

■ SOCD
   Keeps only the latest input when opposite directions are held together.
   Assign the key pairs in VIA CONFIGURE -> FEATURE -> SOCD. Check game and
   tournament rules before using it competitively.

■ KKUK   (formerly named Anti-Ghosting)
   Hold two or more basic keys still and the whole group is released and
   pressed again on a timer: holding a, s and d types asdasdasd, not asddddd.
   It has nothing to do with matrix ghosting, hence the new name. Enable it in
   VIA CONFIGURE -> FEATURE -> KKUK and adjust Delay/Repeat as needed. Enabled
   SOCD keys are excluded.

■ DEBOUNCE
   Adjust switch chatter filtering in VIA CONFIGURE -> FEATURE -> DEBOUNCE.
   The default is Balanced, 5 ms; increase it only when chatter appears.

■ TAPPING
   Adjust Mod-Tap and Layer-Tap timing in VIA CONFIGURE -> FEATURE ->
   TAPPING. The default is 200 ms.

■ MOUSE
   Adjust mouse-key cursor and wheel speed or acceleration in VIA CONFIGURE ->
   FEATURE -> MOUSE. Try the defaults first, then make small changes if they
   feel slow or fast. It has no effect unless mouse keys are in your keymap.

■ NKRO
   Enable effectively unlimited simultaneous key input in VIA CONFIGURE ->
   FEATURE -> NKRO. Turn it off for an old BIOS or boot menu that cannot read it.

■ EEPROM CLEAN / SYNC
   CLEAN runs when all three confirmation toggles in VIA CONFIGURE -> SYSTEM
   -> EEPROM are enabled within 10 seconds. Connected halves erase and reboot
   together; disconnected halves act separately. Back up your keymap first.

   Configure EEPROM, INPUT, and RGB synchronization in VIA CONFIGURE -> SYSTEM
   -> SYNC. All are on by default. After an EEPROM change, wait a few seconds
   for synchronization and refresh VIA on the other half to see the update.

■ LINK SPEED (the cable between the two halves)
   Set it in VIA CONFIGURE -> SYSTEM -> LINK. High is recommended.

   - High   460800 bps, 1 ms poll (default)
   - Medium 230400 bps, 2 ms poll
   - Low    115200 bps, 4 ms poll

   Lower it only when the link is unstable. Choose a value and turn on Apply
   to change both connected halves. At boot the pair first meets at Low and
   then raises to the stored speed; on failure it stays at Low for that boot
   and reports three long red pulses.

■ Red LED signals
   Whole-keyboard red status alerts appear regardless of RGB settings.

   - Two short pulses, repeated three times: communication-core startup
     failed. Reconnect power; if it repeats, reflash that half.
   - Three long pulses: the stored LINK SPEED could not be applied. This boot
     stays at Low; check the cable or select a lower speed.
   - Steady red: EEPROM SYNC is active. Do not disconnect power or the link
     cable until it turns off.
