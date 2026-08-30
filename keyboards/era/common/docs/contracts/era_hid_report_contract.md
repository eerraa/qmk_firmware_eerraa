# ERA HID 키보드 리포트 계약

Genre: contract
Canonical for: ERA가 호스트로 내보내는 HID 키보드 리포트의 형상과 폭, BIOS에서
동작하게 만드는 boot 프로토콜 폴백, NKRO 옵트인 정책, 리포트 배열 폭과
엔드포인트 크기를 업스트림 값에 묶어 두는 세 건의 거절, 그리고 리포트 버퍼를
잠시 비웠다 되돌리는 코드가 활성 형식을 따라야 한다는 규칙(1.7)

**이 문서만 한국어다.** 소유자 지시로 그렇게 되었고, 문서 집합의 나머지와
맞추려면 번역이 필요하다. 규칙 자체는 언어와 무관하다.

## 결론

ERA는 **규격 준수 6키 리포트 + 옵트인 NKRO 토글**을 유지한다. 자매 프로젝트
(STM32H7S)가 쓰는 "확장 배열 + 토글 없음" 구성은 이 저장소에서 기능적 이득이
없다: 그 프로젝트에서 확장 배열은 NKRO를 쓸 수 없어서 고른 **대체재**이고,
여기서는 NKRO가 이미 있으며 20키보다 크다. 남는 차이는 *기본값* 하나이고, 그
기본값을 위해 치를 값이 영구 코어 포크 한 건과 출하된 22개 보드의 디스크립터
변경이다.

이 판단과 함께 발견된 결함 하나 — **KKUK(연타 보정)가 NKRO에서 눌린 키를
복원하지 못하던 것** — 은 선택지와 무관했고, 수리되어 1.7의 계약이 되었다.

## 1. 확인된 사실

### 1.1 리포트 형상 — 8바이트, 전용 인터페이스

`report_keyboard_t`는 mods(1) + reserved(1) + `keys[KEYBOARD_REPORT_KEYS]`이고
`KEYBOARD_REPORT_KEYS`는 6이다(`tmk_core/protocol/report.h:145`). 구조체 정의는
`tmk_core/protocol/report.h:172`부터이며, `report_id` 필드는 `KEYBOARD_SHARED_EP`
아래에서만 존재한다. **`KEYBOARD_SHARED_EP`는 이 트리 어디에서도 정의되지
않는다** — `keyboards/era` 전체에서 이 이름을 쓰는 파일은 없고, 유일한 히트는
`split/diagnostics/era_split_wire_diagnostics.c:780`의 `SHARED_EP_ENABLE`
테스트다. 그래서 키보드 리포트는 리포트 ID 없는 정확히 8바이트이고, 그
8바이트는 HID boot 키보드 포맷과 바이트 단위로 같다.

키보드는 **자기 인터페이스와 자기 엔드포인트**를 갖는다. 인터페이스
디스크립터는 subclass = `HID_CSCP_BootSubclass`, protocol =
`HID_CSCP_KeyboardBootProtocol`이고(`tmk_core/protocol/usb_descriptor.c:572`,
`:573`), 엔드포인트 크기는 `KEYBOARD_EPSIZE`
(`tmk_core/protocol/usb_descriptor.c:594`), 값은 8
(`tmk_core/protocol/usb_descriptor.h:287`). 폴링 주기는 1 ms
(`tmk_core/protocol/usb_descriptor.c:539`의 `#ifndef` 기본값이며 ERA는 덮지
않는다) — RP2040은 full-speed 장치라 이보다 빠를 수 없다. 여기가 자매
프로젝트와 갈리는 첫 지점이다: **이 저장소에는 8000 Hz 송신 경로가 없다.**

리포트 디스크립터는 `tmk_core/protocol/usb_descriptor.c:71`의 `KeyboardReport[]`
이고, 키코드 배열은 `REPORT_COUNT = 0x06`, `REPORT_SIZE = 8`, ARRAY
(`tmk_core/protocol/usb_descriptor.c:98`). 브리핑이 인용한 H7S의
`95 14 75 08`은 같은 두 항목의 `0x14`(20) 판이다.

### 1.2 boot / report 프로토콜 — 상태를 누가 언제 정하는가

