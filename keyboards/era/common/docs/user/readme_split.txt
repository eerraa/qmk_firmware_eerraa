======================================================================
ERA Firmware Quick Guide - 분리형 키보드 (TOMAK 시리즈)
======================================================================

이 문서는 ERA 펌웨어를 쓰는 분리형(스플릿) 키보드용 안내입니다.
일체형 키보드는 readme.txt를 보십시오.

분리형에만 있는 것은 두 반쪽 사이의 SYNC 기능, 두 반쪽을 잇는 케이블
속도(LINK SPEED), 그리고 상태를 알리는 빨간색 LED 신호입니다.
나머지 기능은 일체형과 같습니다.


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
   한 키를 짧게 누르기, 길게 누르기, 두 번 누르기, 두 번 누른 뒤
   길게 누르기처럼 여러 동작으로 나누어 쓰는 기능입니다.

   예: Tap = Esc, Hold = Fn 레이어, Double Tap = Caps Lock

   사용:
   - VIA CONFIGURE -> TAPDANCE에서 TD0~TD7 슬롯 설정
   - On Tap / On Hold 등에 keycode 직접 입력
   - keycode 예시는 via_keycodes.txt 참고
   - 키맵에 TD0~TD7 키를 실제 위치에 배치

   TD 슬롯을 설정만 하고 키맵에 TD 키를 배치하지 않으면 동작하지 않습니다.
   TOMAK의 VIA JSON은 커스텀 키코드를 선언하므로 VIA의 KEYMAP -> CUSTOM
   목록에서 바로 배치할 수 있습니다.

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

   이 설정은 EEPROM SYNC로 반대쪽에도 전달됩니다.

■ NKRO
   동시에 누른 키를 몇 개까지 PC에 보고할지 정합니다. 켜면 개수 제한이
   사실상 없어지고, 끄면 일반적인 6키 방식으로 보고합니다.

   사용:
   - VIA CONFIGURE -> FEATURE -> NKRO -> Enable
   - 타이핑 중에 바꿔도 안전합니다. 전환 순간 눌려 있던 키는 펌웨어가
     정리하므로 키가 눌린 채로 남지 않습니다.
   - 아주 오래된 PC의 BIOS나 부팅 메뉴에서 키보드가 안 먹으면 꺼 보세요.

■ EEPROM CLEAN / SYNC
   - CLEAN: VIA CONFIGURE -> SYSTEM -> EEPROM에서 확인 토글 세 개를
     모두 켜면 EEPROM을 초기화하고 재부팅합니다.
     세 개는 첫 토글을 켠 뒤 10초 안에 모두 켜야 합니다. 시간이 지나면
     켜 두었던 토글이 전부 풀리고, 처음부터 다시 켜면 됩니다.
     켜 둔 토글을 다시 끄면 그 자리에서 취소됩니다.
     두 반쪽이 케이블로 연결되어 있으면 CLEAN은 양쪽을 함께
     초기화하고 양쪽이 같이 재부팅합니다. 한쪽만 연결된 상태에서
     하면 그 반쪽만 초기화됩니다.
   - SYNC: VIA CONFIGURE -> SYSTEM -> SYNC
     · EEPROM SYNC: 양쪽 반쪽의 설정을 자동으로 맞춥니다. 기본값은
       켬입니다. 케이블 한 개로 쓸 때든, 반쪽 두 개를 각각 USB
       케이블로 연결해 쓸 때(듀얼 호스트)든 똑같이 동작합니다.
       듀얼 호스트에서는 어느 쪽 반쪽에서 VIA로 설정을 바꾸든
       약 1초 뒤 반대쪽으로 옮겨갑니다. 그동안 잠깐 빨간색 LED가
       켜질 수 있습니다.
       양쪽을 짧은 간격으로 번갈아 고치면 한쪽 변경이 밀릴 수
       있으니, 한쪽에서 고친 뒤 몇 초 두었다가 다른 쪽으로
       넘어가면 확실합니다.
       반대쪽에서 넘어온 변경은 VIA 화면에 바로 보이지 않습니다.
       VIA는 새로고침할 때만 키맵을 다시 읽어옵니다. 설정이
       사라진 것처럼 보이면 먼저 새로고침해 보십시오.
     · INPUT SYNC: 듀얼 호스트에서 레이어와 짧게/길게 판정을 양쪽
       반쪽이 함께 씁니다. 기본값은 켬입니다.
     · RGB SYNC: 듀얼 호스트에서 양쪽 반쪽의 조명을 맞춥니다.
       기본값은 켬입니다.

EEPROM 초기화 전에는 키맵 백업을 권장합니다.

