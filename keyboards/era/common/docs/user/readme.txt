======================================================================
Firmware Quick Guide
======================================================================

----------------------------------------------------------------------
펌웨어
----------------------------------------------------------------------

수정 QMK 펌웨어입니다. RP2040 기반 키보드는 매트릭스 스캔에
PIO/DMA 구조를 도입하여 스캔 성능을 약 4 kHz에서 48 kHz 수준으로
높였고, 조명 효과가 스캔레이트에 미치는 영향도 줄였습니다. 그 밖에
TAPDANCE, SOCD, KKUK, DEBOUNCE, TAPPING, MOUSE,
NKRO, 조명·인디케이터, EEPROM 초기화 기능을 추가했습니다.


----------------------------------------------------------------------
제공 파일
----------------------------------------------------------------------

■ .uf2
   키보드에 올리는 펌웨어 파일입니다.

■ VIA JSON
   VIA(usevia.app)가 키보드를 인식하도록 하는 Draft Definition 파일입니다.
   VIA에서 키맵이나 FEATURE/TAPDANCE/SYSTEM 메뉴를 쓰려면 먼저
   로드해야 합니다.

■ via_keycodes.txt
   TAPDANCE 입력창에 넣을 수 있는 keycode 예시 문서입니다.
   VIA는 최신 QMK keycode 이름을 모두 인식하지 못할 수 있으므로,
   KC_A, MO(1), MT(MOD_LSFT,KC_A) 같은 값을 직접 입력할 때 먼저
   이 파일을 확인하세요.


----------------------------------------------------------------------
펌웨어 업데이트
----------------------------------------------------------------------

1. 키보드를 Bootloader 모드로 진입시킵니다.
2. PC에 RPI-RP2 드라이브가 나타나면 .uf2 파일을 복사합니다.
3. 잠시 후 드라이브가 사라지고 업데이트가 완료됩니다.

Bootloader 진입 방법:
- VIA: CONFIGURE -> SYSTEM -> BOOT에서 Jump To BOOT를 켜면 곧바로
  부트로더로 들어갑니다. 켜는 즉시 동작하며, 이 토글은 화면에 항상
  꺼진 것으로 보입니다.
- Bootmagic: 키보드 가장 왼쪽 위 자리의 키를 누른 채 USB 연결.
  이 키는 자판 배열이 아니라 물리 위치로 정해지므로, VIA에서 다른
  키로 바꿔 두어도 자리는 그대로입니다.
- 물리 리셋: 리셋 버튼을 빠르게 두 번 누르거나 RST/GND를 짧게 접촉
- 리셋 키코드: 키맵에 QK_BOOT이 배치되어 있을 때 사용합니다.


----------------------------------------------------------------------
VIA 사용
----------------------------------------------------------------------

1. https://usevia.app 접속
2. SETTINGS -> Show Design tab 활성화
3. DESIGN -> Load Draft Definition에서 제공된 VIA JSON 로드
4. CONFIGURE에서 키맵과 기능 설정
5. 설정을 보존하려면 VIA의 Save 기능 사용

펌웨어를 업데이트하면 기존 키맵이 삭제됩니다. 키맵을 유지하려면
VIA의 SAVE + LOAD에서 미리 백업하십시오.


----------------------------------------------------------------------
주요 기능
----------------------------------------------------------------------

■ TAPDANCE
   한 키에 네 가지 동작을 담습니다.

   - On Tap         짧게 눌림으로 판정됐을 때 입력되는 키코드.
   - On Hold        길게 눌림으로 판정됐을 때 입력되는 키코드.
   - On Double Tap  Term 안에 두 번 탭했을 때 입력되는 키코드.
   - Tap+Hold       한 번 탭한 뒤 이어서 길게 눌렀을 때 입력되는 키코드.
   - Term (ms)      어느 동작인지 판단하기까지 기다리는 시간.

   VIA CONFIGURE -> TAPDANCE에서 TD0~TD7을 설정하고, KEYMAP -> TAPDANCE의
   같은 TD 키를 원하는 위치에 배치하십시오. 입력 예시는
   via_keycodes.txt에 있습니다.

■ SOCD
   반대 방향키가 함께 눌리면 마지막 입력만 남기는 Last Input Wins
   기능입니다. VIA CONFIGURE -> FEATURE -> SOCD에서 사용할 키 조합을
   지정하십시오.