- **초기값은 REPORT다.** `usb_device_state`의 정적 초기화가
  `.protocol = USB_PROTOCOL_REPORT`
  (`tmk_core/protocol/usb_device_state.c:27`).
- **USB 리셋마다 REPORT로 되돌아간다.** 이벤트 큐 처리에서
  `usb_device_state_set_protocol(USB_PROTOCOL_REPORT)`
  (`tmk_core/protocol/chibios/usb_main.c:159`).
- **SET_PROTOCOL은 키보드 인터페이스에서만 상태를 바꾼다.** 클래스 요청 훅이
  `wIndex == KEYBOARD_INTERFACE`일 때만 `usb_device_state_set_protocol()`을
  부르고, 그 외에는 상태를 두고 0-length 응답만 한다
  (`tmk_core/protocol/chibios/usb_main.c:284`). GET_PROTOCOL도 같은 인터페이스
  조건으로 답한다(`tmk_core/protocol/chibios/usb_main.c:259`).
- **호스트가 SET_PROTOCOL을 보내지 않으면 상태는 REPORT로 남는다.** 이것이
  선택지 B의 위험이 실재하는 이유이고, 1.4에서 결과를 따진다.

**길이를 프로토콜에 따라 줄이는 코드는 이미 있다.** `send_keyboard()`
(`tmk_core/protocol/chibios/usb_main.c:437`)는 boot 프로토콜이면
`&report->mods`에서 8바이트만, 아니면 `KEYBOARD_REPORT_SIZE`만큼 보낸다. 즉
브리핑이 H7S의 규격 편차 ①이라고 부른 것 — "SET_PROTOCOL(boot) 이후에도 줄이지
않음" — 은 **이 저장소에 존재하지 않는다.** 그리고 이 저장소에서 선택지 C를
고른다는 것은 없던 편차를 도입하는 것이 아니라 **이미 있는 올바른 분기를
삭제하는 것**이다.

**규격 편차 ②(인터페이스 0의 GET_REPORT STALL)도 존재하지 않는다.**
`usb_get_report_cb()`(`tmk_core/protocol/chibios/usb_report_handling.c:81`)가
인터페이스→엔드포인트 룩업으로 마지막 리포트를 돌려준다. 저장되는 길이는 실제
전송된 바이트 수다 — 전송 완료 콜백이
`set_report(..., buffer, n)`을 부르고 `n`은 큐에서 나간 실제 길이
(`tmk_core/protocol/chibios/usb_driver.c:205`) — 이므로 boot 프로토콜에서는
GET_REPORT도 8바이트로 답한다.

> **REFUSED:** `send_keyboard()`의 boot 분기를 지우고 항상 확장 리포트를 보낸다
> (자매 프로젝트 방식의 직접 이식).
> **WHY:** 그 분기는 이 펌웨어가 이미 통과시켜 놓은 HID 1.11 §7.2.6 준수분이고,
> 지우면 얻는 것은 코드 몇 줄뿐인데 잃는 것은 boot 프로토콜을 실제로 협상하는
> 모든 호스트에서의 정확성이다.
> **REOPENS:** 가변 길이 전송이 측정 가능한 비용을 만든다는 증거 — 이 저장소의
> 1 ms 폴링과 큐 기반 전송에서는 길이가 인자일 뿐이므로, 그 증거는 아직 없다.

### 1.3 NKRO 경로와 기본값

- 리포트는 `report_nkro_t` = report_id(1) + mods(1) + `bits[NKRO_REPORT_BITS]`,
  `NKRO_REPORT_BITS`는 30(`tmk_core/protocol/report.h:137`) → 32바이트, 240키.
- **공유 엔드포인트로 나간다.** `send_nkro()`가
  `USB_ENDPOINT_IN_SHARED`로 보내고(`tmk_core/protocol/chibios/usb_main.c:446`),
  리포트 저장소 항목은 `REPORT_ID_NKRO`
  (`tmk_core/protocol/chibios/usb_endpoints.c:37` 부근의
  `QMK_USB_REPORT_STROAGE_ENTRY`). 즉 **NKRO가 활성인 동안 키보드 인터페이스
  (인터페이스 0)로는 아무것도 나가지 않는다.**
