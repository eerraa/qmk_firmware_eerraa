======================================================================
ERA Firmware Quick Guide - 일체형 키보드
======================================================================

이 문서는 ERA 펌웨어를 쓰는 일체형 키보드용 안내입니다.
분리형(스플릿) 키보드 - TOMAK 시리즈 - 는 readme_split.txt를 보십시오.


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
   한 키를 짧게 누르기, 길게 누르기, 두 번 누르기, 두 번 누른 뒤
   길게 누르기처럼 여러 동작으로 나누어 쓰는 기능입니다.

   예: Tap = Esc, Hold = Fn 레이어, Double Tap = Caps Lock

   사용:
   - VIA CONFIGURE -> TAPDANCE에서 TD0~TD7 슬롯 설정
   - On Tap / On Hold 등에 keycode 직접 입력
   - keycode 예시는 via_keycodes.txt 참고
   - 키맵에 TD0~TD7 키를 실제 위치에 배치

   TD 슬롯을 설정만 하고 키맵에 TD 키를 배치하지 않으면 동작하지 않습니다.
   TD0~TD7은 VIA의 KEYMAP -> CUSTOM 목록에 있습니다.

■ SOCD
   서로 반대되는 방향키가 동시에 눌렸을 때 어떤 입력을 남길지 정합니다.
   ERA 펌웨어는 Last Input Wins 방식입니다. 즉, 마지막에 누른 키가 이깁니다.

   예: A를 누른 상태에서 D를 누르면 D만 입력됩니다.

   사용:
   - VIA CONFIGURE -> FEATURE -> SOCD
   - Left/Right 또는 Up/Down Enable
   - A/D, W/S, 방향키처럼 사용할 키 지정

   SOCD는 Street Fighter 6, Tekken 8 같은 격투게임을 키보드나
   레버리스 컨트롤러로 플레이하는 유저들이 신경 쓰는 기능입니다.
   경쟁 환경에서는 게임/랭크/대회 규칙을 먼저 확인하세요.

■ Anti-Ghosting (KKUK / 꾹보드 보정)
   여기서의 Anti-Ghosting은 물리 회로를 바꾸는 기능이 아니라,
   여러 키를 계속 누르고 있을 때 눌린 키 상태를 주기적으로 새로
   보고하는 보정 기능입니다.

   국내에서는 비슷한 동시입력 반복 보정을 "꾹보드" 또는 "KKUK"이라고
   부르는 경우가 있습니다. 메이플스토리 일부 유저처럼 두 키를 오래
   누른 상태의 반복 입력감을 중요하게 보는 유저가 이 계열 기능을 찾습니다.

   사용:
   - VIA CONFIGURE -> FEATURE -> Anti-Ghosting
   - Enable 켜기
   - First Delay Time / Repeat Time은 기본값 근처에서 조금씩 조정

   SOCD에 지정한 키는 이 보정에서 제외됩니다.

■ DEBOUNCE
   스위치 채터링을 정리하는 기능입니다.

   사용:
   - VIA CONFIGURE -> FEATURE -> DEBOUNCE
   - 기본값: Balanced, 5 ms
   - 채터링이 보이면 시간을 늘리고, 반응을 빠르게 하고 싶으면 줄입니다.

■ TAPPING
   Mod-Tap, Layer-Tap처럼 짧게 누름과 길게 누름을 구분하는 시간을
   조정합니다.

   사용:
   - VIA CONFIGURE -> FEATURE -> TAPPING
   - Global Tapping Term 기본값: 200 ms
   - 짧게 눌렀는데 Hold가 되면 시간을 늘리고, 길게 눌렀는데 Tap이 되면
     시간을 줄입니다.

■ MOUSE (마우스 키)
   마우스 키(MS_LEFT, MS_UP 등)로 움직이는 커서와 휠의 속도를 정합니다.
   키맵에 마우스 키를 넣지 않았다면 아무 영향이 없습니다.

   커서 속도는 "한 번에 몇 px" 과 "초당 몇 걸음" 의 곱입니다. 둘을 따로
   두는 이유는, 같은 속도라도 한 걸음이 크면 움직임이 뚝뚝 끊겨 보이기
   때문입니다 — 부드럽게 하려면 걸음을 줄이고 초당 걸음 수를 올리십시오.

   사용:
   - VIA CONFIGURE -> FEATURE -> MOUSE
   - Cursor Acceleration: Off 로 두면 누르고 있는 내내 같은 속도이고,
     0.5~2.0 s 는 Start Speed 에서 Top Speed 까지 올라가는 데 걸리는
     시간입니다. 기본값 1.0 s
   - Cursor Speed (가속이 Off 일 때만 보입니다): 그 등속 속도.
   - Cursor Start Speed / Cursor Top Speed (가속이 켜져 있을 때만 보입니다):
     누른 직후의 한 걸음과, 끝까지 올라갔을 때의 한 걸음.
     기본값 4 px / 16 px
   - Cursor Steps Per Second: 커서가 1초에 몇 걸음 내딛는지. 가속의 세기가
     아니라 빈도입니다. 기본값 100 /s
   - Wheel Rate / Wheel Acceleration: 휠 쪽의 같은 두 가지.
     기본값 13 /s, Strong

   화면 크기에 맞추기:
   기본값은 2560x1440 근처를 겨냥했습니다. 정밀도에 해당하는 값들 — 등속
   Cursor Speed, Start Speed, 가속 시간 — 은 화면이 커져도 그대로 쓸 수
   있습니다. 버튼 하나는 어느 해상도에서도 같은 픽셀 수이기 때문입니다.
   화면 폭을 타는 것은 Top Speed 하나뿐이고, 화면을 가로지르는 데 1.5~2.5초
   걸리게 맞추면 대체로 편합니다.

     1920 폭    Top  8 px      2560 폭    Top 16 px
     3840 폭    Top 24 px      5120 폭    Top 32 px

   위 표는 초당 걸음 수 100 /s 기준입니다. 그 값을 바꾸면 두 숫자가 곱해지는
   관계라 Top 도 같은 비율로 조정해야 합니다.

   키보드가 보내는 것은 픽셀이 아니라 상대 이동량이라, OS 의 포인터 속도
   설정이 이 위에 배율로 더 곱해집니다 — 같은 설정도 PC 마다 조금씩 다르게
   느껴집니다.