■ KKUK
   기본 키 두 개 이상을 가만히 누르고 있으면 그 묶음 전체를 주기적으로
   뗐다가 다시 눌러 줍니다. asd를 누르고 있으면 asddddd가 아니라
   asdasdasd가 입력됩니다. VIA CONFIGURE -> FEATURE -> KKUK에서 켜고
   Delay/Repeat
   값을 조정하십시오. SOCD에 지정한 키는 제외됩니다.

■ DEBOUNCE
   VIA CONFIGURE -> FEATURE -> DEBOUNCE에서 스위치 채터링을 조정합니다.
   기본값은 Balanced, 5 ms이며 채터링이 보일 때만 조금씩 늘리십시오.

■ TAPPING
   VIA CONFIGURE -> FEATURE -> TAPPING에서 Mod-Tap과 Layer-Tap의
   짧게/길게 누름 판정 시간을 조정합니다. 기본값은 200 ms입니다.

■ MOUSE (마우스 키)
   VIA CONFIGURE -> FEATURE -> MOUSE에서 조정합니다. 키맵에 마우스 키가
   없으면 아무 영향이 없습니다.

   - Cursor Acceleration   시작 속도에서 최고 속도까지 걸리는 시간.
                           Off면 시작 속도로 고정됩니다.
   - Cursor Start Speed    키를 누른 직후 한 스텝의 이동 거리.
   - Cursor Top Speed      가속이 끝난 뒤 한 스텝의 이동 거리.
   - Cursor Speed          가속이 Off일 때의 한 스텝 이동 거리.
   - Cursor Steps Per Second  초당 이동 스텝 수. 클수록 부드러워지고
                           가속 시간은 그대로입니다.
   - Wheel Rate            초당 스크롤 스텝 수.
   - Wheel Acceleration    누르고 있는 동안 스크롤이 빨라지는 정도.

■ NKRO
   동시 입력 키를 무제한으로 확장합니다. 오래된 시스템의 BIOS나 KVM 스위치가
   입력을 받지 못한다면 VIA CONFIGURE -> FEATURE -> NKRO에서 끄십시오.
   끄면 6KRO로 동작해 최대 6키까지 동시입력됩니다.

■ LIGHTING (조명)
   VIA CONFIGURE -> LIGHTING에서 밝기, 효과, 속도와 색을 조정합니다.
   키보드에 따라 Backlight, RGB Matrix, Underglow, INDICATOR 중 지원하는
   메뉴만 보입니다. INDICATOR는 Caps/Scroll/Num Lock 표시를 지정합니다.

■ EEPROM CLEAN
   키맵과 모든 설정을 초기화합니다.

   VIA CONFIGURE -> SYSTEM -> EEPROM에서 10초 안에 확인 토글 세 개를 모두
   켜면 실행됩니다. 저장된 모든 데이터가 초기화되고 키보드가 재시작됩니다.
   10초를 넘기면 켜 둔 토글이 저절로 풀립니다.

   실행 전 SAVE + LOAD에서 키맵을 백업하십시오.


======================================================================
Firmware Quick Guide
======================================================================

----------------------------------------------------------------------
Firmware
----------------------------------------------------------------------

This is modified QMK firmware. RP2040-based keyboards use a PIO/DMA matrix
scanner to raise performance from about 4 kHz to around 48 kHz, with further
optimization to reduce the scan-rate impact of lighting effects. It also adds
TAPDANCE, SOCD, KKUK (hold-and-cycle), DEBOUNCE, TAPPING, MOUSE,
NKRO, lighting and indicator controls, and EEPROM reset.


----------------------------------------------------------------------
Files
----------------------------------------------------------------------

■ .uf2
   Firmware file to flash to the keyboard.

■ VIA JSON
   Draft Definition file for VIA(usevia.app). Load it first to make VIA show
   KEYMAP, FEATURE, TAPDANCE, and SYSTEM controls.

■ via_keycodes.txt
   Keycode examples for TAPDANCE fields. VIA may not recognize every latest
   QMK keycode name, so check this file before entering values such as KC_A,
   MO(1), or MT(MOD_LSFT,KC_A).