- **분기 조건은 두 개의 AND다.** `send_keyboard_report()`
  (`quantum/action_util.c:335`)가 `host_can_send_nkro() && keymap_config.nkro`
  일 때만 NKRO 경로를 탄다. 같은 조건이 `tmk_core/protocol/report.c`의
  `has_anykey()`, `is_key_pressed()`, `add_key_to_report()`,
  `del_key_from_report()`, `clear_keys_from_report()`에 반복된다.
- **`host_can_send_nkro()`는 프로토콜 상태다.** 정의는
  `tmk_core/protocol/host.c:137`이고, USB 경로에서는
  `usb_device_state_get_protocol() == USB_PROTOCOL_REPORT`를 돌려준다. 이것이
  "BIOS에서 자동으로 6KRO로 떨어진다"의 전부다 — **호스트가 SET_PROTOCOL(boot)을
  보낼 때만 작동한다.**
- **기본값은 꺼짐이다.** `eeconfig_init_quantum()`의 기본 구조체가
  `.nkro = NKRO_DEFAULT_ON`(`quantum/eeconfig.c:89`)이고 `NKRO_DEFAULT_ON`은
  정의되지 않았을 때 `false`(`quantum/eeconfig.c:50`). ERA는 이 이름을 어디에도
  정의하지 않는다. `FORCE_NKRO`(`quantum/keyboard.c:513`)도 정의하지 않는다.
- `keymap_config`의 `nkro` 비트는 `NKRO_ENABLE`과 무관하게 항상 존재한다
  (`quantum/keycode_config.h:39`, 공용체 전체가 무조건부이며
  `sizeof == 2`를 `STATIC_ASSERT`가 잡는다). **NKRO를 빌드에서 빼도 EEPROM
  레이아웃은 변하지 않는다.**

### 1.4 호스트가 SET_PROTOCOL을 보내지 않을 때

1.2와 1.3을 합치면 결론은 하나다. SET_PROTOCOL을 보내지 않는 호스트에서는
상태가 REPORT로 남고 → `host_can_send_nkro()`가 참이고 → `keymap_config.nkro`가
켜져 있으면 모든 키 입력이 공유 엔드포인트로만 나간다. 그런 호스트가 boot
키보드 인터페이스 하나만 열어 두는 종류라면 **입력이 완전히 사라진다** —
느려지거나 6키로 제한되는 것이 아니라, 아무것도 도착하지 않는다.

이것은 추론이 아니라 위 다섯 인용의 직접적 귀결이다. 다만 *어떤* BIOS·KVM이
그렇게 동작하는지는 소스가 답할 수 없고, 5절의 실측 항목이다. 사용자 문서는
이미 이 사실을 전제로 쓰여 있다: `user/readme.txt`의 NKRO 항목이 "오래된
BIOS나 부트 메뉴에서 읽지 못하면 끄라"고 안내한다.

### 1.5 23개 보드와 VIA 노출

- `keyboard.json`을 가진 보드는 23개, **전부 `features.nkro`가 참**이다.
- ERA VIA NKRO 토글이 붙는 보드는 **22개**다. 선택자는 보드가 고르는 것이
  아니라 QMK 스위치에서 파생된다: `NKRO_ENABLE = yes`일 때만
  `features/era_nkro_via.c`가 `SRC`에 들어가고 `-DERA_NKRO_VIA_ENABLE`이
  나간다(`system/era_common_qmk_rules.mk:257`). 그 파일을 읽는 경로는 보드의
  `post_rules.mk`가 `system/era_common_qmk_rules.mk`를 포함하는 경로다.
  `sirind/brick65/post_rules.mk`는 빌드 이름 검증과 옵션 출력만 포함하고
  그 공통 펌웨어 규칙은 포함하지 않는다 — atmega32u4 영구 예외이며, 그
  보드의 VIA 정의에도 NKRO 메뉴가 없다.
- VIA 정의 파일은 26개(`*-VIA.json`, 스플릿 3보드가 L/R 두 벌씩), 그중
  **25개에 NKRO 메뉴가 있다.** 값 id는 5
  (`features/era_nkro_via.h:17`), 채널은 `id_custom_channel`. 이 번호는
  `maps/era_identifier_map.md`가 관리하고, 인디케이터 슬롯이 6부터 시작하는
  이유가 이 5다.