■ LINK SPEED (두 반쪽을 잇는 케이블 속도)
   VIA CONFIGURE -> SYSTEM -> LINK
   기본값은 High이고, 대부분은 그대로 두시면 됩니다.

   - High   460800 bps, 1 ms 주기 (기본값)
   - Medium 230400 bps, 2 ms 주기
   - Low    115200 bps, 4 ms 주기

   낮출 이유는 하나뿐입니다. 케이블이 길거나 상태가 좋지 않아 두 반쪽이
   서로를 자꾸 놓치면, 속도를 낮추는 쪽이 더 안정적으로 붙습니다.
   이유 없이 낮추면 느려지기만 합니다.

   바뀌는 것: 듀얼 호스트에서 한쪽 반쪽의 레이어 키가 반대쪽에 전달되는
   시간이 Low에서 최대 4 ms까지 늦어집니다. 키 입력 자체는 각 반쪽이
   자기 PC로 직접 보내므로 이 설정과 무관합니다.

   적용 방법:
   1. Split Link Speed에서 원하는 값을 고릅니다. 이때는 아무 일도
      일어나지 않습니다.
   2. Apply를 켭니다. 두 반쪽이 같은 순간에 새 속도로 바뀝니다.
      키보드는 재부팅하지 않습니다. 값이 실제로 바뀐 뒤에, Apply를
      누른 쪽 VIA는 USB가 한 번 끊겼다 다시 붙고 Apply는 꺼진
      상태로 다시 읽힙니다. 이미 그 값이거나 Apply가 먹지 않으면
      USB는 그대로이고 Apply는 켜진 채로 남습니다.

   켤 때마다 두 반쪽은 먼저 Low로 붙고, 화면·동기 상태가 오간 뒤
   저장해 둔 목표 속도로 올립니다. 듀얼 호스트에서는 왼쪽 반쪽의
   저장 값, USB가 한쪽에만 꽂힌 때는 USB가 꽂힌 반쪽(HOST)의 저장
   값이 목표입니다. 케이블이 그 속도를 못 버티면 이번 켜짐은 Low에
   남고, 저장된 High/Medium은 지우지 않습니다. 다음에 켜면 다시
   Low로 만나 다시 시도합니다. Low조차 못 버티는 케이블은 붙지
   않습니다.

   따로 맞출 때의 규칙:
   - 붙어 있을 때는 어느 쪽 반쪽에서 Apply 해도 양쪽이 함께 바뀝니다.
   - 케이블을 뺀 채 한 반쪽만 USB 로 연결해 적용하면 그 반쪽만
     바뀝니다. 듀얼 호스트로 쓸 쌍의 값을 이렇게 바꾸려면 왼쪽
     반쪽에서 하십시오. USB가 한쪽에만 꽂힌 채로 쓸 때는 USB가
     꽂힌 반쪽에서 하십시오. 다른 쪽에만 따로 적용한 값은 다시
     이어 붙는 순간 목표 쪽으로 덮입니다.

■ 빨간색 LED 신호
   키 전체가 빨간색으로 켜지는 것은 설정이 아니라 상태 알림입니다.
   세 가지가 있고, 켜지는 모양으로 구분합니다.
   RGB를 꺼 두었거나 밝기를 0으로 두었어도 이 알림은 나옵니다.

   - 깜빡깜빡 . . 깜빡깜빡 . . 깜빡깜빡 (2번씩 3세트, 약 3.7초)
     부팅할 때 한 번만 나오고, 끝나면 평소 RGB 효과로 돌아옵니다.
     그 반쪽의 통신 코어가 시작되지 못했다는 뜻이며, 양쪽 반쪽을 잇는
     통신이 동작하지 않습니다. 그 반쪽 자체의 키 입력과 VIA는 정상
     동작하므로, 한쪽만 쓰는 상태로는 계속 사용할 수 있습니다.
     전원을 뺐다 다시 꽂아 보시고, 반복되면 그 반쪽에 펌웨어를 다시
     올려 보세요. 그래도 같으면 문의해 주세요.

   - 길게 켜졌다 꺼졌다 세 번
     케이블이 저장해 둔 속도(High/Medium)를 못 버텨 이번 켜짐은
     Low로 남은 상태입니다. VIA의 LINK SPEED 값은 그대로입니다.
     케이블을 점검하거나, 왼쪽(듀얼 호스트) 또는 USB가 꽂힌 반쪽
     (HOST-PEER)에서 Low로 Apply 하면 다음 부팅에 다시 시도하지
     않습니다. 통신 코어 실패 신호와는 모양이 다르고, 그 신호가
     나오는 동안에는 이 신호가 나오지 않습니다.

   - 계속 켜져 있음 (깜빡이지 않음)
     EEPROM SYNC 전송 중입니다. 양쪽 반쪽의 설정을 맞추는 동안 켜지고
     끝나면 저절로 꺼집니다. 키맵 편집은 0.2초 안팎, 큰 매크로는 1~2초
     정도입니다. 이때는 전원이나 연결 케이블을 빼지 마세요.


======================================================================
ERA Firmware Quick Guide - Split Keyboards (TOMAK series)
======================================================================

This guide covers ERA firmware on a split keyboard. For a one-piece
keyboard, see readme.txt.

