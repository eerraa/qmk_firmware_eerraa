# EERRAA QMK — 공통 작업 진입점

이 폴더가 통합 Git 루트다. Git 관리는 루트에서, 빌드·검사는 `qmk_firmware_eerraa/`에서 실행한다.
제품 계약·작업별 검증은 하위 AGENTS가 소유하며, 그 문서의 경로·명령은 펌웨어 컴포넌트 기준이다.

## 필수 시작 순서

1. 먼저 작업을 분류한다. 불명확하면 분류에 필요한 최소 경로만 확인한다.
2. 실제 작업 워크트리의 이 문서와 아래 관련 AGENTS를 반드시 읽은 뒤 조사·수정·검증한다. 범위가 넓어지면 해당 범위의 AGENTS도 먼저 읽으며, 필수 문서를 읽을 수 없으면 차단 사유를 보고한다.

| 작업 | 필독 |
| --- | --- |
| 펌웨어·도구·컴포넌트 문서·설정 | [펌웨어 AGENTS](qmk_firmware_eerraa/AGENTS.md); 이어서 해당 작업 색인 |
| 루트 공통 정책·Git·워크트리·핸드오버 | 이 문서; 컴포넌트 영향이 있으면 하위 AGENTS도 필독 |
| 외부 H7S·VIA와의 호환 | 내부 생산자·계약 작업은 펌웨어 AGENTS; 외부 구현을 조사·수정할 때 상대 AGENTS도 필독. 정확성 판단에 필요한 외부 계약은 확인하고, 외부 수정은 승인 범위에서만 |

## 승인과 Git

- 실제 root·branch·HEAD·status·관련 diff를 확인하고 사용자 변경과 ignored 입력을 보존한다.
- 현재 branch를 임의로 전환하지 않는다. 과거 커밋·태그는 이전 구조이므로 별도 워크트리에서 조회한다.
- commit·push·merge·rebase·tag·release·배포·의존성 변경·실기기 조작은 사용자가 명시한 범위에서만 수행한다.
- commit은 검증된 관심사별로 자신의 변경과 승인된 사용자 변경만 포함한다. 사용자 삭제를 자동 복원하지 않는다.
- hook 설정은 `git config core.hooksPath qmk_firmware_eerraa/hooks`다. 루트 `.gitmodules`가 Git submodule 경로를 소유하며, 컴포넌트 `.gitmodules`는 QMK 도구용 상대 경로 목록이다.
- 검증은 로컬 명령과 Git hook으로 실행한다. 컴포넌트 내부의 upstream workflow는 원본 보존용이며 GitHub Actions가 실행하지 않는다.
- 기존 폴더 경로와 컴포넌트 ignore·개행 정책을 보존한다. 백업 bundle을 자동 복원하지 않는다.

## 워크트리와 핸드오버

- `qmk_firmware_eerraa_worktrees/`는 추가 워크트리 공간이다. 경로·등록·잠금·변경·ignored·submodule 상태를 확인하고 실제 병합 또는 테스트 종료가 확인된 경우만 `git worktree remove`로 제거한다. 자료 보존 없이 삭제하거나 junction을 재귀 삭제하지 않는다.
- `qmk_firmware_eerraa_handover/`는 사용자 저장 지시에만 쓰는 Git 비추적 공간이다. 테스트 펌웨어 저장 시 일반 빌드와 구분되는 파일명·경로를 제공한다. 자동 기록·stage·활성 계약의 정본 취급은 금지한다.
- 복구 백업은 핸드오버 아래 `backups/`에 보존한다. `.mixdog/`도 Git 비추적 로컬 도구 공간이다.

## 검증과 언어

- 저장소 AGENTS와 해당 작업의 Verify가 지정한 최소 검사를 실행한다. 소스 변경 없는 안내 작업에 전체 빌드·HIL을 요구하지 않는다.
- 실제 실행과 소스 추론을 구분하고 미실행·미측정을 PASS로 보고하지 않는다.
- 대화·공통 진입점은 한국어로 쓰고, 저장소 내부 문서·소스는 해당 로컬 규칙을 따른다.