- `features/era_nkro_via.c`의 계약은 어댑터다: 상태를 소유하지 않고 QMK의
  `keymap_config.nkro`와 eeconfig를 그대로 쓰며, 소유한 것은 전환 앞뒤의
  `clear_keyboard()` 두 번뿐이다. 리포트 형식이 바뀌는 순간 눌려 있던 키가
  한 형식으로 보고되고 다른 형식으로 해제되어 호스트가 해제를 못 보는 것을
  막는다. `id_custom_save` 처리는 의도적으로 없다.

### 1.6 코어 포크가 이 프로젝트에서 허용된 관행인가

허용된 관행이되, **한 건마다 원장 한 줄을 지는 관행**이다.
`manuals/era_qmk_fork_ledger.md`가 그 원장이고, 권위 있는 열거는 목록이 아니라
파생이다: 정본 업스트림 커밋 `c93ef27143`과 HEAD의 차이를 다섯 디렉터리로
제한한 것. 지금 그 명령
(`git diff --name-only c93ef27143 HEAD -- tmk_core quantum platforms drivers builddefs`)은
**42개 파일**을 돌려준다. 원장 본문은 41이라고 적고 있다 — 5절의 재조정 항목.

이 검토가 건드릴 파일들의 현재 상태:

| 파일 | 지금 |
| --- | --- |
| `tmk_core/protocol/report.h` | **업스트림과 동일.** 포크 표면에 없다 |
| `tmk_core/protocol/usb_descriptor.h` | 이미 포크됨 (ERA USB identity 오버라이드 선언 3개) |
| `tmk_core/protocol/usb_descriptor.c` | 이미 포크됨 (같은 오버라이드의 구현 훅) |
| `tmk_core/protocol/chibios/usb_main.c` | **업스트림과 동일** — 원장이 명시적으로 "owe nothing"으로 지목 |
| `tmk_core/protocol/chibios/usb_endpoints.c` | 업스트림과 동일 |
| `tmk_core/protocol/host.c` | **업스트림과 동일** — 원장이 명시적으로 지목 |
| `quantum/action_util.c`, `tmk_core/protocol/report.c` | 업스트림과 동일 |

원장의 실무 규칙 두 가지가 비용 계산에 직접 들어온다. **첫째, 게이트 없는
편집은 오프-레이어 빌드 확인을 진다** — 이 포크에는 ERA 밖 키보드 전체가 들어
있고, `tmk_core/protocol/report.h`는 그 전부가 컴파일한다. 둘째, 파생 명령이
파일을 돌려주고 원장이 이유를 돌려주므로 **양방향으로 맞춰야 한다**: 차이에
있는데 줄이 없으면 미기록 포크, 줄이 있는데 차이에 없으면 반대 방향의 같은
결함.

### 1.7 KKUK 펄스는 활성 리포트 형식을 되돌린다

`era_kkuk_start_empty_pulse()`(`features/era_kkuk.c`)는 눌린 키를 전부 뗀 빈
리포트를 보내고, 다음 태스크 틱의 `era_kkuk_finish_restore_pulse()`가 다시
`send_keyboard_report()`를 불러 되돌린다. **그 사이에 스냅숏해야 하는 것은
`clear_keys()`가 비울 리포트이고, 그것이 항상 6KRO는 아니다**:
`clear_keys_from_report()`(`tmk_core/protocol/report.c`)는 NKRO가 협상된 동안
`nkro_report->bits`를, 아니면 `keyboard_report->keys`를 지운다. 펄스는 두 배열을
모두 저장하고 모두 되돌린다 — 지워지지 않은 쪽은 자기 바이트를 다시 쓰므로
아무것도 바뀌지 않고, 그 대가로 QMK의 판별식이 이 파일에 복사되지 않는다. 두
스냅숏은 그 함수 안에서만 살아 있다: 내부 리포트 복원은 같은 호출 안에서
끝나고 호스트 쪽 복원만 미뤄지므로, 틱을 건너 살아남아야 하는 것은
`kkuk_pulse_state` 하나다.

키 배열만 움직인다. `clear_keys()`는 mods를 지우지 않고 전송이 매번
`get_mods_for_report()`로 다시 계산하므로, 펄스가 뗐다 되돌리는 것은 눌린 **키**
뿐이다.