----------------------------------------------------------------------
Firmware Update
----------------------------------------------------------------------

1. Enter Bootloader mode.
2. Copy the .uf2 file to the RPI-RP2 drive.
3. The drive disappears when flashing is complete.

Bootloader options:
- VIA: turn on CONFIGURE -> SYSTEM -> BOOT -> Jump To BOOT and the keyboard
  enters the bootloader at once. It acts the moment you switch it on, and the
  toggle always reads back off.
- Bootmagic: hold the key in the keyboard's top-left position while plugging
  in USB. The key is a physical position, not a legend, so it stays there even
  if VIA remaps it.
- Physical reset: double-tap reset or briefly short RST/GND.
- Reset keycode: use QK_BOOT if it is placed in the keymap.


----------------------------------------------------------------------
Using VIA
----------------------------------------------------------------------

1. Open https://usevia.app.
2. Enable SETTINGS -> Show Design tab.
3. Load the provided VIA JSON in DESIGN -> Load Draft Definition.
4. Configure keymap and features in CONFIGURE.
5. Use VIA Save when you want settings to persist.

A firmware update deletes the existing keymap. Back it up from SAVE + LOAD
first if you want to keep it.


----------------------------------------------------------------------
Key Features
----------------------------------------------------------------------

■ TAPDANCE
   Four actions on one key.

   - On Tap         Keycode sent when the press is judged a short one.
   - On Hold        Keycode sent when the press is judged a long one.
   - On Double Tap  Keycode sent when the key is tapped twice inside Term.
   - Tap+Hold       Keycode sent when a tap is followed by holding the key.
   - Term (ms)      How long the keyboard waits before deciding which one.

   Configure
   TD0~TD7 in VIA CONFIGURE -> TAPDANCE, then place the matching TD key from
   KEYMAP -> TAPDANCE. See via_keycodes.txt for input examples.

■ SOCD
   Keeps only the latest input when opposite directions are held together.
   Assign the key pairs in VIA CONFIGURE -> FEATURE -> SOCD.

■ KKUK
   Hold two or more basic keys still and the whole group is released and
   pressed again on a timer: holding a, s and d types asdasdasd, not asddddd.
   Enable it in VIA CONFIGURE -> FEATURE -> KKUK and adjust Delay/Repeat as
   needed. Enabled
   SOCD keys are excluded.

■ DEBOUNCE
   Adjust switch chatter filtering in VIA CONFIGURE -> FEATURE -> DEBOUNCE.
   The default is Balanced, 5 ms; increase it only when chatter appears.

■ TAPPING
   Adjust Mod-Tap and Layer-Tap timing in VIA CONFIGURE -> FEATURE ->
   TAPPING. The default is 200 ms.

■ MOUSE
   Set it in VIA CONFIGURE -> FEATURE -> MOUSE. It has no effect unless mouse
   keys are in your keymap.

   - Cursor Acceleration   Time from start speed to top speed. Off holds the
                           start speed.
   - Cursor Start Speed    Distance per step the instant you press the key.
   - Cursor Top Speed      Distance per step once acceleration has finished.
   - Cursor Speed          Distance per step while acceleration is Off.
   - Cursor Steps Per Second  Move steps per second. Higher is smoother and
                           does not change the acceleration time.
   - Wheel Rate            Scroll steps per second.
   - Wheel Acceleration    How much scrolling speeds up while you hold.

■ NKRO
   Expands simultaneous key input without a limit. Turn it off in VIA
   CONFIGURE -> FEATURE -> NKRO if an old BIOS or a KVM switch cannot see your
   typing; off, the keyboard falls back to 6KRO and registers six keys at once.

■ LIGHTING
   Adjust brightness, effects, speed, and colour in VIA CONFIGURE -> LIGHTING.
   Only the supported Backlight, RGB Matrix, Underglow, and INDICATOR menus
   appear. INDICATOR assigns Caps, Scroll, or Num Lock status LEDs.

■ EEPROM CLEAN
   Erases the keymap and every setting.

   Switch all three confirm toggles in VIA CONFIGURE -> SYSTEM -> EEPROM within
   ten seconds to run it. Everything stored is erased and the keyboard restarts.
   Miss the ten seconds and the toggles you switched clear themselves.

   Back up your keymap from SAVE + LOAD first.