■ NKRO
   동시에 누른 키를 몇 개까지 PC에 보고할지 정합니다. 켜면 개수 제한이
   사실상 없어지고, 끄면 일반적인 6키 방식으로 보고합니다.

   사용:
   - VIA CONFIGURE -> FEATURE -> NKRO -> Enable
   - 타이핑 중에 바꿔도 안전합니다. 전환 순간 눌려 있던 키는 펌웨어가
     정리하므로 키가 눌린 채로 남지 않습니다.
   - 아주 오래된 PC의 BIOS나 부팅 메뉴에서 키보드가 안 먹으면 꺼 보세요.

■ LIGHTING (조명)
   조명 메뉴는 키보드마다 다릅니다. 아래 중 그 키보드가 가진 것만 보입니다.

   - Backlight
     단색 백라이트의 밝기와 효과를 정합니다. 효과는 None / Breathing /
     Blink-Out on Keypress / Blink-In on Keypress 네 가지이고, Breathing을
     고르면 주기가, Blink를 고르면 속도가 함께 나타납니다.
     Blink-Out은 평소 켜져 있다가 키를 누르면 잠깐 꺼지고, Blink-In은 평소
     꺼져 있다가 키를 누르면 잠깐 켜집니다.

     백라이트가 락 표시등만 밝히는 키보드에는 이 메뉴가 없습니다. 그 키보드의
     백라이트는 조명이 아니라 락 표시등의 전원이라서 끌 수 있으면 안 되고,
     그래서 조절 항목도 조명 키코드도 제공하지 않습니다.

   - RGB Matrix / Underglow
     밝기, 효과, 효과 속도, 색을 정합니다.

   - INDICATOR
     락 표시등입니다. Caps Lock / Scroll Lock / Num Lock 중 무엇을 표시할지,
     그리고 그 밝기와 색을 정합니다. 표시등이 두 개인 키보드는 1번과 2번을
     따로 정합니다. Off를 고르면 그 LED는 표시등을 그만두고 보통 RGB LED로
     돌아갑니다.

     Indicator Enable 토글이 있는 키보드에서는 그 토글 하나로 표시등 전체를
     껐다 켤 수 있고, 끄면 해당 LED들이 모두 보통 RGB LED로 돌아갑니다.

■ EEPROM CLEAN
   저장된 키맵과 설정을 모두 지우고 공장 기본값으로 되돌립니다.

   사용:
   - VIA CONFIGURE -> SYSTEM -> EEPROM에서 확인 토글 세 개를 모두
     켜면 EEPROM을 초기화하고 재부팅합니다.
   - 세 개는 첫 토글을 켠 뒤 10초 안에 모두 켜야 합니다. 시간이 지나면
     켜 두었던 토글이 전부 풀리고, 처음부터 다시 켜면 됩니다.
   - 켜 둔 토글을 다시 끄면 그 자리에서 취소됩니다.

   EEPROM 초기화 전에는 키맵 백업을 권장합니다.


======================================================================
ERA Firmware Quick Guide - Non-Split Keyboards
======================================================================

This guide covers ERA firmware on a one-piece keyboard. For a split
keyboard - the TOMAK series - see readme_split.txt.


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
   One key can do different actions for tap, hold, double tap, or tap-hold.

   Example: Tap = Esc, Hold = Fn layer, Double Tap = Caps Lock

   Use VIA CONFIGURE -> TAPDANCE to set TD0~TD7 and enter keycodes directly,
   then place the matching TD0~TD7 key in the keymap. TD0~TD7 are in VIA's
   KEYMAP -> CUSTOM list.
   See via_keycodes.txt for VIA-compatible keycode examples.