> **REFUSED:** 펄스가 `keyboard_report`만 스냅숏하고 복원한다.
> **WHY:** NKRO에서 `clear_keys()`가 비우는 것은 `nkro_report->bits`라 되돌릴
> 것이 남지 않고, `send_nkro_report()`(`quantum/action_util.c`)의 `memcmp`
> 중복 억제가 복원 전송을 방금 보낸 빈 리포트와 같다고 판정해 삼킨다 — 눌려
> 있던 키가 호스트에서 해제된 채로 남고 다시 누를 때까지 돌아오지 않는다.
> **REOPENS:** 이 펌웨어가 리포트 형식을 하나만 갖게 될 때. `NKRO_ENABLE`이
> 없는 빌드에서는 `#ifdef`가 이미 그 형태로 접혀 있다.

**두 기능은 이제 독립적으로 조합된다.** 사용자 문서가 둘을 상호작용 없는
항목으로 소개하는 것(`user/readme.txt`의 KKUK 및 NKRO 항목,
`user/readme_split.txt`도 같음)은 이 계약이 지켜지는 한 참이다. 출하 기본
상태는 어느 쪽도 켜지 않는다 — KKUK 기본값은 `era_kkuk_apply_defaults()`가
`memset` 0으로 세우고(`enable = 0`), NKRO 기본값은 1.3이다.

### 1.8 스플릿과 저장소에 대한 영향

**리포트 크기는 와이어를 건너지 않는다.** HOST-PEER 레인이 나르는 것은 원시
매트릭스이지 HID 리포트가 아니며, 그 의미론은
`contracts/era_host_peer_matrix_contract.md`가 정본이다. `keyboards/era` 전체에서
HID 리포트 버퍼를 직접 만지는 곳은 `features/era_kkuk.c`의 펄스 하나뿐이고(1.7),
`send_keyboard_report()`를 부르는 곳은 그 파일과
`split/era_split_authority_reducer.c`(HID 재개통 시 1회 재전송)뿐이다.

**NKRO 비트는 이미 반쪽 사이에서 동기화된다.** QMK `keymap_config` 2바이트가
동기 도메인이다(`split/era_host_peer_storage.h:52`). 스플릿 3보드
(`sirind/tomak`, `sirind/tomak79h`, `sirind/tomak79s`)가 VIA 빌드에서
`ERA_SPLIT_EEPROM_SYNC_ENABLE := yes`를 세운다. 1.3의 결론과 합치면 **리포트
폭을 바꾸어도 스키마·도메인 크기는 변하지 않는다.**

## 2. 선택지 비교

- **A** — 현상 유지: 6키 + NKRO 토글(기본 꺼짐).
- **B** — `NKRO_DEFAULT_ON`을 참으로.
- **C** — 배열을 20키로 확장하고 토글 제거, boot 분기 삭제(자매 프로젝트 이식).
- **D** — 배열을 20키로 확장, boot 프로토콜에서만 8바이트로 절단(= 지금 있는
  `send_keyboard()` 분기를 그대로 둠), 토글 제거.
- **D′** — D와 같되 **NKRO 토글을 남긴다**. 표에 넣는 이유는 3절에 있다.

| | A | B | C | D | D′ |
| --- | --- | --- | --- | --- | --- |
| 전환 없는 동시입력 | 6키 | 240키 | 20키 | 20키 | 20키 |
| 최대 동시입력 | 240키 | 240키 | 20키 | 20키 | 240키 |
| 코어 파일 변경 | 없음 | 없음 | `report.h`, `usb_descriptor.[ch]`, `usb_main.c` | `report.h`, `usb_descriptor.[ch]` | 좌동 |
| 새로 포크되는 파일 | 0 | 0 | 2 (`report.h`, `usb_main.c`) | **1** (`report.h`) | 1 |
| ERA 계층 변경 | 없음 | 없음 | `era_nkro_via.[ch]` 삭제, `era_common_qmk_rules.mk`, `era_kkuk.c` | 좌동 | `era_kkuk.c`만 |
| VIA 정의 재배포 | 없음 | 없음 | 25개 파일 | 25개 파일 | 없음 |
| 사용자 문서 | 없음 | NKRO 항목 개정 | 2개 파일 개정 | 2개 파일 개정 | Anti-Ghosting 주의만 |
| USB 디스크립터 변경 | 없음 | 없음 | 있음 | 있음 | 있음 |
| `usb.device_version` 상향 | 불필요 | 불필요 | 22보드 | 22보드 | 22보드 |
| RAM 증분(산술 추정) | 0 | 0 | 약 +42 −140 B | 약 +42 −140 B | 약 **+138 B** |
| 규격 편차 | 없음 | 없음 | **신규 도입 1건** | 없음 | 없음 |
| 최악 회귀 | 없음 | boot 화면 무입력 | 좌동 + babble | 비준수 호스트 babble | 좌동 |