What only a split keyboard has is the SYNC family between the two halves,
the link speed of the cable joining them, and the red status lights.
Everything else matches the one-piece guide.


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
   One key can do different actions for tap, hold, double tap, or tap-hold.

   Example: Tap = Esc, Hold = Fn layer, Double Tap = Caps Lock

   Use VIA CONFIGURE -> TAPDANCE to set TD0~TD7 and enter keycodes directly,
   then place the matching TD0~TD7 key from VIA KEYMAP -> CUSTOM — the TOMAK
   definitions declare the custom keycodes, so they appear there.
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

   EEPROM SYNC carries this page to the other half.

■ NKRO
   Sets how many simultaneously held keys are reported to the PC. On, the
   count is effectively unlimited; off, the keyboard reports the usual six.

   Set it in VIA CONFIGURE -> FEATURE -> NKRO -> Enable.
   It is safe to change while typing: the firmware clears whatever was held
   across the switch, so no key is left stuck down.
   If a very old PC's BIOS or boot menu does not see the keyboard, try
   turning it off.

■ EEPROM CLEAN / SYNC
   CLEAN resets EEPROM from VIA CONFIGURE -> SYSTEM -> EEPROM. Turn on all
   three confirm toggles and the keyboard erases EEPROM and reboots.
   All three have to be on within 10 seconds of the first one. After that
   the ones already on are released and you start over. Turning a toggle
   back off cancels.
   With the two halves joined by their cable, CLEAN resets both halves and
   both reboot together. Done with the halves apart, it resets only the half
   you did it on.
   SYNC lives in VIA CONFIGURE -> SYSTEM -> SYNC.
   EEPROM SYNC keeps both halves' settings aligned automatically; it is on
   by default. It works the same whether you run one USB cable or connect
   each half over its own cable (dual host). In dual host it does not matter
   which half you edit from VIA: the change moves to the other half about a
   second later, and you may see the red light briefly while it does.
   If you edit both halves in quick succession one of the two edits can be
   dropped, so finish on one half, wait a few seconds, then switch.
   A change that arrives from the other half does not appear in VIA on its
   own: VIA re-reads the keymap only when you refresh. If a setting looks
   like it went missing, refresh first.
   INPUT SYNC shares layers and tap/hold decisions across both halves in dual
   host; on by default. RGB SYNC matches both halves' lighting in dual host;
   on by default.
   Back up your keymap from SAVE + LOAD before you clean the EEPROM — CLEAN
   erases it too.

■ LINK SPEED (the cable between the two halves)
   VIA CONFIGURE -> SYSTEM -> LINK
   The default is High and most people should leave it there.

   - High   460800 bps, 1 ms poll (default)
   - Medium 230400 bps, 2 ms poll
   - Low    115200 bps, 4 ms poll

   There is one reason to go slower: if the cable is long or in poor shape
   and the two halves keep losing each other, a slower rate holds the link
   better. Lowering it for any other reason only makes things slower.

   What changes: in dual host a layer key held on one half reaches the other
   half up to 4 ms later at Low. Your keystrokes are not affected at all —
   each half sends its own keys straight to the computer.

   To apply:
   1. Pick a value in Split Link Speed. Nothing happens yet.
   2. Turn on Apply. Both halves change to the new rate at the same
      moment. The keyboard does not reboot. After the change actually
      takes, VIA on the half you clicked disconnects once and comes
      back with Apply off. If the value was already running, or the
      Apply did not take, USB stays up and Apply stays on so you can
      see it.

   On every power-up the two halves first meet at Low, exchange the
   visible state, then raise to the stored target. In dual host that
   target is the left half's stored value; with USB in only one half it
   is the USB half's (HOST). If the cable cannot hold that rate, this
   session stays at Low and the stored High/Medium is left alone. The
   next power-up meets at Low and tries again. A cable that cannot hold
   even Low will not join.

   When setting the halves separately:
   - While they are joined, Apply on either half changes both.
   - With the cable out and one half on its own USB, Apply changes that
     half alone. To set the pair this way for dual host, do it on the
     left half. For one-USB use, do it on the USB half. A value applied
     to the other half alone is overwritten by the target the moment
     they are joined again.

■ Red LED signals
   A whole-keyboard red light is a status report, not a setting. There are three,
   and the shape tells them apart. All appear even with RGB switched off or
   brightness at zero.

   - Blink-blink . . blink-blink . . blink-blink (two at a time, three times,
     about 3.7 seconds)
     Shown once at startup, then the normal RGB effect returns. It means that
     half's communication core failed to start, so the link between the two
     halves is not working. That half's own keys and VIA still work, so it
     stays usable on its own.
     Unplug and reconnect power. If it repeats, reflash that half. If it still
     repeats, get in touch.

   - Three long pulses
     The cable could not hold the stored rate (High/Medium), so this
     power-up stayed at Low. The LINK SPEED value in VIA is unchanged.
     Check the cable, or Apply Low from the left half (dual host) or the
     USB half (HOST-PEER) so the next power-up does not try the higher
     rate again. The shape is not the communication-core failure sign,
     and this report does not run while that sign owns the field.

   - Steady red, no blinking
     An EEPROM SYNC transfer is in progress. It stays on while the two halves
     align their settings and goes out by itself. A keymap edit takes about
     0.2 s; a large macro takes one to two seconds. Do not remove power or the
     link cable while it is on.