■ SOCD
   Handles opposite direction keys. ERA firmware uses Last Input Wins.
   Example: hold A, then press D -> only D is sent.

   Set it in VIA CONFIGURE -> FEATURE -> SOCD.
   SOCD is relevant to keyboard/leverless fighting-game players, including
   Street Fighter 6 and Tekken 8 users. Check game and tournament rules first.

■ Anti-Ghosting (KKUK / held-key refresh)
   This is a held-key report refresh helper, not a physical matrix change.
   Some Korean users call this behavior KKUK or kkuk-board. MapleStory users,
   for example, may care about long held-key repeat behavior.

   Set it in VIA CONFIGURE -> FEATURE -> Anti-Ghosting.
   Start near the defaults for First Delay Time and Repeat Time.
   Keys assigned to an enabled SOCD pair are excluded from this refresh.

■ DEBOUNCE
   Cleans switch chatter. Default: Balanced, 5 ms.
   Increase time for chatter; decrease it for faster response.

■ TAPPING
   Adjusts tap-hold decision time for Mod-Tap and Layer-Tap keys.
   Default Global Tapping Term: 200 ms.

■ MOUSE
   Sets cursor and wheel speed for the mouse keys (MS_LEFT, MS_UP and the
   rest). It changes nothing if your keymap has no mouse keys on it.

   Cursor speed is "how many px at a time" times "how many steps a second".
   They are separate controls because the same speed made of larger steps
   looks like the cursor is jumping rather than moving -- for smoother motion,
   use a smaller step and raise the steps per second.

   Set it in VIA CONFIGURE -> FEATURE -> MOUSE.
   Cursor Acceleration set to Off holds one speed for as long as the key is
   down; 0.5 to 2.0 s is how long a held key takes to climb from the start
   speed to the top speed (default 1.0 s). Cursor Speed appears only with
   acceleration off and is that one constant speed. Cursor Start Speed and
   Cursor Top Speed appear only with it on, and are the step taken right after
   the press and the step reached at the top of the climb (defaults 4 px and
   16 px). Cursor Steps Per Second is how many steps a second the cursor takes
   -- a frequency, not an acceleration strength (default 100 /s). Wheel Rate
   and Wheel Acceleration are the same two for the wheel (defaults 13 /s and
   Strong).

   Matching your screen:
   The defaults aim at about 2560x1440. The precision settings -- the constant
   Cursor Speed, the start speed and the climb -- carry over to any screen,
   because a button is the same number of pixels at any resolution. Only the
   top speed follows screen width, and aiming for one and a half to two and a
   half seconds to cross the screen is comfortable for most people.

     1920 wide    Top  8 px      2560 wide    Top 16 px
     3840 wide    Top 24 px      5120 wide    Top 32 px

   Those rows assume 100 steps a second. The two multiply, so changing the
   steps per second means moving the top speed by the same factor.

   The keyboard sends relative motion rather than pixels, so your operating
   system's pointer speed multiplies on top of this -- the same setting feels a
   little different per PC.

■ NKRO
   Sets how many simultaneously held keys are reported to the PC. On, the
   count is effectively unlimited; off, the keyboard reports the usual six.

   Set it in VIA CONFIGURE -> FEATURE -> NKRO -> Enable.
   It is safe to change while typing: the firmware clears whatever was held
   across the switch, so no key is left stuck down.
   If a very old PC's BIOS or boot menu does not see the keyboard, try
   turning it off.

■ LIGHTING
   The lighting menus differ per keyboard. You see only the ones your keyboard
   has.

   - Backlight
     Brightness and effect for a single-colour backlight. The effects are None,
     Breathing, Blink-Out on Keypress and Blink-In on Keypress; Breathing adds
     a period and either Blink adds a speed.
     Blink-Out is normally lit and goes dark for a moment on a keypress;
     Blink-In is normally dark and lights for a moment.

     Keyboards whose backlight lights nothing but the lock indicators do not
     have this menu. There the backlight is the indicators' power rather than
     lighting, so it must not be switchable off, and no control and no lighting
     keycode is offered for it.

   - RGB Matrix / Underglow
     Brightness, effect, effect speed and colour.

   - INDICATOR
     The lock indicators. Choose which of Caps Lock, Scroll Lock or Num Lock
     each one shows, and its brightness and colour; a keyboard with two of them
     sets 1 and 2 separately. Choosing Off stops that LED being an indicator
     and returns it to being an ordinary RGB LED.

     Where an Indicator Enable toggle is present, it switches the whole
     indicator role off in one place and those LEDs go back to being ordinary
     RGB LEDs.

■ EEPROM CLEAN
   Erases the stored keymap and settings and returns the keyboard to its
   defaults.

   Turn on all three confirm toggles in VIA CONFIGURE -> SYSTEM -> EEPROM and
   the keyboard erases EEPROM and reboots. All three have to be on within 10
   seconds of the first one. After that the ones already on are released and
   you start over. Turning a toggle back off cancels.

   Back up your keymap from SAVE + LOAD before you clean the EEPROM — CLEAN
   erases it too.