RAM 숫자는 구조체 정의에서 뽑은 **산술 추정이고 빌드 측정이 아니다**. 근거:
`report_keyboard_t` 인스턴스 3개(`quantum/action_util.c:38`의 `keyboard_report`,
`quantum/action_util.c:307`의 `last_report`, `features/era_kkuk.c:59`)가 각각
14바이트 늘고, 키보드 IN 엔드포인트 버퍼가 `BQ_BUFFER_SIZE(4, 8)`에서
`BQ_BUFFER_SIZE(4, 32)`로 가면서 96바이트 는다
(`tmk_core/protocol/chibios/usb_endpoints.c:52`,
`tmk_core/protocol/chibios/usb_driver.h:56`). NKRO를 함께 빼면 `report_nkro_t`
2개와 공유 EP의 `usb_fs_report_t` 항목 하나
(`tmk_core/protocol/chibios/usb_report_handling.h:17`, `data[64]` 고정)와
디스크립터 블록이 빠진다. 어느 쪽이든
`contracts/era_sram_residency_contract.md`의 32 KiB 여유 바닥에서 보면 잡음이다.

### C와 D가 왜 엔드포인트까지 건드리는가

`KEYBOARD_EPSIZE`가 8인 채로 22바이트를 보낼 수는 없다. 전송 함수가
`osalDbgCheck(... size <= endpoint->config.buffer_size)`로 막고
(`tmk_core/protocol/chibios/usb_driver.c:253`), `buffer_size`는 EP 크기 그대로다
(`tmk_core/protocol/chibios/usb_driver.h:55`). 디버그 체크가 꺼진 빌드라 해도
출력 큐가 8바이트 버퍼 단위라 리포트가 두 패킷으로 쪼개진다. 그래서 C/D는
`KEYBOARD_EPSIZE`를 32로 올려야 하고, 그 값은 엔드포인트 디스크립터에 그대로
실려 나간다(`tmk_core/protocol/usb_descriptor.c:594`) — **디스크립터 변경이
불가피한 이유가 리포트 폭이 아니라 여기다.**

## 3. 권고

### 권고: A를 유지한다.

근거는 넷이다.

**첫째, 기능적 이득이 사실상 없다.** 자매 프로젝트에서 20키 배열은 NKRO의
*대체재*였다. 여기서 NKRO는 이미 있고 240키이며 토글 하나 뒤에 있다. C/D를
고르면 사용자가 도달할 수 있는 최대치가 240에서 20으로 **내려간다.** 남는
차이는 "VIA를 한 번도 열지 않는 사용자의 기본값이 6이냐 20이냐" 하나다.

**둘째, 이 저장소에 없는 문제를 이식하게 된다.** 자매 프로젝트가 두 편차를
유지하기로 한 근거는 "이미 출하되어 동작 중이고, 고치려면 8000 Hz 경로를 가변
길이로 바꿔야 한다"였다. 이 저장소에는 그 두 전제가 모두 없다: 편차는 애초에
없고(1.2), 8000 Hz 경로도 없다(1.1). 남의 제약을 근거로 내 결정을 하는 셈이
된다.

**셋째, 비용이 영구적이다.** `tmk_core/protocol/report.h`는 지금 업스트림과
동일하고, 이 포크에 들어 있는 ERA 밖 키보드 전부가 그 파일을 컴파일한다. 폭
변경을 무게이트로 하면 그 전부의 리포트 구조체가 바뀐다 — 원장이 무게이트
편집에 요구하는 오프-레이어 빌드 확인의 대상이 사실상 포크 전체가 된다.
게이트를 달면(예: `era_common_qmk_rules.mk`에서 내보내는 ERA 마커) 안전하지만,
그 대가로 **`report.h`가 포크 표면에 새로 올라가고 원장 한 줄을 영구히 진다.**

**넷째, 출하된 동작을 바꾸는 일회성 비용이 22개 보드에 걸린다.** 디스크립터가
바뀌면 호스트가 캐시한 파싱 결과와 어긋날 수 있고, 그 레버는 각
`keyboard.json`의 `usb.device_version` 상향이다 — 22개 파일. VIA 정의는
`vendorId`/`productId`로만 매칭하므로 디스크립터 변경만으로는 무효화되지
않지만, **토글을 없애면 25개 정의 파일과 사용자 문서 2개가 같이 움직인다.**

### 권고하지 않는 안과 그 이유

**B(NKRO 기본 ON)를 권고하지 않는다.** 코드 변경은 한 줄이고 회수도 쉽지만,
1.4의 실패 모드가 실재한다 — 그 호스트에서는 입력이 줄어드는 게 아니라
사라진다. 덧붙여 **효과 자체가 좁다**:
`NKRO_DEFAULT_ON`은 `eeconfig_init_quantum()`의 기본 구조체에만 들어가므로
(`quantum/eeconfig.c:89`), 이미 eeconfig를 저장한 기존 사용자는 그대로 6KRO다.
새로 플래시하거나 EEPROM CLEAN을 한 보드만 바뀐다. 위험은 전체가 지고 효과는
일부만 받는다.

> **REFUSED:** `NKRO_DEFAULT_ON`을 참으로 두어 NKRO를 기본 켜짐으로 출하한다.
> **WHY:** SET_PROTOCOL을 보내지 않는 호스트에서는 키보드 인터페이스로 아무것도
> 나가지 않아 입력이 사라지고, 그 대가로 얻는 것은 EEPROM을 새로 초기화한
> 보드에만 적용되는 기본값 변경이다.
> **REOPENS:** 소유자가 대상으로 삼는 BIOS·KVM 표본에서 SET_PROTOCOL(boot)이
> 실제로 관측될 때.

**C를 권고하지 않는다.** D가 지배한다. 이 저장소에서 C는 D보다 코드를 *더*
지우면서(1.2의 boot 분기) 규격 편차를 신규 도입한다. AGENTS.md의 기준으로도
"이미 있어서 유지"와 "새로 도입"은 다른 판단이고, 여기서는 후자다.

> **REFUSED:** 키보드 배열을 확장하면서 boot 프로토콜 절단을 포기한다.
> **WHY:** 이 저장소에는 절단이 이미 구현되어 있고 그것을 지우는 데 드는 비용이
> 음수가 아니므로, 규격 편차를 새로 만드는 유일한 이유가 "다른 프로젝트가
> 그렇다"밖에 남지 않는다.
> **REOPENS:** 절단 분기 자체가 측정된 결함(예: 프로토콜 전환 경계에서의 리포트
> 유실)을 일으킨다는 기기 증거.

**D / D′를 권고하지 않는다 — 다만 유일하게 기술적으로 성립하는 상향안이다.**
D는 규격을 지키면서 기본값을 20키로 올린다. D′는 거기에 NKRO 토글까지 남겨
최대치도 잃지 않으므로 **D보다 엄밀히 낫다** — VIA 정의 25개와 사용자 문서를
건드리지 않고, 잃는 기능이 없다. 그럼에도 권고하지 않는 이유는 위 셋째·넷째,
즉 얻는 것이 "기본값 6 → 20" 하나인 데 비해 영구 포크 한 줄과 출하 22보드의
디스크립터 변경이 붙기 때문이다. 그리고 D/D′도 1.4의 위험을 완전히 없애지는
못한다: SET_PROTOCOL을 보내지 않는 호스트는 REPORT 상태에 남고, 그 호스트는
확장 리포트를 받는다. 무입력 대신 babble로 실패 모드가 바뀔 뿐이다.

> **REFUSED:** `KEYBOARD_REPORT_KEYS`와 `KEYBOARD_EPSIZE`를 ERA용으로 키워
> 키보드 리포트 폭을 업스트림에서 떼어낸다.
> **WHY:** 얻는 것은 기본값 하나(6 → 20)이고 최대치는 이미 240인데, 대가는
> 업스트림과 동일한 `tmk_core/protocol/report.h`를 영구 포크 표면에 올리는 것과
> 출하된 22개 보드의 USB 디스크립터를 바꾸는 것이다.
> **REOPENS:** 소유자가 "VIA를 열지 않는 사용자의 기본 동시입력"을 제품 요구로
> 승격하고, 5절의 실측 두 건(SET_PROTOCOL 표본, 확장 리포트의 boot 호환)이
> 통과했을 때. 그때 고를 것은 D가 아니라 **D′**다.

## 4. 남은 작업과 검증

권고안(A 유지)이 요구한 작업은 1.7 수리 하나였고, 그것은 `features/era_kkuk.c`에
들어갔다 — 펄스가 두 리포트 형식의 키 배열을 모두 저장하고 모두 되돌린다.

**사용자 문서 개정은 이 수리와 함께 사라졌다.** 그 항목이 있었던 이유는 두
기능이 서로를 망가뜨리는데 `user/readme.txt`와 `user/readme_split.txt`는 둘을
상호작용 없는 항목으로 소개한다는 것이었다. 대안(KKUK를 6KRO 전용으로 명시하고
NKRO에서 펄스를 건너뛰기)을 골랐다면 그 제약을 두 문서에 적어야 했다. 형식을
따라가게 고친 쪽에서는 두 기능이 실제로 독립이므로 문서가 이미 맞다.

남은 항목은 하나이고, 이 수리와 무관하다.

1. **포크 원장을 재조정한다.** 1.6의 42 대 41. 1.7이 건드린 파일은
   `keyboards/era` 안에 있어 포크 표면을 넓히지 않는다.

검증은 `manuals/era_performance_gates.md`의 **What A Change Owes**가 정본이다.
수리는 소스를 건드리므로 동기화된 트리에서 런처를 통한 타깃 빌드를 진다. 스캔
경로·RAM 배치·닫힌 표면·QMK 코어 매트릭스 파일 어느 것도 건드리지 않으므로
Source Gate와 Layout Checks는 해당 없다. 실제 확인은 기기에서: NKRO 켜짐 +
KKUK 켜짐으로 두 키 이상을 길게 누르고 있을 때 호스트가 키를 잃지
않는지.

**게이트를 지는 것은 1.7의 소스 변경이지 이 문서가 아니다.**

## 5. 실측이 필요한 항목

소스가 답할 수 없어 남는 것들이다. 1~3은 D′를 재검토할 때의 진입 조건이고,
4는 지금도 유효하다.

1. **소유자가 대상으로 삼는 BIOS·KVM 표본이 SET_PROTOCOL(boot)을 보내는가.**
   1.4 전체가 여기에 걸린다. 측정: 대표 기기에서 NKRO를 켠 채 부트 메뉴에
   진입해 입력이 살아 있는지 — 살아 있으면 그 호스트는 SET_PROTOCOL을 보냈다.
   USB 분석기가 있으면 직접 관측이 낫다.
2. **확장 리포트(22바이트)를 boot subclass 인터페이스로 받는 호스트가 실제로
   어디까지인가.** 자매 프로젝트의 현장 증거는 그 하드웨어·그 호스트 스택
   기준이고, ERA 23보드의 사용자군으로 이월되지 않는다. 특히 1과 같은 표본에서
   재봐야 한다 — SET_PROTOCOL을 보내지 않는 호스트가 정확히 확장 리포트를 받는
   호스트이기 때문이다.
3. **디스크립터가 바뀌었는데 `usb.device_version`을 올리지 않았을 때 호스트가
   캐시한 파싱을 계속 쓰는가.** 출하된 보드 하나로 재현 시험이 필요하다.
4. **포크 표면 42 대 원장 41.** 파생 명령
   (`git diff --name-only c93ef27143 HEAD -- tmk_core quantum platforms drivers builddefs`)
   을 원장의 서술 집합과 양방향으로 맞춰, 늘어난 한 건이 새 포크인지 원장 숫자의
   지연인지 확정한다. 이 검토가 인용한 "업스트림과 동일" 판정들(1.6의 표)은
   그 명령의 현재 출력에서 직접 읽은 것이라 이 재조정과 독립적으로 유효하다.
